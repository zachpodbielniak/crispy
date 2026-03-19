/* test-linter.c - Tests for compiler-based linting utilities */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "../src/core/crispy-linter-private.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

/* helper: write a temp .c file with given source, return its path */
static gchar *
write_temp_source(
    const gchar *source
){
    g_autofree gchar *tmpl = NULL;
    gint fd;

    tmpl = g_strdup("/tmp/crispy-linter-test-XXXXXX.c");
    fd = g_mkstemp(tmpl);
    g_assert_cmpint(fd, >=, 0);
    write(fd, source, strlen(source));
    close(fd);

    return g_steal_pointer(&tmpl);
}

/* test: get_flags returns a non-empty string */
static void
test_get_flags(void)
{
    const gchar *flags;

    flags = crispy_linter_get_flags();

    g_assert_nonnull(flags);
    g_assert_cmpuint(strlen(flags), >, 0);
}

/* test: lint a clean source returns TRUE */
static void
test_check_clean(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *output = NULL;
    gboolean clean;
    const gchar *source =
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv) {\n"
        "    (void)argc;\n"
        "    (void)argv;\n"
        "    g_print(\"hello\\n\");\n"
        "    return 0;\n"
        "}\n";

    path = write_temp_source(source);

    clean = crispy_linter_check(path, NULL, &output, &error);

    g_assert_no_error(error);
    g_assert_true(clean);

    g_unlink(path);
}

/* test: lint a script with a variable shadow warning returns FALSE */
static void
test_check_warnings(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *output = NULL;
    gboolean clean;
    /*
     * Declare "x" at outer scope and then declare it again in an inner
     * block — this triggers -Wshadow.
     */
    const gchar *source =
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv) {\n"
        "    (void)argc; (void)argv;\n"
        "    gint x = 1;\n"
        "    {\n"
        "        gint x = 2;\n"  /* shadows outer x */
        "        g_print(\"%d\\n\", x);\n"
        "    }\n"
        "    return x;\n"
        "}\n";

    path = write_temp_source(source);

    clean = crispy_linter_check(path, NULL, &output, &error);

    /* should detect the shadow warning → not clean */
    g_assert_false(clean);

    g_unlink(path);
}

/* test: lint of a nonexistent file returns error */
static void
test_check_nonexistent(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *output = NULL;
    gboolean clean;

    clean = crispy_linter_check(
        "/nonexistent/path/to/source.c", NULL, &output, &error);

    g_assert_false(clean);
    g_assert_nonnull(error);
}

/* test: lint catches a syntax error */
static void
test_check_syntax_error(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *output = NULL;
    gboolean clean;
    const gchar *source = "this is not valid C code !!!\n";

    path = write_temp_source(source);

    clean = crispy_linter_check(path, NULL, &output, &error);

    g_assert_false(clean);

    g_unlink(path);
}

/* test: extra flags are passed through without crashing */
static void
test_check_with_extra_flags(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *output = NULL;
    gboolean clean;
    const gchar *source =
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv) {\n"
        "    (void)argc; (void)argv;\n"
        "    return 0;\n"
        "}\n";

    path = write_temp_source(source);

    clean = crispy_linter_check(path, "-DSOME_FLAG=1", &output, &error);

    g_assert_no_error(error);
    g_assert_true(clean);

    g_unlink(path);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/linter/get-flags",
                    test_get_flags);
    g_test_add_func("/linter/check-clean",
                    test_check_clean);
    g_test_add_func("/linter/check-warnings",
                    test_check_warnings);
    g_test_add_func("/linter/check-nonexistent",
                    test_check_nonexistent);
    g_test_add_func("/linter/check-syntax-error",
                    test_check_syntax_error);
    g_test_add_func("/linter/check-with-extra-flags",
                    test_check_with_extra_flags);

    return g_test_run();
}
