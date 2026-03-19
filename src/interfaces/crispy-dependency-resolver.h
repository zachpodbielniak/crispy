/* crispy-dependency-resolver.h - CrispyDependencyResolver GInterface */

#ifndef CRISPY_DEPENDENCY_RESOLVER_H
#define CRISPY_DEPENDENCY_RESOLVER_H

#if !defined(CRISPY_INSIDE) && !defined(CRISPY_COMPILATION)
#error "Only <crispy.h> can be included directly."
#endif

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _CrispyDependencyInfo CrispyDependencyInfo;

#define CRISPY_TYPE_DEPENDENCY_RESOLVER (crispy_dependency_resolver_get_type())

G_DECLARE_INTERFACE(CrispyDependencyResolver, crispy_dependency_resolver, CRISPY, DEPENDENCY_RESOLVER, GObject)

/**
 * CrispyDependencyResolverInterface:
 * @parent_iface: the parent interface
 * @resolve: resolves a dependency by name, returning its info
 * @is_available: checks if a dependency is available on the system
 * @get_version: returns the version string for a dependency
 *
 * The virtual function table for the #CrispyDependencyResolver interface.
 * Implementations provide a dependency resolution backend (e.g., pkg-config).
 */
struct _CrispyDependencyResolverInterface
{
    GTypeInterface parent_iface;

    /* virtual methods */

    CrispyDependencyInfo * (*resolve)      (CrispyDependencyResolver  *self,
                                            const gchar               *name,
                                            GError                   **error);

    gboolean               (*is_available) (CrispyDependencyResolver *self,
                                            const gchar              *name);

    gchar                * (*get_version)  (CrispyDependencyResolver *self,
                                            const gchar              *name);
};

/**
 * crispy_dependency_resolver_resolve:
 * @self: a #CrispyDependencyResolver
 * @name: the dependency name to resolve
 * @error: return location for a #GError, or %NULL
 *
 * Resolves a dependency by name, returning a #CrispyDependencyInfo
 * containing compiler and linker flags for the dependency.
 *
 * Returns: (transfer full) (nullable): a #CrispyDependencyInfo on success,
 *   or %NULL on error
 */
CrispyDependencyInfo *crispy_dependency_resolver_resolve (CrispyDependencyResolver  *self,
                                                          const gchar               *name,
                                                          GError                   **error);

/**
 * crispy_dependency_resolver_is_available:
 * @self: a #CrispyDependencyResolver
 * @name: the dependency name to check
 *
 * Checks whether the named dependency is available on the system.
 *
 * Returns: %TRUE if the dependency is available, %FALSE otherwise
 */
gboolean crispy_dependency_resolver_is_available (CrispyDependencyResolver *self,
                                                  const gchar              *name);

/**
 * crispy_dependency_resolver_get_version:
 * @self: a #CrispyDependencyResolver
 * @name: the dependency name
 *
 * Returns the version string for the named dependency.
 *
 * Returns: (transfer full) (nullable): the version string, free with g_free(),
 *   or %NULL if the dependency is not available
 */
gchar *crispy_dependency_resolver_get_version (CrispyDependencyResolver *self,
                                               const gchar              *name);

G_END_DECLS

#endif /* CRISPY_DEPENDENCY_RESOLVER_H */
