/* crispy-repl.h - Interactive REPL for evaluating C expressions */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef CRISPY_REPL_H
#define CRISPY_REPL_H

#if !defined(CRISPY_INSIDE) && !defined(CRISPY_COMPILATION)
#error "Only <crispy.h> can be included directly."
#endif

#include <glib-object.h>
#include "../crispy-types.h"

G_BEGIN_DECLS

#define CRISPY_TYPE_REPL (crispy_repl_get_type())

G_DECLARE_FINAL_TYPE(CrispyRepl, crispy_repl, CRISPY, REPL, GObject)

/**
 * crispy_repl_new:
 * @compiler: a #CrispyCompiler implementation
 * @cache: a #CrispyCacheProvider implementation
 *
 * Creates a new #CrispyRepl. The REPL wraps each line entered by the
 * user in a main() function, compiles it as a shared library, loads it,
 * and executes it.  Accumulated #include and #define lines are prepended
 * to every subsequent evaluation.
 *
 * Returns: (transfer full): a new #CrispyRepl
 */
CrispyRepl *crispy_repl_new (CrispyCompiler      *compiler,
                              CrispyCacheProvider *cache);

/**
 * crispy_repl_start:
 * @self: a #CrispyRepl
 * @error: return location for a #GError, or %NULL
 *
 * Starts the interactive REPL loop. Reads lines from stdin, evaluates
 * each one, and prints results until the user types "exit", "quit",
 * or sends EOF (Ctrl-D). This function blocks until the loop exits.
 *
 * Returns: %TRUE on clean exit, %FALSE on error
 */
gboolean crispy_repl_start (CrispyRepl  *self,
                             GError     **error);

/**
 * crispy_repl_eval:
 * @self: a #CrispyRepl
 * @code: a line of C code to evaluate
 * @error: return location for a #GError, or %NULL
 *
 * Evaluates a single line of C code. If the line begins with #include
 * or #define it is accumulated into the preamble for future evaluations
 * rather than executed immediately. Otherwise the code is wrapped in a
 * main() function, compiled as a shared library, and executed.
 *
 * Emits #CrispyRepl::line-evaluated on success or
 * #CrispyRepl::error-occurred on failure.
 *
 * Returns: the exit code of the evaluated code (0 = success), or -1 on error
 */
gint crispy_repl_eval (CrispyRepl   *self,
                       const gchar  *code,
                       GError      **error);

/**
 * crispy_repl_set_prompt:
 * @self: a #CrispyRepl
 * @prompt: the prompt string to display before each input line
 *
 * Sets the prompt string.  The default is "crispy> ".
 */
void crispy_repl_set_prompt (CrispyRepl  *self,
                              const gchar *prompt);

/**
 * crispy_repl_get_prompt:
 * @self: a #CrispyRepl
 *
 * Returns the current prompt string.
 *
 * Returns: (transfer none): the prompt string
 */
const gchar *crispy_repl_get_prompt (CrispyRepl *self);

/**
 * crispy_repl_set_extra_flags:
 * @self: a #CrispyRepl
 * @flags: (nullable): additional compiler flags, or %NULL to clear
 *
 * Sets extra compiler flags that are passed to the compiler on each
 * evaluation.
 */
void crispy_repl_set_extra_flags (CrispyRepl  *self,
                                   const gchar *flags);

G_END_DECLS

#endif /* CRISPY_REPL_H */
