/* test-dependency-info.c - Tests for CrispyDependencyInfo boxed type */

#define CRISPY_COMPILATION
#include "../src/crispy.h"

#include <glib.h>
#include <string.h>

/* test: create with all fields and verify getters */
static void
test_new(void)
{
    CrispyDependencyInfo *info;

    info = crispy_dependency_info_new("glib-2.0", "-I/usr/include/glib-2.0",
                                     "-lglib-2.0", "2.78.0", TRUE);
    g_assert_nonnull(info);
    g_assert_cmpstr(crispy_dependency_info_get_name(info), ==, "glib-2.0");
    g_assert_cmpstr(crispy_dependency_info_get_cflags(info), ==,
                    "-I/usr/include/glib-2.0");
    g_assert_cmpstr(crispy_dependency_info_get_ldflags(info), ==, "-lglib-2.0");
    g_assert_cmpstr(crispy_dependency_info_get_version(info), ==, "2.78.0");
    g_assert_true(crispy_dependency_info_is_resolved(info));

    crispy_dependency_info_free(info);
}

/* test: create with NULL version is safe */
static void
test_new_null_version(void)
{
    CrispyDependencyInfo *info;

    info = crispy_dependency_info_new("mypkg", "-Ifoo", "-lfoo", NULL, TRUE);
    g_assert_nonnull(info);
    g_assert_null(crispy_dependency_info_get_version(info));

    crispy_dependency_info_free(info);
}

/* test: deep copy produces independent struct */
static void
test_copy(void)
{
    CrispyDependencyInfo *orig;
    CrispyDependencyInfo *copy;

    orig = crispy_dependency_info_new("pkg-a", "-Ia", "-la", "1.0", TRUE);
    copy = crispy_dependency_info_copy(orig);

    g_assert_nonnull(copy);
    g_assert_cmpstr(crispy_dependency_info_get_name(copy), ==, "pkg-a");
    g_assert_cmpstr(crispy_dependency_info_get_cflags(copy), ==, "-Ia");
    g_assert_cmpstr(crispy_dependency_info_get_ldflags(copy), ==, "-la");
    g_assert_cmpstr(crispy_dependency_info_get_version(copy), ==, "1.0");

    /* pointers must differ — actual deep copy */
    g_assert_true(crispy_dependency_info_get_name(orig)
                  != crispy_dependency_info_get_name(copy));

    crispy_dependency_info_free(orig);
    crispy_dependency_info_free(copy);
}

/* test: freeing NULL is safe (must not crash) */
static void
test_free_null(void)
{
    crispy_dependency_info_free(NULL);
}

/* test: get_name returns correct name */
static void
test_get_name(void)
{
    CrispyDependencyInfo *info;

    info = crispy_dependency_info_new("json-glib-1.0", "", "", NULL, FALSE);
    g_assert_cmpstr(crispy_dependency_info_get_name(info), ==, "json-glib-1.0");

    crispy_dependency_info_free(info);
}

/* test: get_cflags returns correct flags */
static void
test_get_cflags(void)
{
    CrispyDependencyInfo *info;

    info = crispy_dependency_info_new("pkg", "-I/some/path -DFOO=1", "", NULL, TRUE);
    g_assert_cmpstr(crispy_dependency_info_get_cflags(info), ==,
                    "-I/some/path -DFOO=1");

    crispy_dependency_info_free(info);
}

/* test: get_ldflags returns correct flags */
static void
test_get_ldflags(void)
{
    CrispyDependencyInfo *info;

    info = crispy_dependency_info_new("pkg", "", "-L/usr/lib -lpkg -Wl,--rpath",
                                     NULL, TRUE);
    g_assert_cmpstr(crispy_dependency_info_get_ldflags(info), ==,
                    "-L/usr/lib -lpkg -Wl,--rpath");

    crispy_dependency_info_free(info);
}

/* test: get_version returns correct version string */
static void
test_get_version(void)
{
    CrispyDependencyInfo *info;

    info = crispy_dependency_info_new("libfoo", "", "", "3.14.15", TRUE);
    g_assert_cmpstr(crispy_dependency_info_get_version(info), ==, "3.14.15");

    crispy_dependency_info_free(info);
}

/* test: get_version returns NULL when created with NULL */
static void
test_get_version_null(void)
{
    CrispyDependencyInfo *info;

    info = crispy_dependency_info_new("libfoo", "", "", NULL, TRUE);
    g_assert_null(crispy_dependency_info_get_version(info));

    crispy_dependency_info_free(info);
}

/* test: is_resolved returns TRUE */
static void
test_is_resolved_true(void)
{
    CrispyDependencyInfo *info;

    info = crispy_dependency_info_new("pkg", "-I.", "-lx", "1.0", TRUE);
    g_assert_true(crispy_dependency_info_is_resolved(info));

    crispy_dependency_info_free(info);
}

/* test: is_resolved returns FALSE */
static void
test_is_resolved_false(void)
{
    CrispyDependencyInfo *info;

    info = crispy_dependency_info_new("nonexistent-pkg", "", "", NULL, FALSE);
    g_assert_false(crispy_dependency_info_is_resolved(info));

    crispy_dependency_info_free(info);
}

/* test: get_combined_flags returns "cflags ldflags" */
static void
test_get_combined_flags(void)
{
    CrispyDependencyInfo *info;
    g_autofree gchar *combined = NULL;

    info = crispy_dependency_info_new("pkg", "-Ifoo", "-lfoo", "1.0", TRUE);
    combined = crispy_dependency_info_get_combined_flags(info);

    g_assert_nonnull(combined);
    g_assert_true(strstr(combined, "-Ifoo") != NULL);
    g_assert_true(strstr(combined, "-lfoo") != NULL);

    crispy_dependency_info_free(info);
}

/* test: get_combined_flags with both empty strings */
static void
test_get_combined_flags_empty(void)
{
    CrispyDependencyInfo *info;
    g_autofree gchar *combined = NULL;

    info = crispy_dependency_info_new("pkg", "", "", NULL, FALSE);
    combined = crispy_dependency_info_get_combined_flags(info);

    /* must not crash; result is a valid string */
    g_assert_nonnull(combined);

    crispy_dependency_info_free(info);
}

/* test: CRISPY_TYPE_DEPENDENCY_INFO is a valid registered GType */
static void
test_boxed_type(void)
{
    GType type;

    type = CRISPY_TYPE_DEPENDENCY_INFO;
    g_assert_cmpuint(type, !=, G_TYPE_INVALID);
    g_assert_true(G_TYPE_IS_BOXED(type));
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/dependency-info/new",
                    test_new);
    g_test_add_func("/dependency-info/new-null-version",
                    test_new_null_version);
    g_test_add_func("/dependency-info/copy",
                    test_copy);
    g_test_add_func("/dependency-info/free-null",
                    test_free_null);
    g_test_add_func("/dependency-info/get-name",
                    test_get_name);
    g_test_add_func("/dependency-info/get-cflags",
                    test_get_cflags);
    g_test_add_func("/dependency-info/get-ldflags",
                    test_get_ldflags);
    g_test_add_func("/dependency-info/get-version",
                    test_get_version);
    g_test_add_func("/dependency-info/get-version-null",
                    test_get_version_null);
    g_test_add_func("/dependency-info/is-resolved-true",
                    test_is_resolved_true);
    g_test_add_func("/dependency-info/is-resolved-false",
                    test_is_resolved_false);
    g_test_add_func("/dependency-info/get-combined-flags",
                    test_get_combined_flags);
    g_test_add_func("/dependency-info/get-combined-flags-empty",
                    test_get_combined_flags_empty);
    g_test_add_func("/dependency-info/boxed-type",
                    test_boxed_type);

    return g_test_run();
}
