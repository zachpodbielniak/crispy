/* crispy-repl.c - Interactive REPL for evaluating C expressions */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * #CrispyRepl provides a read-eval-print loop for C code snippets.
 * Each evaluated line is wrapped in an entry function (_crispy_eval),
 * compiled as a shared library, loaded with g_module_open(), and
 * executed.  Lines that begin with #include, #define, or that define
 * functions/types are accumulated into a preamble prepended to every
 * subsequent evaluation.
 *
 * Features:
 *   - readline support for line editing and persistent history
 *   - multiline input (tracks brace/paren depth)
 *   - auto-print for bare expressions (lines without trailing ';')
 *   - meta-commands: .help, .clear, .preamble, .quit
 *   - proper gcc error display on compilation failure
 *   - function/struct/typedef/enum preamble accumulation
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
#include <readline/readline.h>
#include <readline/history.h>

/* ------------------------------------------------------------------ */
/* property / signal enums                                             */
/* ------------------------------------------------------------------ */

enum
{
    PROP_0,
    PROP_PROMPT,
    N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

enum
{
    SIGNAL_LINE_EVALUATED,
    SIGNAL_ERROR_OCCURRED,
    N_SIGNALS
};

static guint obj_signals[N_SIGNALS];

/* ------------------------------------------------------------------ */
/* private struct                                                       */
/* ------------------------------------------------------------------ */

struct _CrispyRepl
{
    GObject              parent_instance;

    CrispyCompiler      *compiler;
    CrispyCacheProvider *cache;

    gchar               *prompt;
    gchar               *cont_prompt;    /* continuation prompt for multiline */
    gchar               *extra_flags;
    GString             *preamble;       /* accumulated #include / #define / functions */
    guint                eval_count;     /* unique temp file counter */
};

G_DEFINE_FINAL_TYPE(CrispyRepl, crispy_repl, G_TYPE_OBJECT)

/* ------------------------------------------------------------------ */
/* GObject property accessors                                          */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* GObject finalize                                                     */
/* ------------------------------------------------------------------ */

static void
crispy_repl_finalize(
    GObject *object
){
    CrispyRepl *self;

    self = CRISPY_REPL(object);

    g_clear_object(&self->compiler);
    g_clear_object(&self->cache);

    g_free(self->prompt);
    g_free(self->cont_prompt);
    g_free(self->extra_flags);

    if (self->preamble != NULL)
        g_string_free(self->preamble, TRUE);

    G_OBJECT_CLASS(crispy_repl_parent_class)->finalize(object);
}

/* ------------------------------------------------------------------ */
/* class init                                                           */
/* ------------------------------------------------------------------ */

static void
crispy_repl_class_init(
    CrispyReplClass *klass
){
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);

    object_class->finalize     = crispy_repl_finalize;
    object_class->set_property = crispy_repl_set_property;
    object_class->get_property = crispy_repl_get_property;

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
     * @repl: the #CrispyRepl
     * @code: the C code that was evaluated
     * @exit_code: exit code returned by the executed code
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
     * @repl: the #CrispyRepl
     * @code: the C code that failed
     * @error: a #GError describing the failure
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

/* ------------------------------------------------------------------ */
/* instance init                                                        */
/* ------------------------------------------------------------------ */

static void
crispy_repl_init(
    CrispyRepl *self
){
    self->prompt      = g_strdup("crispy> ");
    self->cont_prompt = g_strdup("  ...>  ");
    self->preamble    = g_string_new(NULL);
    self->eval_count  = 0;
}

/* ------------------------------------------------------------------ */
/* constructor                                                          */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* property setters / getters                                           */
/* ------------------------------------------------------------------ */

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

void
crispy_repl_reset(
    CrispyRepl *self
){
    g_return_if_fail(CRISPY_IS_REPL(self));

    g_string_truncate(self->preamble, 0);
    self->eval_count = 0;
}

const gchar *
crispy_repl_get_preamble(
    CrispyRepl *self
){
    g_return_val_if_fail(CRISPY_IS_REPL(self), NULL);
    return self->preamble->str;
}

/* ------------------------------------------------------------------ */
/* helpers: brace depth tracking for multiline input                    */
/* ------------------------------------------------------------------ */

/*
 * compute_depth_delta:
 * @line: a line of C code
 *
 * Counts unmatched opening/closing braces, parens, and brackets,
 * ignoring those inside string literals, char literals, and comments.
 *
 * Returns: net depth change (positive = more opens, negative = more closes)
 */
static gint
compute_depth_delta(
    const gchar *line
){
    gint   delta;
    gint   i;
    gint   len;
    gchar  c;

    delta = 0;
    len   = (gint)strlen(line);

    for (i = 0; i < len; i++)
    {
        c = line[i];

        /* skip string literals */
        if (c == '"')
        {
            i++;
            while (i < len && line[i] != '"')
            {
                if (line[i] == '\\')
                    i++;
                i++;
            }
            continue;
        }

        /* skip char literals */
        if (c == '\'')
        {
            i++;
            while (i < len && line[i] != '\'')
            {
                if (line[i] == '\\')
                    i++;
                i++;
            }
            continue;
        }

        /* skip line comments */
        if (c == '/' && i + 1 < len && line[i + 1] == '/')
            break;

        /* skip block comments */
        if (c == '/' && i + 1 < len && line[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < len && !(line[i] == '*' && line[i + 1] == '/'))
                i++;
            i++; /* skip past '/' */
            continue;
        }

        if (c == '{' || c == '(' || c == '[')
            delta++;
        else if (c == '}' || c == ')' || c == ']')
            delta--;
    }

    return delta;
}

/* ------------------------------------------------------------------ */
/* helpers: detect preamble-worthy code                                 */
/* ------------------------------------------------------------------ */

/*
 * is_preamble_code:
 * @code: trimmed C code
 *
 * Returns %TRUE if the code should be accumulated in the preamble
 * rather than executed in the entry function.  This covers:
 *   - preprocessor directives (#include, #define, #ifdef, etc.)
 *   - function definitions (type name(args) { ... })
 *   - typedef / struct / enum / union declarations
 *
 * Returns: %TRUE if preamble material
 */
static gboolean
is_preamble_code(
    const gchar *code
){
    /* preprocessor directives */
    if (code[0] == '#')
        return TRUE;

    /* type/struct/enum/union declarations */
    if (g_str_has_prefix(code, "typedef ") ||
        g_str_has_prefix(code, "struct ") ||
        g_str_has_prefix(code, "enum ") ||
        g_str_has_prefix(code, "union "))
    {
        return TRUE;
    }

    /*
     * Function definitions: heuristic detection.  A function definition
     * has the pattern:  [static] type name(...) {
     * We check if the code contains a '{' and a '(' and starts with
     * what looks like a type keyword.  This is imperfect but covers
     * the common cases.
     */
    if (strchr(code, '(') != NULL && strchr(code, '{') != NULL)
    {
        const gchar *p;

        p = code;

        /* skip "static " or "inline " */
        if (g_str_has_prefix(p, "static "))
            p += 7;
        if (g_str_has_prefix(p, "inline "))
            p += 7;

        /* common return type prefixes that indicate a function definition */
        if (g_str_has_prefix(p, "void ") ||
            g_str_has_prefix(p, "int ") ||
            g_str_has_prefix(p, "gint ") ||
            g_str_has_prefix(p, "guint ") ||
            g_str_has_prefix(p, "gboolean ") ||
            g_str_has_prefix(p, "gchar ") ||
            g_str_has_prefix(p, "char ") ||
            g_str_has_prefix(p, "long ") ||
            g_str_has_prefix(p, "short ") ||
            g_str_has_prefix(p, "unsigned ") ||
            g_str_has_prefix(p, "signed ") ||
            g_str_has_prefix(p, "float ") ||
            g_str_has_prefix(p, "double ") ||
            g_str_has_prefix(p, "gchar *") ||
            g_str_has_prefix(p, "char *") ||
            g_str_has_prefix(p, "const ") ||
            g_str_has_prefix(p, "gpointer ") ||
            g_str_has_prefix(p, "gconstpointer ") ||
            g_str_has_prefix(p, "gsize ") ||
            g_str_has_prefix(p, "gssize ") ||
            g_str_has_prefix(p, "GList ") ||
            g_str_has_prefix(p, "GSList ") ||
            g_str_has_prefix(p, "GPtrArray ") ||
            g_str_has_prefix(p, "GHashTable ") ||
            g_str_has_prefix(p, "GString "))
        {
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * is_expression:
 * @code: trimmed C code
 *
 * Returns %TRUE if the code looks like a bare expression that should
 * be auto-printed.  An expression is code that does not end with ';'
 * and does not start with a control-flow keyword.
 */
static gboolean
is_expression(
    const gchar *code
){
    gsize len;

    len = strlen(code);
    if (len == 0)
        return FALSE;

    /* if it ends with ';', it's a statement, not an expression */
    if (code[len - 1] == ';')
        return FALSE;

    /* if it ends with '}', it's a block */
    if (code[len - 1] == '}')
        return FALSE;

    /* control flow keywords are not expressions */
    if (g_str_has_prefix(code, "if ") ||
        g_str_has_prefix(code, "if(") ||
        g_str_has_prefix(code, "for ") ||
        g_str_has_prefix(code, "for(") ||
        g_str_has_prefix(code, "while ") ||
        g_str_has_prefix(code, "while(") ||
        g_str_has_prefix(code, "switch ") ||
        g_str_has_prefix(code, "switch(") ||
        g_str_has_prefix(code, "return ") ||
        g_str_has_prefix(code, "goto ") ||
        g_str_has_prefix(code, "break") ||
        g_str_has_prefix(code, "continue"))
    {
        return FALSE;
    }

    /* variable declarations are not expressions */
    if (g_str_has_prefix(code, "int ") ||
        g_str_has_prefix(code, "char ") ||
        g_str_has_prefix(code, "void ") ||
        g_str_has_prefix(code, "long ") ||
        g_str_has_prefix(code, "short ") ||
        g_str_has_prefix(code, "float ") ||
        g_str_has_prefix(code, "double ") ||
        g_str_has_prefix(code, "unsigned ") ||
        g_str_has_prefix(code, "signed ") ||
        g_str_has_prefix(code, "const ") ||
        g_str_has_prefix(code, "gint ") ||
        g_str_has_prefix(code, "guint ") ||
        g_str_has_prefix(code, "gchar ") ||
        g_str_has_prefix(code, "gboolean ") ||
        g_str_has_prefix(code, "gpointer ") ||
        g_str_has_prefix(code, "gsize ") ||
        g_str_has_prefix(code, "gssize ") ||
        g_str_has_prefix(code, "g_auto") ||
        g_str_has_prefix(code, "GList ") ||
        g_str_has_prefix(code, "GSList ") ||
        g_str_has_prefix(code, "GString ") ||
        g_str_has_prefix(code, "GError ") ||
        g_str_has_prefix(code, "GPtrArray ") ||
        g_str_has_prefix(code, "GHashTable "))
    {
        return FALSE;
    }

    return TRUE;
}

/* ------------------------------------------------------------------ */
/* helpers: source generation                                           */
/* ------------------------------------------------------------------ */

/*
 * build_eval_source:
 * @preamble: accumulated preprocessor/function/type lines
 * @code: the user's code snippet
 * @auto_print: whether to wrap the code in auto-print logic
 *
 * Wraps @code in a compilable translation unit with _crispy_eval as
 * the entry point (avoids special handling of main()).
 *
 * Returns: (transfer full): the complete source text
 */
static gchar *
build_eval_source(
    const gchar *preamble,
    const gchar *code,
    gboolean     auto_print
){
    GString *src;

    src = g_string_new(NULL);

    /* default includes */
    g_string_append(src, "#include <stdio.h>\n");
    g_string_append(src, "#include <stdlib.h>\n");
    g_string_append(src, "#include <string.h>\n");
    g_string_append(src, "#include <glib.h>\n");
    g_string_append(src, "#include <gio/gio.h>\n");

    /* accumulated preamble from prior evaluations */
    if (preamble != NULL && preamble[0] != '\0')
    {
        g_string_append(src, preamble);
        if (preamble[strlen(preamble) - 1] != '\n')
            g_string_append_c(src, '\n');
    }

    /*
     * Entry function.  Using _crispy_eval instead of main avoids
     * any special compiler/linker treatment of the main symbol.
     */
    g_string_append(src,
                    "\nint\n_crispy_eval(void)\n{\n");

    if (auto_print)
    {
        /*
         * Auto-print: wrap the expression in printf.  We try casting
         * to double first (handles int, float, double, char, short,
         * long).  If the expression is a string, this will fail at
         * compile time and the caller will retry with a string format.
         */
        g_string_append(src, "    printf(\"=> %g\\n\", (double)(");
        g_string_append(src, code);
        g_string_append(src, "));\n");
    }
    else
    {
        g_string_append(src, "    ");
        g_string_append(src, code);

        /* append trailing newline if needed */
        if (code[strlen(code) - 1] != '\n')
            g_string_append_c(src, '\n');

        /* add return 0 if user code doesn't already return */
        if (strstr(code, "return") == NULL)
            g_string_append(src, "    return 0;\n");
    }

    g_string_append(src, "    return 0;\n}\n");

    return g_string_free(src, FALSE);
}

/*
 * build_string_print_source:
 *
 * Like build_eval_source with auto_print but uses %s for string expressions.
 */
static gchar *
build_string_print_source(
    const gchar *preamble,
    const gchar *code
){
    GString *src;

    src = g_string_new(NULL);

    g_string_append(src, "#include <stdio.h>\n");
    g_string_append(src, "#include <stdlib.h>\n");
    g_string_append(src, "#include <string.h>\n");
    g_string_append(src, "#include <glib.h>\n");
    g_string_append(src, "#include <gio/gio.h>\n");

    if (preamble != NULL && preamble[0] != '\0')
    {
        g_string_append(src, preamble);
        if (preamble[strlen(preamble) - 1] != '\n')
            g_string_append_c(src, '\n');
    }

    g_string_append(src, "\nint\n_crispy_eval(void)\n{\n");
    g_string_append(src, "    printf(\"=> %s\\n\", (const char *)(");
    g_string_append(src, code);
    g_string_append(src, "));\n");
    g_string_append(src, "    return 0;\n}\n");

    return g_string_free(src, FALSE);
}

/*
 * build_pointer_print_source:
 *
 * Auto-print fallback using %p for pointer expressions.
 */
static gchar *
build_pointer_print_source(
    const gchar *preamble,
    const gchar *code
){
    GString *src;

    src = g_string_new(NULL);

    g_string_append(src, "#include <stdio.h>\n");
    g_string_append(src, "#include <stdlib.h>\n");
    g_string_append(src, "#include <string.h>\n");
    g_string_append(src, "#include <glib.h>\n");
    g_string_append(src, "#include <gio/gio.h>\n");

    if (preamble != NULL && preamble[0] != '\0')
    {
        g_string_append(src, preamble);
        if (preamble[strlen(preamble) - 1] != '\n')
            g_string_append_c(src, '\n');
    }

    g_string_append(src, "\nint\n_crispy_eval(void)\n{\n");
    g_string_append(src, "    printf(\"=> %p\\n\", (void *)(");
    g_string_append(src, code);
    g_string_append(src, "));\n");
    g_string_append(src, "    return 0;\n}\n");

    return g_string_free(src, FALSE);
}

/* ------------------------------------------------------------------ */
/* helpers: compile and execute                                         */
/* ------------------------------------------------------------------ */

typedef int (*CrispyEvalFunc)(void);

/*
 * try_compile_source:
 *
 * Writes source to a temp file, compiles to .so, returns path.
 * Returns NULL on failure (sets @error with gcc diagnostic).
 */
static gchar *
try_compile_source(
    CrispyRepl   *self,
    const gchar  *source,
    GError      **error
){
    g_autofree gchar *temp_path = NULL;
    g_autofree gchar *compile_flags = NULL;
    gchar            *so_path;
    gchar            *hash;
    const gchar      *compiler_version;
    gint              fd;
    gsize             src_len;
    gssize            written;

    /* write source to temp file */
    temp_path = g_strdup_printf("/tmp/crispy-repl-%u-XXXXXX.c",
                                self->eval_count);
    fd = g_mkstemp(temp_path);
    if (fd < 0)
    {
        g_set_error(error, CRISPY_ERROR, CRISPY_ERROR_IO,
                    "Failed to create temp file");
        return NULL;
    }

    src_len = strlen(source);
    written = write(fd, source, src_len);
    close(fd);

    if (written < 0 || (gsize)written != src_len)
    {
        g_unlink(temp_path);
        g_set_error(error, CRISPY_ERROR, CRISPY_ERROR_IO,
                    "Failed to write REPL source to temp file");
        return NULL;
    }

    /* compute cache path */
    compile_flags = g_strdup(self->extra_flags != NULL
                             ? self->extra_flags : "");
    compiler_version = crispy_compiler_get_version(self->compiler);

    hash = crispy_cache_provider_compute_hash(
        self->cache, source, (gssize)strlen(source),
        compile_flags, compiler_version);

    so_path = crispy_cache_provider_get_path(self->cache, hash);
    g_free(hash);

    /* compile if not cached */
    if (!crispy_cache_provider_has_valid(self->cache, hash, NULL))
    {
        if (!crispy_compiler_compile_shared(self->compiler,
                                            temp_path,
                                            so_path,
                                            compile_flags[0] != '\0'
                                                ? compile_flags : NULL,
                                            error))
        {
            g_unlink(temp_path);
            g_free(so_path);
            return NULL;
        }
    }

    g_unlink(temp_path);
    return so_path;
}

/*
 * execute_module:
 *
 * Loads a compiled .so, finds _crispy_eval, calls it, returns exit code.
 */
static gint
execute_module(
    const gchar  *so_path,
    GError      **error
){
    GModule        *module;
    CrispyEvalFunc  eval_func;
    gint            exit_code;

    module = g_module_open(so_path, G_MODULE_BIND_LAZY);
    if (module == NULL)
    {
        g_set_error(error, CRISPY_ERROR, CRISPY_ERROR_LOAD,
                    "Failed to load module: %s", g_module_error());
        return -1;
    }

    eval_func = NULL;
    if (!g_module_symbol(module, "_crispy_eval", (gpointer *)&eval_func) ||
        eval_func == NULL)
    {
        g_set_error(error, CRISPY_ERROR, CRISPY_ERROR_NO_MAIN,
                    "No _crispy_eval symbol found in compiled module");
        g_module_close(module);
        return -1;
    }

    /* flush before executing user code so output ordering is correct */
    fflush(stdout);
    fflush(stderr);

    exit_code = eval_func();

    /* flush user code output */
    fflush(stdout);
    fflush(stderr);

    g_module_close(module);
    return exit_code;
}

/* ------------------------------------------------------------------ */
/* helpers: format gcc errors for display                               */
/* ------------------------------------------------------------------ */

/*
 * format_gcc_error:
 * @error_msg: raw GError message from compilation
 *
 * Extracts the useful part of a gcc compilation error for display.
 * Strips the "Command: ..." line and temp file paths, showing only
 * the diagnostic messages with simplified locations.
 *
 * Returns: (transfer full): formatted error string
 */
static gchar *
format_gcc_error(
    const gchar *error_msg
){
    gchar  **lines;
    GString *out;
    gint     i;

    if (error_msg == NULL)
        return g_strdup("(unknown error)");

    out   = g_string_new(NULL);
    lines = g_strsplit(error_msg, "\n", -1);

    for (i = 0; lines[i] != NULL; i++)
    {
        const gchar *line;

        line = lines[i];

        /* skip "Compilation failed:" prefix */
        if (g_str_has_prefix(line, "Compilation failed:"))
            continue;

        /* skip "Command: gcc ..." line */
        if (g_str_has_prefix(line, "Command: "))
            continue;

        /* skip empty lines */
        if (line[0] == '\0')
            continue;

        /*
         * Replace temp file paths (crispy-repl-N-XXXXXX.c) with
         * a simple "<repl>" indicator for readability.
         */
        if (strstr(line, "crispy-repl-") != NULL)
        {
            const gchar *colon;
            const gchar *rest;

            /* find the part after the filename — typically ":line:col:" */
            colon = strstr(line, ".c:");
            if (colon != NULL)
            {
                rest = colon + 2; /* skip ".c" to get ":line:col: ..." */
                g_string_append(out, "<repl>");
                g_string_append(out, rest);
            }
            else
            {
                g_string_append(out, line);
            }
        }
        else
        {
            g_string_append(out, line);
        }

        g_string_append_c(out, '\n');
    }

    g_strfreev(lines);
    return g_string_free(out, FALSE);
}

/* ------------------------------------------------------------------ */
/* crispy_repl_eval                                                     */
/* ------------------------------------------------------------------ */

gint
crispy_repl_eval(
    CrispyRepl   *self,
    const gchar  *code,
    GError      **error
){
    gchar   *source;
    gchar   *so_path;
    gint     exit_code;
    GError  *local_error;

    g_return_val_if_fail(CRISPY_IS_REPL(self), -1);
    g_return_val_if_fail(code != NULL, -1);

    /*
     * Preprocessor directives and function/type definitions are
     * accumulated in the preamble rather than compiled immediately.
     */
    if (is_preamble_code(code))
    {
        g_string_append(self->preamble, code);
        if (code[strlen(code) - 1] != '\n')
            g_string_append_c(self->preamble, '\n');

        g_signal_emit(self, obj_signals[SIGNAL_LINE_EVALUATED],
                      0, code, 0);
        return 0;
    }

    local_error = NULL;
    self->eval_count++;

    /*
     * Auto-print logic: if the code looks like a bare expression
     * (no trailing semicolon, no control-flow keyword), try compiling
     * with auto-print wrappers.  We attempt three formats:
     *   1. (double)(expr) — handles all numeric types
     *   2. (const char *)(expr) — handles strings
     *   3. (void *)(expr) — handles other pointers
     * If all three fail, fall back to executing as a plain statement.
     */
    if (is_expression(code))
    {
        /* attempt 1: numeric auto-print */
        source  = build_eval_source(self->preamble->str, code, TRUE);
        so_path = try_compile_source(self, source, &local_error);
        g_free(source);

        if (so_path != NULL)
        {
            exit_code = execute_module(so_path, &local_error);
            g_free(so_path);

            if (local_error == NULL)
            {
                g_signal_emit(self, obj_signals[SIGNAL_LINE_EVALUATED],
                              0, code, exit_code);
                return exit_code;
            }
        }

        /* attempt 2: string auto-print */
        g_clear_error(&local_error);
        source  = build_string_print_source(self->preamble->str, code);
        so_path = try_compile_source(self, source, &local_error);
        g_free(source);

        if (so_path != NULL)
        {
            exit_code = execute_module(so_path, &local_error);
            g_free(so_path);

            if (local_error == NULL)
            {
                g_signal_emit(self, obj_signals[SIGNAL_LINE_EVALUATED],
                              0, code, exit_code);
                return exit_code;
            }
        }

        /* attempt 3: pointer auto-print */
        g_clear_error(&local_error);
        source  = build_pointer_print_source(self->preamble->str, code);
        so_path = try_compile_source(self, source, &local_error);
        g_free(source);

        if (so_path != NULL)
        {
            exit_code = execute_module(so_path, &local_error);
            g_free(so_path);

            if (local_error == NULL)
            {
                g_signal_emit(self, obj_signals[SIGNAL_LINE_EVALUATED],
                              0, code, exit_code);
                return exit_code;
            }
        }

        /*
         * All auto-print attempts failed.  Fall through to try as
         * a plain statement (add trailing semicolon).
         */
        g_clear_error(&local_error);
    }

    /*
     * Standard evaluation: wrap in entry function as-is.
     * If the code doesn't end with ';', add one.
     */
    {
        g_autofree gchar *stmt = NULL;
        gsize code_len;

        code_len = strlen(code);
        if (code_len > 0 && code[code_len - 1] != ';' &&
            code[code_len - 1] != '}')
        {
            stmt = g_strdup_printf("%s;", code);
        }
        else
        {
            stmt = g_strdup(code);
        }

        source  = build_eval_source(self->preamble->str, stmt, FALSE);
        so_path = try_compile_source(self, source, &local_error);
        g_free(source);
    }

    if (so_path == NULL)
    {
        /* compilation failed — report the error */
        g_signal_emit(self, obj_signals[SIGNAL_ERROR_OCCURRED],
                      0, code, local_error);

        if (error != NULL)
            g_propagate_error(error, local_error);
        else
            g_error_free(local_error);

        return -1;
    }

    exit_code = execute_module(so_path, &local_error);
    g_free(so_path);

    if (local_error != NULL)
    {
        g_signal_emit(self, obj_signals[SIGNAL_ERROR_OCCURRED],
                      0, code, local_error);

        if (error != NULL)
            g_propagate_error(error, local_error);
        else
            g_error_free(local_error);

        return -1;
    }

    g_signal_emit(self, obj_signals[SIGNAL_LINE_EVALUATED],
                  0, code, exit_code);

    return exit_code;
}

/* ------------------------------------------------------------------ */
/* meta-command handlers                                                */
/* ------------------------------------------------------------------ */

static gboolean
handle_meta_command(
    CrispyRepl  *self,
    const gchar *line
){
    if (strcmp(line, ".help") == 0 || strcmp(line, ".h") == 0)
    {
        g_print("\n");
        g_print("  Meta-commands:\n");
        g_print("    .help      Show this help\n");
        g_print("    .clear     Reset preamble (includes, functions, etc.)\n");
        g_print("    .preamble  Show accumulated preamble\n");
        g_print("    .quit      Exit the REPL\n");
        g_print("\n");
        g_print("  Usage:\n");
        g_print("    Expressions (no trailing ';') are auto-printed:\n");
        g_print("      crispy> 1 + 2\n");
        g_print("      => 3\n");
        g_print("    Statements execute as-is:\n");
        g_print("      crispy> g_print(\"hello\\n\");\n");
        g_print("      hello\n");
        g_print("    #include / #define accumulate as preamble:\n");
        g_print("      crispy> #include <math.h>\n");
        g_print("    Function/struct/typedef definitions go to preamble:\n");
        g_print("      crispy> int square(int x) { return x * x; }\n");
        g_print("    Use { } for multi-line blocks (auto-detected):\n");
        g_print("      crispy> for (int i = 0; i < 3; i++) {\n");
        g_print("        ...>    g_print(\"%%d\\n\", i);\n");
        g_print("        ...>  }\n");
        g_print("\n");
        return TRUE;
    }

    if (strcmp(line, ".clear") == 0 || strcmp(line, ".c") == 0)
    {
        crispy_repl_reset(self);
        g_print("Preamble cleared.\n");
        return TRUE;
    }

    if (strcmp(line, ".preamble") == 0 || strcmp(line, ".p") == 0)
    {
        if (self->preamble->len == 0)
        {
            g_print("(preamble is empty)\n");
        }
        else
        {
            g_print("--- preamble ---\n");
            g_print("%s", self->preamble->str);
            g_print("--- end ---\n");
        }
        return TRUE;
    }

    if (strcmp(line, ".quit") == 0 || strcmp(line, ".q") == 0)
    {
        /* handled by caller — this just signals the intent */
        return TRUE;
    }

    return FALSE;
}

/* ------------------------------------------------------------------ */
/* crispy_repl_start                                                    */
/* ------------------------------------------------------------------ */

gboolean
crispy_repl_start(
    CrispyRepl  *self,
    GError     **error
){
    gchar    *line;
    GString  *accum;
    gint      depth;
    gint      eval_result;

    g_return_val_if_fail(CRISPY_IS_REPL(self), FALSE);

    (void)error;

    accum = g_string_new(NULL);
    depth = 0;

    /* welcome banner */
    g_print("Crispy REPL v%s — C expressions, compiled and executed.\n",
            "0.2.0");
    g_print("  Type .help for commands, .quit or Ctrl-D to exit.\n\n");

    /* configure readline */
    rl_bind_key('\t', rl_insert); /* disable tab completion for now */

    while (TRUE)
    {
        const gchar *prompt;

        prompt = (depth > 0) ? self->cont_prompt : self->prompt;

        line = readline(prompt);

        /* EOF — Ctrl-D */
        if (line == NULL)
        {
            g_print("\n");
            break;
        }

        /* skip empty lines unless in multiline mode */
        if (line[0] == '\0' && depth == 0)
        {
            free(line);
            continue;
        }

        /* exit commands */
        if (depth == 0 &&
            (strcmp(line, "exit") == 0 ||
             strcmp(line, "quit") == 0 ||
             strcmp(line, ".quit") == 0 ||
             strcmp(line, ".q") == 0))
        {
            free(line);
            break;
        }

        /* meta-commands (only when not in multiline mode) */
        if (depth == 0 && line[0] == '.')
        {
            g_autofree gchar *trimmed = NULL;

            trimmed = g_strstrip(g_strdup(line));
            if (handle_meta_command(self, trimmed))
            {
                /* add to history if it's not .quit */
                if (strcmp(trimmed, ".quit") != 0 &&
                    strcmp(trimmed, ".q") != 0)
                {
                    add_history(line);
                }
                else
                {
                    free(line);
                    break;
                }

                free(line);
                continue;
            }
        }

        /* accumulate multiline input */
        if (accum->len > 0)
            g_string_append_c(accum, '\n');
        g_string_append(accum, line);

        depth += compute_depth_delta(line);

        /* keep accumulating if braces are not balanced */
        if (depth > 0)
        {
            free(line);
            continue;
        }

        /* reset depth (might go negative from typos) */
        depth = 0;

        /* add complete input to history */
        if (accum->len > 0)
            add_history(accum->str);

        /* evaluate the complete input */
        {
            g_autoptr(GError) eval_error = NULL;

            eval_result = crispy_repl_eval(self, accum->str, &eval_error);

            if (eval_error != NULL)
            {
                g_autofree gchar *formatted = NULL;

                formatted = format_gcc_error(eval_error->message);
                fprintf(stderr, "\033[31merror:\033[0m %s", formatted);
            }
            else if (eval_result != 0 && eval_result != -1)
            {
                g_print("[exit %d]\n", eval_result);
            }
        }

        g_string_truncate(accum, 0);
        free(line);
    }

    g_string_free(accum, TRUE);
    return TRUE;
}
