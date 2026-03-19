/* crispy-shebang-parser-private.h - Shebang line parsing utilities */

/*
 * Private helpers for parsing shebang lines in crispy scripts.
 * Extracts the interpreter path and splits the argument string that
 * the kernel delivers as a single token into individual arguments.
 * This header is NOT installed or included in the public umbrella header.
 */

#ifndef CRISPY_SHEBANG_PARSER_PRIVATE_H
#define CRISPY_SHEBANG_PARSER_PRIVATE_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * crispy_shebang_extract_line:
 * @source: full source text of a C file
 *
 * Extracts the shebang line from the source if present.
 * Returns NULL if no shebang line exists.
 *
 * Returns: (transfer full) (nullable): the shebang line without trailing newline
 */
gchar *crispy_shebang_extract_line (const gchar *source);

/**
 * crispy_shebang_get_interpreter:
 * @shebang_line: the full shebang line
 *
 * Extracts just the interpreter path from a shebang line.
 * E.g., "#!/usr/bin/crispy --foo" → "/usr/bin/crispy"
 * Handles "#!/usr/bin/env crispy" → "/usr/bin/env" (env mode)
 *
 * Returns: (transfer full) (nullable): the interpreter path
 */
gchar *crispy_shebang_get_interpreter (const gchar *shebang_line);

/**
 * crispy_shebang_parse_args:
 * @shebang_line: the full shebang line from the script (e.g., "#!/usr/bin/crispy --no-cache --gdb")
 * @out_argc: (out): number of parsed arguments (not including interpreter path)
 * @out_argv: (out) (array length=out_argc) (transfer full): parsed argument strings
 *
 * Parses a shebang line and extracts the arguments after the interpreter
 * path. The interpreter path itself is NOT included in the output.
 *
 * Handles:
 * - Simple space-separated arguments
 * - Single-quoted strings (no escape processing)
 * - Double-quoted strings (with backslash escapes)
 * - Leading/trailing whitespace
 * - Multiple spaces between arguments
 * - Empty shebang (no arguments)
 *
 * Returns: %TRUE if parsing succeeded, %FALSE on error (e.g., unterminated quote)
 */
gboolean crispy_shebang_parse_args (const gchar   *shebang_line,
                                    gint          *out_argc,
                                    gchar       ***out_argv);

G_END_DECLS

#endif /* CRISPY_SHEBANG_PARSER_PRIVATE_H */
