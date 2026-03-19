/* test-profiler.c - Tests for profiling utilities */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "../src/core/crispy-profiler-private.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

/* test: get_flags returns "-pg" */
static void
test_get_flags(void)
{
    const gchar *flags;

    flags = crispy_profiler_get_flags();

    g_assert_nonnull(flags);
    g_assert_true(strstr(flags, "-pg") != NULL);
}

/* test: cleanup of a nonexistent gmon.out file does not crash */
static void
test_cleanup_nonexistent(void)
{
    /* must not crash or abort */
    crispy_profiler_cleanup("/nonexistent/path/to/gmon.out");
}

/* test: cleanup removes a gmon.out file when it exists */
static void
test_cleanup_existing(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *gmon_path = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-profiler-XXXXXX", &error);
    g_assert_no_error(error);

    gmon_path = g_build_filename(tmpdir, "gmon.out", NULL);

    g_file_set_contents(gmon_path, "dummy gmon data\n", -1, &error);
    g_assert_no_error(error);

    g_assert_true(g_file_test(gmon_path, G_FILE_TEST_EXISTS));

    crispy_profiler_cleanup(gmon_path);

    g_assert_false(g_file_test(gmon_path, G_FILE_TEST_EXISTS));

    g_rmdir(tmpdir);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/profiler/get-flags",
                    test_get_flags);
    g_test_add_func("/profiler/cleanup-nonexistent",
                    test_cleanup_nonexistent);
    g_test_add_func("/profiler/cleanup-existing",
                    test_cleanup_existing);

    return g_test_run();
}
