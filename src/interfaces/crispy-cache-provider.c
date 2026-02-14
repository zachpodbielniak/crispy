/* crispy-cache-provider.c - CrispyCacheProvider GInterface implementation */

#define CRISPY_COMPILATION
#include "crispy-cache-provider.h"

/**
 * SECTION:crispy-cache-provider
 * @title: CrispyCacheProvider
 * @short_description: Interface for caching compiled artifacts
 *
 * #CrispyCacheProvider is a GInterface that defines the contract for
 * caching compiled shared objects. The default implementation is
 * #CrispyFileCache which uses the filesystem (~/.cache/crispy/).
 *
 * Custom implementations can be created for alternative caching
 * strategies such as in-memory caching or shared network caches.
 */

G_DEFINE_INTERFACE(CrispyCacheProvider, crispy_cache_provider, G_TYPE_OBJECT)

static void
crispy_cache_provider_default_init(
    CrispyCacheProviderInterface *iface
){
    /* no default implementations or signals */
    (void)iface;
}

gchar *
crispy_cache_provider_compute_hash(
    CrispyCacheProvider *self,
    const gchar         *source_content,
    gssize               source_len,
    const gchar         *extra_flags,
    const gchar         *compiler_version
){
    CrispyCacheProviderInterface *iface;

    g_return_val_if_fail(CRISPY_IS_CACHE_PROVIDER(self), NULL);
    g_return_val_if_fail(source_content != NULL, NULL);
    g_return_val_if_fail(compiler_version != NULL, NULL);

    iface = CRISPY_CACHE_PROVIDER_GET_IFACE(self);
    g_return_val_if_fail(iface->compute_hash != NULL, NULL);

    return iface->compute_hash(self, source_content, source_len,
                               extra_flags, compiler_version);
}

gchar *
crispy_cache_provider_get_path(
    CrispyCacheProvider *self,
    const gchar         *hash
){
    CrispyCacheProviderInterface *iface;

    g_return_val_if_fail(CRISPY_IS_CACHE_PROVIDER(self), NULL);
    g_return_val_if_fail(hash != NULL, NULL);

    iface = CRISPY_CACHE_PROVIDER_GET_IFACE(self);
    g_return_val_if_fail(iface->get_path != NULL, NULL);

    return iface->get_path(self, hash);
}

gboolean
crispy_cache_provider_has_valid(
    CrispyCacheProvider *self,
    const gchar         *hash,
    const gchar         *source_path
){
    CrispyCacheProviderInterface *iface;

    g_return_val_if_fail(CRISPY_IS_CACHE_PROVIDER(self), FALSE);
    g_return_val_if_fail(hash != NULL, FALSE);

    iface = CRISPY_CACHE_PROVIDER_GET_IFACE(self);
    g_return_val_if_fail(iface->has_valid != NULL, FALSE);

    return iface->has_valid(self, hash, source_path);
}

gboolean
crispy_cache_provider_purge(
    CrispyCacheProvider *self,
    GError             **error
){
    CrispyCacheProviderInterface *iface;

    g_return_val_if_fail(CRISPY_IS_CACHE_PROVIDER(self), FALSE);
    g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

    iface = CRISPY_CACHE_PROVIDER_GET_IFACE(self);
    g_return_val_if_fail(iface->purge != NULL, FALSE);

    return iface->purge(self, error);
}
