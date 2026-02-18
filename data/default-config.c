/*
 * Crispy - Default C Configuration
 *
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Install: crispy --generate-c-config > ~/.config/crispy/config.c
 * Auto-compile: crispy compiles this on first use (cached by SHA256)
 *
 * The CRISPY_PARAMS define below is optional.  If present, crispy
 * extracts it and passes the value as extra flags to gcc when
 * compiling this config file.  Supports shell expansion.
 */

/* #define CRISPY_PARAMS "-I/custom/path -lmylib" */

#include <crispy.h>

/*
 * crispy_config_init:
 * @ctx: the config context for setting defaults
 *
 * Entry point called before the main script runs.  Use the context
 * setter functions to configure crispy's behavior.
 *
 * This runs BEFORE plugins are loaded, so you can add plugin paths
 * here and they will be loaded automatically.
 *
 * You have full access to all GLib/GObject APIs and any library
 * you link against via CRISPY_PARAMS.
 *
 * Compiler flag precedence (gcc last-wins for conflicting flags):
 *   1. extra_flags    - defaults, lowest priority (prepended first)
 *   2. CRISPY_PARAMS  - per-script overrides (from the script itself)
 *   3. plugin flags   - injected by plugins at PRE_COMPILE hook
 *   4. override_flags - forced overrides, highest priority (appended last)
 *
 * Returns: TRUE on success, FALSE to skip config (use defaults).
 */
G_MODULE_EXPORT gboolean
crispy_config_init(CrispyConfigContext *ctx)
{
	/* --- Default compiler flags (prepended before CRISPY_PARAMS) --- */
	/* These act as defaults; scripts can override them. */
	/* crispy_config_context_set_extra_flags(ctx, "-lm"); */
	/* crispy_config_context_append_extra_flags(ctx, */
	/*     "$(pkg-config --cflags --libs json-glib-1.0)"); */

	/* --- Override compiler flags (appended after everything) --- */
	/* These CANNOT be overridden by scripts or plugins. */
	/* Use for flags that must always apply. */
	/* crispy_config_context_set_override_flags(ctx, "-Wall -Werror"); */

	/* --- Per-script conditional example --- */
	/* const gchar *script = crispy_config_context_get_script_path(ctx); */
	/* if (script != NULL && g_str_has_suffix(script, "unsafe.c")) */
	/* { */
	/*     crispy_config_context_set_override_flags(ctx, */
	/*         "-fsanitize=address -fsanitize=undefined"); */
	/* } */

	/* --- Auto-load plugins --- */
	/* crispy_config_context_add_plugin(ctx, */
	/*     "/usr/lib/crispy/plugins/timing.so"); */
	/* crispy_config_context_add_plugin(ctx, */
	/*     "/usr/lib/crispy/plugins/source-guard.so"); */

	/* --- Plugin configuration data --- */
	/* crispy_config_context_set_plugin_data(ctx, */
	/*     "source-guard.blocked", "system,exec,popen"); */

	/* --- Default CrispyFlags --- */
	/* CLI flags are OR'd on top of these (CLI always wins). */
	/* crispy_config_context_set_flags(ctx, CRISPY_FLAG_NONE); */
	/* crispy_config_context_add_flags(ctx, CRISPY_FLAG_PRESERVE_SOURCE); */

	/* --- Override cache directory --- */
	/* crispy_config_context_set_cache_dir(ctx, "/tmp/crispy-cache"); */

	/* --- Inspect or modify script argv before execution --- */
	/* gint argc = crispy_config_context_get_script_argc(ctx); */
	/* gchar **argv = crispy_config_context_get_script_argv(ctx); */
	/* (void)argc; (void)argv; */

	/* --- Inspect crispy's own command line --- */
	/* gint c_argc = crispy_config_context_get_crispy_argc(ctx); */
	/* const gchar * const *c_argv = crispy_config_context_get_crispy_argv(ctx); */
	/* (void)c_argc; (void)c_argv; */

	(void)ctx;
	return TRUE;
}
