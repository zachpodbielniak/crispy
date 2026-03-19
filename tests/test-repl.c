/* test-repl.c - Type-system tests for CrispyRepl */

#define CRISPY_COMPILATION
#include "../src/crispy.h"

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

/* test: eval-ing a #include line accumulates it in the preamble without error */
static void
test_repl_eval_include(void)
{
    g_autoptr(CrispyRepl) repl = NULL;
    g_autoptr(GError) error = NULL;
    gint result;

    repl = crispy_repl_new(CRISPY_COMPILER(g_compiler),
                            CRISPY_CACHE_PROVIDER(g_cache));

    /* #include lines are accumulated, not executed immediately — no compile */
    result = crispy_repl_eval(repl, "#include <glib.h>", &error);

    g_assert_no_error(error);
    /* accumulation returns 0 (success, no execution performed) */
    g_assert_cmpint(result, ==, 0);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_autoptr(GError) error = NULL;

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

    return g_test_run();
}
