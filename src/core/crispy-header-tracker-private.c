/* crispy-header-tracker-private.c - Header dependency tracking utilities */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Parses gcc -MD .d dependency files to discover which headers a cached
 * script artifact depended on at compile time, then checks whether any
 * of those headers have changed so the cache can be invalidated.
 */

#ifndef CRISPY_COMPILATION
#define CRISPY_COMPILATION
#endif
#include "crispy-header-tracker-private.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

/* --- crispy_header_tracker_parse_depfile --- */

gboolean
crispy_header_tracker_parse_depfile(
    const gchar   *depfile_path,
    GPtrArray    **out_deps,
    GError       **error
){
    g_autofree gchar *contents = NULL;
    gsize length;
    const gchar *p;
    const gchar *colon;
    GPtrArray *deps;
    gchar **tokens;
    gint i;

    g_return_val_if_fail(depfile_path != NULL, FALSE);
    g_return_val_if_fail(out_deps != NULL, FALSE);

    *out_deps = NULL;

    if (!g_file_get_contents(depfile_path, &contents, &length, error))
        return FALSE;

    /*
     * The dependency file format produced by gcc -MD is:
     *
     *   target.o: source.c header1.h header2.h \
     *       header3.h header4.h
     *
     * We skip everything up to and including the first colon, then
     * tokenise the remainder on whitespace.  Backslash-newline
     * continuations are treated as plain whitespace — splitting on
     * whitespace naturally discards both the backslash and the newline
     * as separate empty tokens which we filter out.
     *
     * Replace '\' and '\n' with spaces before splitting so that the
     * standard g_strsplit_set tokeniser handles them uniformly.
     */
    colon = strchr(contents, ':');
    if (colon == NULL)
    {
        /*
         * Malformed dependency file: no colon found.  Return an empty
         * array rather than an error — a missing colon just means no
         * dependencies were recorded.
         */
        *out_deps = g_ptr_array_new_with_free_func(g_free);
        return TRUE;
    }

    /* advance past the colon to the dependency list */
    p = colon + 1;

    /*
     * Build a mutable copy of the remaining text with backslashes and
     * newlines replaced by spaces so g_strsplit_set splits cleanly.
     */
    {
        gchar *work = g_strdup(p);
        gchar *w;

        for (w = work; *w != '\0'; w++)
        {
            if (*w == '\\' || *w == '\n' || *w == '\r')
                *w = ' ';
        }

        tokens = g_strsplit_set(work, " \t", -1);
        g_free(work);
    }

    deps = g_ptr_array_new_with_free_func(g_free);

    /*
     * Skip the first non-empty token: it is the source file itself
     * (gcc lists it as the first prerequisite after the target).
     * All subsequent non-empty tokens are header paths.
     */
    {
        gboolean first_skipped = FALSE;

        for (i = 0; tokens[i] != NULL; i++)
        {
            if (tokens[i][0] == '\0')
                continue; /* skip empty tokens from consecutive spaces */

            if (!first_skipped)
            {
                first_skipped = TRUE;
                continue; /* this is the source file — skip it */
            }

            g_ptr_array_add(deps, g_strdup(tokens[i]));
        }
    }

    g_strfreev(tokens);

    *out_deps = deps;
    return TRUE;
}

/* --- crispy_header_tracker_check_stale --- */

gboolean
crispy_header_tracker_check_stale(
    GPtrArray *deps,
    gint64     reference_time
){
    guint i;

    if (deps == NULL)
        return FALSE;

    for (i = 0; i < deps->len; i++)
    {
        const gchar *path = g_ptr_array_index(deps, i);
        GStatBuf st;

        if (g_stat(path, &st) != 0)
        {
            /*
             * File does not exist or is inaccessible.  Treat a missing
             * dependency as stale: the header may have been moved or
             * deleted, which makes the cached artifact unreliable.
             */
            return TRUE;
        }

        if ((gint64)st.st_mtime > reference_time)
            return TRUE;
    }

    return FALSE;
}

/* --- crispy_header_tracker_get_depfile_path --- */

gchar *
crispy_header_tracker_get_depfile_path(
    const gchar *cache_dir,
    const gchar *hash
){
    g_return_val_if_fail(cache_dir != NULL, NULL);
    g_return_val_if_fail(hash != NULL, NULL);

    return g_strdup_printf("%s/%s.d", cache_dir, hash);
}
