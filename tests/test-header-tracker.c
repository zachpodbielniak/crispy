/* test-header-tracker.c - Tests for header dependency tracking */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "../src/core/crispy-header-tracker-private.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <utime.h>
#include <time.h>
#include <string.h>

/* helper: write a file with specific content */
static gchar *
write_temp_file(
    const gchar *tmpdir,
    const gchar *name,
    const gchar *contents
){
    gchar *path;
    GError *err = NULL;

    path = g_build_filename(tmpdir, name, NULL);
    g_file_set_contents(path, contents, -1, &err);
    g_assert_no_error(err);

    return path;
}

/* helper: set a file's mtime to a given unix timestamp */
static void
set_mtime(
    const gchar *path,
    time_t       mtime
){
    struct utimbuf times;

    times.actime  = mtime;
    times.modtime = mtime;
    utime(path, &times);
}

/* test: get_depfile_path replaces the artifact's extension */
static void
test_get_depfile_path(void)
{
    g_autofree gchar *path = NULL;
    g_autofree gchar *staged = NULL;
    g_autofree gchar *no_ext = NULL;

    path = crispy_header_tracker_get_depfile_path(
        "/var/cache/crispy/abc123.so");
    g_assert_cmpstr(path, ==, "/var/cache/crispy/abc123.d");

    /*
     * A staged artifact must map to a name of its own, or two publishes
     * of the same hash would fight over one dependency file.
     */
    staged = crispy_header_tracker_get_depfile_path(
        "/var/cache/crispy/.crispy-stage-AB12CD-abc123.so");
    g_assert_cmpstr(staged, ==,
                    "/var/cache/crispy/.crispy-stage-AB12CD-abc123.d");

    /* nothing to replace: append rather than eat a directory's dot */
    no_ext = crispy_header_tracker_get_depfile_path("/tmp/.crispy-dbg-7");
    g_assert_cmpstr(no_ext, ==, "/tmp/.crispy-dbg-7.d");
}

/* test: parse a simple single-line depfile */
static void
test_parse_depfile_simple(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *depfile = NULL;
    GPtrArray *deps = NULL;
    gboolean ok;
    const gchar *content =
        "output.so: foo.c /usr/include/glib.h /usr/include/stdio.h\n";

    tmpdir = g_dir_make_tmp("crispy-test-tracker-XXXXXX", &error);
    g_assert_no_error(error);

    depfile = write_temp_file(tmpdir, "test.d", content);

    ok = crispy_header_tracker_parse_depfile(depfile, &deps, &error);

    g_assert_no_error(error);
    g_assert_true(ok);
    g_assert_nonnull(deps);
    g_assert_cmpuint(deps->len, >=, 1);

    g_ptr_array_free(deps, TRUE);
    g_unlink(depfile);
    g_rmdir(tmpdir);
}

/* test: parse a multiline depfile with backslash continuations */
static void
test_parse_depfile_multiline(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *depfile = NULL;
    GPtrArray *deps = NULL;
    gboolean ok;
    const gchar *content =
        "output.so: foo.c \\\n"
        "  /usr/include/glib.h \\\n"
        "  /usr/include/glib-2.0/glib/gtypes.h \\\n"
        "  /usr/include/stdio.h\n";

    tmpdir = g_dir_make_tmp("crispy-test-tracker-XXXXXX", &error);
    g_assert_no_error(error);

    depfile = write_temp_file(tmpdir, "multi.d", content);

    ok = crispy_header_tracker_parse_depfile(depfile, &deps, &error);

    g_assert_no_error(error);
    g_assert_true(ok);
    g_assert_nonnull(deps);
    g_assert_cmpuint(deps->len, >=, 3);

    g_ptr_array_free(deps, TRUE);
    g_unlink(depfile);
    g_rmdir(tmpdir);
}

/* test: parse returns error for nonexistent file */
static void
test_parse_depfile_nonexistent(void)
{
    g_autoptr(GError) error = NULL;
    GPtrArray *deps = NULL;
    gboolean ok;

    ok = crispy_header_tracker_parse_depfile(
        "/nonexistent/path/that/does/not/exist.d", &deps, &error);

    g_assert_false(ok);
    g_assert_nonnull(error);
}

/* test: check_stale returns FALSE when all deps are older than reference */
static void
test_check_stale_not_stale(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *header = NULL;
    GPtrArray *deps;
    gboolean stale;
    /* reference time: far in the future relative to our temp file */
    time_t reference;

    tmpdir = g_dir_make_tmp("crispy-test-tracker-XXXXXX", &error);
    g_assert_no_error(error);

    header = write_temp_file(tmpdir, "header.h", "/* header */\n");

    /* set header mtime to a known old time */
    set_mtime(header, (time_t)1000000);

    reference = (gint64)2000000;

    deps = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(deps, g_strdup(header));

    stale = crispy_header_tracker_check_stale(deps, reference);

    g_assert_false(stale);

    g_ptr_array_free(deps, TRUE);
    g_unlink(header);
    g_rmdir(tmpdir);
}

/* test: check_stale returns TRUE when one dep is newer than reference */
static void
test_check_stale_is_stale(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *header = NULL;
    GPtrArray *deps;
    gboolean stale;
    time_t reference;

    tmpdir = g_dir_make_tmp("crispy-test-tracker-XXXXXX", &error);
    g_assert_no_error(error);

    header = write_temp_file(tmpdir, "newheader.h", "/* new header */\n");

    /* set header mtime to a time after the reference */
    set_mtime(header, (time_t)3000000);

    reference = (gint64)2000000;

    deps = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(deps, g_strdup(header));

    stale = crispy_header_tracker_check_stale(deps, reference);

    g_assert_true(stale);

    g_ptr_array_free(deps, TRUE);
    g_unlink(header);
    g_rmdir(tmpdir);
}

/* test: missing dep file is treated as stale */
static void
test_check_stale_missing_dep(void)
{
    GPtrArray *deps;
    gboolean stale;
    time_t reference;

    reference = (gint64)2000000;

    deps = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(deps, g_strdup("/nonexistent/header.h"));

    stale = crispy_header_tracker_check_stale(deps, reference);

    /* missing dep should be considered stale (safe default) */
    g_assert_true(stale);

    g_ptr_array_free(deps, TRUE);
}

/* test: empty deps array is not stale */
static void
test_check_stale_empty(void)
{
    GPtrArray *deps;
    gboolean stale;

    deps = g_ptr_array_new_with_free_func(g_free);

    stale = crispy_header_tracker_check_stale(deps, (gint64)2000000);

    g_assert_false(stale);

    g_ptr_array_free(deps, TRUE);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/header-tracker/get-depfile-path",
                    test_get_depfile_path);
    g_test_add_func("/header-tracker/parse-depfile-simple",
                    test_parse_depfile_simple);
    g_test_add_func("/header-tracker/parse-depfile-multiline",
                    test_parse_depfile_multiline);
    g_test_add_func("/header-tracker/parse-depfile-nonexistent",
                    test_parse_depfile_nonexistent);
    g_test_add_func("/header-tracker/check-stale-not-stale",
                    test_check_stale_not_stale);
    g_test_add_func("/header-tracker/check-stale-is-stale",
                    test_check_stale_is_stale);
    g_test_add_func("/header-tracker/check-stale-missing-dep",
                    test_check_stale_missing_dep);
    g_test_add_func("/header-tracker/check-stale-empty",
                    test_check_stale_empty);

    return g_test_run();
}
