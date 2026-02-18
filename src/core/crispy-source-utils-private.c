/* crispy-source-utils-private.c - Internal source parsing utilities */

/*
 * Shared helpers for CRISPY_PARAMS extraction, shebang stripping,
 * and shell expansion.  Factored out of crispy-script.c so that
 * both the script orchestrator and the config loader can reuse
 * the same logic without duplication.
 */

#define CRISPY_COMPILATION
#include "crispy-source-utils-private.h"
#include "../crispy-types.h"

#include <glib.h>
#include <string.h>

/* --- crispy_source_extract_params --- */

gchar *
crispy_source_extract_params(
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
     * begins with optional whitespace followed by #define CRISPY_PARAMS.
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

        /* check for #define CRISPY_PARAMS */
        if (g_str_has_prefix(p, "#define") &&
            strstr(p, "CRISPY_PARAMS") != NULL)
        {
            /* find the quoted value */
            start = strchr(p, '"');
            if (start != NULL)
            {
                start++; /* skip opening quote */
                end = strrchr(start, '"');
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

/* --- crispy_source_strip_header --- */

gchar *
crispy_source_strip_header(
    const gchar *source,
    gsize       *out_len
){
    GString *modified;
    gchar **lines;
    gint i;
    gboolean params_found;

    if (source == NULL)
    {
        if (out_len != NULL)
            *out_len = 0;
        return g_strdup("");
    }

    params_found = FALSE;
    lines = g_strsplit(source, "\n", -1);
    modified = g_string_new(NULL);

    for (i = 0; lines[i] != NULL; i++)
    {
        const gchar *line;
        const gchar *p;

        line = lines[i];

        /* skip shebang on the first line */
        if (i == 0 && g_str_has_prefix(line, "#!"))
            continue;

        /* skip the first #define CRISPY_PARAMS line */
        if (!params_found)
        {
            p = line;
            while (*p == ' ' || *p == '\t')
                p++;

            if (g_str_has_prefix(p, "#define") &&
                strstr(p, "CRISPY_PARAMS") != NULL)
            {
                params_found = TRUE;
                continue;
            }
        }

        /* keep the line */
        g_string_append(modified, line);
        g_string_append_c(modified, '\n');
    }

    g_strfreev(lines);

    if (out_len != NULL)
        *out_len = modified->len;

    return g_string_free(modified, FALSE);
}

/* --- crispy_source_shell_expand --- */

gchar *
crispy_source_shell_expand(
    const gchar  *params,
    GError      **error
){
    g_autofree gchar *cmd = NULL;
    gchar *std_out;
    gchar *std_err;
    gint exit_status;

    if (params == NULL || params[0] == '\0')
        return g_strdup("");

    std_out = NULL;
    std_err = NULL;

    /*
     * Use printf '%s ' to avoid echo's interpretation of backslashes.
     * The trailing space after %s ensures word-split arguments from
     * command substitutions like $(pkg-config ...) are rejoined with
     * spaces.  g_strstrip below removes the final trailing space.
     */
    cmd = g_strdup_printf("/bin/sh -c \"printf '%%s ' %s\"", params);

    if (!g_spawn_command_line_sync(cmd, &std_out, &std_err,
                                   &exit_status, error))
    {
        g_free(std_out);
        g_free(std_err);
        return NULL;
    }

    g_free(std_err);

    if (!g_spawn_check_wait_status(exit_status, error))
    {
        g_free(std_out);
        return NULL;
    }

    g_strstrip(std_out);
    return std_out;
}
