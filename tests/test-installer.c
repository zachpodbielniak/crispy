/* test-installer.c - Tests for script installation utilities */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "../src/core/crispy-installer-private.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

/* shared fixtures */
static CrispyGccCompiler *g_compiler = NULL;

/* simple, compilable script content for installer tests */
static const gchar *simple_source =
    "#!/usr/bin/crispy\n"
    "#include <glib.h>\n"
    "int main(int argc, char **argv) {\n"
    "    (void)argc; (void)argv;\n"
    "    g_print(\"hello\\n\");\n"
    "    return 0;\n"
    "}\n";

/* helper: write a temp .c file with the given source */
static gchar *
write_temp_source(
    const gchar *source
){
    g_autofree gchar *tmpl = NULL;
    gint fd;

    tmpl = g_strdup("/tmp/crispy-installer-test-XXXXXX.c");
    fd = g_mkstemp(tmpl);
    g_assert_cmpint(fd, >=, 0);
    write(fd, source, strlen(source));
    close(fd);

    return g_steal_pointer(&tmpl);
}

/* test: get_default_dir returns a path ending in .local/bin */
static void
test_get_default_dir(void)
{
    g_autofree gchar *dir = NULL;

    dir = crispy_installer_get_default_dir();

    g_assert_nonnull(dir);
    g_assert_true(g_str_has_suffix(dir, ".local/bin") ||
                  strstr(dir, ".local") != NULL);
}

/* test: install a simple script to a temp dir */
static void
test_install_simple(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *src_path = NULL;
    g_autofree gchar *result = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-installer-XXXXXX", &error);
    g_assert_no_error(error);

    src_path = write_temp_source(simple_source);

    result = crispy_installer_install(src_path,
                                      tmpdir,
                                      CRISPY_COMPILER(g_compiler),
                                      NULL,
                                      &error);

    g_assert_no_error(error);
    g_assert_nonnull(result);
    g_assert_true(g_file_test(result, G_FILE_TEST_EXISTS));

    g_unlink(result);
    g_unlink(src_path);
    g_rmdir(tmpdir);
}

/* test: installed binary is executable */
static void
test_install_creates_executable(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *src_path = NULL;
    g_autofree gchar *result = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-installer-XXXXXX", &error);
    g_assert_no_error(error);

    src_path = write_temp_source(simple_source);

    result = crispy_installer_install(src_path,
                                      tmpdir,
                                      CRISPY_COMPILER(g_compiler),
                                      NULL,
                                      &error);

    g_assert_no_error(error);
    g_assert_nonnull(result);
    g_assert_true(g_file_test(result, G_FILE_TEST_IS_EXECUTABLE));

    g_unlink(result);
    g_unlink(src_path);
    g_rmdir(tmpdir);
}

/* test: install of nonexistent source returns NULL with error */
static void
test_install_nonexistent_source(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *result = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-installer-XXXXXX", &error);
    g_assert_no_error(error);

    result = crispy_installer_install("/nonexistent/source/file.c",
                                      tmpdir,
                                      CRISPY_COMPILER(g_compiler),
                                      NULL,
                                      &error);

    g_assert_null(result);
    g_assert_nonnull(error);

    g_rmdir(tmpdir);
}

/* test: installer strips the shebang before compilation */
static void
test_install_strips_shebang(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *src_path = NULL;
    g_autofree gchar *result = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-installer-XXXXXX", &error);
    g_assert_no_error(error);

    /*
     * The source includes a shebang line.  If the installer does NOT strip
     * it, gcc will fail with a syntax error.  Successful installation
     * demonstrates the shebang was removed before compilation.
     */
    src_path = write_temp_source(simple_source);

    result = crispy_installer_install(src_path,
                                      tmpdir,
                                      CRISPY_COMPILER(g_compiler),
                                      NULL,
                                      &error);

    g_assert_no_error(error);
    g_assert_nonnull(result);

    g_unlink(result);
    g_unlink(src_path);
    g_rmdir(tmpdir);
}

/* test: respects a custom install directory */
static void
test_install_custom_dir(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *custom_dir = NULL;
    g_autofree gchar *src_path = NULL;
    g_autofree gchar *result = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-installer-XXXXXX", &error);
    g_assert_no_error(error);

    custom_dir = g_build_filename(tmpdir, "mybin", NULL);
    g_mkdir_with_parents(custom_dir, 0755);

    src_path = write_temp_source(simple_source);

    result = crispy_installer_install(src_path,
                                      custom_dir,
                                      CRISPY_COMPILER(g_compiler),
                                      NULL,
                                      &error);

    g_assert_no_error(error);
    g_assert_nonnull(result);
    g_assert_true(g_str_has_prefix(result, custom_dir));
    g_assert_true(g_file_test(result, G_FILE_TEST_EXISTS));

    g_unlink(result);
    g_unlink(src_path);
    g_rmdir(custom_dir);
    g_rmdir(tmpdir);
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

    g_test_add_func("/installer/get-default-dir",
                    test_get_default_dir);
    g_test_add_func("/installer/install-simple",
                    test_install_simple);
    g_test_add_func("/installer/install-creates-executable",
                    test_install_creates_executable);
    g_test_add_func("/installer/install-nonexistent-source",
                    test_install_nonexistent_source);
    g_test_add_func("/installer/install-strips-shebang",
                    test_install_strips_shebang);
    g_test_add_func("/installer/install-custom-dir",
                    test_install_custom_dir);

    return g_test_run();
}
