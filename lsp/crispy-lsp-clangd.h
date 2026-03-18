/*
 * Copyright (C) 2026
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/* crispy-lsp-clangd.h - Clangd subprocess proxy */

#ifndef CRISPY_LSP_CLANGD_H
#define CRISPY_LSP_CLANGD_H

#include <glib.h>
#include <json-glib/json-glib.h>

#include "crispy-lsp-document.h"

G_BEGIN_DECLS

typedef struct _CrispyLspClangd CrispyLspClangd;

CrispyLspClangd *crispy_lsp_clangd_new            (const gchar *workspace_root);
gboolean         crispy_lsp_clangd_start           (CrispyLspClangd  *self,
                                                    GError          **error);
void             crispy_lsp_clangd_stop            (CrispyLspClangd *self);
void             crispy_lsp_clangd_free            (CrispyLspClangd *self);

void             crispy_lsp_clangd_sync_document   (CrispyLspClangd   *self,
                                                    CrispyLspDocument *doc);
void             crispy_lsp_clangd_close_document  (CrispyLspClangd *self,
                                                    const gchar     *uri);

JsonNode        *crispy_lsp_clangd_completion      (CrispyLspClangd   *self,
                                                    CrispyLspDocument *doc,
                                                    guint              line,
                                                    guint              col);
JsonNode        *crispy_lsp_clangd_hover           (CrispyLspClangd   *self,
                                                    CrispyLspDocument *doc,
                                                    guint              line,
                                                    guint              col);
JsonNode        *crispy_lsp_clangd_definition      (CrispyLspClangd   *self,
                                                    CrispyLspDocument *doc,
                                                    guint              line,
                                                    guint              col);

/* Flush pending diagnostics — returns GSList of gchar* (JSON strings).
 * Caller owns the list and strings. */
GSList          *crispy_lsp_clangd_flush_diagnostics (CrispyLspClangd *self);

G_END_DECLS

#endif /* CRISPY_LSP_CLANGD_H */
