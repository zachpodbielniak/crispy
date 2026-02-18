/* crispy-config-loader.h - Internal config loading machinery */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private header for compiling and loading C config files.
 * This is NOT installed or included in the public umbrella header.
 * Used only by main.c.
 */

#ifndef CRISPY_CONFIG_LOADER_H
#define CRISPY_CONFIG_LOADER_H

#include <glib.h>
#include "../crispy-types.h"
#include "crispy-config-context.h"

/* forward declarations -- avoid pulling in full interface headers */
typedef struct _CrispyCompiler      CrispyCompiler;
typedef struct _CrispyCacheProvider CrispyCacheProvider;

G_BEGIN_DECLS

/**
 * crispy_config_loader_find:
 * @explicit_path: (nullable): path from -c/--config, or %NULL
 *
 * Searches the config search path for a config.c file.
 *
 * Search order (first found wins):
 *   1. $CRISPY_CONFIG_FILE environment variable
 *   2. @explicit_path (from -c/--config CLI flag)
 *   3. ~/.config/crispy/config.c
 *   4. /etc/crispy/config.c
 *   5. /usr/share/crispy/config.c
 *
 * Returns: (transfer full) (nullable): path to config.c, or %NULL
 */
gchar *crispy_config_loader_find (const gchar *explicit_path);

/**
 * crispy_config_loader_compile_and_load:
 * @config_path: path to the config .c file
 * @compiler: a #CrispyCompiler for compilation
 * @cache: a #CrispyCacheProvider for caching
 * @ctx: the config context to pass to the init function
 * @error: return location for a #GError, or %NULL
 *
 * Compiles the config .c file (with caching), loads the .so,
 * resolves `crispy_config_init`, and calls it with @ctx.
 *
 * The compiled .so module is kept open after this function
 * returns so that any symbols from the config remain available.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean crispy_config_loader_compile_and_load (
    const gchar         *config_path,
    CrispyCompiler      *compiler,
    CrispyCacheProvider *cache,
    CrispyConfigContext *ctx,
    GError             **error);

G_END_DECLS

#endif /* CRISPY_CONFIG_LOADER_H */
