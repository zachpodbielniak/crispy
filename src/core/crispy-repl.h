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
 * Creates a new #CrispyRepl.  Each evaluated line is wrapped in an
 * entry function, compiled as a shared library, and executed in-process.
 * Preprocessor directives, function definitions, and type declarations
 * accumulate in a preamble that is prepended to every subsequent eval.
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
 * Starts the interactive REPL loop with readline support.  Reads lines
 * from stdin, evaluates each one, and prints results until the user
 * types ".quit", "exit", or sends EOF (Ctrl-D).
 *
 * Returns: %TRUE on clean exit, %FALSE on error
 */
gboolean crispy_repl_start (CrispyRepl  *self,
                             GError     **error);

/**
 * crispy_repl_eval:
 * @self: a #CrispyRepl
 * @code: C code to evaluate (may be multiple lines)
 * @error: return location for a #GError, or %NULL
 *
 * Evaluates C code.  Preprocessor directives are accumulated into the
 * preamble.  Statements are wrapped in an entry function, compiled,
 * loaded, and executed.  Compilation errors are reported via @error
 * with the gcc diagnostic text.
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

/**
 * crispy_repl_reset:
 * @self: a #CrispyRepl
 *
 * Clears the accumulated preamble and resets the REPL state.
 */
void crispy_repl_reset (CrispyRepl *self);

/**
 * crispy_repl_get_preamble:
 * @self: a #CrispyRepl
 *
 * Returns the accumulated preamble (includes, defines, functions, etc.).
 *
 * Returns: (transfer none): the preamble string
 */
const gchar *crispy_repl_get_preamble (CrispyRepl *self);

G_END_DECLS

#endif /* CRISPY_REPL_H */
