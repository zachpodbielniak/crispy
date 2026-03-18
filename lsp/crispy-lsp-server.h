/*
 * Copyright (C) 2026
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/* crispy-lsp-server.h - Main LSP server loop */

#ifndef CRISPY_LSP_SERVER_H
#define CRISPY_LSP_SERVER_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _CrispyLspServer CrispyLspServer;

CrispyLspServer *crispy_lsp_server_new  (void);
gint             crispy_lsp_server_run  (CrispyLspServer *self);
void             crispy_lsp_server_free (CrispyLspServer *self);

G_END_DECLS

#endif /* CRISPY_LSP_SERVER_H */
