/*
 * Copyright (C) 2026
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/* crispy-lsp-server.c - Main LSP server loop and request dispatch
 *
 * Reads JSON-RPC messages from stdin, dispatches to the appropriate
 * handler, and writes responses to stdout.  Uses a clangd subprocess
 * for all C intelligence (completion, hover, go-to-definition, and
 * diagnostics).
 */

#include "crispy-lsp-server.h"
#include "crispy-lsp-io.h"
#include "crispy-lsp-document.h"
#include "crispy-lsp-clangd.h"

#include <string.h>

/* ── LSP error codes ──────────────────────────────────────────────── */

#define LSP_ERROR_METHOD_NOT_FOUND  (-32601)

/* ── server state ─────────────────────────────────────────────────── */

struct _CrispyLspServer
{
    GHashTable      *docs;         /* document store */
    CrispyLspClangd *clangd;      /* clangd proxy   */
    gchar           *root_uri;    /* workspace root  */
    gboolean         initialized;
    gboolean         shutdown;
};

/* ── helpers ──────────────────────────────────────────────────────── */

static void
flush_diagnostics(CrispyLspServer *self)
{
    GSList *diags;
    GSList *l;

    if (self->clangd == NULL)
        return;

    diags = crispy_lsp_clangd_flush_diagnostics(self->clangd);
    for (l = diags; l != NULL; l = l->next)
    {
        gchar *json_str = l->data;
        crispy_lsp_write_message(json_str);
        g_free(json_str);
    }
    g_slist_free(diags);
}

/* ── handler: initialize ──────────────────────────────────────────── */

static void
handle_initialize(CrispyLspServer *self,
                  JsonNode        *id,
                  JsonObject      *params)
{
    JsonBuilder *b;
    JsonNode    *result;
    GError      *error = NULL;

    /* extract rootUri */
    if (json_object_has_member(params, "rootUri") &&
        !json_object_get_null_member(params, "rootUri"))
    {
        self->root_uri = g_strdup(
            json_object_get_string_member(params, "rootUri"));
    }

    /* start clangd */
    self->clangd = crispy_lsp_clangd_new(self->root_uri);
    if (!crispy_lsp_clangd_start(self->clangd, &error))
    {
        g_warning("crispy-lsp: failed to start clangd: %s",
                  error != NULL ? error->message : "unknown");
        g_clear_error(&error);
    }

    /* build capabilities */
    b = json_builder_new();
    json_builder_begin_object(b);

    json_builder_set_member_name(b, "capabilities");
    json_builder_begin_object(b);

    /* full document sync */
    json_builder_set_member_name(b, "textDocumentSync");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "openClose");
    json_builder_add_boolean_value(b, TRUE);
    json_builder_set_member_name(b, "change");
    json_builder_add_int_value(b, 1); /* Full */
    json_builder_end_object(b);

    /* completion */
    json_builder_set_member_name(b, "completionProvider");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "triggerCharacters");
    json_builder_begin_array(b);
    json_builder_add_string_value(b, ".");
    json_builder_add_string_value(b, ">");
    json_builder_add_string_value(b, ":");
    json_builder_end_array(b);
    json_builder_end_object(b);

    /* hover */
    json_builder_set_member_name(b, "hoverProvider");
    json_builder_add_boolean_value(b, TRUE);

    /* definition */
    json_builder_set_member_name(b, "definitionProvider");
    json_builder_add_boolean_value(b, TRUE);

    json_builder_end_object(b); /* capabilities */

    json_builder_set_member_name(b, "serverInfo");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "name");
    json_builder_add_string_value(b, "crispy-language-server");
    json_builder_set_member_name(b, "version");
    json_builder_add_string_value(b, "0.1.0");
    json_builder_end_object(b);

    json_builder_end_object(b);

    result = json_builder_get_root(b);
    g_object_unref(b);

    crispy_lsp_send_response(id, result);
    json_node_unref(result);

    self->initialized = TRUE;
}

/* ── handler: shutdown ────────────────────────────────────────────── */

static void
handle_shutdown(CrispyLspServer *self, JsonNode *id)
{
    self->shutdown = TRUE;
    crispy_lsp_send_response(id, NULL);
}

/* ── handler: textDocument/didOpen ────────────────────────────────── */

static void
handle_did_open(CrispyLspServer *self, JsonObject *params)
{
    JsonObject      *td;
    const gchar     *uri;
    const gchar     *text;
    gint64           version;
    CrispyLspDocument *doc;

    td      = json_object_get_object_member(params, "textDocument");
    uri     = json_object_get_string_member(td, "uri");
    text    = json_object_get_string_member(td, "text");
    version = json_object_get_int_member(td, "version");

    crispy_lsp_document_store_open(self->docs, uri, text,
                                   (gint)version);

    doc = crispy_lsp_document_store_get(self->docs, uri);
    if (doc != NULL && self->clangd != NULL)
        crispy_lsp_clangd_sync_document(self->clangd, doc);
}

/* ── handler: textDocument/didChange ──────────────────────────────── */

static void
handle_did_change(CrispyLspServer *self, JsonObject *params)
{
    JsonObject      *td;
    JsonArray       *changes;
    JsonObject      *change;
    const gchar     *uri;
    const gchar     *text;
    gint64           version;
    CrispyLspDocument *doc;

    td      = json_object_get_object_member(params, "textDocument");
    uri     = json_object_get_string_member(td, "uri");
    version = json_object_get_int_member(td, "version");

    changes = json_object_get_array_member(params, "contentChanges");
    change  = json_array_get_object_element(changes, 0);
    text    = json_object_get_string_member(change, "text");

    doc = crispy_lsp_document_store_get(self->docs, uri);
    if (doc != NULL)
    {
        crispy_lsp_document_update(doc, text, (gint)version);
        if (self->clangd != NULL)
            crispy_lsp_clangd_sync_document(self->clangd, doc);
    }
}

/* ── handler: textDocument/didClose ───────────────────────────────── */

static void
handle_did_close(CrispyLspServer *self, JsonObject *params)
{
    JsonObject  *td;
    const gchar *uri;

    td  = json_object_get_object_member(params, "textDocument");
    uri = json_object_get_string_member(td, "uri");

    if (self->clangd != NULL)
        crispy_lsp_clangd_close_document(self->clangd, uri);

    crispy_lsp_document_store_close(self->docs, uri);
}

/* ── handler: textDocument/completion ─────────────────────────────── */

static void
handle_completion(CrispyLspServer *self,
                  JsonNode        *id,
                  JsonObject      *params)
{
    JsonObject        *td;
    JsonObject        *pos;
    const gchar       *uri;
    guint              line, col;
    CrispyLspDocument *doc;
    JsonNode          *result;

    td  = json_object_get_object_member(params, "textDocument");
    uri = json_object_get_string_member(td, "uri");
    pos = json_object_get_object_member(params, "position");
    line = (guint)json_object_get_int_member(pos, "line");
    col  = (guint)json_object_get_int_member(pos, "character");

    doc = crispy_lsp_document_store_get(self->docs, uri);
    if (doc == NULL || self->clangd == NULL)
    {
        crispy_lsp_send_response(id, NULL);
        return;
    }

    result = crispy_lsp_clangd_completion(self->clangd, doc, line, col);
    crispy_lsp_send_response(id, result);
    if (result != NULL)
        json_node_unref(result);
}

/* ── handler: textDocument/hover ──────────────────────────────────── */

static void
handle_hover(CrispyLspServer *self,
             JsonNode        *id,
             JsonObject      *params)
{
    JsonObject        *td;
    JsonObject        *pos;
    const gchar       *uri;
    guint              line, col;
    CrispyLspDocument *doc;
    JsonNode          *result;

    td  = json_object_get_object_member(params, "textDocument");
    uri = json_object_get_string_member(td, "uri");
    pos = json_object_get_object_member(params, "position");
    line = (guint)json_object_get_int_member(pos, "line");
    col  = (guint)json_object_get_int_member(pos, "character");

    doc = crispy_lsp_document_store_get(self->docs, uri);
    if (doc == NULL || self->clangd == NULL)
    {
        crispy_lsp_send_response(id, NULL);
        return;
    }

    result = crispy_lsp_clangd_hover(self->clangd, doc, line, col);
    crispy_lsp_send_response(id, result);
    if (result != NULL)
        json_node_unref(result);
}

/* ── handler: textDocument/definition ─────────────────────────────── */

static void
handle_definition(CrispyLspServer *self,
                  JsonNode        *id,
                  JsonObject      *params)
{
    JsonObject        *td;
    JsonObject        *pos;
    const gchar       *uri;
    guint              line, col;
    CrispyLspDocument *doc;
    JsonNode          *result;

    td  = json_object_get_object_member(params, "textDocument");
    uri = json_object_get_string_member(td, "uri");
    pos = json_object_get_object_member(params, "position");
    line = (guint)json_object_get_int_member(pos, "line");
    col  = (guint)json_object_get_int_member(pos, "character");

    doc = crispy_lsp_document_store_get(self->docs, uri);
    if (doc == NULL || self->clangd == NULL)
    {
        crispy_lsp_send_response(id, NULL);
        return;
    }

    result = crispy_lsp_clangd_definition(self->clangd, doc, line, col);
    crispy_lsp_send_response(id, result);
    if (result != NULL)
        json_node_unref(result);
}

/* ── message dispatch ─────────────────────────────────────────────── */

static void
handle_message(CrispyLspServer *self, const gchar *raw)
{
    JsonParser  *parser;
    JsonObject  *root;
    JsonNode    *id_node;
    JsonObject  *params;
    const gchar *method;

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, raw, -1, NULL))
    {
        g_object_unref(parser);
        return;
    }

    root = json_node_get_object(json_parser_get_root(parser));

    /* exit notification — handled even without id */
    if (json_object_has_member(root, "method"))
    {
        method = json_object_get_string_member(root, "method");

        if (strcmp(method, "exit") == 0)
        {
            self->shutdown = TRUE;
            g_object_unref(parser);
            return;
        }
    }
    else
    {
        /* not a request or notification */
        g_object_unref(parser);
        return;
    }

    method = json_object_get_string_member(root, "method");

    /* requests have "id", notifications do not */
    id_node = json_object_has_member(root, "id")
                  ? json_object_get_member(root, "id")
                  : NULL;

    params = json_object_has_member(root, "params")
                 ? json_object_get_object_member(root, "params")
                 : NULL;

    /* notifications (no id) */
    if (id_node == NULL)
    {
        if (strcmp(method, "initialized") == 0)
        {
            /* nothing to do */
        }
        else if (strcmp(method, "textDocument/didOpen") == 0 &&
                 params != NULL)
        {
            handle_did_open(self, params);
        }
        else if (strcmp(method, "textDocument/didChange") == 0 &&
                 params != NULL)
        {
            handle_did_change(self, params);
        }
        else if (strcmp(method, "textDocument/didClose") == 0 &&
                 params != NULL)
        {
            handle_did_close(self, params);
        }
    }
    /* requests (have id) */
    else
    {
        if (strcmp(method, "initialize") == 0 && params != NULL)
        {
            handle_initialize(self, id_node, params);
        }
        else if (strcmp(method, "shutdown") == 0)
        {
            handle_shutdown(self, id_node);
        }
        else if (strcmp(method, "textDocument/completion") == 0 &&
                 params != NULL)
        {
            handle_completion(self, id_node, params);
        }
        else if (strcmp(method, "textDocument/hover") == 0 &&
                 params != NULL)
        {
            handle_hover(self, id_node, params);
        }
        else if (strcmp(method, "textDocument/definition") == 0 &&
                 params != NULL)
        {
            handle_definition(self, id_node, params);
        }
        else
        {
            crispy_lsp_send_error(id_node,
                                  LSP_ERROR_METHOD_NOT_FOUND,
                                  "Method not supported");
        }
    }

    g_object_unref(parser);
}

/* ── public API ───────────────────────────────────────────────────── */

CrispyLspServer *
crispy_lsp_server_new(void)
{
    CrispyLspServer *self;

    self = g_new0(CrispyLspServer, 1);
    self->docs        = crispy_lsp_document_store_new();
    self->clangd      = NULL;
    self->root_uri    = NULL;
    self->initialized = FALSE;
    self->shutdown    = FALSE;

    return self;
}

gint
crispy_lsp_server_run(CrispyLspServer *self)
{
    while (!self->shutdown)
    {
        GError *error = NULL;
        gchar  *msg;

        msg = crispy_lsp_read_message(&error);
        if (msg == NULL)
        {
            g_clear_error(&error);
            break;
        }

        handle_message(self, msg);
        g_free(msg);

        /* forward any diagnostics from clangd */
        flush_diagnostics(self);
    }

    return 0;
}

void
crispy_lsp_server_free(CrispyLspServer *self)
{
    if (self == NULL)
        return;

    crispy_lsp_clangd_free(self->clangd);
    g_hash_table_destroy(self->docs);
    g_free(self->root_uri);
    g_free(self);
}
