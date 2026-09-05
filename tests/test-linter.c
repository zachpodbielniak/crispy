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

/* helper: write a .c file into a fresh temp directory, return its path */
static gchar *
write_source_in_dir(
    const gchar  *source,
    const gchar  *basename,
    gchar       **out_dir
){
    g_autofree gchar *dir = NULL;
    gchar *path;

    dir = g_dir_make_tmp("crispy-linter-test-XXXXXX", NULL);
    g_assert_nonnull(dir);

    path = g_build_filename(dir, basename, NULL);
    g_assert_true(g_file_set_contents(path, source, -1, NULL));

    *out_dir = g_steal_pointer(&dir);
    return path;
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

/*
 * test: a shebang does not break the lint
 *
 * gcc rejects "#!" as a preprocessing directive, so linting the file
 * as-is failed on line 1 of every script that had one -- which is
 * nearly all of them.  The header lines are blanked out of a copy
 * before gcc sees it.
 */
static void
test_check_shebang(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *output = NULL;
    gboolean clean;
    const gchar *source =
        "#!/usr/bin/crispy\n"
        "#define CRISPY_PARAMS \"-lm\"\n"
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv) {\n"
        "    (void)argc;\n"
        "    (void)argv;\n"
        "    return 0;\n"
        "}\n";

    path = write_temp_source(source);

    clean = crispy_linter_check(path, NULL, &output, &error);

    g_assert_no_error(error);
    g_assert_true(clean);
    g_assert_null(output);

    g_unlink(path);
}

/* test: the "#!/usr/bin/env crispy" form the scaffolder emits */
static void
test_check_shebang_env(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *output = NULL;
    gboolean clean;
    const gchar *source =
        "#!/usr/bin/env crispy\n"
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv) {\n"
        "    (void)argc;\n"
        "    (void)argv;\n"
        "    return 0;\n"
        "}\n";

    path = write_temp_source(source);

    clean = crispy_linter_check(path, NULL, &output, &error);

    g_assert_no_error(error);
    g_assert_true(clean);
    g_assert_null(output);

    g_unlink(path);
}

/*
 * test: diagnostics name the script and its real line numbers
 *
 * The header lines are emptied rather than deleted for exactly this
 * reason: dropping them would shift every line after them, and a
 * linter's whole output is line-numbered.
 */
static void
test_check_reports_original_location(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *output = NULL;
    g_autofree gchar *needle = NULL;
    gboolean clean;
    /* the undeclared identifier sits on line 7 */
    const gchar *source =
        "#!/usr/bin/crispy\n"
        "#define CRISPY_PARAMS \"-lm\"\n"
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv) {\n"
        "    (void)argc;\n"
        "    (void)argv;\n"
        "    return undefined_symbol_here;\n"
        "}\n";

    path = write_temp_source(source);

    clean = crispy_linter_check(path, NULL, &output, &error);

    g_assert_false(clean);
    g_assert_nonnull(error);

    /* the original path, not the temp copy gcc was actually given */
    needle = g_strdup_printf("%s:7:", path);
    g_assert_nonnull(strstr(error->message, needle));

    g_unlink(path);
}

/*
 * test: a quoted include of a sibling header resolves
 *
 * gcc is handed a copy in the temp directory, so the script's own
 * directory has to be put on the include path or the sibling is not
 * found.  Linting from an unrelated working directory is the case
 * that catches a missing -I.
 */
static void
test_check_sibling_header(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *header_path = NULL;
    g_autofree gchar *output = NULL;
    gboolean clean;
    const gchar *source =
        "#!/usr/bin/crispy\n"
        "#include <glib.h>\n"
        "#include \"sibling.h\"\n"
        "gint main(gint argc, gchar **argv) {\n"
        "    (void)argc;\n"
        "    (void)argv;\n"
        "    return SIBLING_VALUE;\n"
        "}\n";

    path = write_source_in_dir(source, "script.c", &dir);

    header_path = g_build_filename(dir, "sibling.h", NULL);
    g_assert_true(g_file_set_contents(header_path,
                                      "#define SIBLING_VALUE 0\n",
                                      -1, NULL));

    clean = crispy_linter_check(path, NULL, &output, &error);

    g_assert_no_error(error);
    g_assert_true(clean);
    g_assert_null(output);

    g_unlink(header_path);
    g_unlink(path);
    g_rmdir(dir);
}

/* test: the temp copy handed to gcc does not outlive the call */
static void
test_check_leaves_no_temp_file(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *output = NULL;
    g_autoptr(GDir) tmp = NULL;
    const gchar *entry;
    const gchar *source =
        "#!/usr/bin/crispy\n"
        "#include <glib.h>\n"
        "gint main(void) { return 0; }\n";

    path = write_temp_source(source);

    crispy_linter_check(path, NULL, &output, &error);

    tmp = g_dir_open(g_get_tmp_dir(), 0, NULL);
    g_assert_nonnull(tmp);

    while ((entry = g_dir_read_name(tmp)) != NULL)
        g_assert_false(g_str_has_prefix(entry, "crispy-lint-"));

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
    g_test_add_func("/linter/check-shebang",
                    test_check_shebang);
    g_test_add_func("/linter/check-shebang-env",
                    test_check_shebang_env);
    g_test_add_func("/linter/check-reports-original-location",
                    test_check_reports_original_location);
    g_test_add_func("/linter/check-sibling-header",
                    test_check_sibling_header);
    g_test_add_func("/linter/check-leaves-no-temp-file",
                    test_check_leaves_no_temp_file);

    return g_test_run();
}
