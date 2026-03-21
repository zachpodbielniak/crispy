/* crispy-profiler-private.c - Profiling utilities (private) */

/*
 * Copyright (C) 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * Implements the profiler helpers: returning the -pg flag string,
 * invoking gprof to post-process a gmon.out file, and cleaning up
 * the gmon.out file after analysis.
 */

#define CRISPY_COMPILATION
#include "crispy-profiler-private.h"
#include "../crispy-types.h"
#include <glib.h>
#include <glib/gstdio.h>

/* --- crispy_profiler_get_flags --- */

const gchar *
crispy_profiler_get_flags(
    void
){
    return "-pg";
}

/* --- crispy_profiler_run_gprof --- */

gboolean
crispy_profiler_run_gprof(
    const gchar  *executable_path,
    const gchar  *gmon_path,
    gchar       **output,
    GError      **error
){
    gchar       *cmd;
    gchar       *std_out;
    gchar       *std_err;
    gint         exit_status;
    gboolean     ok;
    const gchar *effective_gmon;

    g_return_val_if_fail(executable_path != NULL, FALSE);
    g_return_val_if_fail(output != NULL, FALSE);

    effective_gmon = (gmon_path != NULL) ? gmon_path : "gmon.out";

    {
        g_autofree gchar *q_exe = g_shell_quote(executable_path);
        g_autofree gchar *q_gmon = g_shell_quote(effective_gmon);

        cmd = g_strdup_printf("gprof %s %s", q_exe, q_gmon);
    }

    std_out      = NULL;
    std_err      = NULL;
    exit_status  = 0;

    ok = g_spawn_command_line_sync(cmd, &std_out, &std_err,
                                   &exit_status, error);
    g_free(cmd);
    g_free(std_err);

    if (!ok)
    {
        g_free(std_out);
        return FALSE;
    }

    if (!g_spawn_check_wait_status(exit_status, error))
    {
        g_free(std_out);
        return FALSE;
    }

    /* hand the captured output to the caller */
    *output = std_out;
    return TRUE;
}

/* --- crispy_profiler_cleanup --- */

void
crispy_profiler_cleanup(
    const gchar *gmon_path
){
    const gchar *effective_gmon;

    effective_gmon = (gmon_path != NULL) ? gmon_path : "gmon.out";

    g_unlink(effective_gmon);
}
