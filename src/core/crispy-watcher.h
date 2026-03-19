/* crispy-watcher.h - Script file watcher */

/*
 * Copyright (C) 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef CRISPY_WATCHER_H
#define CRISPY_WATCHER_H

#if !defined(CRISPY_INSIDE) && !defined(CRISPY_COMPILATION)
#error "Only <crispy.h> can be included directly."
#endif

#include <glib-object.h>
#include "../crispy-types.h"

G_BEGIN_DECLS

#define CRISPY_TYPE_WATCHER (crispy_watcher_get_type())

G_DECLARE_FINAL_TYPE(CrispyWatcher, crispy_watcher, CRISPY, WATCHER, GObject)

/**
 * crispy_watcher_new:
 * @script_path: path to the C source file to watch
 * @compiler: a #CrispyCompiler implementation
 * @cache: a #CrispyCacheProvider implementation
 * @flags: #CrispyFlags controlling compilation and execution behavior
 *
 * Creates a new #CrispyWatcher that will watch @script_path for changes
 * and re-execute it using @compiler and @cache whenever the file is
 * modified.
 *
 * Returns: (transfer full): a new #CrispyWatcher
 */
CrispyWatcher *crispy_watcher_new (const gchar         *script_path,
                                   CrispyCompiler      *compiler,
                                   CrispyCacheProvider *cache,
                                   CrispyFlags          flags);

/**
 * crispy_watcher_start:
 * @self: a #CrispyWatcher
 * @error: return location for a #GError, or %NULL
 *
 * Starts watching the script file for changes. This function creates a
 * #GFileMonitor on the script path and runs a #GMainLoop until
 * crispy_watcher_stop() is called. The call blocks until the watcher
 * is stopped.
 *
 * Returns: %TRUE on success, %FALSE if the monitor could not be created
 */
gboolean crispy_watcher_start (CrispyWatcher  *self,
                               GError        **error);

/**
 * crispy_watcher_stop:
 * @self: a #CrispyWatcher
 *
 * Stops the file monitor and quits the internal #GMainLoop, causing
 * crispy_watcher_start() to return.
 */
void crispy_watcher_stop (CrispyWatcher *self);

/**
 * crispy_watcher_is_running:
 * @self: a #CrispyWatcher
 *
 * Returns whether the watcher is currently active and monitoring for
 * file changes.
 *
 * Returns: %TRUE if the watcher is running
 */
gboolean crispy_watcher_is_running (CrispyWatcher *self);

/**
 * crispy_watcher_set_debounce_ms:
 * @self: a #CrispyWatcher
 * @ms: debounce interval in milliseconds
 *
 * Sets the debounce interval. When a file change event is received, the
 * watcher waits @ms milliseconds before triggering recompilation. If
 * further change events arrive during that interval, the timer is reset.
 *
 * The default debounce interval is 500 milliseconds.
 */
void crispy_watcher_set_debounce_ms (CrispyWatcher *self,
                                     guint          ms);

/**
 * crispy_watcher_get_debounce_ms:
 * @self: a #CrispyWatcher
 *
 * Returns the current debounce interval in milliseconds.
 *
 * Returns: the debounce interval in milliseconds
 */
guint crispy_watcher_get_debounce_ms (CrispyWatcher *self);

/**
 * crispy_watcher_set_script_argv:
 * @self: a #CrispyWatcher
 * @argc: number of arguments in @argv
 * @argv: (array length=argc) (transfer none): argument vector passed to
 *   the script on each execution
 *
 * Sets the argument vector that is forwarded to the script's main()
 * function on each re-execution triggered by a file change event.
 */
void crispy_watcher_set_script_argv (CrispyWatcher  *self,
                                     gint            argc,
                                     gchar         **argv);

G_END_DECLS

#endif /* CRISPY_WATCHER_H */
