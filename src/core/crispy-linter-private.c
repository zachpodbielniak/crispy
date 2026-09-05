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
 * gcc is not given the script itself but a temporary copy with the
 * crispy header lines blanked out, because a shebang is not a valid
 * preprocessing directive and gcc refuses the file outright.  Two
 * details keep the diagnostics pointing at the real script:
 *
 *   - the header lines are emptied rather than deleted, so every
 *     following line keeps its number, and
 *   - the copy opens with a #line directive naming the original path,
 *     so gcc reports that path and reads its source context from it.
 *
 * The copy lives in the temp directory, so the script's own directory
 * is added to the include path -- otherwise a quoted #include of a
 * sibling header would resolve against the temp directory and fail.
 *
 * Base glib pkg-config flags are resolved at runtime by spawning
 * pkg-config rather than hard-coding paths, so the linter works
 * regardless of how glib was installed.
 */

#ifndef CRISPY_COMPILATION
#define CRISPY_COMPILATION
#endif
#include "crispy-linter-private.h"
#include "crispy-source-utils-private.h"
#include "crispy-temp-registry-private.h"
#include "../crispy-types.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>
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
/* Internal: the #line directive naming the original file              */
/* ------------------------------------------------------------------ */

/*
 * escape_for_c_string:
 * @path: a filesystem path
 *
 * Escapes backslashes and double quotes so @path can sit inside the
 * quoted filename of a #line directive.
 *
 * Returns: (transfer full): the escaped path
 */
static gchar *
escape_for_c_string (const gchar *path)
{
    GString *out;
    const gchar *p;

    out = g_string_new(NULL);

    for (p = path; *p != '\0'; p++)
    {
        if (*p == '\\' || *p == '"')
            g_string_append_c(out, '\\');

        g_string_append_c(out, *p);
    }

    return g_string_free(out, FALSE);
}

/*
 * write_lint_copy:
 * @source_path: the script being linted
 * @out_path: (out) (transfer full): path of the temp copy that was written
 * @error: return location for a #GError, or %NULL
 *
 * Writes a temporary copy of @source_path with the crispy header lines
 * blanked and a leading #line directive naming the original file.  The
 * directive is numbered 1 because the line that follows it is the
 * script's own line 1, so numbering stays aligned even though the copy
 * has one more physical line than the script.
 *
 * The path is registered with the temp registry before it is written,
 * so a signal arriving mid-lint still removes it.
 *
 * Returns: %TRUE on success
 */
static gboolean
write_lint_copy (const gchar  *source_path,
                 gchar       **out_path,
                 GError      **error)
{
    g_autofree gchar *source = NULL;
    g_autofree gchar *blanked = NULL;
    g_autofree gchar *escaped = NULL;
    g_autofree gchar *contents = NULL;
    g_autofree gchar *tmp_path = NULL;
    gint fd;

    *out_path = NULL;

    if (!g_file_get_contents(source_path, &source, NULL, error))
        return FALSE;

    fd = g_file_open_tmp("crispy-lint-XXXXXX.c", &tmp_path, error);
    if (fd < 0)
        return FALSE;

    close(fd);

    /* registered before the write, so a signal mid-write still cleans up */
    crispy_temp_registry_add(tmp_path);

    blanked  = crispy_source_blank_header(source, NULL);
    escaped  = escape_for_c_string(source_path);
    contents = g_strdup_printf("#line 1 \"%s\"\n%s", escaped, blanked);

    if (!g_file_set_contents(tmp_path, contents, -1, error))
    {
        crispy_temp_registry_remove(tmp_path);
        g_unlink(tmp_path);
        return FALSE;
    }

    *out_path = g_steal_pointer(&tmp_path);
    return TRUE;
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
    g_autofree gchar *lint_path = NULL;
    g_autofree gchar *include_flag = NULL;
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
     * gcc gets a header-blanked copy, not the script: a shebang is not
     * a preprocessing directive and gcc rejects the file on line 1.
     */
    if (!write_lint_copy(source_path, &lint_path, error))
    {
        g_prefix_error(error, "Failed to prepare source for linting: ");
        if (error != NULL && *error != NULL)
        {
            (*error)->domain = CRISPY_ERROR;
            (*error)->code   = CRISPY_ERROR_LINT;
        }
        return FALSE;
    }

    /*
     * The copy is in the temp directory, so a quoted #include of a
     * sibling header would look there.  Point it back at the script.
     */
    include_flag = crispy_source_include_flag_for(source_path);

    /*
     * Build the gcc command.  The -fsyntax-only flag instructs gcc to
     * perform parsing and type-checking without producing any output
     * file, which is exactly what a linter needs.
     */
    {
        g_autofree gchar *q_path = g_shell_quote(lint_path);

        cmd = g_strdup_printf(
            "gcc -std=gnu89 -Wall -Wextra %s %s %s %s -fsyntax-only %s",
            LINT_FLAGS,
            glib_cflags,
            include_flag != NULL ? include_flag : "",
            (extra_flags != NULL && extra_flags[0] != '\0') ? extra_flags : "",
            q_path);
    }

    spawn_ok = g_spawn_command_line_sync(cmd, &std_out, &std_err,
                                         &exit_status, error);

    g_free(std_out);

    crispy_temp_registry_remove(lint_path);
    g_unlink(lint_path);

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
