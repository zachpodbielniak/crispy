/* crispy-use-parser-private.h - CRISPY_USE directive parsing utilities */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private helpers for parsing CRISPY_USE directives in crispy scripts.
 * Scripts can declare package dependencies via:
 *
 *   #define CRISPY_USE "json-glib-1.0 libsoup-3.0 libxml-2.0"
 *
 * This module extracts the package list and resolves each entry into
 * compiler and linker flags via a CrispyDependencyResolver.  This
 * header is NOT installed or included in the public umbrella header.
 */

#ifndef CRISPY_USE_PARSER_PRIVATE_H
#define CRISPY_USE_PARSER_PRIVATE_H

#include <glib.h>

/* Forward declaration to avoid circular includes */
typedef struct _CrispyDependencyResolver CrispyDependencyResolver;
typedef struct _CrispyDependencyInfo     CrispyDependencyInfo;

G_BEGIN_DECLS

/**
 * crispy_use_parser_extract:
 * @source: full source text of a C file
 *
 * Scans @source for a line matching `#define CRISPY_USE "..."` and
 * extracts the quoted value containing space-separated package names.
 *
 * Returns: (transfer full) (nullable): the raw CRISPY_USE value string
 *          (without surrounding quotes), or %NULL if not found
 */
gchar *crispy_use_parser_extract (const gchar *source);

/**
 * crispy_use_parser_split:
 * @use_value: the CRISPY_USE value string (space-separated package names)
 * @out_count: (out): number of packages found
 *
 * Splits a CRISPY_USE value string into individual package names.
 * Handles multiple spaces, leading/trailing whitespace.
 *
 * Returns: (transfer full) (array length=out_count): array of package names,
 *          free with g_strfreev()
 */
gchar **crispy_use_parser_split (const gchar *use_value,
                                 gint        *out_count);

/**
 * crispy_use_parser_resolve:
 * @source: full source text of a C file
 * @resolver: a #CrispyDependencyResolver for resolution
 * @error: return location for a #GError, or %NULL
 *
 * Extracts CRISPY_USE from @source, splits into packages, resolves
 * each with @resolver, and returns combined compiler/linker flags
 * as a single string suitable for appending to CRISPY_PARAMS.
 *
 * Returns: (transfer full) (nullable): combined flags string, or %NULL
 *          if no CRISPY_USE found (not an error) or resolution failed
 */
gchar *crispy_use_parser_resolve (const gchar              *source,
                                  CrispyDependencyResolver *resolver,
                                  GError                  **error);

/**
 * crispy_use_parser_resolve_to_list:
 * @source: full source text of a C file
 * @resolver: a #CrispyDependencyResolver for resolution
 * @error: return location for a #GError, or %NULL
 *
 * Like crispy_use_parser_resolve(), but returns individual
 * CrispyDependencyInfo entries in a GPtrArray instead of a
 * flat string.
 *
 * Returns: (transfer full) (element-type CrispyDependencyInfo) (nullable):
 *          array of resolved dependency info, or %NULL
 */
GPtrArray *crispy_use_parser_resolve_to_list (const gchar              *source,
                                              CrispyDependencyResolver *resolver,
                                              GError                  **error);

/**
 * crispy_source_strip_use:
 * @source: full source text
 * @out_len: (out) (optional): length of returned string
 *
 * Returns a copy of @source with the CRISPY_USE define line removed.
 *
 * Returns: (transfer full): modified source text
 */
gchar *crispy_source_strip_use (const gchar *source,
                                gsize       *out_len);

G_END_DECLS

#endif /* CRISPY_USE_PARSER_PRIVATE_H */
