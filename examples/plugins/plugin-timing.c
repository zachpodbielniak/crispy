/* plugin-timing.c - Plugin that reports per-phase timing to stderr */

#define CRISPY_COMPILATION
#include "crispy-plugin.h"

#include <glib.h>

CRISPY_PLUGIN_DEFINE(
    "timing",
    "Reports per-phase execution timing to stderr",
    "0.1.0",
    "Crispy Project",
    "AGPLv3"
);

/**
 * crispy_plugin_on_post_execute:
 * @ctx: the hook context with timing data
 *
 * Prints a timing report for each phase of the execution pipeline
 * to stderr. Only fires after the script has finished executing.
 *
 * Returns: %CRISPY_HOOK_CONTINUE always
 */
CrispyHookResult
crispy_plugin_on_post_execute(CrispyHookContext *ctx)
{
    g_printerr("\n--- Crispy Timing Report ---\n");
    g_printerr("  Source:     %s\n",
               ctx->source_path != NULL ? ctx->source_path : "(inline/stdin)");
    g_printerr("  Cache hit:  %s\n", ctx->cache_hit ? "yes" : "no");
    g_printerr("  Params:     %.3f ms\n", ctx->time_param_expand / 1000.0);
    g_printerr("  Hash:       %.3f ms\n", ctx->time_hash / 1000.0);
    g_printerr("  Cache chk:  %.3f ms\n", ctx->time_cache_check / 1000.0);
    g_printerr("  Compile:    %.3f ms\n", ctx->time_compile / 1000.0);
    g_printerr("  Module ld:  %.3f ms\n", ctx->time_module_load / 1000.0);
    g_printerr("  Execute:    %.3f ms\n", ctx->time_execute / 1000.0);
    g_printerr("  Total:      %.3f ms\n", ctx->time_total / 1000.0);
    g_printerr("  Exit code:  %d\n", ctx->exit_code);
    g_printerr("----------------------------\n");

    return CRISPY_HOOK_CONTINUE;
}
