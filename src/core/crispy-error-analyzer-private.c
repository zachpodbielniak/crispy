/* crispy-error-analyzer-private.c - GCC error analysis and package suggestion */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Scans gcc stderr output for "fatal error: <header>: No such file or
 * directory" lines and maps the missing header paths to their likely
 * pkg-config package names using a static lookup table.
 */

#define CRISPY_COMPILATION
#include "crispy-error-analyzer-private.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

/* --- static header-to-package mapping table --- */

/*
 * Each entry maps a header path prefix (or exact filename) to the
 * pkg-config package name that provides it.  Matching is done by
 * checking whether the extracted header path starts with the prefix.
 * Entries are tested in order; the first match wins.
 */
static const struct {
    const gchar *header_prefix;
    const gchar *pkg_name;
} known_headers[] = {
    { "json-glib/",       "json-glib-1.0"   },
    { "libsoup/",         "libsoup-3.0"     },
    { "libxml/",          "libxml-2.0"      },
    { "sqlite3.h",        "sqlite3"         },
    { "curl/",            "libcurl"         },
    { "cairo",            "cairo"           },
    { "pango/",           "pango"           },
    { "gdk-pixbuf/",      "gdk-pixbuf-2.0"  },
    { "gtk/gtk.h",        "gtk4"            },
    { "gst/",             "gstreamer-1.0"   },
    { "libnotify/",       "libnotify"       },
    { "archive.h",        "libarchive"      },
    { "yaml.h",           "yaml-0.1"        },
    { "uuid/uuid.h",      "uuid"            },
    { "openssl/",         "openssl"         },
    { "zlib.h",           "zlib"            },
    { NULL, NULL }
};

/* --- internal helpers --- */

/*
 * look_up_package:
 * @header: the header path extracted from a gcc error line
 *
 * Searches known_headers for a prefix match and returns the associated
 * pkg-config package name, or %NULL if the header is not recognised.
 *
 * Returns: (nullable): a static string — do not free
 */
static const gchar *
look_up_package(
    const gchar *header
){
    gint i;

    for (i = 0; known_headers[i].header_prefix != NULL; i++)
    {
        if (g_str_has_prefix(header, known_headers[i].header_prefix))
            return known_headers[i].pkg_name;
    }

    return NULL;
}

/* --- crispy_error_analyzer_suggest_packages --- */

GPtrArray *
crispy_error_analyzer_suggest_packages(
    const gchar *error_output
){
    g_autoptr(GRegex) regex = NULL;
    g_autoptr(GMatchInfo) match_info = NULL;
    GPtrArray *suggestions;
    GError *regex_error = NULL;

    if (error_output == NULL || error_output[0] == '\0')
        return NULL;

    /*
     * Pattern matches gcc/clang fatal-error lines of the form:
     *
     *   <file>:<line>:<col>: fatal error: <header>: No such file or directory
     *
     * Capture group 1 extracts the header path between "fatal error: "
     * and the trailing ": No such file or directory".
     */
    regex = g_regex_new(
        "fatal error: ([^:]+): No such file or directory",
        G_REGEX_MULTILINE,
        0,
        &regex_error
    );

    if (regex == NULL)
    {
        /* should never happen with a valid literal pattern */
        g_warning("crispy_error_analyzer_suggest_packages: "
                  "failed to compile regex: %s",
                  regex_error ? regex_error->message : "(unknown)");
        g_clear_error(&regex_error);
        return NULL;
    }

    suggestions = g_ptr_array_new_with_free_func(g_free);

    g_regex_match(regex, error_output, 0, &match_info);

    while (g_match_info_matches(match_info))
    {
        g_autofree gchar *header = NULL;
        const gchar *pkg;

        header = g_match_info_fetch(match_info, 1);

        if (header != NULL && header[0] != '\0')
        {
            pkg = look_up_package(header);

            if (pkg != NULL)
            {
                /*
                 * Avoid duplicate suggestions: only add this package
                 * name if it is not already in the array.
                 */
                gboolean already_present = FALSE;
                guint i;

                for (i = 0; i < suggestions->len; i++)
                {
                    if (g_strcmp0(g_ptr_array_index(suggestions, i), pkg) == 0)
                    {
                        already_present = TRUE;
                        break;
                    }
                }

                if (!already_present)
                    g_ptr_array_add(suggestions, g_strdup(pkg));
            }
        }

        g_match_info_next(match_info, NULL);
    }

    if (suggestions->len == 0)
    {
        g_ptr_array_unref(suggestions);
        return NULL;
    }

    return suggestions;
}

/* --- crispy_error_analyzer_format_suggestions --- */

gchar *
crispy_error_analyzer_format_suggestions(
    GPtrArray *suggestions
){
    GString *result;
    guint i;

    if (suggestions == NULL || suggestions->len == 0)
        return g_strdup("");

    result = g_string_new("Hint: try adding to CRISPY_USE: \"");

    for (i = 0; i < suggestions->len; i++)
    {
        if (i > 0)
            g_string_append_c(result, ' ');
        g_string_append(result, (const gchar *)g_ptr_array_index(suggestions, i));
    }

    g_string_append_c(result, '"');

    return g_string_free(result, FALSE);
}
