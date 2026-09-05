/* crispy-source-utils-private.c - Internal source parsing utilities */

/*
 * Shared helpers for CRISPY_PARAMS extraction, shebang stripping,
 * and shell expansion.  Factored out of crispy-script.c so that
 * both the script orchestrator and the config loader can reuse
 * the same logic without duplication.
 */

#ifndef CRISPY_COMPILATION
#define CRISPY_COMPILATION
#endif
#include "crispy-source-utils-private.h"
#include "crispy-use-parser-private.h"
#include "crispy-pkg-config-resolver.h"
#include "../interfaces/crispy-dependency-resolver.h"
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

/* --- crispy_source_blank_header --- */

gchar *
crispy_source_blank_header(
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
        gboolean blank;

        line = lines[i];
        blank = FALSE;

        /* blank the shebang on the first line */
        if (i == 0 && g_str_has_prefix(line, "#!"))
            blank = TRUE;

        /* blank the first #define CRISPY_PARAMS line */
        if (!blank && !params_found)
        {
            p = line;
            while (*p == ' ' || *p == '\t')
                p++;

            if (g_str_has_prefix(p, "#define") &&
                strstr(p, "CRISPY_PARAMS") != NULL)
            {
                params_found = TRUE;
                blank = TRUE;
            }
        }

        /*
         * An emptied line rather than a dropped one: the caller is
         * about to show the compiler's diagnostics to the user, and
         * every line number after a dropped line would be off.
         */
        if (!blank)
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

/* --- crispy_source_resolve_use_flags --- */

gchar *
crispy_source_resolve_use_flags(
    const gchar  *source,
    GError      **error
){
    g_autoptr(CrispyPkgConfigResolver) resolver = NULL;

    if (source == NULL)
        return NULL;

    resolver = crispy_pkg_config_resolver_new();

    return crispy_use_parser_resolve(
        source, CRISPY_DEPENDENCY_RESOLVER(resolver), error);
}

/* --- crispy_source_include_flag_for --- */

gchar *
crispy_source_include_flag_for(
    const gchar *source_path
){
    g_autofree gchar *absolute = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *quoted = NULL;

    if (source_path == NULL)
        return NULL;

    /*
     * An absolute path, because the flag is recorded in the dependency
     * file and read back by a later run whose working directory is its
     * own business.
     */
    if (g_path_is_absolute(source_path))
    {
        absolute = g_strdup(source_path);
    }
    else
    {
        g_autofree gchar *cwd = g_get_current_dir();

        absolute = g_build_filename(cwd, source_path, NULL);
    }

    dir = g_path_get_dirname(absolute);
    quoted = g_shell_quote(dir);

    return g_strconcat("-I", quoted, NULL);
}
