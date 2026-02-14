/* test-gcc-compiler.c - Tests for CrispyGccCompiler */

#define CRISPY_COMPILATION
#include "../src/crispy.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

/* test: creating a new GCC compiler instance succeeds */
static void
test_gcc_compiler_new(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyGccCompiler) compiler = NULL;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);
    g_assert_nonnull(compiler);
}

/* test: version string is non-NULL and contains "gcc" */
static void
test_gcc_compiler_get_version(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    const gchar *version;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);

    version = crispy_compiler_get_version(CRISPY_COMPILER(compiler));
    g_assert_nonnull(version);
    g_assert_cmpuint(strlen(version), >, 0);
    g_assert_true(strstr(version, "gcc") != NULL ||
                  strstr(version, "GCC") != NULL);
}

/* test: base flags contain glib linker flags */
static void
test_gcc_compiler_get_base_flags(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    const gchar *flags;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);

    flags = crispy_compiler_get_base_flags(CRISPY_COMPILER(compiler));
    g_assert_nonnull(flags);
    g_assert_true(strstr(flags, "glib") != NULL);
}

/* test: CrispyGccCompiler implements CrispyCompiler interface */
static void
test_gcc_compiler_implements_interface(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyGccCompiler) compiler = NULL;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);

    g_assert_true(CRISPY_IS_COMPILER(compiler));
}

/* test: compiling a trivial source to .so succeeds */
static void
test_gcc_compiler_compile_shared_trivial(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    g_autofree gchar *src_path = NULL;
    g_autofree gchar *out_path = NULL;
    gboolean ok;
    gint fd;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);

    /* write trivial source */
    src_path = g_strdup("/tmp/crispy-test-trivial-XXXXXX.c");
    fd = g_mkstemp(src_path);
    g_assert_cmpint(fd, >=, 0);
    write(fd, "int main(){ return 0; }\n", 24);
    close(fd);

    out_path = g_strdup("/tmp/crispy-test-trivial-XXXXXX.so");
    fd = g_mkstemp(out_path);
    close(fd);

    ok = crispy_compiler_compile_shared(
        CRISPY_COMPILER(compiler), src_path, out_path, NULL, &error);
    g_assert_no_error(error);
    g_assert_true(ok);
    g_assert_true(g_file_test(out_path, G_FILE_TEST_IS_REGULAR));

    g_unlink(src_path);
    g_unlink(out_path);
}

/* test: compiling source using g_print succeeds */
static void
test_gcc_compiler_compile_shared_with_glib(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    g_autofree gchar *src_path = NULL;
    g_autofree gchar *out_path = NULL;
    const gchar *source;
    gboolean ok;
    gint fd;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);

    source = "#include <glib.h>\n"
             "int main(){ g_print(\"test\\n\"); return 0; }\n";

    src_path = g_strdup("/tmp/crispy-test-glib-XXXXXX.c");
    fd = g_mkstemp(src_path);
    g_assert_cmpint(fd, >=, 0);
    write(fd, source, strlen(source));
    close(fd);

    out_path = g_strdup("/tmp/crispy-test-glib-XXXXXX.so");
    fd = g_mkstemp(out_path);
    close(fd);

    ok = crispy_compiler_compile_shared(
        CRISPY_COMPILER(compiler), src_path, out_path, NULL, &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    g_unlink(src_path);
    g_unlink(out_path);
}

/* test: compile with extra flags (-lm) */
static void
test_gcc_compiler_compile_shared_with_extra_flags(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    g_autofree gchar *src_path = NULL;
    g_autofree gchar *out_path = NULL;
    const gchar *source;
    gboolean ok;
    gint fd;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);

    source = "#include <math.h>\n"
             "#include <glib.h>\n"
             "int main(){ g_print(\"%f\\n\", sqrt(2.0)); return 0; }\n";

    src_path = g_strdup("/tmp/crispy-test-extra-XXXXXX.c");
    fd = g_mkstemp(src_path);
    g_assert_cmpint(fd, >=, 0);
    write(fd, source, strlen(source));
    close(fd);

    out_path = g_strdup("/tmp/crispy-test-extra-XXXXXX.so");
    fd = g_mkstemp(out_path);
    close(fd);

    ok = crispy_compiler_compile_shared(
        CRISPY_COMPILER(compiler), src_path, out_path, "-lm", &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    g_unlink(src_path);
    g_unlink(out_path);
}

/* test: compilation failure produces GError */
static void
test_gcc_compiler_compile_failure_syntax_error(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    g_autofree gchar *src_path = NULL;
    g_autofree gchar *out_path = NULL;
    const gchar *source;
    gboolean ok;
    gint fd;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);

    source = "this is not valid c code !!!\n";

    src_path = g_strdup("/tmp/crispy-test-fail-XXXXXX.c");
    fd = g_mkstemp(src_path);
    g_assert_cmpint(fd, >=, 0);
    write(fd, source, strlen(source));
    close(fd);

    out_path = g_strdup("/tmp/crispy-test-fail-XXXXXX.so");
    fd = g_mkstemp(out_path);
    close(fd);

    ok = crispy_compiler_compile_shared(
        CRISPY_COMPILER(compiler), src_path, out_path, NULL, &error);
    g_assert_false(ok);
    g_assert_error(error, CRISPY_ERROR, CRISPY_ERROR_COMPILE);

    g_unlink(src_path);
    g_unlink(out_path);
}

/* test: compiling to executable for gdb mode */
static void
test_gcc_compiler_compile_executable(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    g_autofree gchar *src_path = NULL;
    g_autofree gchar *out_path = NULL;
    const gchar *source;
    gboolean ok;
    gint fd;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);

    source = "#include <glib.h>\n"
             "int main(){ return 0; }\n";

    src_path = g_strdup("/tmp/crispy-test-exe-XXXXXX.c");
    fd = g_mkstemp(src_path);
    g_assert_cmpint(fd, >=, 0);
    write(fd, source, strlen(source));
    close(fd);

    out_path = g_strdup("/tmp/crispy-test-exe-XXXXXX");
    fd = g_mkstemp(out_path);
    close(fd);

    ok = crispy_compiler_compile_executable(
        CRISPY_COMPILER(compiler), src_path, out_path, NULL, &error);
    g_assert_no_error(error);
    g_assert_true(ok);
    g_assert_true(g_file_test(out_path, G_FILE_TEST_IS_EXECUTABLE));

    g_unlink(src_path);
    g_unlink(out_path);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/gcc-compiler/new",
                    test_gcc_compiler_new);
    g_test_add_func("/gcc-compiler/get-version",
                    test_gcc_compiler_get_version);
    g_test_add_func("/gcc-compiler/get-base-flags",
                    test_gcc_compiler_get_base_flags);
    g_test_add_func("/gcc-compiler/implements-interface",
                    test_gcc_compiler_implements_interface);
    g_test_add_func("/gcc-compiler/compile-shared-trivial",
                    test_gcc_compiler_compile_shared_trivial);
    g_test_add_func("/gcc-compiler/compile-shared-with-glib",
                    test_gcc_compiler_compile_shared_with_glib);
    g_test_add_func("/gcc-compiler/compile-shared-with-extra-flags",
                    test_gcc_compiler_compile_shared_with_extra_flags);
    g_test_add_func("/gcc-compiler/compile-failure-syntax-error",
                    test_gcc_compiler_compile_failure_syntax_error);
    g_test_add_func("/gcc-compiler/compile-executable",
                    test_gcc_compiler_compile_executable);

    return g_test_run();
}
