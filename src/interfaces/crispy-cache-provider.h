/* crispy-cache-provider.h - CrispyCacheProvider GInterface */

#ifndef CRISPY_CACHE_PROVIDER_H
#define CRISPY_CACHE_PROVIDER_H

#if !defined(CRISPY_INSIDE) && !defined(CRISPY_COMPILATION)
#error "Only <crispy.h> can be included directly."
#endif

#include <glib-object.h>

G_BEGIN_DECLS

#define CRISPY_TYPE_CACHE_PROVIDER (crispy_cache_provider_get_type())

G_DECLARE_INTERFACE(CrispyCacheProvider, crispy_cache_provider, CRISPY, CACHE_PROVIDER, GObject)

/**
 * CrispyCacheProviderInterface:
 * @parent_iface: the parent interface
 * @compute_hash: computes a hash key for caching
 * @get_path: returns the path to a cached artifact
 * @has_valid: checks if a valid cached artifact exists
 * @purge: purges all cached artifacts
 *
 * The virtual function table for the #CrispyCacheProvider interface.
 * Implementations provide a caching backend (e.g., filesystem, in-memory).
 */
struct _CrispyCacheProviderInterface
{
    GTypeInterface parent_iface;

    /* virtual methods */

    gchar    * (*compute_hash)  (CrispyCacheProvider *self,
                                 const gchar         *source_content,
                                 gssize               source_len,
                                 const gchar         *extra_flags,
                                 const gchar         *compiler_version);

    gchar    * (*get_path)      (CrispyCacheProvider *self,
                                 const gchar         *hash);

    gboolean   (*has_valid)     (CrispyCacheProvider *self,
                                 const gchar         *hash,
                                 const gchar         *source_path);

    gboolean   (*purge)         (CrispyCacheProvider *self,
                                 GError             **error);
};

/**
 * crispy_cache_provider_compute_hash:
 * @self: a #CrispyCacheProvider
 * @source_content: the source file contents
 * @source_len: length of source content, or -1 if NUL-terminated
 * @extra_flags: (nullable): expanded CRISPY_PARAMS string
 * @compiler_version: compiler version string
 *
 * Computes a hash key from the source content, extra flags, and
 * compiler version. The hash uniquely identifies a compilation
 * configuration for caching purposes.
 *
 * Returns: (transfer full): hex digest string, free with g_free()
 */
gchar *crispy_cache_provider_compute_hash (CrispyCacheProvider *self,
                                           const gchar         *source_content,
                                           gssize               source_len,
                                           const gchar         *extra_flags,
                                           const gchar         *compiler_version);

/**
 * crispy_cache_provider_get_path:
 * @self: a #CrispyCacheProvider
 * @hash: the hash key
 *
 * Returns the full path to the cached artifact for a given hash.
 *
 * Returns: (transfer full): file path string, free with g_free()
 */
gchar *crispy_cache_provider_get_path (CrispyCacheProvider *self,
                                       const gchar         *hash);

/**
 * crispy_cache_provider_has_valid:
 * @self: a #CrispyCacheProvider
 * @hash: the hash key
 * @source_path: (nullable): original source file path for freshness check
 *
 * Checks if a valid cached artifact exists for the given hash.
 * If @source_path is provided, also verifies the cached artifact
 * is not stale relative to the source file.
 *
 * Returns: %TRUE if a valid cache entry exists, %FALSE otherwise
 */
gboolean crispy_cache_provider_has_valid (CrispyCacheProvider *self,
                                          const gchar         *hash,
                                          const gchar         *source_path);

/**
 * crispy_cache_provider_purge:
 * @self: a #CrispyCacheProvider
 * @error: return location for a #GError, or %NULL
 *
 * Purges all cached artifacts managed by this provider.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean crispy_cache_provider_purge (CrispyCacheProvider *self,
                                      GError             **error);

G_END_DECLS

#endif /* CRISPY_CACHE_PROVIDER_H */
