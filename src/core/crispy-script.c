/* crispy-script.c - Script lifecycle orchestrator */

#define CRISPY_COMPILATION
#include "crispy-script.h"
#include "../interfaces/crispy-compiler.h"
#include "../interfaces/crispy-cache-provider.h"
#include "../crispy-types.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gmodule.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/**
 * SECTION:crispy-script
 * @title: CrispyScript
 * @short_description: Orchestrates the full script lifecycle
 *
 * #CrispyScript manages the complete pipeline from source code to
 * execution: reading source, parsing CRISPY_PARAMS, computing cache
 * hashes, compiling (if needed), loading the compiled module, and
 * executing the script's main() function.
 *
 * It operates on any #CrispyCompiler and #CrispyCacheProvider
 * implementations, making it fully decoupled from specific backends.
 */

struct _CrispyScript
{
    GObject parent_instance;
};

typedef struct
{
    CrispyCompiler      *compiler;
    CrispyCacheProvider *cache;

    gchar       *source_path;       /* original script path (NULL for inline/stdin) */
    gchar       *source_content;    /* full original source text */
    gsize        source_len;

    gchar       *crispy_params;     /* extracted CRISPY_PARAMS value */
    gchar       *expanded_params;   /* shell-expanded CRISPY_PARAMS */
    gchar       *modified_source;   /* source with shebang + CRISPY_PARAMS removed */
    gsize        modified_len;

    gchar       *temp_source_path;  /* /tmp/crispy-XXXXXX.c */
    gchar       *hash;              /* SHA256 hex string */

    GModule     *module;            /* loaded shared object */
    CrispyFlags  flags;

    gint         exit_code;
} CrispyScriptPrivate;

G_DEFINE_FINAL_TYPE_WITH_PRIVATE(CrispyScript, crispy_script, G_TYPE_OBJECT)

/* --- helper: parse #define CRISPY_PARAMS "..." from source --- */
static void
parse_crispy_params(
    CrispyScriptPrivate *priv
){
    GString *modified;
    gchar **lines;
    gint i;
    gboolean found;

    found = FALSE;
    lines = g_strsplit(priv->source_content, "\n", -1);
    modified = g_string_new(NULL);

    for (i = 0; lines[i] != NULL; i++)
    {
        const gchar *line;
        const gchar *p;
        const gchar *start;
        const gchar *end;

        line = lines[i];

        /* skip shebang on the first line */
        if (i == 0 && g_str_has_prefix(line, "#!"))
            continue;

        /* look for #define CRISPY_PARAMS */
        p = line;
        while (*p == ' ' || *p == '\t')
            p++;

        if (!found && g_str_has_prefix(p, "#define") &&
            strstr(p, "CRISPY_PARAMS") != NULL)
        {
            /* extract the quoted value */
            start = strchr(p, '"');
            if (start != NULL)
            {
                start++; /* skip opening quote */
                end = strrchr(start, '"');
                if (end != NULL && end > start)
                {
                    priv->crispy_params = g_strndup(start, (gsize)(end - start));
                    found = TRUE;
                    continue; /* skip this line in modified source */
                }
            }
        }

        /* keep the line in modified source */
        g_string_append(modified, line);
        g_string_append_c(modified, '\n');
    }

    g_strfreev(lines);

    priv->modified_source = g_string_free(modified, FALSE);
    priv->modified_len = strlen(priv->modified_source);
}

/* --- helper: shell-expand CRISPY_PARAMS --- */
static gchar *
shell_expand(
    const gchar  *params,
    GError      **error
){
    g_autofree gchar *cmd = NULL;
    gchar *std_out;
    gchar *std_err;
    gint exit_status;

    if (params == NULL || params[0] == '\0')
        return g_strdup("");

    std_out = NULL;
    std_err = NULL;

    /* use printf '%s' to avoid echo's interpretation of backslashes */
    cmd = g_strdup_printf("/bin/sh -c \"printf '%%s' %s\"", params);

    if (!g_spawn_command_line_sync(cmd, &std_out, &std_err,
                                   &exit_status, error))
    {
        g_free(std_out);
        g_free(std_err);
        return NULL;
    }

    g_free(std_err);

    if (!g_spawn_check_wait_status(exit_status, error))
    {
        g_free(std_out);
        return NULL;
    }

    g_strstrip(std_out);
    return std_out;
}

/* --- helper: write modified source to temp file --- */
static gboolean
write_temp_source(
    CrispyScriptPrivate  *priv,
    GError              **error
){
    gint fd;
    gchar *tmpl;

    tmpl = g_strdup("/tmp/crispy-XXXXXX.c");

    /* g_mkstemp modifies tmpl in-place with the actual filename */
    fd = g_mkstemp(tmpl);
    if (fd < 0)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_IO,
                    "Failed to create temp file from template: %s",
                    tmpl);
        g_free(tmpl);
        return FALSE;
    }

    priv->temp_source_path = tmpl;

    /* write the modified source */
    if (write(fd, priv->modified_source, priv->modified_len) < 0)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_IO,
                    "Failed to write temp source: %s",
                    g_strerror(errno));
        close(fd);
        return FALSE;
    }

    close(fd);
    return TRUE;
}

/* --- helper: build inline source wrapping --- */
static gchar *
build_inline_source(
    const gchar *code,
    const gchar *extra_includes
){
    GString *src;
    gchar **headers;
    gint i;

    src = g_string_new(NULL);

    /* default includes */
    g_string_append(src, "#include <glib.h>\n");
    g_string_append(src, "#include <gio/gio.h>\n");
    g_string_append(src, "#include <glib-object.h>\n");

    /* extra includes from -I flag */
    if (extra_includes != NULL && extra_includes[0] != '\0')
    {
        headers = g_strsplit(extra_includes, ";", -1);
        for (i = 0; headers[i] != NULL; i++)
        {
            g_strstrip(headers[i]);
            if (headers[i][0] != '\0')
                g_string_append_printf(src, "#include <%s>\n", headers[i]);
        }
        g_strfreev(headers);
    }

    /* wrap code in main() */
    g_string_append(src, "\ngint\nmain(\n"
                         "    gint    argc,\n"
                         "    gchar   **argv\n"
                         "){\n");
    g_string_append(src, "    ");
    g_string_append(src, code);
    g_string_append(src, "\n}\n");

    return g_string_free(src, FALSE);
}

/* --- GObject lifecycle --- */

static void
crispy_script_finalize(
    GObject *object
){
    CrispyScriptPrivate *priv;

    priv = crispy_script_get_instance_private(CRISPY_SCRIPT(object));

    /* close module if still loaded */
    if (priv->module != NULL)
        g_module_close(priv->module);

    /* clean up temp file unless preserve flag is set */
    if (priv->temp_source_path != NULL &&
        !(priv->flags & CRISPY_FLAG_PRESERVE_SOURCE))
    {
        g_unlink(priv->temp_source_path);
    }

    g_clear_object(&priv->compiler);
    g_clear_object(&priv->cache);

    g_free(priv->source_path);
    g_free(priv->source_content);
    g_free(priv->crispy_params);
    g_free(priv->expanded_params);
    g_free(priv->modified_source);
    g_free(priv->temp_source_path);
    g_free(priv->hash);

    G_OBJECT_CLASS(crispy_script_parent_class)->finalize(object);
}

static void
crispy_script_class_init(
    CrispyScriptClass *klass
){
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = crispy_script_finalize;
}

static void
crispy_script_init(
    CrispyScript *self
){
    (void)self;
}

/* --- constructors --- */

CrispyScript *
crispy_script_new_from_file(
    const gchar          *path,
    CrispyCompiler       *compiler,
    CrispyCacheProvider  *cache,
    CrispyFlags           flags,
    GError              **error
){
    CrispyScript *self;
    CrispyScriptPrivate *priv;

    g_return_val_if_fail(path != NULL, NULL);
    g_return_val_if_fail(CRISPY_IS_COMPILER(compiler), NULL);
    g_return_val_if_fail(CRISPY_IS_CACHE_PROVIDER(cache), NULL);

    /* read the source file */
    self = g_object_new(CRISPY_TYPE_SCRIPT, NULL);
    priv = crispy_script_get_instance_private(self);

    priv->compiler = g_object_ref(compiler);
    priv->cache = g_object_ref(cache);
    priv->flags = flags;
    priv->source_path = g_strdup(path);

    if (!g_file_get_contents(path, &priv->source_content,
                             &priv->source_len, error))
    {
        g_object_unref(self);
        return NULL;
    }

    /* parse CRISPY_PARAMS and strip shebang */
    parse_crispy_params(priv);

    return self;
}

CrispyScript *
crispy_script_new_from_inline(
    const gchar          *code,
    const gchar          *extra_includes,
    CrispyCompiler       *compiler,
    CrispyCacheProvider  *cache,
    CrispyFlags           flags,
    GError              **error
){
    CrispyScript *self;
    CrispyScriptPrivate *priv;

    g_return_val_if_fail(code != NULL, NULL);
    g_return_val_if_fail(CRISPY_IS_COMPILER(compiler), NULL);
    g_return_val_if_fail(CRISPY_IS_CACHE_PROVIDER(cache), NULL);

    (void)error;

    self = g_object_new(CRISPY_TYPE_SCRIPT, NULL);
    priv = crispy_script_get_instance_private(self);

    priv->compiler = g_object_ref(compiler);
    priv->cache = g_object_ref(cache);
    priv->flags = flags;
    priv->source_path = NULL;

    /* build the full source from inline code */
    priv->source_content = build_inline_source(code, extra_includes);
    priv->source_len = strlen(priv->source_content);

    /* inline source has no CRISPY_PARAMS or shebang */
    priv->modified_source = g_strdup(priv->source_content);
    priv->modified_len = priv->source_len;

    return self;
}

CrispyScript *
crispy_script_new_from_stdin(
    CrispyCompiler       *compiler,
    CrispyCacheProvider  *cache,
    CrispyFlags           flags,
    GError              **error
){
    CrispyScript *self;
    CrispyScriptPrivate *priv;
    GString *buf;
    gchar tmp[4096];

    g_return_val_if_fail(CRISPY_IS_COMPILER(compiler), NULL);
    g_return_val_if_fail(CRISPY_IS_CACHE_PROVIDER(cache), NULL);

    (void)error;

    /* read all of stdin */
    buf = g_string_new(NULL);
    while (fgets(tmp, sizeof(tmp), stdin) != NULL)
        g_string_append(buf, tmp);

    self = g_object_new(CRISPY_TYPE_SCRIPT, NULL);
    priv = crispy_script_get_instance_private(self);

    priv->compiler = g_object_ref(compiler);
    priv->cache = g_object_ref(cache);
    priv->flags = flags;
    priv->source_path = NULL;

    priv->source_content = g_string_free(buf, FALSE);
    priv->source_len = strlen(priv->source_content);

    /* parse CRISPY_PARAMS and strip shebang from stdin source too */
    parse_crispy_params(priv);

    return self;
}

/* --- execution --- */

gint
crispy_script_execute(
    CrispyScript  *self,
    gint           argc,
    gchar        **argv,
    GError       **error
){
    CrispyScriptPrivate *priv;
    const gchar *compiler_version;
    g_autofree gchar *cached_so_path = NULL;
    CrispyMainFunc main_func;
    gboolean cache_hit;

    g_return_val_if_fail(CRISPY_IS_SCRIPT(self), -1);

    priv = crispy_script_get_instance_private(self);
    priv->exit_code = -1;

    /* shell-expand CRISPY_PARAMS */
    priv->expanded_params = shell_expand(priv->crispy_params, error);
    if (priv->expanded_params == NULL)
        return -1;

    /* compute cache hash */
    compiler_version = crispy_compiler_get_version(priv->compiler);
    priv->hash = crispy_cache_provider_compute_hash(
        priv->cache,
        priv->source_content,
        (gssize)priv->source_len,
        priv->expanded_params,
        compiler_version);

    /* build cached .so path (get_path returns path with .so extension) */
    cached_so_path = crispy_cache_provider_get_path(priv->cache, priv->hash);

    /* check cache */
    cache_hit = FALSE;
    if (!(priv->flags & CRISPY_FLAG_FORCE_COMPILE))
    {
        cache_hit = crispy_cache_provider_has_valid(
            priv->cache, priv->hash, priv->source_path);
    }

    if (!cache_hit)
    {
        /* write temp source */
        if (!write_temp_source(priv, error))
            return -1;

        /* dry-run: just show what would happen */
        if (priv->flags & CRISPY_FLAG_DRY_RUN)
        {
            g_print("Would compile: %s -> %s\n",
                    priv->temp_source_path, cached_so_path);
            g_print("Extra flags: %s\n",
                    priv->expanded_params != NULL ? priv->expanded_params : "(none)");
            priv->exit_code = 0;
            return 0;
        }

        /* gdb mode: compile as executable and exec gdb */
        if (priv->flags & CRISPY_FLAG_GDB)
        {
            g_autofree gchar *exe_path = NULL;
            gchar **gdb_argv;
            gint gdb_argc;
            gint i;

            exe_path = g_strdup_printf("/tmp/crispy-dbg-%d", getpid());

            if (!crispy_compiler_compile_executable(
                    priv->compiler,
                    priv->temp_source_path,
                    exe_path,
                    priv->expanded_params,
                    error))
            {
                return -1;
            }

            /* build gdb argument vector: gdb --args <exe> [script args...] */
            gdb_argc = 3 + argc;
            gdb_argv = g_new0(gchar *, gdb_argc + 1);
            gdb_argv[0] = g_strdup("gdb");
            gdb_argv[1] = g_strdup("--args");
            gdb_argv[2] = g_strdup(exe_path);
            for (i = 0; i < argc; i++)
                gdb_argv[3 + i] = g_strdup(argv[i]);
            gdb_argv[gdb_argc] = NULL;

            /* exec gdb -- this replaces the process */
            execvp("gdb", gdb_argv);

            /* if we get here, exec failed */
            g_set_error(error,
                        CRISPY_ERROR,
                        CRISPY_ERROR_IO,
                        "Failed to exec gdb: %s",
                        g_strerror(errno));
            g_strfreev(gdb_argv);
            return -1;
        }

        /* normal compilation: compile to shared object */
        if (!crispy_compiler_compile_shared(
                priv->compiler,
                priv->temp_source_path,
                cached_so_path,
                priv->expanded_params,
                error))
        {
            return -1;
        }
    }

    /* load the compiled shared object */
    priv->module = g_module_open(cached_so_path, G_MODULE_BIND_LAZY);
    if (priv->module == NULL)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_LOAD,
                    "Failed to load module: %s",
                    g_module_error());
        return -1;
    }

    /* look up the main symbol */
    main_func = NULL;
    if (!g_module_symbol(priv->module, "main", (gpointer *)&main_func))
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_NO_MAIN,
                    "No main() function found in script");
        return -1;
    }

    /* execute the script */
    priv->exit_code = main_func(argc, argv);

    return priv->exit_code;
}

gint
crispy_script_get_exit_code(
    CrispyScript *self
){
    CrispyScriptPrivate *priv;

    g_return_val_if_fail(CRISPY_IS_SCRIPT(self), -1);

    priv = crispy_script_get_instance_private(self);
    return priv->exit_code;
}

const gchar *
crispy_script_get_temp_source_path(
    CrispyScript *self
){
    CrispyScriptPrivate *priv;

    g_return_val_if_fail(CRISPY_IS_SCRIPT(self), NULL);

    priv = crispy_script_get_instance_private(self);
    return priv->temp_source_path;
}
