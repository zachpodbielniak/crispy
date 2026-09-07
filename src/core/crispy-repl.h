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
 * types ":quit", "exit", or sends EOF (Ctrl-D).
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
 * One simple scalar or pointer declaration per call creates session storage.
 * Initializers execute once; later evaluations share the same address and
 * mutations. Unsupported declarators return an error; explicit brace blocks
 * still use ordinary local variables. Modules, including string literals,
 * remain loaded until reset or finalization. Failed compilation does not
 * change session state. Includes/types/functions persist as validated source.
 *
 * This executes trusted native code in-process, not in a sandbox. Serialize
 * calls on a session and never reset/free it while its code or callbacks run.
 * Pointer targets remain caller-owned; release them before resetting.
 *
 * Returns: the C exit code (normally 0), or -1 with @error set on failure.
 *   User code may itself return -1 without setting @error.
 */
gint crispy_repl_eval (CrispyRepl   *self,
                         const gchar  *code,
                         GError      **error);

/**
 * crispy_repl_needs_continuation:
 * @code: complete accumulated C input, including any embedded newlines
 *
 * Lexically checks delimiters, escaped string/character literals, comments,
 * and escaped newlines. Open delimiters/literals/block comments and trailing
 * '=', ',' or backslash request more input. Whitespace and comments do not
 * hide a trailing operator. Mismatched closing delimiters return %FALSE so
 * the compiler can diagnose them. This is a prompt hint, not C validation;
 * it does not expand macros or recognize every incomplete C construct.
 *
 * Returns: %TRUE if the caller should collect another line, otherwise %FALSE
 */
gboolean crispy_repl_needs_continuation (const gchar *code);

/**
 * crispy_repl_has_variable:
 * @self: a #CrispyRepl
 * @name: exact, case-sensitive variable identifier to look up
 *
 * Looks up a successfully declared session variable without evaluating code
 * or changing state. Failed declarations, block locals, preamble macros,
 * types and functions are not session variables. Reset clears the registry.
 * Serialize this lookup with evaluation and reset on the same session.
 *
 * Returns: %TRUE if @name is a current session variable, otherwise %FALSE
 */
gboolean crispy_repl_has_variable (CrispyRepl *self, const gchar *name);

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
 * Clears the accumulated preamble and variables, and unloads retained modules
 * in reverse order. Addresses into session storage, literals and functions
 * become invalid. Does not free heap objects referenced by session pointers.
 */
void crispy_repl_reset (CrispyRepl *self);

/**
 * crispy_repl_get_preamble:
 * @self: a #CrispyRepl
 *
 * Returns the accumulated preamble, including generated variable bindings.
 * This is diagnostic source, not a serializable session or replay log.
 *
 * Returns: (transfer none): the preamble string
 */
const gchar *crispy_repl_get_preamble (CrispyRepl *self);

G_END_DECLS

#endif /* CRISPY_REPL_H */
