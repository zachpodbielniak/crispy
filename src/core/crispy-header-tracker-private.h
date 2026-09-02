/* crispy-header-tracker-private.h - Header dependency tracking utilities */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private helpers for parsing gcc -MD dependency files and checking
 * whether cached build artifacts are stale with respect to their
 * included headers.  This header is NOT installed or included in
 * the public umbrella header.
 */

#ifndef CRISPY_HEADER_TRACKER_PRIVATE_H
#define CRISPY_HEADER_TRACKER_PRIVATE_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * crispy_header_tracker_parse_depfile:
 * @depfile_path: path to a gcc .d dependency file
 * @out_deps: (out) (transfer full) (element-type utf8): list of dependency paths
 * @error: return location for a #GError, or %NULL
 *
 * Parses a gcc -MD dependency file and extracts all header paths.
 * The format is: target: dep1 dep2 \
 *                dep3 dep4
 *
 * Returns: %TRUE on success
 */
gboolean crispy_header_tracker_parse_depfile (const gchar   *depfile_path,
                                              GPtrArray    **out_deps,
                                              GError       **error);

/**
 * crispy_header_tracker_check_stale:
 * @deps: (element-type utf8): array of dependency paths from parse_depfile
 * @reference_time: the modification time to compare against (e.g., cached .so mtime)
 *
 * Checks whether any dependency file has been modified after @reference_time.
 *
 * Returns: %TRUE if any dependency is newer than @reference_time (cache is stale)
 */
gboolean crispy_header_tracker_check_stale (GPtrArray *deps,
                                            gint64     reference_time);

/**
 * crispy_header_tracker_get_depfile_path:
 * @artifact_path: path to a compiled artifact (a cached .so, or the
 *   staging name a compile is about to publish from)
 *
 * Returns the path of the dependency file that belongs to @artifact_path:
 * the artifact's own name with any extension replaced by `.d`.
 *
 * This is the one spelling of that name.  The compiler writes the
 * dependency file beside the artifact it just built and the cache reads
 * it back to decide whether an included header has changed since; two
 * private copies of the rule would be two answers to one question.
 *
 * Returns: (transfer full): the .d file path
 */
gchar *crispy_header_tracker_get_depfile_path (const gchar *artifact_path);

G_END_DECLS

#endif /* CRISPY_HEADER_TRACKER_PRIVATE_H */
