/* crispy-temp-registry-private.h - Temp files to remove if we are killed */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Several places write a C file to the temp directory and remove it when
 * they are done: the script runner strips a script into one, the REPL
 * writes one per evaluation, the test runner writes its harness into
 * one.  Every one of them leaks the file if the process is killed part
 * of the way through, and the CLI's own cleanup could not see any of
 * them -- it captured the script's path before the file existed, so it
 * was always %NULL.  This is the one list of what to remove, and the one
 * handler that removes it.  This header is NOT installed or included in
 * the public umbrella header.
 */

#ifndef CRISPY_TEMP_REGISTRY_PRIVATE_H
#define CRISPY_TEMP_REGISTRY_PRIVATE_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * crispy_temp_registry_add:
 * @path: path of a temp file that must not outlive this process
 *
 * Records @path so that a fatal signal removes it.  Call this as soon as
 * the file exists; a path recorded before the file is created is a path
 * the cleanup cannot use.
 */
void crispy_temp_registry_add (const gchar *path);

/**
 * crispy_temp_registry_remove:
 * @path: a path previously passed to crispy_temp_registry_add()
 *
 * Forgets @path.  Call this once the owner has removed the file itself,
 * or has decided to keep it (--source-preserve).  Removing a path that
 * was never added does nothing.
 */
void crispy_temp_registry_remove (const gchar *path);

/**
 * crispy_temp_registry_contains:
 * @path: a path to look for
 *
 * Returns: %TRUE if @path is currently registered
 */
gboolean crispy_temp_registry_contains (const gchar *path);

/**
 * crispy_temp_registry_unlink_all:
 *
 * Removes every registered file.  Async-signal-safe: it reads the slots
 * atomically and calls unlink() directly, so it can run from a signal
 * handler.
 */
void crispy_temp_registry_unlink_all (void);

/**
 * crispy_temp_registry_install_signal_handlers:
 *
 * Installs SIGINT and SIGTERM handlers that empty the registry and then
 * let the signal kill the process.
 *
 * These are real sigaction() dispositions rather than
 * g_unix_signal_add(): a GLib unix signal source only runs when
 * something is iterating the main context it was attached to, and a
 * crispy run has no main loop outside --watch.  Registering them the
 * GLib way replaced the default disposition with a handler nobody ever
 * dispatched, so Ctrl+C and SIGTERM were swallowed entirely -- the
 * process ran on with the signal bit set in SigCgt and nothing to show
 * for it.
 */
void crispy_temp_registry_install_signal_handlers (void);

G_END_DECLS

#endif /* CRISPY_TEMP_REGISTRY_PRIVATE_H */
