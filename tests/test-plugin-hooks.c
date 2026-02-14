/* test-plugin-hooks.c - Test plugin that tracks hook calls */

#define CRISPY_COMPILATION
#include "crispy-plugin.h"

#include <glib.h>
#include <string.h>

CRISPY_PLUGIN_DEFINE("test-hooks", "Tracks hook invocations", "0.1.0", "Test", "AGPLv3");

/* per-plugin state: a bitmask of which hooks were called */
typedef struct
{
    guint hooks_called;
    gint  last_exit_code;
} TestHooksData;

gpointer
crispy_plugin_init(void)
{
    TestHooksData *data;

    data = g_new0(TestHooksData, 1);
    return (gpointer)data;
}

void
crispy_plugin_shutdown(gpointer plugin_data)
{
    g_free(plugin_data);
}

CrispyHookResult
crispy_plugin_on_source_loaded(CrispyHookContext *ctx)
{
    TestHooksData *data;

    data = (TestHooksData *)ctx->plugin_data;
    data->hooks_called |= (1 << CRISPY_HOOK_SOURCE_LOADED);
    return CRISPY_HOOK_CONTINUE;
}

CrispyHookResult
crispy_plugin_on_params_expanded(CrispyHookContext *ctx)
{
    TestHooksData *data;

    data = (TestHooksData *)ctx->plugin_data;
    data->hooks_called |= (1 << CRISPY_HOOK_PARAMS_EXPANDED);
    return CRISPY_HOOK_CONTINUE;
}

CrispyHookResult
crispy_plugin_on_hash_computed(CrispyHookContext *ctx)
{
    TestHooksData *data;

    data = (TestHooksData *)ctx->plugin_data;
    data->hooks_called |= (1 << CRISPY_HOOK_HASH_COMPUTED);
    return CRISPY_HOOK_CONTINUE;
}

CrispyHookResult
crispy_plugin_on_cache_checked(CrispyHookContext *ctx)
{
    TestHooksData *data;

    data = (TestHooksData *)ctx->plugin_data;
    data->hooks_called |= (1 << CRISPY_HOOK_CACHE_CHECKED);
    return CRISPY_HOOK_CONTINUE;
}

CrispyHookResult
crispy_plugin_on_pre_compile(CrispyHookContext *ctx)
{
    TestHooksData *data;

    data = (TestHooksData *)ctx->plugin_data;
    data->hooks_called |= (1 << CRISPY_HOOK_PRE_COMPILE);
    return CRISPY_HOOK_CONTINUE;
}

CrispyHookResult
crispy_plugin_on_post_compile(CrispyHookContext *ctx)
{
    TestHooksData *data;

    data = (TestHooksData *)ctx->plugin_data;
    data->hooks_called |= (1 << CRISPY_HOOK_POST_COMPILE);
    return CRISPY_HOOK_CONTINUE;
}

CrispyHookResult
crispy_plugin_on_module_loaded(CrispyHookContext *ctx)
{
    TestHooksData *data;

    data = (TestHooksData *)ctx->plugin_data;
    data->hooks_called |= (1 << CRISPY_HOOK_MODULE_LOADED);
    return CRISPY_HOOK_CONTINUE;
}

CrispyHookResult
crispy_plugin_on_pre_execute(CrispyHookContext *ctx)
{
    TestHooksData *data;

    data = (TestHooksData *)ctx->plugin_data;
    data->hooks_called |= (1 << CRISPY_HOOK_PRE_EXECUTE);
    return CRISPY_HOOK_CONTINUE;
}

CrispyHookResult
crispy_plugin_on_post_execute(CrispyHookContext *ctx)
{
    TestHooksData *data;

    data = (TestHooksData *)ctx->plugin_data;
    data->hooks_called |= (1 << CRISPY_HOOK_POST_EXECUTE);
    data->last_exit_code = ctx->exit_code;
    return CRISPY_HOOK_CONTINUE;
}
