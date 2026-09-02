/* crispy-temp-registry-private.c - Temp files to remove if we are killed */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef CRISPY_COMPILATION
#define CRISPY_COMPILATION
#endif
#include "crispy-temp-registry-private.h"

#include <glib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

/*
 * A fixed table rather than a GPtrArray or a GHashTable: the signal
 * handler walks it, and neither reallocation nor a lock is something a
 * handler can wait on.  One entry per temp file that can be open at
 * once -- a script has one, the REPL one per evaluation in flight -- so
 * this is far more than anything reaches.
 */
#define CRISPY_TEMP_REGISTRY_SLOTS (32)

static gchar *registry_slots[CRISPY_TEMP_REGISTRY_SLOTS];

/* --- crispy_temp_registry_add --- */

void
crispy_temp_registry_add(
    const gchar *path
){
    gchar *owned;
    gint i;

    g_return_if_fail(path != NULL);

    owned = g_strdup(path);

    for (i = 0; i < CRISPY_TEMP_REGISTRY_SLOTS; i++)
    {
        if (g_atomic_pointer_compare_and_exchange(&registry_slots[i],
                                                  (gchar *)NULL, owned))
        {
            return;
        }
    }

    /*
     * Nothing to do but say so: dropping the entry silently would mean a
     * file that survives a Ctrl+C with no record of why.
     */
    g_warning("Temp file registry is full; '%s' will not be removed "
              "if crispy is interrupted", path);
    g_free(owned);
}

/* --- crispy_temp_registry_remove --- */

void
crispy_temp_registry_remove(
    const gchar *path
){
    gint i;

    if (path == NULL)
        return;

    for (i = 0; i < CRISPY_TEMP_REGISTRY_SLOTS; i++)
    {
        gchar *slot;

        slot = g_atomic_pointer_get(&registry_slots[i]);
        if (slot == NULL || strcmp(slot, path) != 0)
            continue;

        /*
         * Clear the slot before freeing it.  A signal arriving between
         * the two sees %NULL and skips the entry; one arriving before
         * the clear unlinks a file that was about to be forgotten
         * anyway.  Freeing first would leave the handler reading memory
         * that is already gone.
         */
        if (g_atomic_pointer_compare_and_exchange(&registry_slots[i],
                                                  slot, (gchar *)NULL))
        {
            g_free(slot);
        }
        return;
    }
}

/* --- crispy_temp_registry_contains --- */

gboolean
crispy_temp_registry_contains(
    const gchar *path
){
    gint i;

    if (path == NULL)
        return FALSE;

    for (i = 0; i < CRISPY_TEMP_REGISTRY_SLOTS; i++)
    {
        gchar *slot;

        slot = g_atomic_pointer_get(&registry_slots[i]);
        if (slot != NULL && strcmp(slot, path) == 0)
            return TRUE;
    }

    return FALSE;
}

/* --- crispy_temp_registry_unlink_all --- */

void
crispy_temp_registry_unlink_all(
    void
){
    gint i;

    for (i = 0; i < CRISPY_TEMP_REGISTRY_SLOTS; i++)
    {
        gchar *slot;

        slot = g_atomic_pointer_get(&registry_slots[i]);
        if (slot != NULL)
        {
            /*
             * unlink(), not g_unlink(): this runs from a signal handler,
             * and only the raw syscall is on the list of things that may.
             */
            (void)unlink(slot);
        }
    }
}

/* --- signal handling --- */

/*
 * on_fatal_signal:
 * @signo: the signal that arrived
 *
 * Removes the registered temp files, then dies of @signo rather than
 * calling exit(): a caller that looks at WIFSIGNALED -- a shell, timeout(1),
 * a supervisor -- must see the signal it sent, and exit() would run atexit
 * handlers from a context where almost nothing is safe to call.
 */
static void
on_fatal_signal(
    int signo
){
    crispy_temp_registry_unlink_all();

    signal(signo, SIG_DFL);
    raise(signo);
}

/* --- crispy_temp_registry_install_signal_handlers --- */

void
crispy_temp_registry_install_signal_handlers(
    void
){
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = on_fatal_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
}
