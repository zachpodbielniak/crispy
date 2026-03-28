/* crispy-dependency-resolver.c - CrispyDependencyResolver GInterface implementation */

#ifndef CRISPY_COMPILATION
#define CRISPY_COMPILATION
#endif
#include "crispy-dependency-resolver.h"
#include "../crispy-types.h"

/**
 * SECTION:crispy-dependency-resolver
 * @title: CrispyDependencyResolver
 * @short_description: Interface for resolving build dependencies
 *
 * #CrispyDependencyResolver is a GInterface that defines the contract for
 * resolving build dependencies and querying their compiler and linker flags.
 * The default implementation uses pkg-config to look up installed libraries.
 *
 * Custom implementations can be created to support alternative dependency
 * resolution strategies such as CMake find-modules or vendored libraries.
 */

G_DEFINE_INTERFACE(CrispyDependencyResolver, crispy_dependency_resolver, G_TYPE_OBJECT)

static void
crispy_dependency_resolver_default_init(
    CrispyDependencyResolverInterface *iface
){
    /* no default implementations or signals */
    (void)iface;
}

CrispyDependencyInfo *
crispy_dependency_resolver_resolve(
    CrispyDependencyResolver  *self,
    const gchar               *name,
    GError                   **error
){
    CrispyDependencyResolverInterface *iface;

    g_return_val_if_fail(CRISPY_IS_DEPENDENCY_RESOLVER(self), NULL);
    g_return_val_if_fail(name != NULL, NULL);
    g_return_val_if_fail(error == NULL || *error == NULL, NULL);

    iface = CRISPY_DEPENDENCY_RESOLVER_GET_IFACE(self);
    g_return_val_if_fail(iface->resolve != NULL, NULL);

    return iface->resolve(self, name, error);
}

gboolean
crispy_dependency_resolver_is_available(
    CrispyDependencyResolver *self,
    const gchar              *name
){
    CrispyDependencyResolverInterface *iface;

    g_return_val_if_fail(CRISPY_IS_DEPENDENCY_RESOLVER(self), FALSE);
    g_return_val_if_fail(name != NULL, FALSE);

    iface = CRISPY_DEPENDENCY_RESOLVER_GET_IFACE(self);
    g_return_val_if_fail(iface->is_available != NULL, FALSE);

    return iface->is_available(self, name);
}

gchar *
crispy_dependency_resolver_get_version(
    CrispyDependencyResolver *self,
    const gchar              *name
){
    CrispyDependencyResolverInterface *iface;

    g_return_val_if_fail(CRISPY_IS_DEPENDENCY_RESOLVER(self), NULL);
    g_return_val_if_fail(name != NULL, NULL);

    iface = CRISPY_DEPENDENCY_RESOLVER_GET_IFACE(self);
    g_return_val_if_fail(iface->get_version != NULL, NULL);

    return iface->get_version(self, name);
}
