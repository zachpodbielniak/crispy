/*
 * Copyright (C) 2026
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/* crispy-lsp-document.c - Document state management */

#include "crispy-lsp-document.h"

#include <string.h>

/* --- helpers --- */

static void
analyze_crispy(CrispyLspDocument *doc)
{
    gchar **lines;
    guint i;

    doc->is_crispy     = FALSE;
    doc->shebang_line  = G_MAXUINT;
    doc->params_line   = G_MAXUINT;
    g_free(doc->crispy_params);
    doc->crispy_params = NULL;

    if (doc->text == NULL || doc->text[0] == '\0')
        return;

    lines = g_strsplit(doc->text, "\n", -1);

    /* check first line for crispy shebang */
    if (lines[0] != NULL &&
        g_str_has_prefix(lines[0], "#!") &&
        strstr(lines[0], "crispy") != NULL)
    {
        doc->is_crispy    = TRUE;
        doc->shebang_line = 0;
    }

    if (!doc->is_crispy)
    {
        g_strfreev(lines);
        return;
    }

    /* scan for CRISPY_PARAMS */
    for (i = 0; lines[i] != NULL; i++)
    {
        const gchar *p = lines[i];

        /* skip whitespace */
        while (*p == ' ' || *p == '\t')
            p++;

        if (g_str_has_prefix(p, "#define") &&
            strstr(p, "CRISPY_PARAMS") != NULL)
        {
            const gchar *start, *end;

            doc->params_line = i;

            start = strchr(p, '"');
            if (start != NULL)
            {
                const gchar *line_end;

                start++;
                line_end = lines[i] + strlen(lines[i]);
                end = memchr(start, '"', (gsize)(line_end - start));
                if (end != NULL && end > start)
                    doc->crispy_params = g_strndup(start, (gsize)(end - start));
            }
            break;
        }
    }

    g_strfreev(lines);
}

/* --- public API --- */

CrispyLspDocument *
crispy_lsp_document_new(const gchar *uri, const gchar *text, gint version)
{
    CrispyLspDocument *doc;

    doc = g_new0(CrispyLspDocument, 1);
    doc->uri           = g_strdup(uri);
    doc->text          = g_strdup(text);
    doc->version       = version;
    doc->crispy_params = NULL;

    analyze_crispy(doc);
    return doc;
}

void
crispy_lsp_document_update(CrispyLspDocument *doc,
                           const gchar       *text,
                           gint               version)
{
    g_free(doc->text);
    doc->text    = g_strdup(text);
    doc->version = version;

    analyze_crispy(doc);
}

void
crispy_lsp_document_free(CrispyLspDocument *doc)
{
    if (doc == NULL)
        return;

    g_free(doc->uri);
    g_free(doc->text);
    g_free(doc->crispy_params);
    g_free(doc);
}

/* --- document store --- */

GHashTable *
crispy_lsp_document_store_new(void)
{
    return g_hash_table_new_full(g_str_hash, g_str_equal,
                                 g_free,
                                 (GDestroyNotify)crispy_lsp_document_free);
}

CrispyLspDocument *
crispy_lsp_document_store_get(GHashTable *store, const gchar *uri)
{
    return g_hash_table_lookup(store, uri);
}

void
crispy_lsp_document_store_open(GHashTable  *store,
                               const gchar *uri,
                               const gchar *text,
                               gint         version)
{
    CrispyLspDocument *doc;

    doc = crispy_lsp_document_new(uri, text, version);
    g_hash_table_replace(store, g_strdup(uri), doc);
}

void
crispy_lsp_document_store_close(GHashTable *store, const gchar *uri)
{
    g_hash_table_remove(store, uri);
}
