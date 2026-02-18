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
 * Convenience wrapper around crispy_file_cache_new_with_dir() with
 * a %NULL cache directory (uses the default).
 *
 * The created object implements the #CrispyCacheProvider interface.
 *
 * Returns: (transfer full): a new #CrispyFileCache
 */
CrispyFileCache *crispy_file_cache_new (void);

/**
 * crispy_file_cache_new_with_dir:
 * @cache_dir: (nullable): path to the cache directory, or %NULL for default
 *
 * Creates a new #CrispyFileCache instance with a custom cache directory.
 * If @cache_dir is %NULL, the default `~/.cache/crispy/` is used.
 *
 * The directory (and any parent directories) will be created if they
 * do not exist. If creation fails, a warning is logged but the object
 * is still returned — cache operations will simply miss every time.
 *
 * The created object implements the #CrispyCacheProvider interface.
 *
 * Returns: (transfer full): a new #CrispyFileCache
 */
CrispyFileCache *crispy_file_cache_new_with_dir (const gchar *cache_dir);

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
