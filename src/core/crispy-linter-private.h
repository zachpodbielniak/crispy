/* crispy-linter-private.h - Compiler-based linting utilities */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private helpers for running gcc with strict warning flags on a crispy
 * script without producing any output binary.  Implements the `crispy lint`
 * subcommand.  This header is NOT installed or included in the public
 * umbrella header.
 */

#ifndef CRISPY_LINTER_PRIVATE_H
#define CRISPY_LINTER_PRIVATE_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * crispy_linter_check:
 * @source_path: path to the C source file
 * @extra_flags: (nullable): additional compiler flags (from CRISPY_PARAMS/CRISPY_USE)
 * @output: (out) (transfer full) (nullable): captured warning/error output
 * @error: return location for a #GError, or %NULL
 *
 * Runs gcc with strict warning flags on the source file without producing
 * an output binary. Captures all warnings and errors.
 *
 * Returns: %TRUE if no warnings/errors found (clean lint)
 */
gboolean crispy_linter_check (const gchar  *source_path,
                              const gchar  *extra_flags,
                              gchar       **output,
                              GError      **error);

/**
 * crispy_linter_get_flags:
 *
 * Returns the extra warning flags used for linting, beyond the base -Wall -Wextra.
 *
 * Returns: (transfer none): the lint warning flags string
 */
const gchar *crispy_linter_get_flags (void);

G_END_DECLS

#endif /* CRISPY_LINTER_PRIVATE_H */
