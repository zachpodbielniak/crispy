/* crispy-plugin-engine.c - Plugin engine implementation */

#define CRISPY_COMPILATION
#include "crispy-plugin-engine.h"
#include "crispy-plugin-engine-private.h"
#include "../crispy-plugin.h"
#include "../crispy-types.h"

#include <glib.h>
#include <gmodule.h>
#include <string.h>

/**
 * SECTION:crispy-plugin-engine
 * @title: CrispyPluginEngine
 * @short_description: Loads and dispatches plugins
 *
 * #CrispyPluginEngine manages the lifecycle of Crispy plugins.
 * Plugins are shared libraries (.so) loaded via GModule that export
 * well-known C function symbols. The engine resolves these symbols
 * at load time and dispatches hook calls during script execution.
 *
 * The engine also provides a shared data store (string-keyed hash
 * table) for inter-plugin communication.
 */

/* hook symbol names, indexed by CrispyHookPoint */
static const gchar *hook_symbol_names[CRISPY_HOOK_POINT_COUNT] =
{
    "crispy_plugin_on_source_loaded",
    "crispy_plugin_on_params_expanded",
    "crispy_plugin_on_hash_computed",
    "crispy_plugin_on_cache_checked",
    "crispy_plugin_on_pre_compile",
    "crispy_plugin_on_post_compile",
    "crispy_plugin_on_module_loaded",
    "crispy_plugin_on_pre_execute",
    "crispy_plugin_on_post_execute"
};

/* internal per-plugin entry */
typedef struct
{
    GModule                   *module;
    const CrispyPluginInfo    *info;
    gpointer                   plugin_data;
    CrispyPluginShutdownFunc   shutdown_func;
    CrispyPluginHookFunc       hooks[CRISPY_HOOK_POINT_COUNT];
} CrispyPluginEntry;

/* data store entry for destroy notify */
typedef struct
{
    gpointer       data;
    GDestroyNotify destroy;
} DataStoreEntry;

static void
data_store_entry_free(
    gpointer ptr
){
    DataStoreEntry *entry;

    entry = (DataStoreEntry *)ptr;
    if (entry->destroy != NULL && entry->data != NULL)
        entry->destroy(entry->data);
    g_free(entry);
}

static void
plugin_entry_free(
    gpointer ptr
){
    CrispyPluginEntry *entry;

    entry = (CrispyPluginEntry *)ptr;

    /* call shutdown if the plugin provided one */
    if (entry->shutdown_func != NULL)
        entry->shutdown_func(entry->plugin_data);

    if (entry->module != NULL)
        g_module_close(entry->module);

    g_free(entry);
}

struct _CrispyPluginEngine
{
    GObject parent_instance;
};

typedef struct
{
    GPtrArray   *plugins;     /* of CrispyPluginEntry* */
    GHashTable  *data_store;  /* string -> DataStoreEntry* */
} CrispyPluginEnginePrivate;

G_DEFINE_FINAL_TYPE_WITH_PRIVATE(CrispyPluginEngine, crispy_plugin_engine, G_TYPE_OBJECT)

/* --- GObject lifecycle --- */

static void
crispy_plugin_engine_finalize(
    GObject *object
){
    CrispyPluginEnginePrivate *priv;

    priv = crispy_plugin_engine_get_instance_private(CRISPY_PLUGIN_ENGINE(object));

    /* plugins are freed (and shutdown called) via the element free func */
    g_ptr_array_unref(priv->plugins);
    g_hash_table_unref(priv->data_store);

    G_OBJECT_CLASS(crispy_plugin_engine_parent_class)->finalize(object);
}

static void
crispy_plugin_engine_class_init(
    CrispyPluginEngineClass *klass
){
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = crispy_plugin_engine_finalize;
}

static void
crispy_plugin_engine_init(
    CrispyPluginEngine *self
){
    CrispyPluginEnginePrivate *priv;

    priv = crispy_plugin_engine_get_instance_private(self);

    priv->plugins = g_ptr_array_new_with_free_func(plugin_entry_free);
    priv->data_store = g_hash_table_new_full(g_str_hash, g_str_equal,
                                              g_free, data_store_entry_free);
}

/* --- public API --- */

CrispyPluginEngine *
crispy_plugin_engine_new(
    void
){
    return (CrispyPluginEngine *)g_object_new(CRISPY_TYPE_PLUGIN_ENGINE, NULL);
}

gboolean
crispy_plugin_engine_load(
    CrispyPluginEngine  *self,
    const gchar         *path,
    GError             **error
){
    CrispyPluginEnginePrivate *priv;
    CrispyPluginEntry *entry;
    GModule *module;
    const CrispyPluginInfo *info;
    CrispyPluginInitFunc init_func;
    gint i;

    g_return_val_if_fail(CRISPY_IS_PLUGIN_ENGINE(self), FALSE);
    g_return_val_if_fail(path != NULL, FALSE);

    priv = crispy_plugin_engine_get_instance_private(self);

    /* open the shared library */
    module = g_module_open(path, G_MODULE_BIND_LAZY);
    if (module == NULL)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_PLUGIN,
                    "Failed to load plugin '%s': %s",
                    path, g_module_error());
        return FALSE;
    }

    /* resolve the mandatory info symbol */
    info = NULL;
    if (!g_module_symbol(module, "crispy_plugin_info", (gpointer *)&info) ||
        info == NULL)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_PLUGIN,
                    "Plugin '%s' does not export 'crispy_plugin_info'",
                    path);
        g_module_close(module);
        return FALSE;
    }

    /* build the entry */
    entry = g_new0(CrispyPluginEntry, 1);
    entry->module = module;
    entry->info = info;

    /* resolve optional lifecycle functions */
    init_func = NULL;
    g_module_symbol(module, "crispy_plugin_init", (gpointer *)&init_func);
    g_module_symbol(module, "crispy_plugin_shutdown",
                    (gpointer *)&entry->shutdown_func);

    /* resolve optional hook functions */
    for (i = 0; i < CRISPY_HOOK_POINT_COUNT; i++)
    {
        entry->hooks[i] = NULL;
        g_module_symbol(module, hook_symbol_names[i],
                        (gpointer *)&entry->hooks[i]);
    }

    /* call init if provided */
    if (init_func != NULL)
        entry->plugin_data = init_func();

    g_ptr_array_add(priv->plugins, entry);
    return TRUE;
}

gboolean
crispy_plugin_engine_load_paths(
    CrispyPluginEngine  *self,
    const gchar         *paths,
    GError             **error
){
    gchar **tokens;
    gint i;

    g_return_val_if_fail(CRISPY_IS_PLUGIN_ENGINE(self), FALSE);
    g_return_val_if_fail(paths != NULL, FALSE);

    /* split on both ':' and ',' */
    tokens = g_strsplit_set(paths, ":,", -1);

    for (i = 0; tokens[i] != NULL; i++)
    {
        g_strstrip(tokens[i]);
        if (tokens[i][0] == '\0')
            continue;

        if (!crispy_plugin_engine_load(self, tokens[i], error))
        {
            g_strfreev(tokens);
            return FALSE;
        }
    }

    g_strfreev(tokens);
    return TRUE;
}

guint
crispy_plugin_engine_get_plugin_count(
    CrispyPluginEngine *self
){
    CrispyPluginEnginePrivate *priv;

    g_return_val_if_fail(CRISPY_IS_PLUGIN_ENGINE(self), 0);

    priv = crispy_plugin_engine_get_instance_private(self);
    return priv->plugins->len;
}

void
crispy_plugin_engine_set_data(
    CrispyPluginEngine *self,
    const gchar        *key,
    gpointer            data,
    GDestroyNotify      destroy
){
    CrispyPluginEnginePrivate *priv;
    DataStoreEntry *entry;

    g_return_if_fail(CRISPY_IS_PLUGIN_ENGINE(self));
    g_return_if_fail(key != NULL);

    priv = crispy_plugin_engine_get_instance_private(self);

    entry = g_new0(DataStoreEntry, 1);
    entry->data = data;
    entry->destroy = destroy;

    /* replaces any existing entry (old one freed via destroy notify) */
    g_hash_table_replace(priv->data_store, g_strdup(key), entry);
}

gpointer
crispy_plugin_engine_get_data(
    CrispyPluginEngine *self,
    const gchar        *key
){
    CrispyPluginEnginePrivate *priv;
    DataStoreEntry *entry;

    g_return_val_if_fail(CRISPY_IS_PLUGIN_ENGINE(self), NULL);
    g_return_val_if_fail(key != NULL, NULL);

    priv = crispy_plugin_engine_get_instance_private(self);

    entry = (DataStoreEntry *)g_hash_table_lookup(priv->data_store, key);
    if (entry != NULL)
        return entry->data;

    return NULL;
}

/* --- internal dispatch --- */

CrispyHookResult
crispy_plugin_engine_dispatch(
    CrispyPluginEngine *self,
    CrispyHookPoint     hook_point,
    CrispyHookContext  *ctx
){
    CrispyPluginEnginePrivate *priv;
    CrispyPluginEntry *entry;
    CrispyHookResult result;
    guint i;

    g_return_val_if_fail(CRISPY_IS_PLUGIN_ENGINE(self), CRISPY_HOOK_CONTINUE);
    g_return_val_if_fail(hook_point < CRISPY_HOOK_POINT_COUNT, CRISPY_HOOK_CONTINUE);
    g_return_val_if_fail(ctx != NULL, CRISPY_HOOK_CONTINUE);

    priv = crispy_plugin_engine_get_instance_private(self);

    ctx->hook_point = hook_point;
    ctx->engine = (gpointer)self;

    for (i = 0; i < priv->plugins->len; i++)
    {
        entry = (CrispyPluginEntry *)g_ptr_array_index(priv->plugins, i);

        if (entry->hooks[hook_point] == NULL)
            continue;

        /* swap in this plugin's private data */
        ctx->plugin_data = entry->plugin_data;

        result = entry->hooks[hook_point](ctx);

        /* store back any updated plugin_data */
        entry->plugin_data = ctx->plugin_data;

        if (result != CRISPY_HOOK_CONTINUE)
            return result;
    }

    return CRISPY_HOOK_CONTINUE;
}
