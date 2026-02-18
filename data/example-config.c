/*
 * Crispy - Example Configuration
 *
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This config demonstrates the configuration system by:
 *
 *   1. Providing -lm as a default flag (extra_flags) so scripts
 *      can use math functions without declaring CRISPY_PARAMS.
 *      Try: crispy -c data/example-config.c examples/physics.c
 *
 *   2. Auto-loading the timing plugin so every script execution
 *      gets a per-phase timing report on stderr.
 *
 *   3. Using per-script conditional logic to force sanitizers
 *      on scripts whose names contain "unsafe".
 *
 *   4. Setting -Wall as an override flag that cannot be disabled
 *      by individual scripts' CRISPY_PARAMS.
 *
 * Usage:
 *   crispy -c data/example-config.c examples/physics.c
 *   crispy -c data/example-config.c examples/physics.c 100 60
 *   crispy -c data/example-config.c examples/hello.c
 *   crispy -c data/example-config.c examples/math.c
 *
 * Install as your default config:
 *   cp data/example-config.c ~/.config/crispy/config.c
 *   (then edit the plugin path to match your system)
 */

#include <crispy.h>
#include <string.h>

G_MODULE_EXPORT gboolean
crispy_config_init(CrispyConfigContext *ctx)
{
	const gchar *script;

	/*
	 * --- Default compiler flags ---
	 *
	 * Prepended before CRISPY_PARAMS (lowest priority).
	 * Scripts can override these with their own CRISPY_PARAMS.
	 *
	 * This is what lets examples/physics.c work -- it uses
	 * sin/cos/sqrt but does not declare CRISPY_PARAMS "-lm"
	 * itself.  The config provides it as a default.
	 */
	crispy_config_context_set_extra_flags(ctx, "-lm");

	/*
	 * --- Override compiler flags ---
	 *
	 * Appended after everything (highest priority).
	 * These CANNOT be overridden by scripts or plugins.
	 *
	 * Force -Wall on all scripts. Even if a script's
	 * CRISPY_PARAMS tries to set -w (suppress warnings),
	 * our -Wall comes last and wins.
	 */
	crispy_config_context_set_override_flags(ctx, "-Wall");

	/*
	 * --- Per-script conditional overrides ---
	 *
	 * get_script_path() returns NULL for inline (-i) and
	 * stdin (-) modes. For file mode, it's the script's
	 * filesystem path.
	 */
	script = crispy_config_context_get_script_path(ctx);

	if (script != NULL && strstr(script, "unsafe") != NULL)
	{
		/*
		 * Force address and undefined behavior sanitizers
		 * for any script with "unsafe" in its path.
		 * The override_flags replace what we set above.
		 */
		crispy_config_context_set_override_flags(ctx,
			"-Wall -fsanitize=address -fsanitize=undefined");
	}

	/*
	 * --- Auto-load the timing plugin ---
	 *
	 * This plugin prints a per-phase timing report to stderr
	 * after every script execution.  Config-loaded plugins run
	 * before any CLI-specified plugins (-P).
	 *
	 * The path below assumes you're running from the project
	 * root and have built the example plugins with:
	 *   make -C examples/plugins
	 */
	crispy_config_context_add_plugin(ctx,
		"examples/plugins/plugin-timing.so");

	return TRUE;
}
