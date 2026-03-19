/* crispy-repl.c - Interactive REPL for evaluating C expressions */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * #CrispyRepl provides a read-eval-print loop for C code snippets.
 * Each evaluated line is wrapped in a main() function, compiled as a
 * shared library, loaded with g_module_open(), and executed.
 * Lines that begin with #include or #define are accumulated into a
 * preamble that is prepended to every subsequent evaluation.
 */

#define CRISPY_COMPILATION
#include "crispy-repl.h"
#include "crispy-script.h"
#include "../interfaces/crispy-compiler.h"
#include "../interfaces/crispy-cache-provider.h"
#include "../crispy-types.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <gmodule.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/**
 * SECTION:crispy-repl
 * @title: CrispyRepl
 * @short_description: Interactive REPL for evaluating C expressions
 *
 * #CrispyRepl wraps each line of C code entered by the user in a full
 * compilation unit, compiles it to a shared library, and executes it
 * in-process.  Preprocessor directives (#include, #define) accumulate
 * in a preamble that is prepended to every subsequent evaluation so
 * that the user does not need to repeat them.
 *
 * Two signals are provided: #CrispyRepl::line-evaluated fires after
 * each successful evaluation, and #CrispyRepl::error-occurred fires
 * when compilation or loading fails.
 */

/* --- property enum --- */

enum
{
    PROP_0,
    PROP_PROMPT,
    N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

/* --- signal enum --- */

enum
{
    SIGNAL_LINE_EVALUATED,
    SIGNAL_ERROR_OCCURRED,
    N_SIGNALS
};

static guint obj_signals[N_SIGNALS];

/* --- private struct --- */

struct _CrispyRepl
{
    GObject              parent_instance;

    CrispyCompiler      *compiler;
    CrispyCacheProvider *cache;

    gchar               *prompt;
    gchar               *extra_flags;
    GString             *preamble;   /* accumulated #include / #define lines */
    guint                eval_count; /* number of evaluations (unique temp names) */
};

G_DEFINE_FINAL_TYPE(CrispyRepl, crispy_repl, G_TYPE_OBJECT)

/* -------------------------------------------------------------------------
 * GObject property accessors
 * ---------------------------------------------------------------------- */

static void
crispy_repl_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
){
    CrispyRepl *self;

    self = CRISPY_REPL(object);

    switch (prop_id)
    {
    case PROP_PROMPT:
        g_free(self->prompt);
        self->prompt = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
crispy_repl_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
){
    CrispyRepl *self;

    self = CRISPY_REPL(object);

    switch (prop_id)
    {
    case PROP_PROMPT:
        g_value_set_string(value, self->prompt);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/* -------------------------------------------------------------------------
 * GObject finalize
 * ---------------------------------------------------------------------- */

static void
crispy_repl_finalize(
    GObject *object
){
    CrispyRepl *self;

    self = CRISPY_REPL(object);

    g_clear_object(&self->compiler);
    g_clear_object(&self->cache);

    g_free(self->prompt);
    g_free(self->extra_flags);

    if (self->preamble != NULL)
        g_string_free(self->preamble, TRUE);

    G_OBJECT_CLASS(crispy_repl_parent_class)->finalize(object);
}

/* -------------------------------------------------------------------------
 * class init
 * ---------------------------------------------------------------------- */

static void
crispy_repl_class_init(
    CrispyReplClass *klass
){
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);

    object_class->finalize     = crispy_repl_finalize;
    object_class->set_property = crispy_repl_set_property;
    object_class->get_property = crispy_repl_get_property;

    /**
     * CrispyRepl:prompt:
     *
     * The prompt string displayed before each input line.
     * Default is "crispy> ".
     */
    obj_props[PROP_PROMPT] =
        g_param_spec_string("prompt",
                            "Prompt",
                            "Prompt string displayed before each input line",
                            "crispy> ",
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(object_class, N_PROPS, obj_props);

    /**
     * CrispyRepl::line-evaluated:
     * @repl: the #CrispyRepl that received the signal
     * @code: the C code that was evaluated
     * @exit_code: exit code returned by the executed code
     *
     * Emitted after a line of C code has been successfully compiled
     * and executed.
     */
    obj_signals[SIGNAL_LINE_EVALUATED] =
        g_signal_new("line-evaluated",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     2,
                     G_TYPE_STRING,
                     G_TYPE_INT);

    /**
     * CrispyRepl::error-occurred:
     * @repl: the #CrispyRepl that received the signal
     * @code: the C code that failed to compile or execute
     * @error: a #GError describing the failure
     *
     * Emitted when evaluation of a line of C code fails due to a
     * compilation or loading error.
     */
    obj_signals[SIGNAL_ERROR_OCCURRED] =
        g_signal_new("error-occurred",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     2,
                     G_TYPE_STRING,
                     G_TYPE_ERROR);
}

/* -------------------------------------------------------------------------
 * instance init
 * ---------------------------------------------------------------------- */

static void
crispy_repl_init(
    CrispyRepl *self
){
    self->prompt     = g_strdup("crispy> ");
    self->preamble   = g_string_new(NULL);
    self->eval_count = 0;
}

/* -------------------------------------------------------------------------
 * constructor
 * ---------------------------------------------------------------------- */

CrispyRepl *
crispy_repl_new(
    CrispyCompiler      *compiler,
    CrispyCacheProvider *cache
){
    CrispyRepl *self;

    g_return_val_if_fail(CRISPY_IS_COMPILER(compiler), NULL);
    g_return_val_if_fail(CRISPY_IS_CACHE_PROVIDER(cache), NULL);

    self = g_object_new(CRISPY_TYPE_REPL, NULL);

    self->compiler = (CrispyCompiler *)g_object_ref(compiler);
    self->cache    = (CrispyCacheProvider *)g_object_ref(cache);

    return self;
}

/* -------------------------------------------------------------------------
 * property setters / getters
 * ---------------------------------------------------------------------- */

void
crispy_repl_set_prompt(
    CrispyRepl  *self,
    const gchar *prompt
){
    g_return_if_fail(CRISPY_IS_REPL(self));

    g_free(self->prompt);
    self->prompt = g_strdup(prompt != NULL ? prompt : "crispy> ");
    g_object_notify_by_pspec(G_OBJECT(self), obj_props[PROP_PROMPT]);
}

const gchar *
crispy_repl_get_prompt(
    CrispyRepl *self
){
    g_return_val_if_fail(CRISPY_IS_REPL(self), NULL);
    return self->prompt;
}

void
crispy_repl_set_extra_flags(
    CrispyRepl  *self,
    const gchar *flags
){
    g_return_if_fail(CRISPY_IS_REPL(self));

    g_free(self->extra_flags);
    self->extra_flags = g_strdup(flags);
}

/* -------------------------------------------------------------------------
 * helper: build a complete source file for one evaluation
 * ---------------------------------------------------------------------- */

/*
 * build_eval_source:
 * @preamble: accumulated preprocessor lines
 * @code: the user's code snippet
 *
 * Wraps @code in a compilable translation unit.  If @code already
 * contains a "return" keyword the generated main() body is used as-is;
 * otherwise a trailing "return 0;" is appended.
 *
 * Returns: (transfer full): the complete source text
 */
static gchar *
build_eval_source(
    const gchar *preamble,
    const gchar *code
){
    GString     *src;
    gboolean     has_return;

    src = g_string_new(NULL);

    /* default includes */
    g_string_append(src, "#include <glib.h>\n");
    g_string_append(src, "#include <gio/gio.h>\n");

    /* accumulated preamble from prior evaluations */
    if (preamble != NULL && preamble[0] != '\0')
    {
        g_string_append(src, preamble);
        if (preamble[strlen(preamble) - 1] != '\n')
            g_string_append_c(src, '\n');
    }

    g_string_append(src,
                    "\nint\nmain(\n"
                    "    int    argc,\n"
                    "    char **argv\n"
                    "){\n"
                    "    (void)argc;\n"
                    "    (void)argv;\n"
                    "    ");

    g_string_append(src, code);

    /* append trailing newline if needed */
    if (code[strlen(code) - 1] != '\n')
        g_string_append_c(src, '\n');

    /* only add "return 0;" if the code doesn't already return */
    has_return = (strstr(code, "return") != NULL);
    if (!has_return)
        g_string_append(src, "    return 0;\n");

    g_string_append(src, "}\n");

    return g_string_free(src, FALSE);
}

/* -------------------------------------------------------------------------
 * crispy_repl_eval
 * ---------------------------------------------------------------------- */

gint
crispy_repl_eval(
    CrispyRepl   *self,
    const gchar  *code,
    GError      **error
){
    gchar         *source;
    gchar         *hash;
    gchar         *so_path;
    gchar         *temp_path;
    gchar         *compile_flags;
    GModule       *module;
    CrispyMainFunc main_func;
    gint           exit_code;
    gint           fd;
    const gchar   *compiler_version;
    GError        *local_error;

    g_return_val_if_fail(CRISPY_IS_REPL(self), -1);
    g_return_val_if_fail(code != NULL, -1);

    source        = NULL;
    hash          = NULL;
    so_path       = NULL;
    temp_path     = NULL;
    compile_flags = NULL;
    module        = NULL;
    exit_code     = -1;
    local_error   = NULL;

    /*
     * Preprocessor directives are added to the preamble rather than
     * compiled immediately — they will be prepended to every future eval.
     */
    if (g_str_has_prefix(code, "#include") ||
        g_str_has_prefix(code, "#define"))
    {
        g_string_append(self->preamble, code);
        if (code[strlen(code) - 1] != '\n')
            g_string_append_c(self->preamble, '\n');

        g_signal_emit(self,
                      obj_signals[SIGNAL_LINE_EVALUATED],
                      0,
                      code,
                      0);
        return 0;
    }

    /* Build the complete translation unit */
    source = build_eval_source(self->preamble->str, code);

    /* Compute cache hash */
    compile_flags = g_strdup(self->extra_flags != NULL ? self->extra_flags : "");
    compiler_version = crispy_compiler_get_version(self->compiler);

    hash = crispy_cache_provider_compute_hash(
        self->cache,
        source,
        (gssize)strlen(source),
        compile_flags,
        compiler_version);

    so_path = crispy_cache_provider_get_path(self->cache, hash);

    /* Write to a uniquely named temp file */
    temp_path = g_strdup_printf("/tmp/crispy-repl-%u-XXXXXX.c",
                                self->eval_count);
    fd = g_mkstemp(temp_path);
    if (fd < 0)
    {
        g_set_error(&local_error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_IO,
                    "Failed to create temp file for REPL evaluation");
        goto done;
    }

    {
        gsize   src_len;
        gssize  written;

        src_len = strlen(source);
        written = write(fd, source, src_len);
        close(fd);

        if (written < 0 || (gsize)written != src_len)
        {
            g_set_error(&local_error,
                        CRISPY_ERROR,
                        CRISPY_ERROR_IO,
                        "Failed to write REPL source to temp file");
            goto done;
        }
    }

    self->eval_count++;

    /* Compile if not cached */
    if (!crispy_cache_provider_has_valid(self->cache, hash, NULL))
    {
        if (!crispy_compiler_compile_shared(self->compiler,
                                            temp_path,
                                            so_path,
                                            compile_flags[0] != '\0'
                                                ? compile_flags : NULL,
                                            &local_error))
        {
            goto done;
        }
    }

    /* Load and execute */
    module = g_module_open(so_path, G_MODULE_BIND_LAZY);
    if (module == NULL)
    {
        g_set_error(&local_error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_LOAD,
                    "Failed to load REPL module: %s",
                    g_module_error());
        goto done;
    }

    main_func = NULL;
    if (!g_module_symbol(module, "main", (gpointer *)&main_func) ||
        main_func == NULL)
    {
        g_set_error(&local_error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_NO_MAIN,
                    "No main() symbol found in REPL module");
        goto done;
    }

    {
        gint    eval_argc;
        gchar  *eval_argv[2];

        eval_argv[0] = (gchar *)"crispy-repl";
        eval_argv[1] = NULL;
        eval_argc    = 1;

        exit_code = main_func(eval_argc, eval_argv);
    }

    g_signal_emit(self,
                  obj_signals[SIGNAL_LINE_EVALUATED],
                  0,
                  code,
                  exit_code);

done:
    if (module != NULL)
        g_module_close(module);

    if (temp_path != NULL)
    {
        g_unlink(temp_path);
        g_free(temp_path);
    }

    g_free(source);
    g_free(hash);
    g_free(so_path);
    g_free(compile_flags);

    if (local_error != NULL)
    {
        g_signal_emit(self,
                      obj_signals[SIGNAL_ERROR_OCCURRED],
                      0,
                      code,
                      local_error);

        if (error != NULL)
            g_propagate_error(error, local_error);
        else
            g_error_free(local_error);

        return -1;
    }

    return exit_code;
}

/* -------------------------------------------------------------------------
 * crispy_repl_start
 * ---------------------------------------------------------------------- */

gboolean
crispy_repl_start(
    CrispyRepl  *self,
    GError     **error
){
    gchar    line_buf[4096];
    gchar   *trimmed;
    gint     eval_result;

    g_return_val_if_fail(CRISPY_IS_REPL(self), FALSE);

    (void)error;

    /* Welcome banner */
    g_print("Crispy REPL — type C expressions and press Enter.\n");
    g_print("  #include / #define lines are accumulated as preamble.\n");
    g_print("  Type \"exit\" or \"quit\" or send EOF (Ctrl-D) to leave.\n\n");

    while (TRUE)
    {
        /* Print prompt to stderr so it is not mixed with program output */
        fprintf(stderr, "%s", self->prompt);
        fflush(stderr);

        if (fgets(line_buf, sizeof(line_buf), stdin) == NULL)
        {
            /* EOF — Ctrl-D or closed pipe */
            g_print("\n");
            break;
        }

        /* Trim trailing newline */
        trimmed = g_strstrip(line_buf);

        /* Skip empty lines */
        if (trimmed[0] == '\0')
            continue;

        /* Exit commands */
        if (strcmp(trimmed, "exit") == 0 ||
            strcmp(trimmed, "quit") == 0)
        {
            break;
        }

        eval_result = crispy_repl_eval(self, trimmed, NULL);

        if (eval_result != 0 && eval_result != -1)
        {
            g_print("[exit %d]\n", eval_result);
        }
    }

    return TRUE;
}
