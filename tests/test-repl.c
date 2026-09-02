/* test-repl.c - Tests for CrispyRepl */

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
        "Point p = {3, 4}; g_print(\"(%d,%d)\\n\", p.x, p.y);", &error);
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

    return g_test_run();
}
