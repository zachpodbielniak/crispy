/* crispy-plugin-engine.h - Plugin engine for loading and dispatching plugins */

#ifndef CRISPY_PLUGIN_ENGINE_H
#define CRISPY_PLUGIN_ENGINE_H

#if !defined(CRISPY_INSIDE) && !defined(CRISPY_COMPILATION)
#error "Only <crispy.h> can be included directly."
#endif

#include <glib-object.h>
#include "../crispy-types.h"
#include "../crispy-plugin.h"

G_BEGIN_DECLS

#define CRISPY_TYPE_PLUGIN_ENGINE (crispy_plugin_engine_get_type())

G_DECLARE_FINAL_TYPE(CrispyPluginEngine, crispy_plugin_engine, CRISPY, PLUGIN_ENGINE, GObject)

/**
 * crispy_plugin_engine_new:
 *
 * Creates a new empty plugin engine with no plugins loaded.
 *
 * Returns: (transfer full): a new #CrispyPluginEngine
 */
CrispyPluginEngine *crispy_plugin_engine_new (void);

/**
 * crispy_plugin_engine_load:
 * @self: a #CrispyPluginEngine
 * @path: path to a plugin .so file
 * @error: return location for a #GError, or %NULL
 *
 * Loads a single plugin from the given path. The plugin must export
 * a `crispy_plugin_info` symbol. If the plugin exports a
 * `crispy_plugin_init` function, it is called immediately.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean crispy_plugin_engine_load (CrispyPluginEngine  *self,
                                    const gchar         *path,
                                    GError             **error);

/**
 * crispy_plugin_engine_load_paths:
 * @self: a #CrispyPluginEngine
 * @paths: colon-or-comma-separated list of plugin .so paths
 * @error: return location for a #GError, or %NULL
 *
 * Splits @paths on ':' and ',' delimiters, then loads each path
 * via crispy_plugin_engine_load(). Stops on the first failure.
 *
 * Returns: %TRUE if all plugins loaded, %FALSE on first error
 */
gboolean crispy_plugin_engine_load_paths (CrispyPluginEngine  *self,
                                          const gchar         *paths,
                                          GError             **error);

/**
 * crispy_plugin_engine_get_plugin_count:
 * @self: a #CrispyPluginEngine
 *
 * Returns the number of currently loaded plugins.
 *
 * Returns: the plugin count
 */
guint crispy_plugin_engine_get_plugin_count (CrispyPluginEngine *self);

/**
 * crispy_plugin_engine_set_data:
 * @self: a #CrispyPluginEngine
 * @key: a string key for the data
 * @data: (nullable): the data to store
 * @destroy: (nullable): destroy notify for @data
 *
 * Stores arbitrary data in the engine's shared data store, keyed
 * by @key. This allows inter-plugin communication. If @key already
 * exists, the old value is freed via its destroy notify.
 */
void crispy_plugin_engine_set_data (CrispyPluginEngine *self,
                                    const gchar        *key,
                                    gpointer            data,
                                    GDestroyNotify      destroy);

/**
 * crispy_plugin_engine_get_data:
 * @self: a #CrispyPluginEngine
 * @key: a string key to look up
 *
 * Retrieves data previously stored with crispy_plugin_engine_set_data().
 *
 * Returns: (transfer none) (nullable): the stored data, or %NULL
 */
gpointer crispy_plugin_engine_get_data (CrispyPluginEngine *self,
                                        const gchar        *key);

G_END_DECLS

#endif /* CRISPY_PLUGIN_ENGINE_H */
