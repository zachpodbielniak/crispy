/*
 * Copyright (C) 2026
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/* crispy-lsp-document.h - Document state management */

#ifndef CRISPY_LSP_DOCUMENT_H
#define CRISPY_LSP_DOCUMENT_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _CrispyLspDocument CrispyLspDocument;

struct _CrispyLspDocument
{
    gchar    *uri;
    gchar    *text;
    gint      version;
    gboolean  is_crispy;       /* TRUE if shebang contains 'crispy' */
    gchar    *crispy_params;   /* extracted CRISPY_PARAMS value     */
    guint     shebang_line;    /* 0 if no shebang, else line index  */
    guint     params_line;     /* line index of CRISPY_PARAMS, or G_MAXUINT */
};

CrispyLspDocument  *crispy_lsp_document_new    (const gchar *uri,
                                                const gchar *text,
                                                gint         version);
void                crispy_lsp_document_update  (CrispyLspDocument *doc,
                                                const gchar       *text,
                                                gint               version);
void                crispy_lsp_document_free    (CrispyLspDocument *doc);

/* Document store (GHashTable wrapper) */
GHashTable         *crispy_lsp_document_store_new  (void);
CrispyLspDocument  *crispy_lsp_document_store_get  (GHashTable  *store,
                                                    const gchar *uri);
void                crispy_lsp_document_store_open  (GHashTable  *store,
                                                    const gchar *uri,
                                                    const gchar *text,
                                                    gint         version);
void                crispy_lsp_document_store_close (GHashTable  *store,
                                                    const gchar *uri);

G_END_DECLS

#endif /* CRISPY_LSP_DOCUMENT_H */
