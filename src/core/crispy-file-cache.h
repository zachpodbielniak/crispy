/* crispy-file-cache.h - Filesystem CrispyCacheProvider implementation */

#ifndef CRISPY_FILE_CACHE_H
#define CRISPY_FILE_CACHE_H

#if !defined(CRISPY_INSIDE) && !defined(CRISPY_COMPILATION)
#error "Only <crispy.h> can be included directly."
#endif

#include <glib-object.h>

G_BEGIN_DECLS

#define CRISPY_TYPE_FILE_CACHE (crispy_file_cache_get_type())

G_DECLARE_FINAL_TYPE(CrispyFileCache, crispy_file_cache, CRISPY, FILE_CACHE, GObject)

/**
 * crispy_file_cache_new:
 *
 * Creates a new #CrispyFileCache instance. The cache directory
 * is created at `~/.cache/crispy/` if it does not already exist.
 *
 * The created object implements the #CrispyCacheProvider interface.
 *
 * Returns: (transfer full): a new #CrispyFileCache
 */
CrispyFileCache *crispy_file_cache_new (void);

/**
 * crispy_file_cache_get_dir:
 * @self: a #CrispyFileCache
 *
 * Returns the path to the cache directory.
 *
 * Returns: (transfer none): the cache directory path
 */
const gchar *crispy_file_cache_get_dir (CrispyFileCache *self);

G_END_DECLS

#endif /* CRISPY_FILE_CACHE_H */
