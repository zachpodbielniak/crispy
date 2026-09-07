/* test-repl.c - Tests for CrispyRepl */
/* SPDX-License-Identifier: AGPL-3.0-or-later */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "crispy-test-cache.h"

#include <glib.h>
#include <string.h>

/* shared fixtures */
static CrispyGccCompiler *g_compiler = NULL;
static CrispyFileCache   *g_cache    = NULL;

/* test: CRISPY_TYPE_REPL is a valid, final GObject type */
static void
test_repl_type(void)
{
    GType type;

    type = CRISPY_TYPE_REPL;
    g_assert_cmpuint(type, !=, G_TYPE_INVALID);
    g_assert_true(G_TYPE_IS_OBJECT(type));
    g_assert_true(G_TYPE_IS_FINAL(type));
}

/* test: creates successfully */
static void
test_repl_new(void)
{
    g_autoptr(CrispyRepl) repl = NULL;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    g_assert_nonnull(repl);
    g_assert_true(CRISPY_IS_REPL(repl));
}

/* test: default prompt is "crispy> " */
static void
test_repl_default_prompt(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    const gchar *prompt;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    prompt = crispy_repl_get_prompt(repl);

    g_assert_nonnull(prompt);
    g_assert_cmpstr(prompt, ==, "crispy> ");
}

/* test: set_prompt round-trips through get_prompt */
static void
test_repl_set_prompt(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    const gchar *prompt;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    crispy_repl_set_prompt(repl, ">> ");
    prompt = crispy_repl_get_prompt(repl);

    g_assert_cmpstr(prompt, ==, ">> ");
}

/* test: both signals are registered on the type */
static void
test_repl_signals_exist(void)
{
    guint id_evaluated;
    guint id_error;

    id_evaluated = g_signal_lookup("line-evaluated",  CRISPY_TYPE_REPL);
    id_error     = g_signal_lookup("error-occurred",  CRISPY_TYPE_REPL);

    g_assert_cmpuint(id_evaluated, !=, 0);
    g_assert_cmpuint(id_error,     !=, 0);
}

/* test: eval-ing a #include line accumulates it in the preamble */
static void
test_repl_eval_include(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(GError) error = NULL;
    const gchar *preamble;
    gint result;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    result = crispy_repl_eval(repl, "#include <math.h>", &error);

    g_assert_no_error(error);
    g_assert_cmpint(result, ==, 0);

    preamble = crispy_repl_get_preamble(repl);
    g_assert_nonnull(preamble);
    g_assert_nonnull(strstr(preamble, "#include <math.h>"));
}

/* test: eval-ing a simple statement succeeds */
static void
test_repl_eval_statement(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(GError) error = NULL;
    gint result;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    result = crispy_repl_eval(repl, "g_print(\"hello\\n\");", &error);

    g_assert_no_error(error);
    g_assert_cmpint(result, ==, 0);
}

/* test: eval-ing a bare expression auto-prints (numeric) */
static void
test_repl_eval_expression(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(GError) error = NULL;
    gint result;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    /* auto-print expression: should compile and return 0 */
    result = crispy_repl_eval(repl, "1 + 2", &error);

    g_assert_no_error(error);
    g_assert_cmpint(result, ==, 0);
}

/* test: eval-ing a string expression auto-prints */
static void
test_repl_eval_string_expression(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(GError) error = NULL;
    gint result;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    result = crispy_repl_eval(repl, "\"hello world\"", &error);

    g_assert_no_error(error);
    g_assert_cmpint(result, ==, 0);
}

/* test: eval-ing invalid code returns -1 with an error */
static void
test_repl_eval_error(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(GError) error = NULL;
    gint result;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    result = crispy_repl_eval(repl, "this is total garbage!", &error);

    g_assert_cmpint(result, ==, -1);
    g_assert_nonnull(error);
}

/* test: #define accumulates in preamble */
static void
test_repl_eval_define(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(GError) error = NULL;
    const gchar *preamble;
    gint result;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    result = crispy_repl_eval(repl, "#define MY_CONST 42", &error);

    g_assert_no_error(error);
    g_assert_cmpint(result, ==, 0);

    preamble = crispy_repl_get_preamble(repl);
    g_assert_nonnull(strstr(preamble, "#define MY_CONST 42"));
}

/* test: reset clears the preamble */
static void
test_repl_reset(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(GError) error = NULL;
    const gchar *preamble;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    crispy_repl_eval(repl, "#include <math.h>", &error);
    g_assert_no_error(error);

    preamble = crispy_repl_get_preamble(repl);
    g_assert_cmpuint(strlen(preamble), >, 0);

    crispy_repl_reset(repl);

    preamble = crispy_repl_get_preamble(repl);
    g_assert_cmpuint(strlen(preamble), ==, 0);
}

/* test: function definition goes to preamble, then can be called */
static void
test_repl_eval_function_def(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(GError) error = NULL;
    const gchar *preamble;
    gint result;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    /* define a function — should go to preamble */
    result = crispy_repl_eval(repl,
        "int square(int x) { return x * x; }", &error);
    g_assert_no_error(error);
    g_assert_cmpint(result, ==, 0);

    preamble = crispy_repl_get_preamble(repl);
    g_assert_nonnull(strstr(preamble, "int square"));

    /* call the function as an expression — should auto-print */
    result = crispy_repl_eval(repl, "square(7)", &error);
    g_assert_no_error(error);
    g_assert_cmpint(result, ==, 0);
}

/* test: preamble persists across evaluations */
static void
test_repl_preamble_persistence(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(GError) error = NULL;
    gint result;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    /* accumulate math.h */
    result = crispy_repl_eval(repl, "#include <math.h>", &error);
    g_assert_no_error(error);

    /* define a constant */
    result = crispy_repl_eval(repl, "#define MY_PI 3.14159265", &error);
    g_assert_no_error(error);

    /* use both in a statement — should compile because preamble persists */
    result = crispy_repl_eval(repl,
        "g_print(\"sin(PI/2) = %g\\n\", sin(MY_PI / 2.0));", &error);
    g_assert_no_error(error);
    g_assert_cmpint(result, ==, 0);
}

/* test: multiline code (block with braces) evaluates correctly */
static void
test_repl_eval_multiline(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(GError) error = NULL;
    gint result;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    /* multiline block — already assembled by the caller */
    result = crispy_repl_eval(repl,
        "{\n"
        "    gint i;\n"
        "    for (i = 0; i < 3; i++)\n"
        "        g_print(\"%d \", i);\n"
        "    g_print(\"\\n\");\n"
        "}", &error);

    g_assert_no_error(error);
    g_assert_cmpint(result, ==, 0);
}

/* test: error-occurred signal fires on bad code */
static gboolean error_signal_fired = FALSE;

static void
on_error_occurred(
    CrispyRepl  *repl,
    const gchar *code,
    GError      *err,
    gpointer     user_data
){
    (void)repl;
    (void)code;
    (void)err;
    (void)user_data;
    error_signal_fired = TRUE;
}

static void
test_repl_error_signal(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    gint result;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    error_signal_fired = FALSE;
    g_signal_connect(repl, "error-occurred",
                     G_CALLBACK(on_error_occurred), NULL);

    result = crispy_repl_eval(repl, "totally invalid code!", NULL);

    g_assert_cmpint(result, ==, -1);
    g_assert_true(error_signal_fired);
}

/* test: line-evaluated signal fires on success */
static gboolean eval_signal_fired = FALSE;
static gint     eval_signal_code  = -999;

static void
on_line_evaluated(
    CrispyRepl  *repl,
    const gchar *code,
    gint         exit_code,
    gpointer     user_data
){
    (void)repl;
    (void)code;
    (void)user_data;
    eval_signal_fired = TRUE;
    eval_signal_code  = exit_code;
}

static void
test_repl_eval_signal(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(GError) error = NULL;
    gint result;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    eval_signal_fired = FALSE;
    eval_signal_code  = -999;
    g_signal_connect(repl, "line-evaluated",
                     G_CALLBACK(on_line_evaluated), NULL);

    result = crispy_repl_eval(repl, "g_print(\"signal test\\n\");", &error);

    g_assert_no_error(error);
    g_assert_cmpint(result, ==, 0);
    g_assert_true(eval_signal_fired);
    g_assert_cmpint(eval_signal_code, ==, 0);
}

/* test: set_extra_flags does not crash */
static void
test_repl_extra_flags(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(GError) error = NULL;
    gint result;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    crispy_repl_set_extra_flags(repl, "-DTEST_FLAG=1");

    result = crispy_repl_eval(repl, "g_print(\"%d\\n\", TEST_FLAG);", &error);

    g_assert_no_error(error);
    g_assert_cmpint(result, ==, 0);
}

/* test: typedef in preamble is usable in later evals */
static void
test_repl_typedef_preamble(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(GError) error = NULL;
    const gchar *preamble;
    gint result;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    result = crispy_repl_eval(repl,
        "typedef struct { gint x; gint y; } Point;", &error);
    g_assert_no_error(error);

    preamble = crispy_repl_get_preamble(repl);
    g_assert_nonnull(strstr(preamble, "typedef struct"));

    /* use the typedef in a subsequent eval */
    result = crispy_repl_eval(repl,
        "{ Point p = {3, 4}; g_print(\"(%d,%d)\\n\", p.x, p.y); }", &error);
    g_assert_no_error(error);
    g_assert_cmpint(result, ==, 0);
}


/*
 * test: empty input does not read before the string
 *
 * Nothing between crispy_repl_eval() and code[strlen(code) - 1] said the
 * string had to be non-empty, so an empty line read the byte before the
 * allocation.  It is silent in an ordinary build and an ASan build
 * aborts on it, which is what this test is for.
 */
static void
test_repl_eval_empty(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autoptr(CrispyRepl) repl = NULL;
    gint result;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);
    cache = crispy_file_cache_new();

    repl = crispy_repl_new(CRISPY_COMPILER(compiler),
                           CRISPY_CACHE_PROVIDER(cache));

    result = crispy_repl_eval(repl, "", &error);
    g_assert_cmpint(result, >=, 0);
    g_clear_error(&error);

    /* whitespace only takes the same path with nothing to compile */
    result = crispy_repl_eval(repl, "   ", &error);
    g_assert_cmpint(result, >=, 0);
    g_clear_error(&error);
}

/* Assert both halves of the public return contract, including nonzero C returns. */
static void
assert_eval(CrispyRepl *repl, const gchar *code, gint expected)
{
    g_autoptr(GError) error = NULL;
    gint result;

    result = crispy_repl_eval(repl, code, &error);
    g_assert_no_error(error);
    g_assert_cmpint(result, ==, expected);
}

/* Storage addresses, literal modules and heap pointers survive without replay. */
static void
test_repl_session_storage(void)
{
    g_autoptr(CrispyRepl) repl = NULL;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler), CRISPY_CACHE_PROVIDER(g_cache));
    crispy_repl_set_extra_flags(repl, "-Wall -Wextra -Werror");
    assert_eval(repl, "  gchar *name = \"blah\";  ", 0);
    assert_eval(repl, "g_print(\"%s\\n\", name);", 0);
    assert_eval(repl, "g_assert_cmpstr(name, ==, \"blah\");", 0);
    assert_eval(repl, "gint count = 0;", 0);
    assert_eval(repl, "gint *address = &count;", 0);
    assert_eval(repl, "count++;", 0);
    assert_eval(repl, "count++;", 0);
    assert_eval(repl, "return count;", 2);
    assert_eval(repl, "count = 17;", 0);
    assert_eval(repl, "g_assert_true(address == &count); return *address;", 17);
    assert_eval(repl, "gchar *heap = (count++, g_strdup(name));", 0);
    assert_eval(repl, "gchar *saved = heap;", 0);
    assert_eval(repl, "heap[0] = 'B';", 0);
    assert_eval(repl, "g_assert_true(saved == heap); g_assert_cmpstr(heap, ==, \"Blah\"); return count;", 18);
    assert_eval(repl, "name = \"a later module's literal\";", 0);
    assert_eval(repl, "g_assert_cmpstr(name, ==, \"a later module's literal\");", 0);
    assert_eval(repl, "g_free(heap); heap = NULL; saved = NULL;", 0);
    assert_eval(repl, "return count;", 18);
    assert_eval(repl, "gdouble fraction = 1.25;", 0);
    assert_eval(repl, "fraction *= 2;", 0);
    assert_eval(repl, "g_assert_cmpfloat(fraction, ==, 2.5);", 0);
    assert_eval(repl, "gint zero;", 0);
    assert_eval(repl, "return zero;", 0);
    assert_eval(repl, "gchar letter = 'a';", 0);
    assert_eval(repl, "g_assert_cmpint(letter, ==, 'a');", 0);
    assert_eval(repl, "gint*compact=&count;", 0);
    assert_eval(repl, "return *compact;", 18);
    assert_eval(repl, "/* comment */ gchar *punctuation = \";{},\\\" /* text */\"; /* end */", 0);
    assert_eval(repl, "g_assert_cmpstr(punctuation, ==, \";{},\\\" /* text */\");", 0);
    assert_eval(repl, "const gchar *literal = \"constant pointee\";", 0);
    assert_eval(repl, "g_assert_cmpstr(literal, ==, \"constant pointee\");", 0);
    assert_eval(repl, "return -1;", -1);
}

/* Identical cache inputs must not cause two instances to share module data. */
static void
test_repl_session_isolation(void)
{
    g_autoptr(CrispyRepl) first = NULL;
    g_autoptr(CrispyRepl) second = NULL;

    first = crispy_repl_new(CRISPY_COMPILER(g_compiler), CRISPY_CACHE_PROVIDER(g_cache));
    second = crispy_repl_new(CRISPY_COMPILER(g_compiler), CRISPY_CACHE_PROVIDER(g_cache));
    assert_eval(first, "gint count = 0;", 0);
    assert_eval(second, "gint count = 0;", 0);
    assert_eval(first, "count++;", 0);
    assert_eval(first, "int read_count(void) { return count; }", 0);
    assert_eval(second, "int read_count(void) { return count; }", 0);
    assert_eval(first, "return read_count();", 1);
    assert_eval(first, "int write_count(void) { count = 9; return count; }", 0);
    assert_eval(first, "return write_count();", 9);
    assert_eval(second, "return read_count();", 0);
    crispy_repl_reset(first);
    assert_eval(first, "gint count = 0;", 0);
    assert_eval(first, "return count;", 0);
    g_clear_object(&first);
    assert_eval(second, "count = 23;", 0);
    assert_eval(second, "return read_count();", 23);
}

/* Rejected declarations and failed preamble edits leave usable session state. */
static void
test_repl_session_errors(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(GError) error = NULL;
    const gchar *invalid[] = {
        "gint count = 99;", "gint broken = (count++, missing_symbol);",
        "gint items[2];", "gint one = 1, two = 2;",
        "gint local = 1; count++;", "count++; gint local = 1;",
        "static gint local = 1;", "g_autofree gchar *local = g_strdup(\"x\");",
        "gint (*callback)(void);", "typedef garbage broken;",
        "#include <crispy_nonexistent_header.h>",
        "int broken(void) { return missing_symbol; }",
        "struct Pair { int x; } pair;",
        "{} gint local = 1;", "gint(local);",
        "typedef gint Alias; gint local = 1;",
        "#include <stdio.h>\ngint local = 1;",
        "int helper(void) { return 0; } gint local = 1;",
        "gint local; /* unfinished", "gchar *local = \"unfinished",
        "Pair aggregate;", "Numbers array;",
        NULL
    };
    guint i;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler), CRISPY_CACHE_PROVIDER(g_cache));
    assert_eval(repl, "gint count = 3;", 0);
    assert_eval(repl, "typedef struct { gint x; } Pair;", 0);
    assert_eval(repl, "typedef gint Numbers[2];", 0);
    for (i = 0; invalid[i] != NULL; i++)
    {
        g_test_message("Reject: %s", invalid[i]);
        g_assert_cmpint(crispy_repl_eval(repl, invalid[i], &error), ==, -1);
        g_assert_nonnull(error);
        g_clear_error(&error);
        assert_eval(repl, "return count;", 3);
    }
    assert_eval(repl, "gint broken = 8;", 0);
    assert_eval(repl, "typedef gint Counter;", 0);
    assert_eval(repl, "Counter alias = count;", 0);
    assert_eval(repl, "return alias;", 3);
    assert_eval(repl, "struct Opaque;", 0);
    assert_eval(repl, "struct Opaque *opaque = NULL;", 0);
    assert_eval(repl, "g_assert_null(opaque);", 0);
    crispy_repl_reset(repl);
    g_assert_cmpint(crispy_repl_eval(repl, "return count;", &error), ==, -1);
    g_assert_nonnull(error);
    g_clear_error(&error);
    assert_eval(repl, "gint count = 4;", 0);
    assert_eval(repl, "return count;", 4);
}

/* Continuation is a lexical hint over the entire buffer, not a C parser. */
static void
test_repl_needs_continuation(void)
{
    const struct {
        const gchar *code;
        gboolean expected;
    } cases[] = {
        { "", FALSE },
        { " \t\n", FALSE },
        { "gint count = 0;", FALSE },
        { "gchar *name = \"blah\";", FALSE },
        { "count++;", FALSE },
        { "count = 2; /* done */", FALSE },
        { "gint count =", TRUE },
        { "count = \t /* value follows */", TRUE },
        { "count = // value follows\n", TRUE },
        { "gint a = 1,", TRUE },
        { "gint a = 1, /* another */\n", TRUE },
        { "#define VALUE \\", TRUE },
        { "#define VALUE \\\n", TRUE },
        { "#define VALUE \\\r\n", TRUE },
        { "#define VALUE \\\n42", FALSE },
        { "g_print(", TRUE },
        { "g_print(\"%d\", values[", TRUE },
        { "g_print(\"%d\", values[0]);", FALSE },
        { "{\n if (values[0]) {", TRUE },
        { "{\n if (values[0]) {}\n}", FALSE },
        { "([)]", FALSE },
        { ")(", FALSE },
        { "}", FALSE },
        { "\"unclosed", TRUE },
        { "'x", TRUE },
        { "\"escaped quote \\\"", TRUE },
        { "\"escaped quote \\\" end\"", FALSE },
        { "\"escaped slash \\\\\"", FALSE },
        { "'\\''", FALSE },
        { "'\\\\'", FALSE },
        { "\"({[=,\"", FALSE },
        { "'{'", FALSE },
        { "/* unclosed", TRUE },
        { "/* ( ' \"\n still open", TRUE },
        { "/* ( ' \"\n closed */", FALSE },
        { "// ({[\"'=,", FALSE },
        { "// comment \\", TRUE },
        { "// (\ncount++;", FALSE },
        { "{ // }\n}", FALSE },
        { "{ /* }\n */", TRUE },
        { "{ /* }\n */ }", FALSE },
        { "// comment \\\n( ignored\ncount++;", FALSE },
        { "/\\\n* open comment", TRUE },
        { "/* closed *\\\n/", FALSE },
        { "\"joined \\\nstring\";", FALSE },
        { "gint count", FALSE }
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS(cases); i++)
    {
        g_test_message("Continuation case %u: %s", i, cases[i].code);
        g_assert_cmpint(crispy_repl_needs_continuation(cases[i].code),
                        ==, cases[i].expected);
    }
}

/* Embedders can route mutations using the authoritative session registry. */
static void
test_repl_has_variable(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(CrispyRepl) other = NULL;
    g_autoptr(GError) error = NULL;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler), CRISPY_CACHE_PROVIDER(g_cache));
    other = crispy_repl_new(CRISPY_COMPILER(g_compiler), CRISPY_CACHE_PROVIDER(g_cache));
    g_assert_false(crispy_repl_has_variable(repl, "count"));
    g_assert_false(crispy_repl_has_variable(repl, ""));
    assert_eval(repl, "gint count = 0;", 0);
    assert_eval(repl, "gchar *name = \"blah\";", 0);
    g_assert_true(crispy_repl_has_variable(repl, "count"));
    g_assert_true(crispy_repl_has_variable(repl, "name"));
    g_assert_false(crispy_repl_has_variable(repl, "Count"));
    g_assert_false(crispy_repl_has_variable(repl, "count++"));
    g_assert_false(crispy_repl_has_variable(repl, " count"));
    g_assert_false(crispy_repl_has_variable(other, "count"));
    assert_eval(repl, "count++;", 0);
    assert_eval(repl, "count = 2;", 0);
    g_assert_true(crispy_repl_has_variable(repl, "count"));
    g_assert_cmpint(crispy_repl_eval(repl, "gint broken = missing;", &error), ==, -1);
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_assert_false(crispy_repl_has_variable(repl, "broken"));
    g_assert_true(crispy_repl_has_variable(repl, "count"));
    assert_eval(repl, "{ gint temporary = count; g_assert_cmpint(temporary, ==, 2); }", 0);
    assert_eval(repl, "#define MACRO 2", 0);
    assert_eval(repl, "typedef gint Counter;", 0);
    assert_eval(repl, "gint helper(void) { return count; }", 0);
    g_assert_false(crispy_repl_has_variable(repl, "temporary"));
    g_assert_false(crispy_repl_has_variable(repl, "MACRO"));
    g_assert_false(crispy_repl_has_variable(repl, "Counter"));
    g_assert_false(crispy_repl_has_variable(repl, "helper"));
    crispy_repl_reset(repl);
    g_assert_false(crispy_repl_has_variable(repl, "count"));
    g_assert_false(crispy_repl_has_variable(repl, "name"));
    assert_eval(repl, "gint count = 7;", 0);
    g_assert_true(crispy_repl_has_variable(repl, "count"));
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_autoptr(GError) error = NULL;

    /*
     * Before g_test_init(), because g_get_user_cache_dir() caches
     * its first answer and this suite must not compile into -- or
     * purge -- the developer's own ~/.cache/crispy.
     */
    crispy_test_use_temp_cache();

    g_test_init(&argc, &argv, NULL);

    g_compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);
    g_cache = crispy_file_cache_new();

    g_test_add_func("/repl/repl-type",
                    test_repl_type);
    g_test_add_func("/repl/repl-new",
                    test_repl_new);
    g_test_add_func("/repl/default-prompt",
                    test_repl_default_prompt);
    g_test_add_func("/repl/set-prompt",
                    test_repl_set_prompt);
    g_test_add_func("/repl/signals-exist",
                    test_repl_signals_exist);
    g_test_add_func("/repl/eval-include",
                    test_repl_eval_include);
    g_test_add_func("/repl/eval-statement",
                    test_repl_eval_statement);
    g_test_add_func("/repl/eval-expression",
                    test_repl_eval_expression);
    g_test_add_func("/repl/eval-string-expression",
                    test_repl_eval_string_expression);
    g_test_add_func("/repl/eval-error",
                    test_repl_eval_error);
    g_test_add_func("/repl/eval-define",
                    test_repl_eval_define);
    g_test_add_func("/repl/reset",
                    test_repl_reset);
    g_test_add_func("/repl/eval-function-def",
                    test_repl_eval_function_def);
    g_test_add_func("/repl/preamble-persistence",
                    test_repl_preamble_persistence);
    g_test_add_func("/repl/eval-multiline",
                    test_repl_eval_multiline);
    g_test_add_func("/repl/error-signal",
                    test_repl_error_signal);
    g_test_add_func("/repl/eval-signal",
                    test_repl_eval_signal);
    g_test_add_func("/repl/extra-flags",
                    test_repl_extra_flags);
    g_test_add_func("/repl/eval-empty",
                    test_repl_eval_empty);
    g_test_add_func("/repl/typedef-preamble",
                    test_repl_typedef_preamble);
    g_test_add_func("/repl/session-storage", test_repl_session_storage);
    g_test_add_func("/repl/session-isolation", test_repl_session_isolation);
    g_test_add_func("/repl/session-errors", test_repl_session_errors);
    g_test_add_func("/repl/needs-continuation", test_repl_needs_continuation);
    g_test_add_func("/repl/has-variable", test_repl_has_variable);

    return g_test_run();
}
