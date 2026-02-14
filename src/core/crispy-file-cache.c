/* crispy-file-cache.c - Filesystem CrispyCacheProvider implementation */

#define CRISPY_COMPILATION
#include "crispy-file-cache.h"
#include "../interfaces/crispy-cache-provider.h"
#include "../crispy-types.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

/**
 * SECTION:crispy-file-cache
 * @title: CrispyFileCache
 * @short_description: Filesystem implementation of CrispyCacheProvider
 *
 * #CrispyFileCache stores compiled shared objects in `~/.cache/crispy/`
 * using SHA256 content hashes as filenames. It validates cache entries
 * by checking both existence and mtime relative to the source file.
 */

struct _CrispyFileCache
{
    GObject parent_instance;
};

typedef struct
{
    gchar *cache_dir;
} CrispyFileCachePrivate;

static void crispy_file_cache_provider_init (CrispyCacheProviderInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE(
    CrispyFileCache,
    crispy_file_cache,
    G_TYPE_OBJECT,
    G_ADD_PRIVATE(CrispyFileCache)
    G_IMPLEMENT_INTERFACE(CRISPY_TYPE_CACHE_PROVIDER,
                          crispy_file_cache_provider_init)
)

/* --- CrispyCacheProvider interface implementation --- */

static gchar *
file_cache_compute_hash(
    CrispyCacheProvider *self,
    const gchar         *source_content,
    gssize               source_len,
    const gchar         *extra_flags,
    const gchar         *compiler_version
){
    GChecksum *checksum;
    const gchar *hex;
    gchar *result;
    gchar separator;

    (void)self;

    if (source_len < 0)
        source_len = (gssize)strlen(source_content);

    separator = '\0';

    checksum = g_checksum_new(CRISPY_HASH_ALGO);

    /* hash source content */
    g_checksum_update(checksum, (const guchar *)source_content,
                      (gssize)source_len);

    /* NUL separator */
    g_checksum_update(checksum, (const guchar *)&separator, 1);

    /* hash extra flags (or empty string) */
    if (extra_flags != NULL)
        g_checksum_update(checksum, (const guchar *)extra_flags,
                          (gssize)strlen(extra_flags));

    /* NUL separator */
    g_checksum_update(checksum, (const guchar *)&separator, 1);

    /* hash compiler version */
    g_checksum_update(checksum, (const guchar *)compiler_version,
                      (gssize)strlen(compiler_version));

    hex = g_checksum_get_string(checksum);
    result = g_strdup(hex);

    g_checksum_free(checksum);
    return result;
}

static gchar *
file_cache_get_path(
    CrispyCacheProvider *self,
    const gchar         *hash
){
    CrispyFileCachePrivate *priv;
    g_autofree gchar *filename = NULL;

    priv = crispy_file_cache_get_instance_private(CRISPY_FILE_CACHE(self));
    filename = g_strdup_printf("%s.so", hash);
    return g_build_filename(priv->cache_dir, filename, NULL);
}

static gboolean
file_cache_has_valid(
    CrispyCacheProvider *self,
    const gchar         *hash,
    const gchar         *source_path
){
    g_autofree gchar *so_path = NULL;
    GStatBuf so_stat;
    GStatBuf src_stat;

    so_path = file_cache_get_path(self, hash);

    /* check if the cached .so exists */
    if (!g_file_test(so_path, G_FILE_TEST_IS_REGULAR))
        return FALSE;

    /* if no source path, existence is sufficient (inline/stdin) */
    if (source_path == NULL)
        return TRUE;

    /* compare mtime: cached .so must be newer than source */
    if (g_stat(so_path, &so_stat) != 0)
        return FALSE;

    if (g_stat(source_path, &src_stat) != 0)
        return FALSE;

    return so_stat.st_mtime >= src_stat.st_mtime;
}

static gboolean
file_cache_purge(
    CrispyCacheProvider *self,
    GError             **error
){
    CrispyFileCachePrivate *priv;
    GDir *dir;
    const gchar *entry;
    gint count;

    priv = crispy_file_cache_get_instance_private(CRISPY_FILE_CACHE(self));

    dir = g_dir_open(priv->cache_dir, 0, error);
    if (dir == NULL)
        return FALSE;

    count = 0;
    while ((entry = g_dir_read_name(dir)) != NULL)
    {
        if (g_str_has_suffix(entry, ".so"))
        {
            g_autofree gchar *path = NULL;

            path = g_build_filename(priv->cache_dir, entry, NULL);
            if (g_unlink(path) == 0)
                count++;
        }
    }

    g_dir_close(dir);

    g_message("Purged %d cached file(s) from %s", count, priv->cache_dir);
    return TRUE;
}

static void
crispy_file_cache_provider_init(
    CrispyCacheProviderInterface *iface
){
    iface->compute_hash = file_cache_compute_hash;
    iface->get_path     = file_cache_get_path;
    iface->has_valid    = file_cache_has_valid;
    iface->purge        = file_cache_purge;
}

/* --- GObject lifecycle --- */

static void
crispy_file_cache_finalize(
    GObject *object
){
    CrispyFileCachePrivate *priv;

    priv = crispy_file_cache_get_instance_private(CRISPY_FILE_CACHE(object));
    g_free(priv->cache_dir);

    G_OBJECT_CLASS(crispy_file_cache_parent_class)->finalize(object);
}

static void
crispy_file_cache_class_init(
    CrispyFileCacheClass *klass
){
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = crispy_file_cache_finalize;
}

static void
crispy_file_cache_init(
    CrispyFileCache *self
){
    (void)self;
}

/* --- public API --- */

CrispyFileCache *
crispy_file_cache_new(void)
{
    CrispyFileCache *self;
    CrispyFileCachePrivate *priv;

    self = g_object_new(CRISPY_TYPE_FILE_CACHE, NULL);
    priv = crispy_file_cache_get_instance_private(self);

    priv->cache_dir = g_build_filename(g_get_user_cache_dir(), "crispy", NULL);
    g_mkdir_with_parents(priv->cache_dir, 0755);

    return self;
}

const gchar *
crispy_file_cache_get_dir(
    CrispyFileCache *self
){
    CrispyFileCachePrivate *priv;

    g_return_val_if_fail(CRISPY_IS_FILE_CACHE(self), NULL);

    priv = crispy_file_cache_get_instance_private(self);
    return priv->cache_dir;
}
