/* test-gcc-compiler.c - Tests for CrispyGccCompiler */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "../src/core/crispy-header-tracker-private.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

/*
 * helper: remove a compiled artifact and the dependency file beside it
 *
 * A shared-object compile now records the headers it read as
 * <artifact>.d.  Unlinking only the object leaves that file behind on
 * every run, which is how /tmp collects a thousand of them.
 */
static void
unlink_artifact(
    const gchar *path
){
    g_autofree gchar *depfile = NULL;

    depfile = crispy_header_tracker_get_depfile_path(path);

    g_unlink(depfile);
    g_unlink(path);
}

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
    unlink_artifact(out_path);
}

/*
 * test: a shared-object compile records the headers it included
 *
 * The cache key is the script text, the flags and the compiler version;
 * none of them changes when a header the script includes is edited.  The
 * dependency file is the only record of what else went into the object,
 * and without it editing a header gave back last week's code.  Asserted
 * here rather than only in the cache, because a cache that reads a
 * dependency file nothing writes is a check that always passes.
 */
static void
test_gcc_compiler_compile_shared_writes_depfile(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *src_path = NULL;
    g_autofree gchar *hdr_path = NULL;
    g_autofree gchar *out_path = NULL;
    g_autofree gchar *dep_path = NULL;
    g_autofree gchar *dep_text = NULL;
    g_autofree gchar *include_flag = NULL;
    GPtrArray *deps = NULL;
    gboolean found;
    guint i;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);

    tmpdir = g_dir_make_tmp("crispy-test-depfile-XXXXXX", &error);
    g_assert_no_error(error);

    hdr_path = g_build_filename(tmpdir, "greet.h", NULL);
    g_assert_true(g_file_set_contents(hdr_path,
                                      "#define GREETING \"hi\"\n",
                                      -1, NULL));

    src_path = g_build_filename(tmpdir, "script.c", NULL);
    g_assert_true(g_file_set_contents(src_path,
                                      "#include \"greet.h\"\n"
                                      "int main(void){ return 0; }\n",
                                      -1, NULL));

    out_path = g_build_filename(tmpdir, "out.so", NULL);
    include_flag = g_strconcat("-I", tmpdir, NULL);

    g_assert_true(crispy_compiler_compile_shared(
        CRISPY_COMPILER(compiler), src_path, out_path,
        include_flag, &error));
    g_assert_no_error(error);

    /* the dependency file lands beside the published artifact */
    dep_path = crispy_header_tracker_get_depfile_path(out_path);
    g_assert_true(g_file_test(dep_path, G_FILE_TEST_IS_REGULAR));

    g_assert_true(crispy_header_tracker_parse_depfile(dep_path, &deps,
                                                      &error));
    g_assert_no_error(error);
    g_assert_nonnull(deps);

    found = FALSE;
    for (i = 0; i < deps->len; i++)
    {
        if (strcmp((const gchar *)g_ptr_array_index(deps, i),
                   hdr_path) == 0)
        {
            found = TRUE;
        }
    }
    g_assert_true(found);

    g_ptr_array_unref(deps);
    unlink_artifact(out_path);
    g_unlink(src_path);
    g_unlink(hdr_path);
    g_rmdir(tmpdir);
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
    unlink_artifact(out_path);
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
    unlink_artifact(out_path);
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
    unlink_artifact(out_path);
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
    unlink_artifact(out_path);
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
    g_test_add_func("/gcc-compiler/compile-shared-writes-depfile",
                    test_gcc_compiler_compile_shared_writes_depfile);
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
