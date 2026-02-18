/* crispy-script.c - Script lifecycle orchestrator */

#define CRISPY_COMPILATION
#include "crispy-script.h"
#include "crispy-source-utils-private.h"
#include "crispy-plugin-engine.h"
#include "crispy-plugin-engine-private.h"
#include "../interfaces/crispy-compiler.h"
#include "../interfaces/crispy-cache-provider.h"
#include "../crispy-types.h"
#include "../crispy-plugin.h"

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
    CrispyCompiler       *compiler;
    CrispyCacheProvider  *cache;
    CrispyPluginEngine   *plugin_engine;  /* NULL if no plugins loaded */

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

    /* config-injected compiler flags */
    gchar       *config_extra_flags;    /* prepended before CRISPY_PARAMS */
    gchar       *config_override_flags; /* appended after everything */

    gint         exit_code;
} CrispyScriptPrivate;

G_DEFINE_FINAL_TYPE_WITH_PRIVATE(CrispyScript, crispy_script, G_TYPE_OBJECT)

/*
 * parse_crispy_params:
 * @priv: script private data with source_content populated
 *
 * Extracts CRISPY_PARAMS from the source and produces a modified
 * copy with the shebang and CRISPY_PARAMS define removed.
 * Delegates to the shared source utility functions.
 */
static void
parse_crispy_params(
    CrispyScriptPrivate *priv
){
    priv->crispy_params = crispy_source_extract_params(priv->source_content);
    priv->modified_source = crispy_source_strip_header(
        priv->source_content, &priv->modified_len);
}

/*
 * shell_expand:
 * @params: raw CRISPY_PARAMS value
 * @error: return location for a #GError, or %NULL
 *
 * Thin wrapper around crispy_source_shell_expand() for local use.
 */
static gchar *
shell_expand(
    const gchar  *params,
    GError      **error
){
    return crispy_source_shell_expand(params, error);
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
    g_clear_object(&priv->plugin_engine);

    g_free(priv->source_path);
    g_free(priv->source_content);
    g_free(priv->crispy_params);
    g_free(priv->expanded_params);
    g_free(priv->modified_source);
    g_free(priv->temp_source_path);
    g_free(priv->hash);
    g_free(priv->config_extra_flags);
    g_free(priv->config_override_flags);

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

/* --- plugin engine setter --- */

void
crispy_script_set_plugin_engine(
    CrispyScript       *self,
    CrispyPluginEngine *engine
){
    CrispyScriptPrivate *priv;

    g_return_if_fail(CRISPY_IS_SCRIPT(self));

    priv = crispy_script_get_instance_private(self);

    g_clear_object(&priv->plugin_engine);
    if (engine != NULL)
        priv->plugin_engine = (CrispyPluginEngine *)g_object_ref(engine);
}

/* --- config flag setters --- */

void
crispy_script_set_extra_flags(
    CrispyScript *self,
    const gchar  *extra_flags
){
    CrispyScriptPrivate *priv;

    g_return_if_fail(CRISPY_IS_SCRIPT(self));

    priv = crispy_script_get_instance_private(self);

    g_free(priv->config_extra_flags);
    priv->config_extra_flags = g_strdup(extra_flags);
}

void
crispy_script_set_override_flags(
    CrispyScript *self,
    const gchar  *override_flags
){
    CrispyScriptPrivate *priv;

    g_return_if_fail(CRISPY_IS_SCRIPT(self));

    priv = crispy_script_get_instance_private(self);

    g_free(priv->config_override_flags);
    priv->config_override_flags = g_strdup(override_flags);
}

/* --- helper: dispatch hook if engine is set --- */
static CrispyHookResult
dispatch_hook(
    CrispyScriptPrivate *priv,
    CrispyHookPoint      hook_point,
    CrispyHookContext   *ctx
){
    if (priv->plugin_engine == NULL)
        return CRISPY_HOOK_CONTINUE;

    return crispy_plugin_engine_dispatch(priv->plugin_engine, hook_point, ctx);
}

/* --- helper: populate hook context from current private state --- */
static void
populate_hook_context(
    CrispyScriptPrivate *priv,
    CrispyHookContext   *ctx,
    const gchar         *cached_so_path,
    gboolean             cache_hit,
    gint                 argc,
    gchar              **argv,
    GError             **error
){
    const gchar *compiler_version;

    compiler_version = crispy_compiler_get_version(priv->compiler);

    ctx->source_path      = priv->source_path;
    ctx->source_content   = priv->source_content;
    ctx->source_len       = priv->source_len;
    ctx->crispy_params    = priv->crispy_params;
    ctx->expanded_params  = priv->expanded_params;
    ctx->hash             = priv->hash;
    ctx->cached_so_path   = cached_so_path;
    ctx->compiler_version = compiler_version;
    ctx->temp_source_path = priv->temp_source_path;
    ctx->flags            = priv->flags;
    ctx->cache_hit        = cache_hit;

    /* mutable fields */
    ctx->modified_source  = priv->modified_source;
    ctx->modified_len     = priv->modified_len;
    ctx->extra_flags      = NULL;
    ctx->argc             = argc;
    ctx->argv             = argv;
    ctx->force_recompile  = FALSE;

    ctx->exit_code        = priv->exit_code;

    ctx->error            = error;
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
    g_autofree gchar *compile_flags = NULL;
    CrispyMainFunc main_func;
    CrispyHookContext ctx;
    CrispyHookResult hook_result;
    gboolean cache_hit;
    gint64 t_start;
    gint64 t_phase;

    g_return_val_if_fail(CRISPY_IS_SCRIPT(self), -1);

    priv = crispy_script_get_instance_private(self);
    priv->exit_code = -1;

    memset(&ctx, 0, sizeof(ctx));
    t_start = g_get_monotonic_time();

    /*
     * [1] SOURCE_LOADED - source has been parsed, shebang/params stripped.
     * Plugins can inspect or modify the source here.
     */
    populate_hook_context(priv, &ctx, NULL, FALSE, argc, argv, error);
    hook_result = dispatch_hook(priv, CRISPY_HOOK_SOURCE_LOADED, &ctx);
    if (hook_result == CRISPY_HOOK_ABORT)
        return -1;

    /* apply source modifications from plugin */
    if (ctx.modified_source != NULL &&
        ctx.modified_source != priv->modified_source)
    {
        g_free(priv->modified_source);
        priv->modified_source = g_strdup(ctx.modified_source);
        priv->modified_len = ctx.modified_len;
    }

    /* [2] PARAMS_EXPANDED - shell-expand CRISPY_PARAMS */
    t_phase = g_get_monotonic_time();
    priv->expanded_params = shell_expand(priv->crispy_params, error);
    if (priv->expanded_params == NULL)
        return -1;
    ctx.time_param_expand = g_get_monotonic_time() - t_phase;

    populate_hook_context(priv, &ctx, NULL, FALSE, argc, argv, error);
    ctx.time_total = g_get_monotonic_time() - t_start;
    hook_result = dispatch_hook(priv, CRISPY_HOOK_PARAMS_EXPANDED, &ctx);
    if (hook_result == CRISPY_HOOK_ABORT)
        return -1;

    /* [3] HASH_COMPUTED - compute cache hash
     *
     * The hash must include ALL flags that affect compilation:
     * config extra_flags, expanded CRISPY_PARAMS, and config
     * override_flags.  Otherwise different config flag sets
     * produce the same hash and stale cache entries get reused.
     */
    t_phase = g_get_monotonic_time();
    compiler_version = crispy_compiler_get_version(priv->compiler);

    {
        g_autoptr(GString) hash_flags = g_string_new(NULL);

        if (priv->config_extra_flags != NULL &&
            priv->config_extra_flags[0] != '\0')
        {
            g_string_append(hash_flags, priv->config_extra_flags);
            g_string_append_c(hash_flags, ' ');
        }

        if (priv->expanded_params != NULL &&
            priv->expanded_params[0] != '\0')
        {
            g_string_append(hash_flags, priv->expanded_params);
            g_string_append_c(hash_flags, ' ');
        }

        if (priv->config_override_flags != NULL &&
            priv->config_override_flags[0] != '\0')
        {
            g_string_append(hash_flags, priv->config_override_flags);
        }

        priv->hash = crispy_cache_provider_compute_hash(
            priv->cache,
            priv->source_content,
            (gssize)priv->source_len,
            hash_flags->str,
            compiler_version);
    }
    ctx.time_hash = g_get_monotonic_time() - t_phase;

    /* build cached .so path */
    cached_so_path = crispy_cache_provider_get_path(priv->cache, priv->hash);

    populate_hook_context(priv, &ctx, cached_so_path, FALSE, argc, argv, error);
    ctx.time_total = g_get_monotonic_time() - t_start;
    hook_result = dispatch_hook(priv, CRISPY_HOOK_HASH_COMPUTED, &ctx);
    if (hook_result == CRISPY_HOOK_ABORT)
        return -1;

    /* [4] CACHE_CHECKED - check cache */
    t_phase = g_get_monotonic_time();
    cache_hit = FALSE;
    if (!(priv->flags & CRISPY_FLAG_FORCE_COMPILE))
    {
        cache_hit = crispy_cache_provider_has_valid(
            priv->cache, priv->hash, priv->source_path);
    }
    ctx.time_cache_check = g_get_monotonic_time() - t_phase;

    populate_hook_context(priv, &ctx, cached_so_path, cache_hit, argc, argv, error);
    ctx.time_total = g_get_monotonic_time() - t_start;
    hook_result = dispatch_hook(priv, CRISPY_HOOK_CACHE_CHECKED, &ctx);
    if (hook_result == CRISPY_HOOK_ABORT)
        return -1;
    if (hook_result == CRISPY_HOOK_FORCE_RECOMPILE || ctx.force_recompile)
        cache_hit = FALSE;

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

        /* [5] PRE_COMPILE */
        populate_hook_context(priv, &ctx, cached_so_path, cache_hit,
                              argc, argv, error);
        ctx.time_total = g_get_monotonic_time() - t_start;
        hook_result = dispatch_hook(priv, CRISPY_HOOK_PRE_COMPILE, &ctx);
        if (hook_result == CRISPY_HOOK_ABORT)
            return -1;

        /*
         * Build compile_flags with three-tier precedence.
         * gcc uses last-wins for conflicting flags, so order matters:
         *   1. config extra_flags    (defaults, lowest priority)
         *   2. CRISPY_PARAMS         (script-level overrides)
         *   3. plugin extra_flags    (from PRE_COMPILE hook)
         *   4. config override_flags (forced, highest priority)
         */
        {
            GString *flags_buf;

            flags_buf = g_string_new(NULL);

            /* tier 1: config extra_flags (defaults) */
            if (priv->config_extra_flags != NULL &&
                priv->config_extra_flags[0] != '\0')
            {
                g_string_append(flags_buf, priv->config_extra_flags);
            }

            /* tier 2: script's own CRISPY_PARAMS */
            if (priv->expanded_params != NULL &&
                priv->expanded_params[0] != '\0')
            {
                if (flags_buf->len > 0)
                    g_string_append_c(flags_buf, ' ');
                g_string_append(flags_buf, priv->expanded_params);
            }

            /* tier 3: plugin-injected extra_flags */
            if (ctx.extra_flags != NULL && ctx.extra_flags[0] != '\0')
            {
                if (flags_buf->len > 0)
                    g_string_append_c(flags_buf, ' ');
                g_string_append(flags_buf, ctx.extra_flags);
            }

            /* tier 4: config override_flags (highest priority) */
            if (priv->config_override_flags != NULL &&
                priv->config_override_flags[0] != '\0')
            {
                if (flags_buf->len > 0)
                    g_string_append_c(flags_buf, ' ');
                g_string_append(flags_buf, priv->config_override_flags);
            }

            compile_flags = g_string_free(flags_buf, FALSE);
        }

        /* normal compilation: compile to shared object */
        t_phase = g_get_monotonic_time();
        if (!crispy_compiler_compile_shared(
                priv->compiler,
                priv->temp_source_path,
                cached_so_path,
                compile_flags,
                error))
        {
            return -1;
        }
        ctx.time_compile = g_get_monotonic_time() - t_phase;

        /* [6] POST_COMPILE */
        populate_hook_context(priv, &ctx, cached_so_path, cache_hit,
                              argc, argv, error);
        ctx.time_total = g_get_monotonic_time() - t_start;
        hook_result = dispatch_hook(priv, CRISPY_HOOK_POST_COMPILE, &ctx);
        if (hook_result == CRISPY_HOOK_ABORT)
            return -1;
    }

    /* load the compiled shared object */
    t_phase = g_get_monotonic_time();
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
    ctx.time_module_load = g_get_monotonic_time() - t_phase;

    /* [7] MODULE_LOADED */
    populate_hook_context(priv, &ctx, cached_so_path, cache_hit,
                          argc, argv, error);
    ctx.time_total = g_get_monotonic_time() - t_start;
    hook_result = dispatch_hook(priv, CRISPY_HOOK_MODULE_LOADED, &ctx);
    if (hook_result == CRISPY_HOOK_ABORT)
        return -1;

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

    /* [8] PRE_EXECUTE - plugins can modify argc/argv here */
    populate_hook_context(priv, &ctx, cached_so_path, cache_hit,
                          argc, argv, error);
    ctx.time_total = g_get_monotonic_time() - t_start;
    hook_result = dispatch_hook(priv, CRISPY_HOOK_PRE_EXECUTE, &ctx);
    if (hook_result == CRISPY_HOOK_ABORT)
        return -1;
    /* use potentially modified argc/argv from plugin */
    argc = ctx.argc;
    argv = ctx.argv;

    /* execute the script */
    t_phase = g_get_monotonic_time();
    priv->exit_code = main_func(argc, argv);
    ctx.time_execute = g_get_monotonic_time() - t_phase;

    /* [9] POST_EXECUTE */
    ctx.time_total = g_get_monotonic_time() - t_start;
    populate_hook_context(priv, &ctx, cached_so_path, cache_hit,
                          argc, argv, error);
    ctx.exit_code = priv->exit_code;
    ctx.time_total = g_get_monotonic_time() - t_start;
    hook_result = dispatch_hook(priv, CRISPY_HOOK_POST_EXECUTE, &ctx);
    if (hook_result == CRISPY_HOOK_ABORT)
        return -1;

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
