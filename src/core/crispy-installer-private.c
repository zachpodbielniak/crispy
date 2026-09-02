/* crispy-installer-private.c - Script installation utilities */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Compiles a crispy script to a standalone executable and installs it
 * to a user-specified or default (~/.local/bin) directory.
 *
 * The source header lines (shebang + CRISPY_PARAMS/CRISPY_USE defines)
 * are stripped before compilation because they are not valid C when gcc
 * is invoked directly.  A temporary file is used for the stripped source
 * so the original is never modified.
 */

#ifndef CRISPY_COMPILATION
#define CRISPY_COMPILATION
#endif
#include "crispy-installer-private.h"
#include "crispy-source-utils-private.h"
#include "../interfaces/crispy-compiler.h"
#include "../crispy-types.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* crispy_installer_get_default_dir                                     */
/* ------------------------------------------------------------------ */

gchar *
crispy_installer_get_default_dir (void)
{
    gchar *dir;

    dir = g_build_filename(g_get_home_dir(), ".local", "bin", NULL);

    /*
     * Create the directory (and any missing parents) if it does not
     * already exist.  We ignore errors here — if the directory cannot
     * be created, the subsequent install step will fail with a clear
     * message of its own.
     */
    g_mkdir_with_parents(dir, 0755);

    return dir;
}

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * basename_strip_c:
 * @source_path: full path to the .c source file
 *
 * Returns the basename of @source_path with any trailing ".c" extension
 * removed.  E.g. "/home/user/scripts/foo.c" → "foo".
 *
 * Returns: (transfer full): bare name string
 */
static gchar *
basename_strip_c (const gchar *source_path)
{
    gchar *base;
    gsize len;

    base = g_path_get_basename(source_path);
    len  = strlen(base);

    if (len > 2 &&
        base[len - 2] == '.' &&
        base[len - 1] == 'c')
    {
        base[len - 2] = '\0';
    }

    return base;
}

/*
 * write_stripped_source:
 * @source_path: path to the original .c source
 * @out_tmp_path: (out) (transfer full): path to the temp file written
 * @error: return location for a #GError, or %NULL
 *
 * Reads @source_path, strips the crispy header lines (shebang +
 * CRISPY_PARAMS define), and writes the result to a temporary file.
 * The caller is responsible for deleting the temp file when done.
 *
 * Returns: %TRUE on success
 */
static gboolean
write_stripped_source (const gchar  *source_path,
                       gchar       **out_tmp_path,
                       GError      **error)
{
    g_autofree gchar *source = NULL;
    g_autofree gchar *stripped = NULL;
    gsize stripped_len;
    gint tmp_fd;
    gchar *tmp_path = NULL;

    g_return_val_if_fail(source_path != NULL, FALSE);
    g_return_val_if_fail(out_tmp_path != NULL, FALSE);

    *out_tmp_path = NULL;

    /* Read the original source. */
    if (!g_file_get_contents(source_path, &source, NULL, error))
        return FALSE;

    /* Strip the crispy header lines. */
    stripped = crispy_source_strip_header(source, &stripped_len);

    /* Create a secure temporary file for the stripped source. */
    tmp_fd = g_file_open_tmp("crispy-install-XXXXXX.c", &tmp_path, error);
    if (tmp_fd == -1)
        return FALSE;

    close(tmp_fd);

    if (!g_file_set_contents(tmp_path, stripped, (gssize)stripped_len, error))
    {
        g_unlink(tmp_path);
        g_free(tmp_path);
        return FALSE;
    }

    *out_tmp_path = tmp_path;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* crispy_installer_install                                             */
/* ------------------------------------------------------------------ */

gchar *
crispy_installer_install (const gchar     *source_path,
                          const gchar     *install_dir,
                          CrispyCompiler  *compiler,
                          const gchar     *extra_flags,
                          GError         **error)
{
    g_autofree gchar *effective_dir = NULL;
    g_autofree gchar *basename = NULL;
    g_autofree gchar *output_path = NULL;
    g_autofree gchar *tmp_source = NULL;
    g_autofree gchar *tmp_binary = NULL;
    g_autofree gchar *include_flag = NULL;
    g_autofree gchar *all_flags = NULL;

    g_return_val_if_fail(source_path != NULL, NULL);
    g_return_val_if_fail(CRISPY_IS_COMPILER(compiler), NULL);

    /* Verify source file exists. */
    if (!g_file_test(source_path, G_FILE_TEST_EXISTS))
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_IO,
                    "Source file not found: %s",
                    source_path);
        return NULL;
    }

    /* Determine the installation directory. */
    if (install_dir != NULL)
        effective_dir = g_strdup(install_dir);
    else
        effective_dir = crispy_installer_get_default_dir();

    /* Ensure the directory exists. */
    if (g_mkdir_with_parents(effective_dir, 0755) != 0)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_INSTALL,
                    "Failed to create installation directory: %s",
                    effective_dir);
        return NULL;
    }

    /* Derive the executable name from the source filename. */
    basename    = basename_strip_c(source_path);
    output_path = g_build_filename(effective_dir, basename, NULL);

    /*
     * Write a stripped copy of the source to a temp file so the
     * compiler sees plain C without the shebang or CRISPY_PARAMS line.
     */
    if (!write_stripped_source(source_path, &tmp_source, error))
        return NULL;

    /*
     * Compile the stripped source to a temporary binary in /tmp, then
     * move it to the final install path.  Using a temp target means the
     * install is atomic: the final path is only updated on success.
     */
    {
        gint bin_fd;
        gchar *bin_template;

        bin_template = g_strdup_printf("crispy-install-bin-%s-XXXXXX", basename);
        bin_fd = g_file_open_tmp(bin_template, &tmp_binary, error);
        g_free(bin_template);

        if (bin_fd == -1)
        {
            g_unlink(tmp_source);
            return NULL;
        }

        close(bin_fd);

        /* Remove the empty placeholder so the compiler can create it. */
        g_unlink(tmp_binary);
    }

    /*
     * The stripped copy is compiled from the temp directory, so a quoted
     * include of a header sitting beside the script needs to be told
     * where the script is.  Same rule as the script runner and the test
     * runner, which compile a copy for the same reason.
     */
    include_flag = crispy_source_include_flag_for(source_path);
    if (include_flag != NULL && extra_flags != NULL && extra_flags[0] != '\0')
        all_flags = g_strconcat(include_flag, " ", extra_flags, NULL);
    else if (include_flag != NULL)
        all_flags = g_strdup(include_flag);
    else
        all_flags = g_strdup(extra_flags);

    if (!crispy_compiler_compile_executable(compiler,
                                            tmp_source,
                                            tmp_binary,
                                            all_flags,
                                            error))
    {
        g_unlink(tmp_source);
        g_unlink(tmp_binary);
        return NULL;
    }

    /* Clean up the stripped source temp file. */
    g_unlink(tmp_source);

    /*
     * Move the compiled binary to the final install path.
     * GLib does not provide a portable rename, so use g_rename which
     * maps to POSIX rename() on Linux.  This is atomic on the same
     * filesystem, which is the common case (/tmp → ~/.local/bin).
     * If the rename crosses filesystems, g_rename falls back to a
     * copy+delete, which is still correct.
     */
    if (g_rename(tmp_binary, output_path) != 0)
    {
        /*
         * rename() failed (e.g., cross-device).  Fall back to reading
         * and writing the binary contents manually.
         */
        g_autofree gchar *bin_contents = NULL;
        gsize bin_len = 0;

        if (!g_file_get_contents(tmp_binary, &bin_contents, &bin_len, error))
        {
            g_unlink(tmp_binary);
            return NULL;
        }

        g_unlink(tmp_binary);

        if (!g_file_set_contents(output_path, bin_contents,
                                 (gssize)bin_len, error))
            return NULL;
    }

    /* Ensure the installed binary is executable: rwxr-xr-x (0755). */
    if (g_chmod(output_path, 0755) != 0)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_IO,
                    "Failed to set executable permissions on %s",
                    output_path);
        return NULL;
    }

    return g_steal_pointer(&output_path);
}
