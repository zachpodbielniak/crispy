/* test-file-cache.c - Tests for CrispyFileCache */

#define CRISPY_COMPILATION
#include "../src/crispy.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

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

/* test: existing .so file returns TRUE */
static void
test_file_cache_has_valid_hit(void)
{
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autofree gchar *path = NULL;
    gboolean valid;

    cache = crispy_file_cache_new();

    /* create a dummy .so file */
    path = crispy_cache_provider_get_path(
        CRISPY_CACHE_PROVIDER(cache), "test_hit_hash");
    g_file_set_contents(path, "dummy", -1, NULL);

    valid = crispy_cache_provider_has_valid(
        CRISPY_CACHE_PROVIDER(cache), "test_hit_hash", NULL);
    g_assert_true(valid);

    /* cleanup */
    g_unlink(path);
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
    g_test_add_func("/file-cache/purge",
                    test_file_cache_purge);
    g_test_add_func("/file-cache/purge-empty",
                    test_file_cache_purge_empty);

    return g_test_run();
}
