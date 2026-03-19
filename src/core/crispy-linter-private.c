/* crispy-linter-private.c - Compiler-based linting utilities */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implements `crispy lint` by invoking gcc -fsyntax-only with an extended
 * set of warning flags.  The source file is parsed and type-checked but
 * no object file or binary is produced.  All warnings and errors are
 * captured from stderr and returned to the caller.
 *
 * Base glib pkg-config flags are resolved at runtime by spawning
 * pkg-config rather than hard-coding paths, so the linter works
 * regardless of how glib was installed.
 */

#define CRISPY_COMPILATION
#include "crispy-linter-private.h"
#include "../crispy-types.h"
#include <glib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Lint warning flags                                                   */
/* ------------------------------------------------------------------ */

/*
 * Extra warning flags appended after -Wall -Wextra.  These flags catch
 * common C pitfalls beyond what the standard warnings cover.
 */
static const gchar LINT_FLAGS[] =
    "-Wshadow "
    "-Wconversion "
    "-Wstrict-prototypes "
    "-Wmissing-prototypes "
    "-Wold-style-definition "
    "-Wformat=2 "
    "-Wswitch-default "
    "-Wswitch-enum "
    "-Wcast-align "
    "-Wpointer-arith "
    "-Wbad-function-cast "
    "-Wstrict-overflow=5 "
    "-Winline "
    "-Wundef "
    "-Wnested-externs "
    "-Wcast-qual "
    "-Wunreachable-code "
    "-Wwrite-strings "
    "-Wdouble-promotion "
    "-Wnull-dereference";

/* ------------------------------------------------------------------ */
/* crispy_linter_get_flags                                              */
/* ------------------------------------------------------------------ */

const gchar *
crispy_linter_get_flags (void)
{
    return LINT_FLAGS;
}

/* ------------------------------------------------------------------ */
/* Internal: resolve glib pkg-config cflags                            */
/* ------------------------------------------------------------------ */

/*
 * get_glib_cflags:
 * @error: return location for a #GError, or %NULL
 *
 * Spawns pkg-config to get the compiler flags for the standard crispy
 * libraries.  Returns the flags string on success, or %NULL on error.
 *
 * Returns: (transfer full) (nullable): cflags string
 */
static gchar *
get_glib_cflags (GError **error)
{
    gchar *std_out = NULL;
    gchar *std_err = NULL;
    gint exit_status = 0;
    gboolean ok;

    ok = g_spawn_command_line_sync(
        "pkg-config --cflags glib-2.0 gobject-2.0 gio-2.0 gmodule-2.0",
        &std_out,
        &std_err,
        &exit_status,
        error);

    g_free(std_err);

    if (!ok)
    {
        g_free(std_out);
        return NULL;
    }

    if (!g_spawn_check_wait_status(exit_status, error))
    {
        g_free(std_out);
        return NULL;
    }

    g_strstrip(std_out);
    return std_out;
}

/* ------------------------------------------------------------------ */
/* crispy_linter_check                                                  */
/* ------------------------------------------------------------------ */

gboolean
crispy_linter_check (const gchar  *source_path,
                     const gchar  *extra_flags,
                     gchar       **output,
                     GError      **error)
{
    g_autofree gchar *glib_cflags = NULL;
    g_autofree gchar *cmd = NULL;
    gchar *std_out = NULL;
    gchar *std_err = NULL;
    gint exit_status = 0;
    gboolean spawn_ok;

    g_return_val_if_fail(source_path != NULL, FALSE);

    if (output != NULL)
        *output = NULL;

    /* Resolve glib cflags at runtime. */
    glib_cflags = get_glib_cflags(error);
    if (glib_cflags == NULL)
    {
        g_prefix_error(error, "Failed to query pkg-config for glib flags: ");
        return FALSE;
    }

    /*
     * Build the gcc command.  The -fsyntax-only flag instructs gcc to
     * perform parsing and type-checking without producing any output
     * file, which is exactly what a linter needs.
     */
    if (extra_flags != NULL && extra_flags[0] != '\0')
    {
        cmd = g_strdup_printf(
            "gcc -std=gnu89 -Wall -Wextra %s %s %s -fsyntax-only %s",
            LINT_FLAGS,
            glib_cflags,
            extra_flags,
            source_path);
    }
    else
    {
        cmd = g_strdup_printf(
            "gcc -std=gnu89 -Wall -Wextra %s %s -fsyntax-only %s",
            LINT_FLAGS,
            glib_cflags,
            source_path);
    }

    spawn_ok = g_spawn_command_line_sync(cmd, &std_out, &std_err,
                                         &exit_status, error);

    g_free(std_out);

    if (!spawn_ok)
    {
        g_free(std_err);
        g_prefix_error(error, "Failed to spawn gcc: ");
        if (error != NULL && *error != NULL)
        {
            /*
             * Re-code as a lint error so callers can distinguish spawn
             * failure from a failed lint run.
             */
            (*error)->domain = CRISPY_ERROR;
            (*error)->code   = CRISPY_ERROR_LINT;
        }
        return FALSE;
    }

    /*
     * gcc exit code 0 with empty stderr means a clean lint.
     * Any output on stderr (warnings or errors) counts as a lint failure.
     */
    if (exit_status == 0 && (std_err == NULL || std_err[0] == '\0'))
    {
        g_free(std_err);
        return TRUE;
    }

    /*
     * Non-zero exit status means gcc encountered an error (not just
     * warnings).  Set a GError so callers can distinguish "file has
     * warnings" (FALSE, no error) from "gcc failed" (FALSE, error).
     */
    if (!g_spawn_check_wait_status(exit_status, NULL))
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_LINT,
                    "gcc exited with errors:\n%s",
                    std_err != NULL ? std_err : "(no output)");
    }

    /* Lint found issues — hand the output to the caller if requested. */
    if (output != NULL)
        *output = std_err;
    else
        g_free(std_err);

    return FALSE;
}
