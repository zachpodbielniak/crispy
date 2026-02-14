/* plugin-source-guard.c - Plugin that rejects dangerous function calls */

#define CRISPY_COMPILATION
#include "crispy-plugin.h"
#include "crispy-types.h"

#include <glib.h>
#include <string.h>

CRISPY_PLUGIN_DEFINE(
    "source-guard",
    "Rejects scripts containing dangerous function calls",
    "0.1.0",
    "Crispy Project",
    "AGPLv3"
);

/* list of forbidden function patterns */
static const gchar *forbidden[] =
{
    "system(",
    "popen(",
    "exec(",
    "execvp(",
    "execve(",
    "execl(",
    "execlp(",
    "fork(",
    NULL
};

/**
 * crispy_plugin_on_source_loaded:
 * @ctx: the hook context with source content
 *
 * Scans the script source for calls to dangerous functions like
 * system(), popen(), exec(), and fork(). If any are found, the
 * pipeline is aborted with an error message.
 *
 * Returns: %CRISPY_HOOK_CONTINUE if safe, %CRISPY_HOOK_ABORT if dangerous
 */
CrispyHookResult
crispy_plugin_on_source_loaded(CrispyHookContext *ctx)
{
    const gchar *source;
    gint i;

    source = ctx->source_content;
    if (source == NULL)
        return CRISPY_HOOK_CONTINUE;

    for (i = 0; forbidden[i] != NULL; i++)
    {
        if (strstr(source, forbidden[i]) != NULL)
        {
            g_set_error(ctx->error,
                        CRISPY_ERROR,
                        CRISPY_ERROR_PLUGIN,
                        "source-guard: script contains forbidden call '%s' "
                        "(source: %s)",
                        forbidden[i],
                        ctx->source_path != NULL ? ctx->source_path : "<inline>");
            return CRISPY_HOOK_ABORT;
        }
    }

    return CRISPY_HOOK_CONTINUE;
}
