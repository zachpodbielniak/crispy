/* test-file-cache.c - Tests for CrispyFileCache */

#define CRISPY_COMPILATION
#include "../src/crispy.h"

#include "../src/core/crispy-header-tracker-private.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <time.h>
#include <utime.h>

/* test: creating a new file cache instance succeeds */
static void
test_file_cache_new(void)
{
    g_autoptr(CrispyFileCache) cache = NULL;
    const gchar *dir;

    cache = crispy_file_cache_new();
    g_assert_nonnull(cache);

    dir = crispy_file_cache_get_dir(cache);
    g_assert_nonnull(dir);
    g_assert_true(g_file_test(dir, G_FILE_TEST_IS_DIR));
}

/* test: CrispyFileCache implements CrispyCacheProvider interface */
static void
test_file_cache_implements_interface(void)
{
    g_autoptr(CrispyFileCache) cache = NULL;

    cache = crispy_file_cache_new();
    g_assert_true(CRISPY_IS_CACHE_PROVIDER(cache));
}

/* test: same inputs produce same hash */
static void
test_file_cache_compute_hash_deterministic(void)
{
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autofree gchar *hash1 = NULL;
    g_autofree gchar *hash2 = NULL;

    cache = crispy_file_cache_new();

    hash1 = crispy_cache_provider_compute_hash(
        CRISPY_CACHE_PROVIDER(cache),
        "hello world", -1, "-lm", "gcc 14.0");
    hash2 = crispy_cache_provider_compute_hash(
        CRISPY_CACHE_PROVIDER(cache),
        "hello world", -1, "-lm", "gcc 14.0");

    g_assert_cmpstr(hash1, ==, hash2);
}

/* test: different source produces different hash */
static void
test_file_cache_compute_hash_different_source(void)
{
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autofree gchar *hash1 = NULL;
    g_autofree gchar *hash2 = NULL;

    cache = crispy_file_cache_new();

    hash1 = crispy_cache_provider_compute_hash(
        CRISPY_CACHE_PROVIDER(cache),
        "hello world", -1, NULL, "gcc 14.0");
    hash2 = crispy_cache_provider_compute_hash(
        CRISPY_CACHE_PROVIDER(cache),
        "goodbye world", -1, NULL, "gcc 14.0");

    g_assert_cmpstr(hash1, !=, hash2);
}

/* test: same source with different flags produces different hash */
static void
test_file_cache_compute_hash_different_flags(void)
{
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autofree gchar *hash1 = NULL;
    g_autofree gchar *hash2 = NULL;

    cache = crispy_file_cache_new();

    hash1 = crispy_cache_provider_compute_hash(
        CRISPY_CACHE_PROVIDER(cache),
        "hello", -1, "-lm", "gcc 14.0");
    hash2 = crispy_cache_provider_compute_hash(
        CRISPY_CACHE_PROVIDER(cache),
        "hello", -1, "-lpthread", "gcc 14.0");

    g_assert_cmpstr(hash1, !=, hash2);
}

/* test: same source with different compiler produces different hash */
static void
test_file_cache_compute_hash_different_compiler(void)
{
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autofree gchar *hash1 = NULL;
    g_autofree gchar *hash2 = NULL;

    cache = crispy_file_cache_new();

    hash1 = crispy_cache_provider_compute_hash(
        CRISPY_CACHE_PROVIDER(cache),
        "hello", -1, NULL, "gcc 14.0");
    hash2 = crispy_cache_provider_compute_hash(
        CRISPY_CACHE_PROVIDER(cache),
        "hello", -1, NULL, "gcc 15.0");

    g_assert_cmpstr(hash1, !=, hash2);
}

/* test: get_path returns expected format */
static void
test_file_cache_get_path_format(void)
{
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autofree gchar *path = NULL;

    cache = crispy_file_cache_new();

    path = crispy_cache_provider_get_path(
        CRISPY_CACHE_PROVIDER(cache), "abc123");
    g_assert_nonnull(path);
    g_assert_true(g_str_has_suffix(path, "abc123.so"));
}

/* test: non-existent hash returns FALSE */
static void
test_file_cache_has_valid_miss(void)
{
    g_autoptr(CrispyFileCache) cache = NULL;
    gboolean valid;

    cache = crispy_file_cache_new();

    valid = crispy_cache_provider_has_valid(
        CRISPY_CACHE_PROVIDER(cache),
        "nonexistent_hash_that_should_not_exist_ever",
        NULL);
    g_assert_false(valid);
}

/*
 * helper: put a real, complete ELF object in the cache under @hash
 *
 * The test binary itself is one, so the fixture needs no compiler and
 * cannot accidentally be a file that merely exists -- which is what the
 * old fixture was, and it made "an entry that cannot be loaded is a
 * cache hit" read as the intended behaviour.
 *
 * Returns: (transfer full): the path the entry was written to
 */
static gchar *
put_real_module(
    CrispyFileCache *cache,
    const gchar     *hash
){
    g_autofree gchar *self_image = NULL;
    gchar *path;
    gsize len;

    g_assert_true(g_file_get_contents("/proc/self/exe", &self_image,
                                      &len, NULL));

    path = crispy_cache_provider_get_path(
        CRISPY_CACHE_PROVIDER(cache), hash);
    g_assert_true(g_file_set_contents(path, self_image, (gssize)len, NULL));

    return path;
}

/* helper: set a file's mtime to a given unix timestamp */
static void
set_mtime(
    const gchar *path,
    time_t       when
){
    struct utimbuf times;

    times.actime  = when;
    times.modtime = when;
    g_assert_cmpint(utime(path, &times), ==, 0);
}

/* test: a complete cached module returns TRUE */
static void
test_file_cache_has_valid_hit(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *path = NULL;
    gboolean valid;

    tmpdir = g_dir_make_tmp("crispy-test-cache-XXXXXX", &error);
    g_assert_no_error(error);

    cache = crispy_file_cache_new_with_dir(tmpdir);
    path = put_real_module(cache, "test_hit_hash");

    valid = crispy_cache_provider_has_valid(
        CRISPY_CACHE_PROVIDER(cache), "test_hit_hash", NULL);
    g_assert_true(valid);

    g_unlink(path);
    g_rmdir(tmpdir);
}

/*
 * test: an entry that is only the front of a module is not a hit
 *
 * A compile killed part-way, or one read while the linker was still
 * writing, leaves exactly this: a regular file, newer than the script,
 * that g_module_open() rejects with "file too short".  Existence was the
 * whole test, so it stayed in the cache for ever and only --no-cache got
 * past it.
 */
static void
test_file_cache_has_valid_rejects_truncated(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *whole = NULL;
    gsize len;

    tmpdir = g_dir_make_tmp("crispy-test-cache-XXXXXX", &error);
    g_assert_no_error(error);

    cache = crispy_file_cache_new_with_dir(tmpdir);
    path = put_real_module(cache, "truncated_hash");

    g_assert_true(g_file_get_contents(path, &whole, &len, NULL));
    g_assert_cmpuint(len, >, 4096);

    /* keep a valid ELF header, lose everything the loader needs after it */
    g_assert_true(g_file_set_contents(path, whole, 256, NULL));

    g_assert_false(crispy_cache_provider_has_valid(
        CRISPY_CACHE_PROVIDER(cache), "truncated_hash", NULL));

    g_unlink(path);
    g_rmdir(tmpdir);
}

/* test: an entry that is not an ELF object at all is not a hit */
static void
test_file_cache_has_valid_rejects_garbage(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *path = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-cache-XXXXXX", &error);
    g_assert_no_error(error);

    cache = crispy_file_cache_new_with_dir(tmpdir);

    path = crispy_cache_provider_get_path(
        CRISPY_CACHE_PROVIDER(cache), "garbage_hash");
    g_assert_true(g_file_set_contents(path, "dummy", -1, NULL));

    g_assert_false(crispy_cache_provider_has_valid(
        CRISPY_CACHE_PROVIDER(cache), "garbage_hash", NULL));

    g_unlink(path);
    g_rmdir(tmpdir);
}

/*
 * test: a header modified after the entry was built invalidates it
 *
 * The cache key covers the script text and the compiler flags, neither
 * of which changes when a header the script includes is edited.  Editing
 * one and re-running gave back last week's code, silently.
 */
static void
test_file_cache_has_valid_stale_header(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *depfile = NULL;
    g_autofree gchar *header = NULL;
    g_autofree gchar *depline = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-cache-XXXXXX", &error);
    g_assert_no_error(error);

    cache = crispy_file_cache_new_with_dir(tmpdir);
    path = put_real_module(cache, "header_hash");

    header = g_build_filename(tmpdir, "greet.h", NULL);
    g_assert_true(g_file_set_contents(header, "#define G 1\n", -1, NULL));

    depfile = crispy_header_tracker_get_depfile_path(path);
    depline = g_strdup_printf("out.so: script.c %s\n", header);
    g_assert_true(g_file_set_contents(depfile, depline, -1, NULL));

    /* header older than the artifact: the entry still stands */
    set_mtime(path, (time_t)2000000);
    set_mtime(header, (time_t)1000000);
    g_assert_true(crispy_cache_provider_has_valid(
        CRISPY_CACHE_PROVIDER(cache), "header_hash", NULL));

    /* header edited since: the entry describes code nobody asked for */
    set_mtime(header, (time_t)3000000);
    g_assert_false(crispy_cache_provider_has_valid(
        CRISPY_CACHE_PROVIDER(cache), "header_hash", NULL));

    g_unlink(depfile);
    g_unlink(header);
    g_unlink(path);
    g_rmdir(tmpdir);
}

/* test: purge takes the dependency files and staged objects too */
static void
test_file_cache_purge_sidecars(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *depfile = NULL;
    g_autofree gchar *staged = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-cache-XXXXXX", &error);
    g_assert_no_error(error);

    cache = crispy_file_cache_new_with_dir(tmpdir);

    depfile = g_build_filename(tmpdir, "abc123.d", NULL);
    staged = g_build_filename(tmpdir, ".crispy-stage-AB12CD-abc123.so", NULL);
    g_assert_true(g_file_set_contents(depfile, "out: in.c\n", -1, NULL));
    g_assert_true(g_file_set_contents(staged, "half", -1, NULL));

    g_assert_true(crispy_cache_provider_purge(
        CRISPY_CACHE_PROVIDER(cache), &error));
    g_assert_no_error(error);

    g_assert_false(g_file_test(depfile, G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(staged, G_FILE_TEST_EXISTS));

    g_rmdir(tmpdir);
}

/* test: purge removes cached files */
static void
test_file_cache_purge(void)
{
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path1 = NULL;
    g_autofree gchar *path2 = NULL;
    gboolean ok;

    cache = crispy_file_cache_new();

    /* create dummy .so files */
    path1 = crispy_cache_provider_get_path(
        CRISPY_CACHE_PROVIDER(cache), "purge_test_1");
    path2 = crispy_cache_provider_get_path(
        CRISPY_CACHE_PROVIDER(cache), "purge_test_2");
    g_file_set_contents(path1, "dummy", -1, NULL);
    g_file_set_contents(path2, "dummy", -1, NULL);

    ok = crispy_cache_provider_purge(
        CRISPY_CACHE_PROVIDER(cache), &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    /* verify files are gone */
    g_assert_false(g_file_test(path1, G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(path2, G_FILE_TEST_EXISTS));
}

/* test: purge on empty dir succeeds */
static void
test_file_cache_purge_empty(void)
{
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autoptr(GError) error = NULL;
    gboolean ok;

    cache = crispy_file_cache_new();

    ok = crispy_cache_provider_purge(
        CRISPY_CACHE_PROVIDER(cache), &error);
    g_assert_no_error(error);
    g_assert_true(ok);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/file-cache/new",
                    test_file_cache_new);
    g_test_add_func("/file-cache/implements-interface",
                    test_file_cache_implements_interface);
    g_test_add_func("/file-cache/compute-hash-deterministic",
                    test_file_cache_compute_hash_deterministic);
    g_test_add_func("/file-cache/compute-hash-different-source",
                    test_file_cache_compute_hash_different_source);
    g_test_add_func("/file-cache/compute-hash-different-flags",
                    test_file_cache_compute_hash_different_flags);
    g_test_add_func("/file-cache/compute-hash-different-compiler",
                    test_file_cache_compute_hash_different_compiler);
    g_test_add_func("/file-cache/get-path-format",
                    test_file_cache_get_path_format);
    g_test_add_func("/file-cache/has-valid-miss",
                    test_file_cache_has_valid_miss);
    g_test_add_func("/file-cache/has-valid-hit",
                    test_file_cache_has_valid_hit);
    g_test_add_func("/file-cache/has-valid-rejects-truncated",
                    test_file_cache_has_valid_rejects_truncated);
    g_test_add_func("/file-cache/has-valid-rejects-garbage",
                    test_file_cache_has_valid_rejects_garbage);
    g_test_add_func("/file-cache/has-valid-stale-header",
                    test_file_cache_has_valid_stale_header);
    g_test_add_func("/file-cache/purge-sidecars",
                    test_file_cache_purge_sidecars);
    g_test_add_func("/file-cache/purge",
                    test_file_cache_purge);
    g_test_add_func("/file-cache/purge-empty",
                    test_file_cache_purge_empty);

    return g_test_run();
}
