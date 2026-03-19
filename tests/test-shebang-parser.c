/* test-shebang-parser.c - Tests for shebang parsing utilities */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "../src/core/crispy-shebang-parser-private.h"

#include <glib.h>
#include <string.h>

/* test: extract shebang line when present */
static void
test_extract_line_present(void)
{
    const gchar *source = "#!/usr/bin/crispy\nint main(void) { return 0; }\n";
    g_autofree gchar *line = NULL;

    line = crispy_shebang_extract_line(source);

    g_assert_nonnull(line);
    g_assert_cmpstr(line, ==, "#!/usr/bin/crispy");
}

/* test: extract returns NULL for source that starts with #include */
static void
test_extract_line_not_present(void)
{
    const gchar *source =
        "#include <glib.h>\n"
        "int main(void) { return 0; }\n";
    g_autofree gchar *line = NULL;

    line = crispy_shebang_extract_line(source);

    g_assert_null(line);
}

/* test: extract returns NULL for NULL input */
static void
test_extract_line_null(void)
{
    g_autofree gchar *line = NULL;

    line = crispy_shebang_extract_line(NULL);

    g_assert_null(line);
}

/* test: extract returns the shebang even when there is no trailing newline */
static void
test_extract_line_no_newline(void)
{
    const gchar *source = "#!/usr/bin/crispy";
    g_autofree gchar *line = NULL;

    line = crispy_shebang_extract_line(source);

    g_assert_nonnull(line);
    g_assert_cmpstr(line, ==, "#!/usr/bin/crispy");
}

/* test: get_interpreter extracts path before first argument */
static void
test_get_interpreter(void)
{
    g_autofree gchar *interp = NULL;

    interp = crispy_shebang_get_interpreter("#!/usr/bin/crispy --foo --bar");

    g_assert_nonnull(interp);
    g_assert_cmpstr(interp, ==, "/usr/bin/crispy");
}

/* test: get_interpreter with env-mode shebang returns env path */
static void
test_get_interpreter_env(void)
{
    g_autofree gchar *interp = NULL;

    interp = crispy_shebang_get_interpreter("#!/usr/bin/env crispy");

    g_assert_nonnull(interp);
    g_assert_cmpstr(interp, ==, "/usr/bin/env");
}

/* test: get_interpreter returns NULL for NULL input */
static void
test_get_interpreter_null(void)
{
    g_autofree gchar *interp = NULL;

    interp = crispy_shebang_get_interpreter(NULL);

    g_assert_null(interp);
}

/* test: get_interpreter returns NULL for a line that is not a shebang */
static void
test_get_interpreter_no_shebang(void)
{
    g_autofree gchar *interp = NULL;

    interp = crispy_shebang_get_interpreter("not a shebang");

    g_assert_null(interp);
}

/* test: parse two simple arguments */
static void
test_parse_args_simple(void)
{
    gboolean ok;
    gint argc = 0;
    gchar **argv = NULL;

    ok = crispy_shebang_parse_args("#!/usr/bin/crispy --no-cache --gdb",
                                   &argc, &argv);

    g_assert_true(ok);
    g_assert_cmpint(argc, ==, 2);
    g_assert_cmpstr(argv[0], ==, "--no-cache");
    g_assert_cmpstr(argv[1], ==, "--gdb");

    g_strfreev(argv);
}

/* test: shebang with no arguments produces empty array */
static void
test_parse_args_no_args(void)
{
    gboolean ok;
    gint argc = 0;
    gchar **argv = NULL;

    ok = crispy_shebang_parse_args("#!/usr/bin/crispy", &argc, &argv);

    g_assert_true(ok);
    g_assert_cmpint(argc, ==, 0);

    g_strfreev(argv);
}

/* test: NULL shebang line produces 0 args and returns TRUE */
static void
test_parse_args_null(void)
{
    gboolean ok;
    gint argc = 0;
    gchar **argv = NULL;

    ok = crispy_shebang_parse_args(NULL, &argc, &argv);

    g_assert_true(ok);
    g_assert_cmpint(argc, ==, 0);

    g_strfreev(argv);
}

/* test: env-mode shebang — "crispy" is the interpreter, "--foo" is the arg */
static void
test_parse_args_env_mode(void)
{
    gboolean ok;
    gint argc = 0;
    gchar **argv = NULL;

    ok = crispy_shebang_parse_args("#!/usr/bin/env crispy --foo",
                                   &argc, &argv);

    g_assert_true(ok);
    /* interpreter (/usr/bin/env) and "crispy" are both skipped;
     * only flags after the final interpreter name reach out_argv */
    g_assert_cmpint(argc, ==, 1);
    g_assert_cmpstr(argv[0], ==, "--foo");

    g_strfreev(argv);
}

/* test: single-quoted arg preserving interior space */
static void
test_parse_args_single_quotes(void)
{
    gboolean ok;
    gint argc = 0;
    gchar **argv = NULL;

    ok = crispy_shebang_parse_args("#!/usr/bin/crispy '--flag=hello world'",
                                   &argc, &argv);

    g_assert_true(ok);
    g_assert_cmpint(argc, ==, 1);
    g_assert_cmpstr(argv[0], ==, "--flag=hello world");

    g_strfreev(argv);
}

/* test: double-quoted arg preserving interior space */
static void
test_parse_args_double_quotes(void)
{
    gboolean ok;
    gint argc = 0;
    gchar **argv = NULL;

    ok = crispy_shebang_parse_args("#!/usr/bin/crispy \"--flag=hello world\"",
                                   &argc, &argv);

    g_assert_true(ok);
    g_assert_cmpint(argc, ==, 1);
    g_assert_cmpstr(argv[0], ==, "--flag=hello world");

    g_strfreev(argv);
}

/* test: backslash escape inside double quotes */
static void
test_parse_args_escape_in_double(void)
{
    gboolean ok;
    gint argc = 0;
    gchar **argv = NULL;

    /* "\"--flag=it\\\"s\"" → --flag=it"s */
    ok = crispy_shebang_parse_args("#!/usr/bin/crispy \"--flag=it\\\"s\"",
                                   &argc, &argv);

    g_assert_true(ok);
    g_assert_cmpint(argc, ==, 1);
    g_assert_cmpstr(argv[0], ==, "--flag=it\"s");

    g_strfreev(argv);
}

/* test: unterminated single quote returns FALSE */
static void
test_parse_args_unterminated_single(void)
{
    gboolean ok;
    gint argc = 0;
    gchar **argv = NULL;

    ok = crispy_shebang_parse_args("#!/usr/bin/crispy 'unterminated",
                                   &argc, &argv);

    g_assert_false(ok);

    g_strfreev(argv);
}

/* test: unterminated double quote returns FALSE */
static void
test_parse_args_unterminated_double(void)
{
    gboolean ok;
    gint argc = 0;
    gchar **argv = NULL;

    ok = crispy_shebang_parse_args("#!/usr/bin/crispy \"unterminated",
                                   &argc, &argv);

    g_assert_false(ok);

    g_strfreev(argv);
}

/* test: multiple spaces between arguments are collapsed */
static void
test_parse_args_multiple_spaces(void)
{
    gboolean ok;
    gint argc = 0;
    gchar **argv = NULL;

    ok = crispy_shebang_parse_args("#!/usr/bin/crispy   --foo   --bar   ",
                                   &argc, &argv);

    g_assert_true(ok);
    g_assert_cmpint(argc, ==, 2);
    g_assert_cmpstr(argv[0], ==, "--foo");
    g_assert_cmpstr(argv[1], ==, "--bar");

    g_strfreev(argv);
}

/* test: mix of quoted and unquoted arguments */
static void
test_parse_args_mixed_quotes(void)
{
    gboolean ok;
    gint argc = 0;
    gchar **argv = NULL;

    ok = crispy_shebang_parse_args(
        "#!/usr/bin/crispy --plain 'with space' --after",
        &argc, &argv);

    g_assert_true(ok);
    g_assert_cmpint(argc, ==, 3);
    g_assert_cmpstr(argv[0], ==, "--plain");
    g_assert_cmpstr(argv[1], ==, "with space");
    g_assert_cmpstr(argv[2], ==, "--after");

    g_strfreev(argv);
}

/* test: empty shebang "#!" with no interpreter produces 0 args */
static void
test_parse_args_empty_string(void)
{
    gboolean ok;
    gint argc = 0;
    gchar **argv = NULL;

    ok = crispy_shebang_parse_args("#!", &argc, &argv);

    g_assert_true(ok);
    g_assert_cmpint(argc, ==, 0);

    g_strfreev(argv);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/shebang-parser/extract-line-present",
                    test_extract_line_present);
    g_test_add_func("/shebang-parser/extract-line-not-present",
                    test_extract_line_not_present);
    g_test_add_func("/shebang-parser/extract-line-null",
                    test_extract_line_null);
    g_test_add_func("/shebang-parser/extract-line-no-newline",
                    test_extract_line_no_newline);
    g_test_add_func("/shebang-parser/get-interpreter",
                    test_get_interpreter);
    g_test_add_func("/shebang-parser/get-interpreter-env",
                    test_get_interpreter_env);
    g_test_add_func("/shebang-parser/get-interpreter-null",
                    test_get_interpreter_null);
    g_test_add_func("/shebang-parser/get-interpreter-no-shebang",
                    test_get_interpreter_no_shebang);
    g_test_add_func("/shebang-parser/parse-args-simple",
                    test_parse_args_simple);
    g_test_add_func("/shebang-parser/parse-args-no-args",
                    test_parse_args_no_args);
    g_test_add_func("/shebang-parser/parse-args-null",
                    test_parse_args_null);
    g_test_add_func("/shebang-parser/parse-args-env-mode",
                    test_parse_args_env_mode);
    g_test_add_func("/shebang-parser/parse-args-single-quotes",
                    test_parse_args_single_quotes);
    g_test_add_func("/shebang-parser/parse-args-double-quotes",
                    test_parse_args_double_quotes);
    g_test_add_func("/shebang-parser/parse-args-escape-in-double",
                    test_parse_args_escape_in_double);
    g_test_add_func("/shebang-parser/parse-args-unterminated-single",
                    test_parse_args_unterminated_single);
    g_test_add_func("/shebang-parser/parse-args-unterminated-double",
                    test_parse_args_unterminated_double);
    g_test_add_func("/shebang-parser/parse-args-multiple-spaces",
                    test_parse_args_multiple_spaces);
    g_test_add_func("/shebang-parser/parse-args-mixed-quotes",
                    test_parse_args_mixed_quotes);
    g_test_add_func("/shebang-parser/parse-args-empty-string",
                    test_parse_args_empty_string);

    return g_test_run();
}
