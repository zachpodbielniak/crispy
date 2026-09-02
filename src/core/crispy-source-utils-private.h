/* crispy-source-utils-private.h - Internal source parsing utilities */

/*
 * Shared helpers for extracting CRISPY_PARAMS, stripping shebangs,
 * and shell-expanding parameters.  Used by both CrispyScript and
 * the config loader.  This header is NOT installed or included in
 * the public umbrella header.
 */

#ifndef CRISPY_SOURCE_UTILS_PRIVATE_H
#define CRISPY_SOURCE_UTILS_PRIVATE_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * crispy_source_extract_params:
 * @source: full source text of a C file
 *
 * Scans @source for a line matching `#define CRISPY_PARAMS "..."` and
 * extracts the quoted value.  Only the first occurrence is matched.
 *
 * Returns: (transfer full) (nullable): the extracted params string
 *          (without surrounding quotes), or %NULL if not found
 */
gchar *crispy_source_extract_params (const gchar *source);

/**
 * crispy_source_strip_header:
 * @source: full source text of a C file
 * @out_len: (out) (optional): location to store the length of the
 *           returned string, or %NULL
 *
 * Returns a copy of @source with the shebang line (if present) and
 * the `#define CRISPY_PARAMS` line (if present) removed.  All other
 * lines are preserved verbatim.
 *
 * Returns: (transfer full): the modified source text
 */
gchar *crispy_source_strip_header (const gchar *source,
                                   gsize       *out_len);

/**
 * crispy_source_shell_expand:
 * @params: raw CRISPY_PARAMS value to expand
 * @error: return location for a #GError, or %NULL
 *
 * Shell-expands @params via `/bin/sh -c "printf '%%s ' <params>"`.
 * This allows use of `$(pkg-config ...)` and other substitutions.
 * A trailing space is added by printf to preserve word boundaries
 * from command substitutions, then stripped before returning.
 *
 * If @params is %NULL or empty, returns an empty string.
 *
 * Returns: (transfer full): the expanded string, or %NULL on error
 */
gchar *crispy_source_shell_expand (const gchar  *params,
                                   GError      **error);

/**
 * crispy_source_resolve_use_flags:
 * @source: full source text of a C file
 * @error: return location for a #GError, or %NULL
 *
 * Resolves the script's `#define CRISPY_USE "pkg ..."` line into
 * compiler and linker flags through a pkg-config resolver.
 *
 * Every path that turns a script into a compile needs this, and each one
 * that spelled it out itself was a path that could forget: `crispy lint`,
 * `crispy test` and `crispy install` resolved CRISPY_USE and the one that
 * actually runs a script did not, so a script that linted clean failed to
 * compile with "No such file or directory" on the header it had declared.
 *
 * Returns: (transfer full) (nullable): the resolved flags, %NULL if the
 *          source declares no packages (which is not an error) or if a
 *          package could not be resolved (which sets @error)
 */
gchar *crispy_source_resolve_use_flags (const gchar  *source,
                                        GError      **error);

/**
 * crispy_source_include_flag_for:
 * @source_path: (nullable): path of the script being compiled
 *
 * Returns the `-I` flag that puts @source_path's own directory on the
 * include path, shell-quoted and absolute.
 *
 * Compilation happens on a stripped copy in the temp directory, so the
 * script's directory is not the compiler's idea of "here" and
 * `#include "sibling.h"` cannot resolve -- while `crispy lint` compiles
 * the file in place and does resolve it, so the tool that reported the
 * script clean was the one that was wrong.
 *
 * Returns: (transfer full) (nullable): the flag, or %NULL when there is
 *          no file on disk (inline code, stdin)
 */
gchar *crispy_source_include_flag_for (const gchar *source_path);

G_END_DECLS

#endif /* CRISPY_SOURCE_UTILS_PRIVATE_H */
