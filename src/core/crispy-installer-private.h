/* crispy-installer-private.h - Script installation utilities */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private helpers for compiling a crispy script to a standalone
 * executable and installing it to a bin directory.  Implements the
 * `crispy install` subcommand.  This header is NOT installed or
 * included in the public umbrella header.
 */

#ifndef CRISPY_INSTALLER_PRIVATE_H
#define CRISPY_INSTALLER_PRIVATE_H

#include <glib.h>

/* Forward declaration to avoid pulling in the full interface header. */
typedef struct _CrispyCompiler CrispyCompiler;

G_BEGIN_DECLS

/**
 * crispy_installer_install:
 * @source_path: path to the C source file
 * @install_dir: (nullable): installation directory, or %NULL for ~/.local/bin
 * @compiler: a #CrispyCompiler
 * @extra_flags: (nullable): additional compiler flags
 * @error: return location for a #GError, or %NULL
 *
 * Compiles a crispy script to a standalone executable (not a .so) and
 * installs it to @install_dir. The executable name is derived from the
 * source file name (minus the .c extension).
 *
 * Returns: (transfer full): path to the installed executable, or %NULL on error
 */
gchar *crispy_installer_install (const gchar     *source_path,
                                 const gchar     *install_dir,
                                 CrispyCompiler  *compiler,
                                 const gchar     *extra_flags,
                                 GError         **error);

/**
 * crispy_installer_get_default_dir:
 *
 * Returns the default installation directory (~/.local/bin).
 * Creates the directory if it doesn't exist.
 *
 * Returns: (transfer full): the default install directory path
 */
gchar *crispy_installer_get_default_dir (void);

G_END_DECLS

#endif /* CRISPY_INSTALLER_PRIVATE_H */
