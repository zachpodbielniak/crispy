/*
 * Copyright (C) 2026
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/* crispy-lsp-io.h - JSON-RPC 2.0 transport for LSP */

#ifndef CRISPY_LSP_IO_H
#define CRISPY_LSP_IO_H

#include <glib.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

gchar    *crispy_lsp_read_message       (GError **error);
void      crispy_lsp_write_message      (const gchar *json_str);
void      crispy_lsp_send_response      (JsonNode *id, JsonNode *result);
void      crispy_lsp_send_error         (JsonNode *id, gint code,
                                         const gchar *message);
void      crispy_lsp_send_notification  (const gchar *method,
                                         JsonNode *params);

G_END_DECLS

#endif /* CRISPY_LSP_IO_H */
