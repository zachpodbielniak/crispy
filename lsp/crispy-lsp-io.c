/*
 * Copyright (C) 2026
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/* crispy-lsp-io.c - JSON-RPC 2.0 transport for LSP */

#include "crispy-lsp-io.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

gchar *
crispy_lsp_read_message(GError **error)
{
    char hdr[512];
    gint content_length = -1;
    gchar *body;
    gsize n_read;

    /* read headers until blank line */
    while (fgets(hdr, sizeof(hdr), stdin) != NULL)
    {
        /* strip CRLF / LF */
        gsize len = strlen(hdr);
        while (len > 0 && (hdr[len - 1] == '\r' || hdr[len - 1] == '\n'))
            hdr[--len] = '\0';

        if (len == 0)
            break;  /* blank line — end of headers */

        if (g_ascii_strncasecmp(hdr, "Content-Length:", 15) == 0)
            content_length = atoi(hdr + 15);
    }

    if (content_length < 0)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "Missing Content-Length header (EOF?)");
        return NULL;
    }

    body = g_malloc((gsize)content_length + 1);
    n_read = fread(body, 1, (gsize)content_length, stdin);
    body[n_read] = '\0';

    if ((gint)n_read != content_length)
    {
        g_free(body);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "Short read on message body");
        return NULL;
    }

    return body;
}

void
crispy_lsp_write_message(const gchar *json_str)
{
    gsize len;

    if (json_str == NULL)
        return;

    len = strlen(json_str);
    fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", len, json_str);
    fflush(stdout);
}

static void
send_json_node(JsonNode *root)
{
    JsonGenerator *gen;
    gchar *text;

    gen = json_generator_new();
    json_generator_set_root(gen, root);
    text = json_generator_to_data(gen, NULL);
    g_object_unref(gen);

    crispy_lsp_write_message(text);
    g_free(text);
}

void
crispy_lsp_send_response(JsonNode *id, JsonNode *result)
{
    JsonBuilder *b;
    JsonNode *root;

    b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "jsonrpc");
    json_builder_add_string_value(b, "2.0");
    json_builder_set_member_name(b, "id");
    json_builder_add_value(b, json_node_copy(id));
    json_builder_set_member_name(b, "result");
    json_builder_add_value(b, result != NULL ? json_node_copy(result) : json_node_new(JSON_NODE_NULL));
    json_builder_end_object(b);

    root = json_builder_get_root(b);
    send_json_node(root);
    json_node_unref(root);
    g_object_unref(b);
}

void
crispy_lsp_send_error(JsonNode *id, gint code, const gchar *message)
{
    JsonBuilder *b;
    JsonNode *root;

    b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "jsonrpc");
    json_builder_add_string_value(b, "2.0");
    json_builder_set_member_name(b, "id");
    json_builder_add_value(b, json_node_copy(id));
    json_builder_set_member_name(b, "error");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "code");
    json_builder_add_int_value(b, code);
    json_builder_set_member_name(b, "message");
    json_builder_add_string_value(b, message);
    json_builder_end_object(b);
    json_builder_end_object(b);

    root = json_builder_get_root(b);
    send_json_node(root);
    json_node_unref(root);
    g_object_unref(b);
}

void
crispy_lsp_send_notification(const gchar *method, JsonNode *params)
{
    JsonBuilder *b;
    JsonNode *root;

    b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "jsonrpc");
    json_builder_add_string_value(b, "2.0");
    json_builder_set_member_name(b, "method");
    json_builder_add_string_value(b, method);
    if (params != NULL)
    {
        json_builder_set_member_name(b, "params");
        json_builder_add_value(b, json_node_copy(params));
    }
    json_builder_end_object(b);

    root = json_builder_get_root(b);
    send_json_node(root);
    json_node_unref(root);
    g_object_unref(b);
}
