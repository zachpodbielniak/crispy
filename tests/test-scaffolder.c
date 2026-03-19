/* test-scaffolder.c - Tests for crispy new script scaffolding */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "../src/core/crispy-scaffolder-private.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

/* test: list_templates returns known template names */
static void
test_list_templates(void)
{
    const gchar * const *templates;
    gboolean found_glib = FALSE;
    gboolean found_minimal = FALSE;
    gint i;

    templates = crispy_scaffolder_list_templates();

    g_assert_nonnull(templates);

    for (i = 0; templates[i] != NULL; i++) {
        if (strcmp(templates[i], "glib")    == 0) found_glib    = TRUE;
        if (strcmp(templates[i], "minimal") == 0) found_minimal = TRUE;
    }

    g_assert_true(found_glib);
    g_assert_true(found_minimal);
}

/* test: create with minimal template produces a file */
static void
test_create_minimal(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *path = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-scaffolder-XXXXXX", &error);
    g_assert_no_error(error);

    path = crispy_scaffolder_create("myscript", tmpdir, "minimal", &error);

    g_assert_no_error(error);
    g_assert_nonnull(path);
    g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));

    g_unlink(path);
    g_rmdir(tmpdir);
}

/* test: create with glib template produces a file */
static void
test_create_glib(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *path = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-scaffolder-XXXXXX", &error);
    g_assert_no_error(error);

    path = crispy_scaffolder_create("glibscript", tmpdir, "glib", &error);

    g_assert_no_error(error);
    g_assert_nonnull(path);
    g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));

    g_unlink(path);
    g_rmdir(tmpdir);
}

/* test: create with gtk template produces a file */
static void
test_create_gtk(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *path = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-scaffolder-XXXXXX", &error);
    g_assert_no_error(error);

    path = crispy_scaffolder_create("gtkscript", tmpdir, "gtk", &error);

    g_assert_no_error(error);
    g_assert_nonnull(path);
    g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));

    g_unlink(path);
    g_rmdir(tmpdir);
}

/* test: create with cli template produces a file */
static void
test_create_cli(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *path = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-scaffolder-XXXXXX", &error);
    g_assert_no_error(error);

    path = crispy_scaffolder_create("cliscript", tmpdir, "cli", &error);

    g_assert_no_error(error);
    g_assert_nonnull(path);
    g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));

    g_unlink(path);
    g_rmdir(tmpdir);
}

/* test: NULL template defaults to glib */
static void
test_create_default_template(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *path = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-scaffolder-XXXXXX", &error);
    g_assert_no_error(error);

    path = crispy_scaffolder_create("defaultscript", tmpdir, NULL, &error);

    g_assert_no_error(error);
    g_assert_nonnull(path);
    g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));

    g_unlink(path);
    g_rmdir(tmpdir);
}

/* test: created file starts with shebang */
static void
test_create_has_shebang(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *contents = NULL;
    gsize len = 0;

    tmpdir = g_dir_make_tmp("crispy-test-scaffolder-XXXXXX", &error);
    g_assert_no_error(error);

    path = crispy_scaffolder_create("shebanged", tmpdir, "glib", &error);
    g_assert_no_error(error);
    g_assert_nonnull(path);

    g_file_get_contents(path, &contents, &len, &error);
    g_assert_no_error(error);

    g_assert_nonnull(contents);
    g_assert_true(g_str_has_prefix(contents, "#!"));
    g_assert_true(strstr(contents, "crispy") != NULL);

    g_unlink(path);
    g_rmdir(tmpdir);
}

/* test: created file has executable permission (+x) */
static void
test_create_is_executable(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *path = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-scaffolder-XXXXXX", &error);
    g_assert_no_error(error);

    path = crispy_scaffolder_create("exescript", tmpdir, "glib", &error);
    g_assert_no_error(error);
    g_assert_nonnull(path);

    g_assert_true(g_file_test(path, G_FILE_TEST_IS_EXECUTABLE));

    g_unlink(path);
    g_rmdir(tmpdir);
}

/* test: creating a script when the file already exists returns an error */
static void
test_create_already_exists(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GError) error2 = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *path2 = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-scaffolder-XXXXXX", &error);
    g_assert_no_error(error);

    path = crispy_scaffolder_create("dupscript", tmpdir, "glib", &error);
    g_assert_no_error(error);
    g_assert_nonnull(path);

    /* attempt to create the same script again */
    path2 = crispy_scaffolder_create("dupscript", tmpdir, "glib", &error2);

    g_assert_null(path2);
    g_assert_nonnull(error2);

    g_unlink(path);
    g_rmdir(tmpdir);
}

/* test: empty or slash-containing name fails */
static void
test_create_invalid_name(void)
{
    g_autoptr(GError) error_empty = NULL;
    g_autoptr(GError) error_slash = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *r1 = NULL;
    g_autofree gchar *r2 = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-scaffolder-XXXXXX", &error_empty);
    g_assert_no_error(error_empty);

    r1 = crispy_scaffolder_create("", tmpdir, "glib", &error_empty);
    g_assert_null(r1);
    g_assert_nonnull(error_empty);

    r2 = crispy_scaffolder_create("bad/name", tmpdir, "glib", &error_slash);
    g_assert_null(r2);
    g_assert_nonnull(error_slash);

    g_rmdir(tmpdir);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/scaffolder/list-templates",
                    test_list_templates);
    g_test_add_func("/scaffolder/create-minimal",
                    test_create_minimal);
    g_test_add_func("/scaffolder/create-glib",
                    test_create_glib);
    g_test_add_func("/scaffolder/create-gtk",
                    test_create_gtk);
    g_test_add_func("/scaffolder/create-cli",
                    test_create_cli);
    g_test_add_func("/scaffolder/create-default-template",
                    test_create_default_template);
    g_test_add_func("/scaffolder/create-has-shebang",
                    test_create_has_shebang);
    g_test_add_func("/scaffolder/create-is-executable",
                    test_create_is_executable);
    g_test_add_func("/scaffolder/create-already-exists",
                    test_create_already_exists);
    g_test_add_func("/scaffolder/create-invalid-name",
                    test_create_invalid_name);

    return g_test_run();
}
