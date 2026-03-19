/* test-dependency-resolver.c - Tests for CrispyDependencyResolver and CrispyPkgConfigResolver */

#define CRISPY_COMPILATION
#include "../src/crispy.h"

#include <glib.h>
#include <string.h>

/* test: CrispyDependencyResolver is an interface type */
static void
test_interface_type(void)
{
    GType type;

    type = CRISPY_TYPE_DEPENDENCY_RESOLVER;
    g_assert_true(G_TYPE_IS_INTERFACE(type));
}

/* test: CrispyPkgConfigResolver is a final GObject type */
static void
test_resolver_is_final(void)
{
    GType type;

    type = CRISPY_TYPE_PKG_CONFIG_RESOLVER;
    g_assert_true(G_TYPE_IS_OBJECT(type));
    g_assert_true(G_TYPE_IS_FINAL(type));
}

/* test: CrispyPkgConfigResolver conforms to CrispyDependencyResolver */
static void
test_resolver_conforms(void)
{
    gboolean conforms;

    conforms = g_type_is_a(CRISPY_TYPE_PKG_CONFIG_RESOLVER,
                           CRISPY_TYPE_DEPENDENCY_RESOLVER);
    g_assert_true(conforms);
}

/* test: creates successfully */
static void
test_resolver_new(void)
{
    g_autoptr(CrispyPkgConfigResolver) resolver = NULL;

    resolver = crispy_pkg_config_resolver_new();
    g_assert_nonnull(resolver);
    g_assert_true(CRISPY_IS_DEPENDENCY_RESOLVER(resolver));
}

/* test: resolve "glib-2.0" returns valid info with non-empty cflags/ldflags */
static void
test_resolve_glib(void)
{
    g_autoptr(CrispyPkgConfigResolver) resolver = NULL;
    g_autoptr(GError) error = NULL;
    CrispyDependencyInfo *info;

    resolver = crispy_pkg_config_resolver_new();
    info = crispy_dependency_resolver_resolve(
        CRISPY_DEPENDENCY_RESOLVER(resolver), "glib-2.0", &error);

    g_assert_no_error(error);
    g_assert_nonnull(info);
    g_assert_true(crispy_dependency_info_is_resolved(info));

    /* cflags must mention glib */
    g_assert_nonnull(crispy_dependency_info_get_cflags(info));
    g_assert_cmpuint(strlen(crispy_dependency_info_get_cflags(info)), >, 0);

    /* ldflags must mention glib */
    g_assert_nonnull(crispy_dependency_info_get_ldflags(info));
    g_assert_cmpuint(strlen(crispy_dependency_info_get_ldflags(info)), >, 0);

    crispy_dependency_info_free(info);
}

/* test: is_available returns TRUE for glib-2.0 */
static void
test_is_available_glib(void)
{
    g_autoptr(CrispyPkgConfigResolver) resolver = NULL;
    gboolean available;

    resolver = crispy_pkg_config_resolver_new();
    available = crispy_dependency_resolver_is_available(
        CRISPY_DEPENDENCY_RESOLVER(resolver), "glib-2.0");

    g_assert_true(available);
}

/* test: is_available returns FALSE for nonexistent package */
static void
test_is_available_nonexistent(void)
{
    g_autoptr(CrispyPkgConfigResolver) resolver = NULL;
    gboolean available;

    resolver = crispy_pkg_config_resolver_new();
    available = crispy_dependency_resolver_is_available(
        CRISPY_DEPENDENCY_RESOLVER(resolver), "nonexistent-pkg-12345");

    g_assert_false(available);
}

/* test: get_version returns a non-empty string for glib-2.0 */
static void
test_get_version_glib(void)
{
    g_autoptr(CrispyPkgConfigResolver) resolver = NULL;
    g_autofree gchar *version = NULL;

    resolver = crispy_pkg_config_resolver_new();
    version = crispy_dependency_resolver_get_version(
        CRISPY_DEPENDENCY_RESOLVER(resolver), "glib-2.0");

    g_assert_nonnull(version);
    g_assert_cmpuint(strlen(version), >, 0);
}

/* test: resolving nonexistent package returns NULL with a GError */
static void
test_resolve_nonexistent(void)
{
    g_autoptr(CrispyPkgConfigResolver) resolver = NULL;
    g_autoptr(GError) error = NULL;
    CrispyDependencyInfo *info;

    resolver = crispy_pkg_config_resolver_new();
    info = crispy_dependency_resolver_resolve(
        CRISPY_DEPENDENCY_RESOLVER(resolver),
        "nonexistent-pkg-12345", &error);

    g_assert_null(info);
    g_assert_nonnull(error);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/dependency-resolver/interface-type",
                    test_interface_type);
    g_test_add_func("/dependency-resolver/resolver-is-final",
                    test_resolver_is_final);
    g_test_add_func("/dependency-resolver/resolver-conforms",
                    test_resolver_conforms);
    g_test_add_func("/dependency-resolver/resolver-new",
                    test_resolver_new);
    g_test_add_func("/dependency-resolver/resolve-glib",
                    test_resolve_glib);
    g_test_add_func("/dependency-resolver/is-available-glib",
                    test_is_available_glib);
    g_test_add_func("/dependency-resolver/is-available-nonexistent",
                    test_is_available_nonexistent);
    g_test_add_func("/dependency-resolver/get-version-glib",
                    test_get_version_glib);
    g_test_add_func("/dependency-resolver/resolve-nonexistent",
                    test_resolve_nonexistent);

    return g_test_run();
}
