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
 * subsequent evaluation. Scalar/pointer declarations instead own stable
 * module storage, bound into later modules without replaying initializers.
 *
 * Features:
 *   - readline support for line editing and persistent history
 *   - multiline input (tracks brace/paren depth)
 *   - auto-print for bare expressions (lines without trailing ';')
 *   - meta-commands: :help, :clear, :preamble, :quit
 *   - proper gcc error display on compilation failure
 *   - function/struct/typedef/enum preamble accumulation
 */

#ifndef CRISPY_COMPILATION
#define CRISPY_COMPILATION
#endif
#include "crispy-repl.h"
#include "crispy-script.h"
#include "crispy-temp-registry-private.h"
#include "crispy-header-tracker-private.h"
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
#include "../crispy-version.h"

/* Maximum entries saved to the persistent history file. */
#define REPL_HISTORY_MAX (1000)

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
    GPtrArray           *modules;        /* retained, newest unloaded first */
    GPtrArray           *storage;        /* borrowed addresses inside modules */
    GHashTable          *names;
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

    crispy_repl_reset(self);
    g_ptr_array_unref(self->modules);
    g_ptr_array_unref(self->storage);
    g_hash_table_unref(self->names);
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
    self->modules     = g_ptr_array_new();
    self->storage     = g_ptr_array_new();
    self->names       = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
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
    g_ptr_array_set_size(self->storage, 0);
    g_hash_table_remove_all(self->names);
    while (self->modules->len != 0)
    {
        GModule *module;

        module = g_ptr_array_index(self->modules, self->modules->len - 1);
        g_ptr_array_set_size(self->modules, self->modules->len - 1);
        g_module_close(module);
    }
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
/* public helpers for embedding interactive sessions                   */
/* ------------------------------------------------------------------ */

gboolean
crispy_repl_has_variable(
    CrispyRepl *self,
    const gchar *name
){
    g_return_val_if_fail(CRISPY_IS_REPL(self), FALSE);
    g_return_val_if_fail(name != NULL, FALSE);

    /* Only successfully committed declarations belong to this registry. */
    return g_hash_table_contains(self->names, name);
}

gboolean
crispy_repl_needs_continuation(
    const gchar *code
){
    g_autoptr(GString) logical = NULL;
    g_autoptr(GString) stack = NULL;
    gsize i;
    gchar quote;
    gchar last;
    gboolean block_comment;
    gboolean line_comment;
    gboolean trailing_splice;

    g_return_val_if_fail(code != NULL, FALSE);

    /* C removes escaped newlines before recognizing comments and literals.
     * Do the same, including CRLF input supplied by an embedding caller. */
    logical = g_string_new(NULL);
    trailing_splice = FALSE;
    for (i = 0; code[i] != '\0'; i++)
    {
        if (code[i] == '\\' &&
            (code[i + 1] == '\n' ||
             (code[i + 1] == '\r' && code[i + 2] == '\n')))
        {
            i += code[i + 1] == '\r' ? 2 : 1;
            trailing_splice = TRUE;
            continue;
        }
        g_string_append_c(logical, code[i]);
        if (!g_ascii_isspace(code[i]))
            trailing_splice = code[i] == '\\';
    }

    stack = g_string_new(NULL);
    quote = 0;
    last = 0;
    block_comment = FALSE;
    line_comment = FALSE;
    for (i = 0; i < logical->len; i++)
    {
        gchar c;

        c = logical->str[i];
        if (line_comment)
        {
            if (c == '\n')
                line_comment = FALSE;
            continue;
        }
        if (block_comment)
        {
            if (c == '*' && logical->str[i + 1] == '/')
            {
                i++;
                block_comment = FALSE;
            }
            continue;
        }
        if (quote != 0)
        {
            if (c == '\\' && i + 1 < logical->len)
                i++;
            else if (c == quote)
                quote = 0;
            continue;
        }
        if (c == '/' && logical->str[i + 1] == '/')
        {
            line_comment = TRUE;
            i++;
            continue;
        }
        if (c == '/' && logical->str[i + 1] == '*')
        {
            block_comment = TRUE;
            i++;
            continue;
        }
        if (g_ascii_isspace(c))
            continue;
        last = c;
        if (c == '"' || c == '\'')
            quote = c;
        if (c == '{' || c == '(' || c == '[')
            g_string_append_c(stack, c);
        else if (c == '}' || c == ')' || c == ']')
        {
            gchar expected;

            expected = c == '}' ? '{' : (c == ')' ? '(' : '[');
            /* More input cannot repair a mismatched closing delimiter. */
            if (stack->len == 0 || stack->str[stack->len - 1] != expected)
                return FALSE;
            g_string_truncate(stack, stack->len - 1);
        }
    }

    return stack->len != 0 || quote != 0 || block_comment ||
           trailing_splice || last == '=' || last == ',' || last == '\\';
}

/* ------------------------------------------------------------------ */
/* helpers: detect preamble-worthy code                                 */
/* ------------------------------------------------------------------ */

/*
 * text_ends_with_newline:
 * @text: (nullable): the text to inspect
 *
 * Answers whether @text already ends in a newline, so the caller knows
 * whether to add one.
 *
 * Three of the six places that asked this read text[strlen(text) - 1]
 * with nothing between them and an empty string, which reads the byte
 * before the allocation: `:load` on an empty file reaches one, and
 * crispy_repl_eval(repl, "") reaches another.  The other three tested
 * for an empty string first -- so the rule was known, and applied to the
 * call sites somebody looked at rather than to the question itself.
 *
 * Returns: %TRUE if @text is non-empty and its last character is a newline
 */
static gboolean
text_ends_with_newline(
    const gchar *text
){
    gsize len;

    if (text == NULL)
        return FALSE;

    len = strlen(text);
    if (len == 0)
        return FALSE;

    return text[len - 1] == '\n';
}

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
        g_regex_match_simple("^(struct|union|enum)\\s+[A-Za-z_][A-Za-z_0-9]*\\s*;\\s*$", code, 0, 0) ||
        ((g_str_has_prefix(code, "struct ") ||
          g_str_has_prefix(code, "enum ") ||
          g_str_has_prefix(code, "union ")) &&
         strchr(code, '{') != NULL &&
         g_regex_match_simple("}\\s*;\\s*$", code, 0, 0)))
    {
        return TRUE;
    }

    /* Require a return type and a named function before the opening brace,
     * not merely a call and a brace somewhere in a runtime initializer. */
    return g_regex_match_simple(
        "^[A-Za-z_][A-Za-z_0-9\\s*]*[\\s*]+"
        "[A-Za-z_][A-Za-z_0-9]*\\s*\\([^;={}]*\\)\\s*\\{",
        code, 0, 0);
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
        g_str_has_prefix(code, "GHashTable ") ||
        g_str_has_prefix(code, "GBytes ") ||
        g_str_has_prefix(code, "GObject ") ||
        g_str_has_prefix(code, "GFile ") ||
        g_str_has_prefix(code, "GVariant ") ||
        g_str_has_prefix(code, "FILE ") ||
        g_str_has_prefix(code, "size_t ") ||
        g_str_has_prefix(code, "ssize_t ") ||
        g_str_has_prefix(code, "gint64 ") ||
        g_str_has_prefix(code, "guint64 ") ||
        g_str_has_prefix(code, "gdouble ") ||
        g_str_has_prefix(code, "gfloat ") ||
        g_str_has_prefix(code, "GError ") ||
        g_str_has_prefix(code, "GDateTime ") ||
        g_str_has_prefix(code, "GArray ") ||
        g_str_has_prefix(code, "GByteArray ") ||
        g_str_has_prefix(code, "GRegex "))
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
        if (!text_ends_with_newline(preamble))
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
        if (!text_ends_with_newline(code))
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
        if (!text_ends_with_newline(preamble))
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
        if (!text_ends_with_newline(preamble))
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
    g_autoptr(GString) bound_source = NULL;
    g_autofree gchar *nonce = NULL;
    guint i;

    /* A distinct pathname prevents dlopen from sharing writable module data,
     * even for identical inputs in two sessions using the same cache. */
    nonce = g_uuid_string_random();
    bound_source = g_string_new(source);
    g_string_append_printf(bound_source, "\n/* instance %s */\n", nonce);
    g_string_append(bound_source,
        "void _crispy_bind(void **slots) { (void)slots;\n");
    for (i = 0; i < self->storage->len; i++)
        g_string_append_printf(bound_source,
            "_crispy_slot_%u = slots[%u];\n", i, i);
    g_string_append(bound_source, "}\n");
    source = bound_source->str;

    /* write source to temp file */
    {
        g_autofree gchar *name = NULL;

        name = g_strdup_printf("crispy-repl-%u-XXXXXX.c", self->eval_count);
        temp_path = g_build_filename(g_get_tmp_dir(), name, NULL);
    }

    fd = g_mkstemp(temp_path);
    if (fd < 0)
    {
        g_set_error(error, CRISPY_ERROR, CRISPY_ERROR_IO,
                    "Failed to create temp file");
        return NULL;
    }

    /* a Ctrl+C in the middle of an evaluation must not leave it behind */
    crispy_temp_registry_add(temp_path);

    src_len = strlen(source);
    written = write(fd, source, src_len);
    close(fd);

    if (written < 0 || (gsize)written != src_len)
    {
        crispy_temp_registry_remove(temp_path);
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
            g_free(hash);
            crispy_temp_registry_remove(temp_path);
            g_unlink(temp_path);
            g_free(so_path);
            return NULL;
        }
    }

    g_free(hash);
    crispy_temp_registry_remove(temp_path);
    g_unlink(temp_path);
    return so_path;
}

/* Unique REPL artifacts cannot be reused; unlink after loading (the mapping
 * stays valid until g_module_close), or after preamble validation. */
static void
remove_session_artifact(
    const gchar *so_path
){
    g_autofree gchar *dep_path = NULL;

    dep_path = crispy_header_tracker_get_depfile_path(so_path);
    g_unlink(dep_path);
    g_unlink(so_path);
}

/*
 * execute_module:
 *
 * Loads a compiled .so, finds _crispy_eval, calls it, returns exit code.
 */
static gint
execute_module(
    CrispyRepl   *self,
    const gchar  *so_path,
    gpointer     *storage,
    GError      **error
){
    GModule        *module;
    CrispyEvalFunc  eval_func;
    gint            exit_code;
    void          (*bind_func)(gpointer *);
    gpointer      (*storage_func)(void);

    module = g_module_open(so_path, G_MODULE_BIND_LOCAL);
    remove_session_artifact(so_path);
    if (module == NULL)
    {
        g_set_error(error, CRISPY_ERROR, CRISPY_ERROR_LOAD,
                    "Failed to load module: %s", g_module_error());
        return -1;
    }

    bind_func = NULL;
    storage_func = NULL;
    if (!g_module_symbol(module, "_crispy_bind", (gpointer *)&bind_func) ||
        (storage != NULL && !g_module_symbol(module, "_crispy_get_storage",
                                             (gpointer *)&storage_func)))
    {
        g_set_error(error, CRISPY_ERROR, CRISPY_ERROR_LOAD,
                    "Missing REPL storage binding entry point");
        g_module_close(module);
        return -1;
    }
    bind_func(self->storage->pdata);
    if (storage != NULL)
        *storage = storage_func();

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

    g_ptr_array_add(self->modules, module);
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

/* Recognize a deliberately small declaration grammar, not all of C. Mask
 * comments and literals before examining delimiters, preserving offsets into
 * the original initializer. Explicit brace blocks retain ordinary C locals. */
static gchar *
mask_session_code(
    const gchar *code
){
    gchar *masked;
    gsize i;
    gchar quote;
    gboolean block_comment;
    gboolean line_comment;

    masked = g_strdup(code);
    quote = 0;
    block_comment = FALSE;
    line_comment = FALSE;
    for (i = 0; code[i] != '\0'; i++)
    {
        if (line_comment)
        {
            masked[i] = ' ';
            if (code[i] == '\n')
                line_comment = FALSE;
        }
        else if (block_comment)
        {
            masked[i] = ' ';
            if (code[i] == '*' && code[i + 1] == '/')
            {
                masked[++i] = ' ';
                block_comment = FALSE;
            }
        }
        else if (quote != 0)
        {
            masked[i] = '0';
            if (code[i] == '\\' && code[i + 1] != '\0')
                masked[++i] = '0';
            else if (code[i] == quote)
                quote = 0;
        }
        else if (code[i] == '/' && code[i + 1] == '*')
        {
            masked[i] = masked[i + 1] = ' ';
            i++;
            block_comment = TRUE;
        }
        else if (code[i] == '/' && code[i + 1] == '/')
        {
            masked[i] = masked[i + 1] = ' ';
            i++;
            line_comment = TRUE;
        }
        else if (code[i] == '\'' || code[i] == '"')
        {
            quote = code[i];
            /* Keep a non-whitespace marker so a literal is a statement. */
            masked[i] = '0';
        }
    }

    if (quote != 0 || block_comment)
    {
        g_free(masked);
        return NULL;
    }
    return masked;
}

/* One file-scope definition/directive per call prevents an appended variable
 * from slipping into the textual preamble and getting reinitialized later. */
static gboolean
validate_preamble(
    const gchar *code,
    GError **error
){
    g_autofree gchar *masked = NULL;
    gsize i;
    gint depth;
    gboolean finished;

    masked = mask_session_code(code);
    if (masked == NULL)
        goto unsupported;
    depth = 0;
    finished = FALSE;
    for (i = 0; masked[i] != '\0'; i++)
    {
        if (finished && !g_ascii_isspace(masked[i]))
            goto unsupported;
        if (code[0] == '#')
        {
            if (code[i] == '\n' && (i == 0 || code[i - 1] != '\\'))
                finished = TRUE;
            continue;
        }
        if (masked[i] == '{' || masked[i] == '(' || masked[i] == '[')
            depth++;
        if (masked[i] == '}' || masked[i] == ')' || masked[i] == ']')
            depth--;
        if (depth == 0 && masked[i] == ';')
            finished = TRUE;
        if (depth == 0 && masked[i] == '}' &&
            !g_str_has_prefix(code, "typedef ") &&
            !g_str_has_prefix(code, "struct ") &&
            !g_str_has_prefix(code, "union ") &&
            !g_str_has_prefix(code, "enum "))
            finished = TRUE;
    }
    return TRUE;

unsupported:
    g_set_error(error, CRISPY_ERROR, CRISPY_ERROR_REPL,
                "Use one preprocessor directive, type or function definition per eval; "
                "do not mix preamble definitions with variable declarations or statements");
    return FALSE;
}

/* Session declarations are single, simple named scalar/pointer objects. GCC
 * validates the actual type, while this parser rejects complex declarators. */
static gint
parse_session_declaration(
    const gchar *code,
    gchar **type,
    gchar **name,
    gchar **initializer,
    GError **error
){
    g_autofree gchar *masked = NULL;
    g_autoptr(GRegex) declaration = NULL;
    g_autoptr(GMatchInfo) match = NULL;
    gsize i;
    gsize start;
    gint depth;
    gint declarations;
    gint statements;

    masked = mask_session_code(code);
    if (masked == NULL)
        goto unsupported;
    declaration = g_regex_new(
        "^\\s*((?:[A-Za-z_][A-Za-z_0-9]*\\s+)*"
        "[A-Za-z_][A-Za-z_0-9]*(?:\\s+|\\s*(?:\\*\\s*)+))"
        "([A-Za-z_][A-Za-z_0-9]*)\\s*(.*)$",
        G_REGEX_DOTALL, 0, NULL);
    depth = 0;
    start = 0;
    declarations = 0;
    statements = 0;
    for (i = 0; ; i++)
    {
        gchar c;

        c = masked[i];
        if (c == '(' || c == '[' || c == '{')
            depth++;
        if (c == ')' || c == ']' || c == '}')
            depth--;
        if (((c == ';' || c == '}') && depth == 0) || c == '\0')
        {
            g_autofree gchar *part = NULL;

            part = g_strndup(masked + start, i - start);
            if (g_strstrip(part)[0] != '\0')
            {
                statements++;
                /* Control statements are not declarations. */
                if (!g_regex_match_simple(
                        "^(return|if|else|while|for|switch|do|goto|break|continue)\\b",
                        part, 0, 0) &&
                    (g_regex_match(declaration, part, 0, NULL) ||
                     g_str_has_prefix(part, "g_auto") ||
                     g_regex_match_simple("^(int|char|long|short|float|double|gint|gchar|guint|typeof|__typeof__)\\s*\\(", part, 0, 0) ||
                     g_regex_match_simple("^[A-Za-z_][A-Za-z_0-9]*\\s*\\(\\s*\\*", part, 0, 0)))
                    declarations++;
            }
            start = i + 1;
        }
        if (c == '\0')
            break;
    }
    if (declarations == 0)
        return 0;
    if (declarations != 1 || statements != 1 ||
        !g_regex_match(declaration, masked, 0, &match))
        goto unsupported;
    *type = g_match_info_fetch(match, 1);
    *name = g_match_info_fetch(match, 2);
    if (g_regex_match_simple(
            "\\b(static|extern|register|auto|restrict|__thread|_Thread_local|g_auto[a-z]*)\\b",
            *type, 0, 0) || g_str_has_prefix(*name, "_crispy_"))
        goto unsupported;
    {
        gint pos;
        gsize end;

        g_match_info_fetch_pos(match, 3, &pos, NULL);
        while (g_ascii_isspace(masked[pos]))
            pos++;
        end = strlen(masked);
        while (end > (gsize)pos && g_ascii_isspace(masked[end - 1]))
            end--;
        if (end > (gsize)pos && masked[end - 1] == ';')
            end--;
        if (masked[pos] == '=')
        {
            gsize j;
            gint nesting;

            nesting = 0;
            for (j = (gsize)pos + 1; j < end; j++)
            {
                if (masked[j] == '(')
                    nesting++;
                if (masked[j] == ')')
                    nesting--;
                if (masked[j] == '{' || masked[j] == '}' ||
                    (masked[j] == ',' && nesting == 0))
                    goto unsupported;
            }
            *initializer = g_strndup(code + pos + 1, end - pos - 1);
        }
        else if (masked[pos] != ';' && masked[pos] != '\0')
            goto unsupported;
    }
    return 1;

unsupported:
    g_set_error(error, CRISPY_ERROR, CRISPY_ERROR_REPL,
        "Unsupported session declaration: use one scalar or pointer declaration "
        "per eval, without arrays, aggregates, function declarators, storage "
        "qualifiers or cleanup attributes; use a brace block for local variables");
    return -1;
}

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
    g_autofree gchar *trimmed = NULL;
    g_autofree gchar *type = NULL;
    g_autofree gchar *name = NULL;
    g_autofree gchar *initializer = NULL;
    gint declaration;

    g_return_val_if_fail(CRISPY_IS_REPL(self), -1);
    g_return_val_if_fail(code != NULL, -1);
    trimmed = g_strdup(code);
    code = g_strstrip(trimmed);
    local_error = NULL;
    self->eval_count++;

    /*
     * Preprocessor directives and function/type definitions are
     * validated before committing: a typo must not poison later inputs.
     */
    if (is_preamble_code(code))
    {
        g_autofree gchar *candidate = NULL;

        candidate = g_strconcat(self->preamble->str, "\n", code, "\n", NULL);
        if (!validate_preamble(code, &local_error))
            goto failed;
        source = build_eval_source(candidate, "", FALSE);
        so_path = try_compile_source(self, source, &local_error);
        g_free(source);
        if (so_path == NULL)
            goto failed;
        remove_session_artifact(so_path);
        g_free(so_path);
        g_string_append(self->preamble, code);
        if (!text_ends_with_newline(code))
            g_string_append_c(self->preamble, '\n');

        g_signal_emit(self, obj_signals[SIGNAL_LINE_EVALUATED],
                      0, code, 0);
        return 0;
    }

    declaration = parse_session_declaration(code, &type, &name,
                                            &initializer, &local_error);
    if (declaration < 0)
        goto failed;
    if (declaration > 0)
    {
        g_autofree gchar *candidate = NULL;
        g_autofree gchar *statement = NULL;
        gpointer storage;

        if (g_hash_table_contains(self->names, name))
        {
            g_set_error(&local_error, CRISPY_ERROR, CRISPY_ERROR_REPL,
                        "Session variable '%s' already exists; assign or reset instead", name);
            goto failed;
        }
        /* The initializer runs only in this entry function. Future modules
         * contain a typed pointer to this static object, never its initializer. */
        candidate = g_strdup_printf(
            "%s\nstatic %s _crispy_value;\n"
            "typedef char _crispy_session_requires_scalar_or_pointer["
            "((__builtin_classify_type(_crispy_value) == 1 || "
            "__builtin_classify_type(_crispy_value) == 5 || "
            "__builtin_classify_type(_crispy_value) == 8 || "
            "__builtin_classify_type(_crispy_value) == 9) && "
            "__builtin_types_compatible_p(__typeof__(_crispy_value), "
            "__typeof__(((void)0, _crispy_value)))) ? 1 : -1];\n"
            "void *_crispy_get_storage(void) { return (void *)&_crispy_value; }\n"
            "#define %s (_crispy_value)\n",
            self->preamble->str, type, name);
        statement = initializer != NULL
            ? g_strdup_printf("%s = (%s);", name, initializer)
            : g_strdup("");
        source = build_eval_source(candidate, statement, FALSE);
        so_path = try_compile_source(self, source, &local_error);
        g_free(source);
        if (so_path == NULL)
            goto failed;
        storage = NULL;
        exit_code = execute_module(self, so_path, &storage, &local_error);
        g_free(so_path);
        if (local_error != NULL)
            goto failed;
        g_string_append_printf(self->preamble,
            "\nstatic %s *_crispy_slot_%u;\n#define %s (*_crispy_slot_%u)\n",
            type, self->storage->len, name, self->storage->len);
        g_ptr_array_add(self->storage, storage);
        g_hash_table_add(self->names, g_strdup(name));
        g_signal_emit(self, obj_signals[SIGNAL_LINE_EVALUATED], 0, code, exit_code);
        return exit_code;
    }

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
            exit_code = execute_module(self, so_path, NULL, &local_error);
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
            exit_code = execute_module(self, so_path, NULL, &local_error);
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
            exit_code = execute_module(self, so_path, NULL, &local_error);
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
failed:
        /* compilation failed — report the error */
        g_signal_emit(self, obj_signals[SIGNAL_ERROR_OCCURRED],
                      0, code, local_error);

        if (error != NULL)
            g_propagate_error(error, local_error);
        else
            g_error_free(local_error);

        return -1;
    }

    exit_code = execute_module(self, so_path, NULL, &local_error);
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
/* helpers: history file path                                           */
/* ------------------------------------------------------------------ */

/*
 * get_history_path:
 *
 * Returns the path to ~/.crispy_history.  The caller must free with g_free().
 */
static gchar *
get_history_path(void)
{
    return g_build_filename(g_get_home_dir(), ".crispy_history", NULL);
}

/* ------------------------------------------------------------------ */
/* meta-command handlers                                                */
/* ------------------------------------------------------------------ */

static gboolean
handle_meta_command(
    CrispyRepl  *self,
    const gchar *line
){
    if (strcmp(line, ":help") == 0 || strcmp(line, ":h") == 0)
    {
        g_print("\n");
        g_print("  Meta-commands:\n");
        g_print("    :help          Show this help\n");
        g_print("    :clear         Reset preamble (includes, functions, etc.)\n");
        g_print("    :preamble      Show accumulated preamble\n");
        g_print("    :load <file>   Load a C file into the preamble\n");
        g_print("    :type <expr>   Show the type of an expression\n");
        g_print("    :quit          Exit the REPL\n");
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
        g_print("    A blank line evaluates pending multi-line input\n");
        g_print("    (useful when a typo leaves the input unbalanced).\n");
        g_print("\n");
        return TRUE;
    }

    if (strcmp(line, ":clear") == 0 || strcmp(line, ":c") == 0)
    {
        crispy_repl_reset(self);
        g_print("Preamble cleared.\n");
        return TRUE;
    }

    if (strcmp(line, ":preamble") == 0 || strcmp(line, ":p") == 0)
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

    if (strcmp(line, ":quit") == 0 || strcmp(line, ":q") == 0)
    {
        /* handled by caller — this just signals the intent */
        return TRUE;
    }

    /* :load <file> — read a C file and add its contents to the preamble */
    if (g_str_has_prefix(line, ":load ") || g_str_has_prefix(line, ":l "))
    {
        const gchar *path;
        g_autofree gchar *contents = NULL;
        g_autoptr(GError) err = NULL;

        path = line + (line[1] == 'l' && line[2] == ' ' ? 3 : 6);

        /* skip leading whitespace */
        while (*path == ' ' || *path == '\t')
            path++;

        if (path[0] == '\0')
        {
            g_print("Usage: :load <file>\n");
            return TRUE;
        }

        if (!g_file_get_contents(path, &contents, NULL, &err))
        {
            fprintf(stderr, "\033[31merror:\033[0m %s\n", err->message);
            return TRUE;
        }

        g_string_append(self->preamble, contents);
        if (!text_ends_with_newline(contents))
            g_string_append_c(self->preamble, '\n');

        g_print("Loaded %s into preamble.\n", path);
        return TRUE;
    }

    /* :type <expr> — show the type of an expression using __typeof__ */
    if (g_str_has_prefix(line, ":type ") || g_str_has_prefix(line, ":t "))
    {
        const gchar *expr;
        g_autofree gchar *source = NULL;
        g_autofree gchar *so_path_type = NULL;
        g_autoptr(GError) err = NULL;

        expr = line + (line[1] == 't' && line[2] == ' ' ? 3 : 6);

        while (*expr == ' ' || *expr == '\t')
            expr++;

        if (expr[0] == '\0')
        {
            g_print("Usage: :type <expression>\n");
            return TRUE;
        }

        /*
         * Use __builtin_types_compatible_p to classify the expression
         * at compile time.  This is a gcc extension available in gnu89.
         */
        {
            GString *s;

            s = g_string_new(NULL);
            g_string_append(s, "#include <stdio.h>\n");
            g_string_append(s, "#include <stdlib.h>\n");
            g_string_append(s, "#include <string.h>\n");
            g_string_append(s, "#include <glib.h>\n");
            g_string_append(s, "#include <gio/gio.h>\n");

            if (self->preamble->len > 0)
            {
                g_string_append(s, self->preamble->str);
                if (self->preamble->str[self->preamble->len - 1] != '\n')
                    g_string_append_c(s, '\n');
            }

            g_string_append(s, "\nint\n_crispy_eval(void)\n{\n");
            g_string_append_printf(s,
                "    __typeof__(%s) _type_probe;\n"
                "    (void)_type_probe;\n"
                "    printf(\"%%s\\n\",\n"
                "        __builtin_types_compatible_p(__typeof__(%s), int)           ? \"int\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), unsigned int)  ? \"unsigned int\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), long)          ? \"long\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), unsigned long) ? \"unsigned long\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), short)         ? \"short\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), char)          ? \"char\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), float)         ? \"float\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), double)        ? \"double\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), char *)        ? \"char *\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), const char *)  ? \"const char *\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), void *)        ? \"void *\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), gint)          ? \"gint\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), guint)         ? \"guint\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), gboolean)      ? \"gboolean\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), gchar *)       ? \"gchar *\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), gsize)         ? \"gsize\" :\n"
                "        __builtin_types_compatible_p(__typeof__(%s), gint64)        ? \"gint64\" :\n"
                "        \"(other type)\"\n"
                "    );\n",
                expr, expr, expr, expr, expr, expr, expr, expr,
                expr, expr, expr, expr, expr, expr, expr, expr, expr, expr);
            g_string_append(s, "    return 0;\n}\n");

            source = g_string_free(s, FALSE);
        }

        so_path_type = try_compile_source(self, source, &err);
        if (so_path_type == NULL)
        {
            fprintf(stderr, "\033[31merror:\033[0m cannot determine type\n");
            return TRUE;
        }

        execute_module(self, so_path_type, NULL, NULL);
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
    gboolean  needs_continuation;
    gint      eval_result;

    g_return_val_if_fail(CRISPY_IS_REPL(self), FALSE);

    (void)error;

    accum = g_string_new(NULL);
    needs_continuation = FALSE;

    /* welcome banner */
    g_print("Crispy REPL v%s — C expressions, compiled and executed.\n",
            CRISPY_VERSION_STRING);
    g_print("  Type :help for commands, :quit or Ctrl-D to exit.\n\n");

    /* configure readline */
    rl_bind_key('\t', rl_insert); /* disable tab completion for now */

    /* load persistent history */
    {
        g_autofree gchar *hist_path = get_history_path();

        using_history();
        stifle_history(REPL_HISTORY_MAX);
        read_history(hist_path);
    }

    while (TRUE)
    {
        const gchar *prompt;

        prompt = needs_continuation ? self->cont_prompt : self->prompt;

        line = readline(prompt);

        /* EOF — Ctrl-D */
        if (line == NULL)
        {
            g_print("\n");
            break;
        }

        /*
         * Skip empty lines at top level.  In multiline mode a blank
         * line forces evaluation of the accumulated input instead
         * so unfinished literals or delimiters cannot trap the user
         * in continuation mode forever.
         * Forcing the eval lets gcc report the real error and returns
         * to a fresh prompt.
         */
        if (line[0] == '\0')
        {
            if (!needs_continuation)
            {
                free(line);
                continue;
            }

        }

        /* exit commands */
        if (!needs_continuation &&
            (strcmp(line, "exit") == 0 ||
             strcmp(line, "quit") == 0 ||
             strcmp(line, ":quit") == 0 ||
             strcmp(line, ":q") == 0))
        {
            free(line);
            break;
        }

        /* meta-commands (only when not in multiline mode) */
        if (!needs_continuation && line[0] == ':')
        {
            g_autofree gchar *trimmed = NULL;

            trimmed = g_strstrip(g_strdup(line));
            if (handle_meta_command(self, trimmed))
            {
                /* add to history if it's not .quit */
                if (strcmp(trimmed, ":quit") != 0 &&
                    strcmp(trimmed, ":q") != 0)
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

        /* Rescan the complete input: comments and literals span lines.
         * A blank input line deliberately bypasses the continuation hint. */
        needs_continuation = line[0] != '\0' &&
                            crispy_repl_needs_continuation(accum->str);

        if (needs_continuation)
        {
            free(line);
            continue;
        }

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

    /* save persistent history */
    {
        g_autofree gchar *hist_path = get_history_path();

        write_history(hist_path);
    }

    return TRUE;
}
