/* crispy-shebang-parser-private.c - Shebang line parsing utilities */

/*
 * Implements shebang extraction and argument splitting for crispy scripts.
 * When a script is executed directly, the kernel collapses all text after
 * the interpreter path into a single argv token.  These helpers reconstruct
 * the original argument list from that collapsed string.
 */

#ifndef CRISPY_COMPILATION
#define CRISPY_COMPILATION
#endif
#include "crispy-shebang-parser-private.h"
#include <glib.h>
#include <string.h>

/* --- crispy_shebang_extract_line --- */

gchar *
crispy_shebang_extract_line(
    const gchar *source
){
    const gchar *newline;

    if (source == NULL || !g_str_has_prefix(source, "#!"))
        return NULL;

    newline = strchr(source, '\n');
    if (newline == NULL)
        return g_strdup(source);

    return g_strndup(source, (gsize)(newline - source));
}

/* --- crispy_shebang_get_interpreter --- */

gchar *
crispy_shebang_get_interpreter(
    const gchar *shebang_line
){
    const gchar *p;
    const gchar *start;

    if (shebang_line == NULL || !g_str_has_prefix(shebang_line, "#!"))
        return NULL;

    /* skip "#!" */
    p = shebang_line + 2;

    /* skip leading whitespace */
    while (*p == ' ' || *p == '\t')
        p++;

    if (*p == '\0')
        return NULL;

    start = p;

    /* read until whitespace or end of string */
    while (*p != '\0' && *p != ' ' && *p != '\t')
        p++;

    return g_strndup(start, (gsize)(p - start));
}

/* --- crispy_shebang_parse_args --- */

/*
 * State machine states for the argument parser.
 */
typedef enum {
    STATE_NORMAL,
    STATE_SINGLE_QUOTE,
    STATE_DOUBLE_QUOTE
} ParseState;

gboolean
crispy_shebang_parse_args(
    const gchar   *shebang_line,
    gint          *out_argc,
    gchar       ***out_argv
){
    const gchar *p;
    GPtrArray *args;
    GString *current;
    ParseState state;
    gboolean in_arg;
    g_autofree gchar *interpreter = NULL;

    g_return_val_if_fail(out_argc != NULL, FALSE);
    g_return_val_if_fail(out_argv != NULL, FALSE);

    *out_argc = 0;
    *out_argv = NULL;

    if (shebang_line == NULL || !g_str_has_prefix(shebang_line, "#!"))
    {
        *out_argv = g_new0(gchar *, 1);
        return TRUE;
    }

    /* skip "#!" */
    p = shebang_line + 2;

    /* skip leading whitespace */
    while (*p == ' ' || *p == '\t')
        p++;

    /* skip the interpreter path (first word) */
    {
        const gchar *interp_start = p;
        while (*p != '\0' && *p != ' ' && *p != '\t')
            p++;
        interpreter = g_strndup(interp_start, (gsize)(p - interp_start));
    }

    /*
     * If the interpreter is "env" or ends with "/env", skip the next word
     * as well — that word is the real command name, not an argument.
     */
    if (g_strcmp0(interpreter, "env") == 0 ||
        g_str_has_suffix(interpreter, "/env"))
    {
        while (*p == ' ' || *p == '\t')
            p++;
        /* skip the actual command name */
        while (*p != '\0' && *p != ' ' && *p != '\t')
            p++;
    }

    args    = g_ptr_array_new_with_free_func(g_free);
    current = g_string_new(NULL);
    state   = STATE_NORMAL;
    in_arg  = FALSE;

    for (; ; p++)
    {
        gchar c = *p;

        if (state == STATE_NORMAL)
        {
            if (c == '\0')
            {
                /* end of input — flush any pending argument */
                if (in_arg)
                    g_ptr_array_add(args, g_string_free(current, FALSE));
                else
                    g_string_free(current, TRUE);
                current = NULL;
                break;
            }
            else if (c == ' ' || c == '\t')
            {
                /* whitespace — flush pending argument if any */
                if (in_arg)
                {
                    g_ptr_array_add(args, g_string_free(current, FALSE));
                    current = g_string_new(NULL);
                    in_arg  = FALSE;
                }
            }
            else if (c == '\'')
            {
                in_arg = TRUE;
                state  = STATE_SINGLE_QUOTE;
            }
            else if (c == '"')
            {
                in_arg = TRUE;
                state  = STATE_DOUBLE_QUOTE;
            }
            else
            {
                in_arg = TRUE;
                g_string_append_c(current, c);
            }
        }
        else if (state == STATE_SINGLE_QUOTE)
        {
            if (c == '\0')
            {
                /* unterminated single quote */
                g_string_free(current, TRUE);
                g_ptr_array_unref(args);
                return FALSE;
            }
            else if (c == '\'')
            {
                /* closing single quote — return to normal */
                state = STATE_NORMAL;
            }
            else
            {
                /* everything else is literal inside single quotes */
                g_string_append_c(current, c);
            }
        }
        else /* STATE_DOUBLE_QUOTE */
        {
            if (c == '\0')
            {
                /* unterminated double quote */
                g_string_free(current, TRUE);
                g_ptr_array_unref(args);
                return FALSE;
            }
            else if (c == '"')
            {
                /* closing double quote — return to normal */
                state = STATE_NORMAL;
            }
            else if (c == '\\')
            {
                /* backslash escape inside double quotes */
                gchar next = *(p + 1);
                if (next == '\0')
                {
                    /* trailing backslash before end — treat as literal */
                    g_string_append_c(current, c);
                }
                else
                {
                    p++;
                    switch (next)
                    {
                    case '"':  g_string_append_c(current, '"');  break;
                    case '\\': g_string_append_c(current, '\\'); break;
                    case 'n':  g_string_append_c(current, '\n'); break;
                    case 't':  g_string_append_c(current, '\t'); break;
                    default:
                        /* unrecognised escape — keep both characters */
                        g_string_append_c(current, '\\');
                        g_string_append_c(current, next);
                        break;
                    }
                }
            }
            else
            {
                g_string_append_c(current, c);
            }
        }
    }

    /* NULL-terminate the pointer array and hand ownership to the caller */
    g_ptr_array_add(args, NULL);

    *out_argc = (gint)(args->len - 1); /* exclude the NULL sentinel */
    *out_argv = (gchar **)g_ptr_array_free(args, FALSE);

    return TRUE;
}
