/* crispy-plugin-engine-private.h - Internal dispatch API (not installed) */

#ifndef CRISPY_PLUGIN_ENGINE_PRIVATE_H
#define CRISPY_PLUGIN_ENGINE_PRIVATE_H

#include "crispy-plugin-engine.h"
#include "../crispy-plugin.h"

G_BEGIN_DECLS

/**
 * crispy_plugin_engine_dispatch:
 * @self: a #CrispyPluginEngine
 * @hook_point: which hook point to fire
 * @ctx: the hook context (modified in-place by plugins)
 *
 * Dispatches a hook to all loaded plugins that export a handler for
 * the given @hook_point. Each plugin's handler is called in load order.
 * If any plugin returns %CRISPY_HOOK_ABORT, dispatch stops immediately.
 *
 * The @ctx->plugin_data field is swapped to each plugin's private
 * data before calling its hook, and the @ctx->engine field is set
 * to @self for shared data store access.
 *
 * Returns: %CRISPY_HOOK_CONTINUE if all plugins continued,
 *   or the first non-continue result
 */
CrispyHookResult crispy_plugin_engine_dispatch (CrispyPluginEngine *self,
                                                CrispyHookPoint     hook_point,
                                                CrispyHookContext  *ctx);

G_END_DECLS

#endif /* CRISPY_PLUGIN_ENGINE_PRIVATE_H */
