/* crispy-error-analyzer-private.h - GCC error analysis and package suggestion */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private helpers for inspecting gcc compilation error output and
 * suggesting pkg-config package names that may resolve missing headers.
 * This header is NOT installed or included in the public umbrella header.
 */

#ifndef CRISPY_ERROR_ANALYZER_PRIVATE_H
#define CRISPY_ERROR_ANALYZER_PRIVATE_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * crispy_error_analyzer_suggest_packages:
 * @error_output: gcc stderr output from a failed compilation
 *
 * Analyzes gcc error output for missing header errors and attempts
 * to suggest pkg-config packages that provide the missing headers.
 *
 * Looks for patterns like:
 *   "fatal error: json-glib/json-glib.h: No such file or directory"
 *
 * Maps known headers to their pkg-config package names.
 *
 * Returns: (transfer full) (element-type utf8) (nullable): array of suggested
 *          package names, or %NULL if no suggestions
 */
GPtrArray *crispy_error_analyzer_suggest_packages (const gchar *error_output);

/**
 * crispy_error_analyzer_format_suggestions:
 * @suggestions: (element-type utf8): array of package names from suggest_packages
 *
 * Formats package suggestions into a human-readable hint string, e.g.:
 * "Hint: try adding to CRISPY_USE: \"json-glib-1.0 libsoup-3.0\""
 *
 * Returns: (transfer full): formatted hint string
 */
gchar *crispy_error_analyzer_format_suggestions (GPtrArray *suggestions);

G_END_DECLS

#endif /* CRISPY_ERROR_ANALYZER_PRIVATE_H */
