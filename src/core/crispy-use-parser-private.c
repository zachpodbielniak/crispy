/* crispy-use-parser-private.c - CRISPY_USE directive parsing utilities */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Parses #define CRISPY_USE "pkg1 pkg2 pkg3" directives from C source
 * files and resolves the named packages into compiler/linker flags via
 * a CrispyDependencyResolver.  Mirrors the structure of
 * crispy-source-utils-private.c which handles CRISPY_PARAMS extraction.
 */

#define CRISPY_COMPILATION
#include "crispy-use-parser-private.h"
#include "crispy-dependency-info.h"
#include "../interfaces/crispy-dependency-resolver.h"
#include "../crispy-types.h"

#include <glib.h>
#include <string.h>

/* --- crispy_use_parser_extract --- */

gchar *
crispy_use_parser_extract(
    const gchar *source
){
    const gchar *pos;
    const gchar *line_end;
    const gchar *start;
    const gchar *end;

    if (source == NULL)
        return NULL;

    /*
     * Walk through the source line by line looking for a line that
     * begins with optional whitespace followed by #define CRISPY_USE.
     * Extract the quoted value portion.
     */
    pos = source;
    while (pos != NULL && *pos != '\0')
    {
        const gchar *p;

        /* find end of this line */
        line_end = strchr(pos, '\n');
        if (line_end == NULL)
            line_end = pos + strlen(pos);

        /* skip leading whitespace */
        p = pos;
        while (p < line_end && (*p == ' ' || *p == '\t'))
            p++;

        /* check for #define CRISPY_USE */
        if (g_str_has_prefix(p, "#define") &&
            strstr(p, "CRISPY_USE") != NULL)
        {
            /* find the quoted value */
            start = strchr(p, '"');
            if (start != NULL)
            {
                start++; /* skip opening quote */
                end = memchr(start, '"', (gsize)(line_end - start));
                if (end != NULL && end > start)
                    return g_strndup(start, (gsize)(end - start));
            }
        }

        /* advance to next line */
        if (*line_end == '\n')
            pos = line_end + 1;
        else
            break;
    }

    return NULL;
}

/* --- crispy_use_parser_split --- */

gchar **
crispy_use_parser_split(
    const gchar *use_value,
    gint        *out_count
){
    gchar **all;
    gchar **result;
    gint count;
    gint i;
    gint j;

    if (out_count != NULL)
        *out_count = 0;

    if (use_value == NULL || use_value[0] == '\0')
        return g_new0(gchar *, 1); /* NULL-terminated empty array */

    /*
     * Split on whitespace.  g_strsplit with NULL delimiter is not
     * available, so split on single space then filter out empty tokens
     * that arise from leading, trailing, or multiple consecutive spaces.
     */
    all = g_strsplit(use_value, " ", -1);

    /* count non-empty tokens */
    count = 0;
    for (i = 0; all[i] != NULL; i++)
    {
        if (all[i][0] != '\0')
            count++;
    }

    result = g_new0(gchar *, (gsize)(count + 1));

    j = 0;
    for (i = 0; all[i] != NULL; i++)
    {
        /* also skip tokens that are purely whitespace */
        if (all[i][0] != '\0' && g_strstrip(all[i])[0] != '\0')
        {
            result[j] = g_strdup(g_strstrip(all[i]));
            j++;
        }
    }
    result[j] = NULL;

    g_strfreev(all);

    if (out_count != NULL)
        *out_count = j;

    return result;
}

/* --- crispy_use_parser_resolve --- */

gchar *
crispy_use_parser_resolve(
    const gchar              *source,
    CrispyDependencyResolver *resolver,
    GError                  **error
){
    g_autofree gchar  *use_value = NULL;
    g_auto(GStrv)      packages  = NULL;
    GString           *flags;
    gint               count;
    gint               i;

    use_value = crispy_use_parser_extract(source);
    if (use_value == NULL)
        return NULL; /* no CRISPY_USE -- not an error */

    packages = crispy_use_parser_split(use_value, &count);
    if (count == 0)
        return NULL;

    flags = g_string_new(NULL);

    for (i = 0; packages[i] != NULL; i++)
    {
        CrispyDependencyInfo *info;
        g_autofree gchar     *combined = NULL;

        info = crispy_dependency_resolver_resolve(resolver, packages[i], error);
        if (info == NULL)
        {
            g_string_free(flags, TRUE);
            return NULL;
        }

        combined = crispy_dependency_info_get_combined_flags(info);
        crispy_dependency_info_free(info);

        if (combined != NULL && combined[0] != '\0')
        {
            if (flags->len > 0)
                g_string_append_c(flags, ' ');
            g_string_append(flags, combined);
        }
    }

    return g_string_free(flags, FALSE);
}

/* --- crispy_use_parser_resolve_to_list --- */

GPtrArray *
crispy_use_parser_resolve_to_list(
    const gchar              *source,
    CrispyDependencyResolver *resolver,
    GError                  **error
){
    g_autofree gchar  *use_value = NULL;
    g_auto(GStrv)      packages  = NULL;
    GPtrArray         *list;
    gint               count;
    gint               i;

    use_value = crispy_use_parser_extract(source);
    if (use_value == NULL)
        return NULL; /* no CRISPY_USE -- not an error */

    packages = crispy_use_parser_split(use_value, &count);
    if (count == 0)
        return NULL;

    list = g_ptr_array_new_with_free_func(
        (GDestroyNotify)crispy_dependency_info_free);

    for (i = 0; packages[i] != NULL; i++)
    {
        CrispyDependencyInfo *info;

        info = crispy_dependency_resolver_resolve(resolver, packages[i], error);
        if (info == NULL)
        {
            g_ptr_array_unref(list);
            return NULL;
        }

        g_ptr_array_add(list, info);
    }

    return list;
}

/* --- crispy_source_strip_use --- */

gchar *
crispy_source_strip_use(
    const gchar *source,
    gsize       *out_len
){
    GString  *modified;
    gchar   **lines;
    gint      i;
    gboolean  use_found;

    if (source == NULL)
    {
        if (out_len != NULL)
            *out_len = 0;
        return g_strdup("");
    }

    use_found = FALSE;
    lines     = g_strsplit(source, "\n", -1);
    modified  = g_string_new(NULL);

    for (i = 0; lines[i] != NULL; i++)
    {
        const gchar *line;
        const gchar *p;

        line = lines[i];

        /* skip the first #define CRISPY_USE line */
        if (!use_found)
        {
            p = line;
            while (*p == ' ' || *p == '\t')
                p++;

            if (g_str_has_prefix(p, "#define") &&
                strstr(p, "CRISPY_USE") != NULL)
            {
                use_found = TRUE;
                continue;
            }
        }

        /* keep the line */
        g_string_append(modified, line);
        if (lines[i + 1] != NULL)
            g_string_append_c(modified, '\n');
    }

    g_strfreev(lines);

    if (out_len != NULL)
        *out_len = modified->len;

    return g_string_free(modified, FALSE);
}
