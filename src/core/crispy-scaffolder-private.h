/* crispy-scaffolder-private.h - Script scaffolding utilities */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private helpers for generating new crispy script files from built-in
 * templates.  Handles the `crispy new` subcommand — writing a .c file
 * with a shebang, appropriate boilerplate, and executable permissions.
 * This header is NOT installed or included in the public umbrella header.
 */

#ifndef CRISPY_SCAFFOLDER_PRIVATE_H
#define CRISPY_SCAFFOLDER_PRIVATE_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * crispy_scaffolder_create:
 * @name: script name (without .c extension)
 * @directory: (nullable): output directory, or %NULL for current directory
 * @template: (nullable): template name ("minimal", "glib", "gtk", "cli"), or %NULL for "glib"
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new crispy script from a template. The file is written to
 * @directory/@name.c with executable permissions and a proper shebang.
 *
 * Returns: (transfer full): path to the created file, or %NULL on error
 */
gchar *crispy_scaffolder_create (const gchar  *name,
                                 const gchar  *directory,
                                 const gchar  *template,
                                 GError      **error);

/**
 * crispy_scaffolder_list_templates:
 *
 * Returns a NULL-terminated array of available template names.
 *
 * Returns: (transfer none) (array zero-terminated=1): template names
 */
const gchar * const *crispy_scaffolder_list_templates (void);

G_END_DECLS

#endif /* CRISPY_SCAFFOLDER_PRIVATE_H */
