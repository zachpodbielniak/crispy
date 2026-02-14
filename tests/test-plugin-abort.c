/* test-plugin-abort.c - Test plugin that aborts at PRE_EXECUTE */

#define CRISPY_COMPILATION
#include "crispy-plugin.h"
#include "crispy-types.h"

#include <glib.h>

CRISPY_PLUGIN_DEFINE("test-abort", "Aborts pipeline", "0.1.0", "Test", "AGPLv3");

CrispyHookResult
crispy_plugin_on_pre_execute(CrispyHookContext *ctx)
{
    g_set_error(ctx->error,
                CRISPY_ERROR,
                CRISPY_ERROR_PLUGIN,
                "Aborted by test-abort plugin");
    return CRISPY_HOOK_ABORT;
}
