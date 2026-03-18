/*
 * Copyright (C) 2026
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/* crispy-lsp-clangd.c - Clangd subprocess proxy
 *
 * Manages a clangd child process for LSP intelligence.
 *
 * Regular C files are forwarded to clangd as-is; clangd finds
 * the project's compile_commands.json by walking up from the
 * file's directory.
 *
 * Crispy scripts (shebang + optional CRISPY_PARAMS) get virtual
 * copies with those lines blanked out, plus a generated
 * compile_commands.json in a temp directory so clangd knows the
 * correct compiler flags.  Because we replace lines with empty
 * lines (preserving line count), positions need no translation —
 * only URIs are rewritten.
 */

#include "crispy-lsp-clangd.h"
#include "crispy-lsp-io.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

/* ── virtual-document bookkeeping ─────────────────────────────────── */

typedef struct
{
    gchar    *orig_uri;
    gchar    *virt_path;
    gchar    *virt_uri;
    gchar    *extra_flags;   /* shell-expanded CRISPY_PARAMS */
    gboolean  opened;        /* didOpen sent to clangd */
    gint      version;
} CrispyVirtDoc;

struct _CrispyLspClangd
{
    GSubprocess      *proc;
    GOutputStream    *clangd_in;
    GDataInputStream *clangd_dis;

    GHashTable       *virt_docs;     /* orig_uri → CrispyVirtDoc* */
    GHashTable       *virt_to_orig;  /* virt_uri → orig_uri (borrowed) */

    GSList           *pending_notifs; /* buffered notification strings */

    gchar            *workspace_root;
    gchar            *tmpdir;
    gchar            *pkg_cflags;    /* cached pkg-config output */
    gint64            next_id;
    gboolean          running;
};

/* ── helpers: lifecycle ───────────────────────────────────────────── */

static void
virt_doc_free(gpointer data)
{
    CrispyVirtDoc *vd = data;

    if (vd == NULL)
        return;

    if (vd->virt_path != NULL)
        g_unlink(vd->virt_path);

    g_free(vd->orig_uri);
    g_free(vd->virt_path);
    g_free(vd->virt_uri);
    g_free(vd->extra_flags);
    g_free(vd);
}

static gchar *
get_pkg_cflags(void)
{
    gchar *out = NULL;

    g_spawn_command_line_sync(
        "pkg-config --cflags glib-2.0 gobject-2.0 gio-2.0 gmodule-2.0",
        &out, NULL, NULL, NULL);

    if (out != NULL)
        g_strstrip(out);

    return out;
}

static gchar *
expand_crispy_params(const gchar *raw)
{
    gchar *cmd;
    gchar *out = NULL;

    if (raw == NULL || raw[0] == '\0')
        return NULL;

    cmd = g_strdup_printf("/bin/sh -c \"printf '%%s' %s\"", raw);
    g_spawn_command_line_sync(cmd, &out, NULL, NULL, NULL);
    g_free(cmd);

    if (out != NULL)
        g_strstrip(out);

    return out;
}

static gchar *
sanitize_uri_for_filename(const gchar *uri)
{
    GString *s;
    const gchar *p;

    s = g_string_new(NULL);
    for (p = uri; *p != '\0'; p++)
    {
        if (g_ascii_isalnum(*p) || *p == '-' || *p == '_')
            g_string_append_c(s, *p);
        else
            g_string_append_c(s, '_');
    }
    g_string_append(s, ".c");

    return g_string_free(s, FALSE);
}

/* ── helpers: virtual source ──────────────────────────────────────── */

/*
 * build_virtual_source:
 *
 * Returns a copy of the document text with shebang and
 * CRISPY_PARAMS lines replaced by empty lines.
 */
static gchar *
build_virtual_source(CrispyLspDocument *doc)
{
    gchar  **lines;
    GString *result;
    guint    i;

    if (doc->text == NULL)
        return g_strdup("");

    lines  = g_strsplit(doc->text, "\n", -1);
    result = g_string_new(NULL);

    for (i = 0; lines[i] != NULL; i++)
    {
        if (i > 0)
            g_string_append_c(result, '\n');

        if (i == doc->shebang_line || i == doc->params_line)
        {
            /* blank line — preserves 1:1 line mapping */
        }
        else
        {
            g_string_append(result, lines[i]);
        }
    }

    g_strfreev(lines);
    return g_string_free(result, FALSE);
}

/* ── helpers: compile_commands.json ───────────────────────────────── */

static void
generate_compile_commands(CrispyLspClangd *self)
{
    GHashTableIter  iter;
    gpointer        key, value;
    GString        *json;
    gchar          *path;
    gboolean        first;

    json  = g_string_new("[\n");
    first = TRUE;

    g_hash_table_iter_init(&iter, self->virt_docs);
    while (g_hash_table_iter_next(&iter, &key, &value))
    {
        CrispyVirtDoc *vd = value;

        if (!first)
            g_string_append(json, ",\n");
        first = FALSE;

        g_string_append(json, "  {\"directory\": \"");
        g_string_append(json, self->tmpdir);
        g_string_append(json, "\", \"file\": \"");
        g_string_append(json, vd->virt_path);
        g_string_append(json, "\", \"arguments\": [\"gcc\", \"-std=gnu89\"");

        /* default crispy deps */
        if (self->pkg_cflags != NULL && self->pkg_cflags[0] != '\0')
        {
            gchar **flags;
            gchar **f;

            flags = g_strsplit(self->pkg_cflags, " ", -1);
            for (f = flags; *f != NULL; f++)
            {
                if ((*f)[0] == '\0')
                    continue;
                g_string_append(json, ", \"");
                g_string_append(json, *f);
                g_string_append_c(json, '"');
            }
            g_strfreev(flags);
        }

        /* per-file CRISPY_PARAMS */
        if (vd->extra_flags != NULL && vd->extra_flags[0] != '\0')
        {
            gchar **flags;
            gchar **f;

            flags = g_strsplit(vd->extra_flags, " ", -1);
            for (f = flags; *f != NULL; f++)
            {
                if ((*f)[0] == '\0')
                    continue;
                g_string_append(json, ", \"");
                g_string_append(json, *f);
                g_string_append_c(json, '"');
            }
            g_strfreev(flags);
        }

        g_string_append(json, ", \"-c\", \"");
        g_string_append(json, vd->virt_path);
        g_string_append(json, "\"]}");
    }

    g_string_append(json, "\n]\n");

    path = g_build_filename(self->tmpdir, "compile_commands.json", NULL);
    g_file_set_contents(path, json->str, (gssize)json->len, NULL);
    g_free(path);
    g_string_free(json, TRUE);
}

/* ── helpers: clangd I/O ──────────────────────────────────────────── */

static void
send_to_clangd(CrispyLspClangd *self, const gchar *json_str)
{
    gchar *msg;
    gsize  len;
    gsize  msg_len;

    if (self->proc == NULL || json_str == NULL)
        return;

    len     = strlen(json_str);
    msg     = g_strdup_printf("Content-Length: %" G_GSIZE_FORMAT "\r\n\r\n%s",
                              len, json_str);
    msg_len = strlen(msg);

    g_output_stream_write_all(self->clangd_in, msg, msg_len,
                              NULL, NULL, NULL);
    g_output_stream_flush(self->clangd_in, NULL, NULL);
    g_free(msg);
}

static gchar *
read_from_clangd(CrispyLspClangd *self)
{
    gchar *line;
    gsize  line_len;
    gsize  content_length = 0;
    gchar *body;
    gsize  n_read;

    if (self->proc == NULL)
        return NULL;

    /* read headers */
    for (;;)
    {
        line = g_data_input_stream_read_line(self->clangd_dis,
                                             &line_len, NULL, NULL);
        if (line == NULL)
            return NULL;

        /* strip trailing CR */
        if (line_len > 0 && line[line_len - 1] == '\r')
            line[--line_len] = '\0';

        if (line_len == 0)
        {
            g_free(line);
            break;
        }

        if (g_ascii_strncasecmp(line, "Content-Length:", 15) == 0)
            content_length = (gsize)g_ascii_strtoll(
                g_strstrip(line + 15), NULL, 10);

        g_free(line);
    }

    if (content_length == 0)
        return NULL;

    body = g_malloc(content_length + 1);
    g_input_stream_read_all(
        g_filter_input_stream_get_base_stream(
            G_FILTER_INPUT_STREAM(self->clangd_dis)),
        body, content_length, &n_read, NULL, NULL);
    body[n_read] = '\0';

    return body;
}

/* ── helpers: JSON-RPC builders ───────────────────────────────────── */

static gchar *
build_jsonrpc_request(CrispyLspClangd *self,
                      const gchar     *method,
                      JsonNode        *params)
{
    JsonBuilder   *b;
    JsonNode      *root;
    JsonGenerator *gen;
    gchar         *text;

    b = json_builder_new();
    json_builder_begin_object(b);

    json_builder_set_member_name(b, "jsonrpc");
    json_builder_add_string_value(b, "2.0");
    json_builder_set_member_name(b, "id");
    json_builder_add_int_value(b, self->next_id++);
    json_builder_set_member_name(b, "method");
    json_builder_add_string_value(b, method);

    if (params != NULL)
    {
        json_builder_set_member_name(b, "params");
        json_builder_add_value(b, json_node_copy(params));
    }

    json_builder_end_object(b);

    root = json_builder_get_root(b);
    gen  = json_generator_new();
    json_generator_set_root(gen, root);
    text = json_generator_to_data(gen, NULL);

    g_object_unref(gen);
    json_node_unref(root);
    g_object_unref(b);

    return text;
}

static gchar *
build_jsonrpc_notification(const gchar *method, JsonNode *params)
{
    JsonBuilder   *b;
    JsonNode      *root;
    JsonGenerator *gen;
    gchar         *text;

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
    gen  = json_generator_new();
    json_generator_set_root(gen, root);
    text = json_generator_to_data(gen, NULL);

    g_object_unref(gen);
    json_node_unref(root);
    g_object_unref(b);

    return text;
}

/* ── helpers: request / notify clangd ─────────────────────────────── */

/*
 * Send a notification to clangd (no response expected).
 */
static void
clangd_notify(CrispyLspClangd *self,
              const gchar     *method,
              JsonNode        *params)
{
    gchar *json_str;

    json_str = build_jsonrpc_notification(method, params);
    send_to_clangd(self, json_str);
    g_free(json_str);
}

/*
 * Send a request and wait for the matching response.
 * Notifications received while waiting are buffered.
 * Returns the full response JSON string (caller owns).
 */
static gchar *
clangd_request(CrispyLspClangd *self,
               const gchar     *method,
               JsonNode        *params)
{
    gchar      *json_str;
    gchar      *response;
    JsonParser *parser;
    JsonObject *obj;

    json_str = build_jsonrpc_request(self, method, params);
    send_to_clangd(self, json_str);
    g_free(json_str);

    /* read until we get a response (has "id") */
    for (;;)
    {
        response = read_from_clangd(self);
        if (response == NULL)
            return NULL;

        parser = json_parser_new();
        if (!json_parser_load_from_data(parser, response, -1, NULL))
        {
            g_object_unref(parser);
            g_free(response);
            continue;
        }

        obj = json_node_get_object(json_parser_get_root(parser));
        if (json_object_has_member(obj, "id"))
        {
            g_object_unref(parser);
            return response;
        }

        /* notification — buffer it */
        self->pending_notifs = g_slist_append(self->pending_notifs,
                                              response);
        g_object_unref(parser);
    }
}

/* ── helpers: URI translation ─────────────────────────────────────── */

/*
 * For a given original URI, return the virtual URI if a
 * virtual doc exists, otherwise return the original.
 * The returned string is borrowed — do NOT free.
 */
static const gchar *
get_clangd_uri(CrispyLspClangd   *self,
               CrispyLspDocument *doc)
{
    CrispyVirtDoc *vd;

    if (!doc->is_crispy)
        return doc->uri;

    vd = g_hash_table_lookup(self->virt_docs, doc->uri);
    if (vd != NULL)
        return vd->virt_uri;

    return doc->uri;
}

/*
 * Translate a virtual URI back to the original URI.
 * Returns the original if found, else returns uri unchanged.
 * Returned pointer is borrowed.
 */
static const gchar *
translate_uri_from_virtual(CrispyLspClangd *self,
                           const gchar     *uri)
{
    const gchar *orig;

    orig = g_hash_table_lookup(self->virt_to_orig, uri);
    if (orig != NULL)
        return orig;

    return uri;
}

/* ── helpers: translate locations in responses ────────────────────── */

/*
 * Walk a definition response and rewrite virtual URIs.
 * Handles both Location and Location[].
 */
static void
translate_definition_uris(CrispyLspClangd *self, JsonNode *result)
{
    if (result == NULL || json_node_is_null(result))
        return;

    if (JSON_NODE_HOLDS_ARRAY(result))
    {
        JsonArray *arr;
        guint      i;

        arr = json_node_get_array(result);
        for (i = 0; i < json_array_get_length(arr); i++)
        {
            JsonObject  *loc;
            const gchar *uri;
            const gchar *translated;

            loc = json_array_get_object_element(arr, i);
            if (loc == NULL || !json_object_has_member(loc, "uri"))
                continue;

            uri        = json_object_get_string_member(loc, "uri");
            translated = translate_uri_from_virtual(self, uri);
            if (translated != uri)
                json_object_set_string_member(loc, "uri", translated);
        }
    }
    else if (JSON_NODE_HOLDS_OBJECT(result))
    {
        JsonObject  *loc;
        const gchar *uri;
        const gchar *translated;

        loc = json_node_get_object(result);
        if (loc != NULL && json_object_has_member(loc, "uri"))
        {
            uri        = json_object_get_string_member(loc, "uri");
            translated = translate_uri_from_virtual(self, uri);
            if (translated != uri)
                json_object_set_string_member(loc, "uri", translated);
        }
    }
}

/*
 * Translate diagnostic notification URI if it belongs
 * to a virtual file.  Returns a new JSON string (caller owns).
 */
static gchar *
translate_diagnostic_notif(CrispyLspClangd *self,
                           const gchar     *raw_json)
{
    JsonParser    *parser;
    JsonObject    *root_obj;
    JsonObject    *params;
    const gchar   *uri;
    const gchar   *translated;
    gchar         *out;
    JsonGenerator *gen;

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, raw_json, -1, NULL))
    {
        g_object_unref(parser);
        return g_strdup(raw_json);
    }

    root_obj = json_node_get_object(json_parser_get_root(parser));

    /* only handle publishDiagnostics */
    if (!json_object_has_member(root_obj, "method") ||
        strcmp(json_object_get_string_member(root_obj, "method"),
               "textDocument/publishDiagnostics") != 0)
    {
        g_object_unref(parser);
        return g_strdup(raw_json);
    }

    params = json_object_get_object_member(root_obj, "params");
    if (params == NULL || !json_object_has_member(params, "uri"))
    {
        g_object_unref(parser);
        return g_strdup(raw_json);
    }

    uri        = json_object_get_string_member(params, "uri");
    translated = translate_uri_from_virtual(self, uri);
    if (translated != uri)
        json_object_set_string_member(params, "uri", translated);

    gen = json_generator_new();
    json_generator_set_root(gen, json_parser_get_root(parser));
    out = json_generator_to_data(gen, NULL);

    g_object_unref(gen);
    g_object_unref(parser);

    return out;
}

/* ── helpers: build LSP params ────────────────────────────────────── */

static JsonNode *
build_text_document_position(const gchar *uri, guint line, guint col)
{
    JsonBuilder *b;
    JsonNode    *node;

    b = json_builder_new();
    json_builder_begin_object(b);

    json_builder_set_member_name(b, "textDocument");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "uri");
    json_builder_add_string_value(b, uri);
    json_builder_end_object(b);

    json_builder_set_member_name(b, "position");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "line");
    json_builder_add_int_value(b, (gint64)line);
    json_builder_set_member_name(b, "character");
    json_builder_add_int_value(b, (gint64)col);
    json_builder_end_object(b);

    json_builder_end_object(b);

    node = json_builder_get_root(b);
    g_object_unref(b);

    return node;
}

static JsonNode *
build_text_document_item(const gchar *uri,
                         const gchar *text,
                         gint         version)
{
    JsonBuilder *b;
    JsonNode    *node;

    b = json_builder_new();
    json_builder_begin_object(b);

    json_builder_set_member_name(b, "textDocument");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "uri");
    json_builder_add_string_value(b, uri);
    json_builder_set_member_name(b, "languageId");
    json_builder_add_string_value(b, "c");
    json_builder_set_member_name(b, "version");
    json_builder_add_int_value(b, (gint64)version);
    json_builder_set_member_name(b, "text");
    json_builder_add_string_value(b, text);
    json_builder_end_object(b);

    json_builder_end_object(b);

    node = json_builder_get_root(b);
    g_object_unref(b);

    return node;
}

static JsonNode *
build_did_change_params(const gchar *uri,
                        const gchar *text,
                        gint         version)
{
    JsonBuilder *b;
    JsonNode    *node;

    b = json_builder_new();
    json_builder_begin_object(b);

    json_builder_set_member_name(b, "textDocument");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "uri");
    json_builder_add_string_value(b, uri);
    json_builder_set_member_name(b, "version");
    json_builder_add_int_value(b, (gint64)version);
    json_builder_end_object(b);

    json_builder_set_member_name(b, "contentChanges");
    json_builder_begin_array(b);
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "text");
    json_builder_add_string_value(b, text);
    json_builder_end_object(b);
    json_builder_end_array(b);

    json_builder_end_object(b);

    node = json_builder_get_root(b);
    g_object_unref(b);

    return node;
}

static JsonNode *
build_did_close_params(const gchar *uri)
{
    JsonBuilder *b;
    JsonNode    *node;

    b = json_builder_new();
    json_builder_begin_object(b);

    json_builder_set_member_name(b, "textDocument");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "uri");
    json_builder_add_string_value(b, uri);
    json_builder_end_object(b);

    json_builder_end_object(b);

    node = json_builder_get_root(b);
    g_object_unref(b);

    return node;
}

/* ── public: constructor / destructor ─────────────────────────────── */

CrispyLspClangd *
crispy_lsp_clangd_new(const gchar *workspace_root)
{
    CrispyLspClangd *self;

    self = g_new0(CrispyLspClangd, 1);
    self->workspace_root = g_strdup(workspace_root);
    self->virt_docs      = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                  g_free, virt_doc_free);
    self->virt_to_orig   = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                  g_free, NULL);
    self->pending_notifs = NULL;
    self->next_id        = 1;
    self->running        = FALSE;

    return self;
}

/* ── public: start clangd ─────────────────────────────────────────── */

gboolean
crispy_lsp_clangd_start(CrispyLspClangd *self, GError **error)
{
    GSubprocessLauncher *launcher;
    GInputStream        *stdout_stream;
    gchar               *response;
    JsonBuilder         *b;
    JsonNode            *init_params;
    gchar               *json_str;

    /* create temp dir for virtual files */
    self->tmpdir = g_dir_make_tmp("crispy-lsp-XXXXXX", error);
    if (self->tmpdir == NULL)
        return FALSE;

    /* cache pkg-config flags */
    self->pkg_cflags = get_pkg_cflags();

    /* spawn clangd */
    launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE  |
        G_SUBPROCESS_FLAGS_STDOUT_PIPE |
        G_SUBPROCESS_FLAGS_STDERR_SILENCE);

    self->proc = g_subprocess_launcher_spawn(
        launcher, error,
        "clangd",
        "--background-index=false",
        "--clang-tidy=false",
        "--limit-results=0",
        "--log=error",
        NULL);

    g_object_unref(launcher);

    if (self->proc == NULL)
        return FALSE;

    self->clangd_in  = g_subprocess_get_stdin_pipe(self->proc);
    stdout_stream    = g_subprocess_get_stdout_pipe(self->proc);
    self->clangd_dis = g_data_input_stream_new(stdout_stream);
    self->running    = TRUE;

    /* LSP initialize handshake */
    b = json_builder_new();
    json_builder_begin_object(b);

    json_builder_set_member_name(b, "processId");
    json_builder_add_int_value(b, (gint64)getpid());

    json_builder_set_member_name(b, "rootUri");
    if (self->workspace_root != NULL)
        json_builder_add_string_value(b, self->workspace_root);
    else
        json_builder_add_null_value(b);

    json_builder_set_member_name(b, "capabilities");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "textDocument");
    json_builder_begin_object(b);

    json_builder_set_member_name(b, "completion");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "completionItem");
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "snippetSupport");
    json_builder_add_boolean_value(b, FALSE);
    json_builder_end_object(b);
    json_builder_end_object(b);

    json_builder_end_object(b); /* textDocument */
    json_builder_end_object(b); /* capabilities */

    json_builder_end_object(b);

    init_params = json_builder_get_root(b);
    g_object_unref(b);

    json_str = build_jsonrpc_request(self, "initialize", init_params);
    send_to_clangd(self, json_str);
    g_free(json_str);
    json_node_unref(init_params);

    /* read and discard initialize response */
    response = read_from_clangd(self);
    g_free(response);

    /* send initialized notification */
    clangd_notify(self, "initialized", NULL);

    return TRUE;
}

/* ── public: stop clangd ──────────────────────────────────────────── */

void
crispy_lsp_clangd_stop(CrispyLspClangd *self)
{
    gchar *response;
    gchar *json_str;

    if (!self->running || self->proc == NULL)
        return;

    /* LSP shutdown sequence */
    json_str = build_jsonrpc_request(self, "shutdown", NULL);
    send_to_clangd(self, json_str);
    g_free(json_str);

    response = read_from_clangd(self);
    g_free(response);

    clangd_notify(self, "exit", NULL);

    g_subprocess_force_exit(self->proc);
    g_subprocess_wait(self->proc, NULL, NULL);

    self->running = FALSE;
}

void
crispy_lsp_clangd_free(CrispyLspClangd *self)
{
    if (self == NULL)
        return;

    crispy_lsp_clangd_stop(self);

    g_clear_object(&self->proc);
    g_clear_object(&self->clangd_dis);
    /* clangd_in is owned by the subprocess — do not unref */

    g_hash_table_destroy(self->virt_docs);
    g_hash_table_destroy(self->virt_to_orig);

    g_slist_free_full(self->pending_notifs, g_free);

    if (self->tmpdir != NULL)
    {
        /* remove compile_commands.json */
        gchar *cc = g_build_filename(self->tmpdir,
                                     "compile_commands.json", NULL);
        g_unlink(cc);
        g_free(cc);
        g_rmdir(self->tmpdir);
    }

    g_free(self->workspace_root);
    g_free(self->tmpdir);
    g_free(self->pkg_cflags);
    g_free(self);
}

/* ── public: document sync ────────────────────────────────────────── */

void
crispy_lsp_clangd_sync_document(CrispyLspClangd   *self,
                                CrispyLspDocument  *doc)
{
    if (!self->running)
        return;

    if (doc->is_crispy)
    {
        /* virtual file path */
        CrispyVirtDoc *vd;
        gchar         *virt_src;
        JsonNode      *params;

        vd = g_hash_table_lookup(self->virt_docs, doc->uri);

        if (vd == NULL)
        {
            gchar *fname;

            vd            = g_new0(CrispyVirtDoc, 1);
            vd->orig_uri  = g_strdup(doc->uri);

            fname          = sanitize_uri_for_filename(doc->uri);
            vd->virt_path  = g_build_filename(self->tmpdir, fname, NULL);
            vd->virt_uri   = g_strdup_printf("file://%s", vd->virt_path);
            vd->opened     = FALSE;

            g_free(fname);

            g_hash_table_replace(self->virt_docs,
                                 g_strdup(doc->uri), vd);
            g_hash_table_replace(self->virt_to_orig,
                                 g_strdup(vd->virt_uri),
                                 vd->orig_uri);
        }

        /* update extra flags */
        g_free(vd->extra_flags);
        vd->extra_flags = expand_crispy_params(doc->crispy_params);
        vd->version     = doc->version;

        /* build and write virtual source */
        virt_src = build_virtual_source(doc);
        g_file_set_contents(vd->virt_path, virt_src, -1, NULL);

        /* regenerate compile_commands.json */
        generate_compile_commands(self);

        /* send to clangd */
        if (!vd->opened)
        {
            params = build_text_document_item(vd->virt_uri,
                                              virt_src,
                                              doc->version);
            clangd_notify(self, "textDocument/didOpen", params);
            json_node_unref(params);
            vd->opened = TRUE;
        }
        else
        {
            params = build_did_change_params(vd->virt_uri,
                                             virt_src,
                                             doc->version);
            clangd_notify(self, "textDocument/didChange", params);
            json_node_unref(params);
        }

        g_free(virt_src);
    }
    else
    {
        /* regular C file — forward as-is */
        JsonNode *params;

        params = build_text_document_item(doc->uri,
                                          doc->text,
                                          doc->version);
        clangd_notify(self, "textDocument/didOpen", params);
        json_node_unref(params);
    }
}

void
crispy_lsp_clangd_close_document(CrispyLspClangd *self,
                                 const gchar     *uri)
{
    CrispyVirtDoc *vd;
    JsonNode      *params;
    const gchar   *close_uri;

    if (!self->running)
        return;

    vd = g_hash_table_lookup(self->virt_docs, uri);
    if (vd != NULL)
    {
        close_uri = vd->virt_uri;
        params    = build_did_close_params(close_uri);
        clangd_notify(self, "textDocument/didClose", params);
        json_node_unref(params);

        g_hash_table_remove(self->virt_to_orig, vd->virt_uri);
        g_hash_table_remove(self->virt_docs, uri);
        generate_compile_commands(self);
    }
    else
    {
        params = build_did_close_params(uri);
        clangd_notify(self, "textDocument/didClose", params);
        json_node_unref(params);
    }
}

/* ── public: LSP methods ──────────────────────────────────────────── */

JsonNode *
crispy_lsp_clangd_completion(CrispyLspClangd   *self,
                             CrispyLspDocument  *doc,
                             guint               line,
                             guint               col)
{
    const gchar *uri;
    JsonNode    *params;
    gchar       *response;
    JsonParser  *parser;
    JsonObject  *obj;
    JsonNode    *result = NULL;

    if (!self->running)
        return NULL;

    uri    = get_clangd_uri(self, doc);
    params = build_text_document_position(uri, line, col);

    response = clangd_request(self, "textDocument/completion", params);
    json_node_unref(params);

    if (response == NULL)
        return NULL;

    parser = json_parser_new();
    if (json_parser_load_from_data(parser, response, -1, NULL))
    {
        obj = json_node_get_object(json_parser_get_root(parser));
        if (json_object_has_member(obj, "result"))
            result = json_node_copy(json_object_get_member(obj, "result"));
    }

    g_object_unref(parser);
    g_free(response);

    return result;
}

JsonNode *
crispy_lsp_clangd_hover(CrispyLspClangd   *self,
                        CrispyLspDocument  *doc,
                        guint               line,
                        guint               col)
{
    const gchar *uri;
    JsonNode    *params;
    gchar       *response;
    JsonParser  *parser;
    JsonObject  *obj;
    JsonNode    *result = NULL;

    if (!self->running)
        return NULL;

    uri    = get_clangd_uri(self, doc);
    params = build_text_document_position(uri, line, col);

    response = clangd_request(self, "textDocument/hover", params);
    json_node_unref(params);

    if (response == NULL)
        return NULL;

    parser = json_parser_new();
    if (json_parser_load_from_data(parser, response, -1, NULL))
    {
        obj = json_node_get_object(json_parser_get_root(parser));
        if (json_object_has_member(obj, "result"))
            result = json_node_copy(json_object_get_member(obj, "result"));
    }

    g_object_unref(parser);
    g_free(response);

    return result;
}

JsonNode *
crispy_lsp_clangd_definition(CrispyLspClangd   *self,
                             CrispyLspDocument  *doc,
                             guint               line,
                             guint               col)
{
    const gchar *uri;
    JsonNode    *params;
    gchar       *response;
    JsonParser  *parser;
    JsonObject  *obj;
    JsonNode    *result = NULL;

    if (!self->running)
        return NULL;

    uri    = get_clangd_uri(self, doc);
    params = build_text_document_position(uri, line, col);

    response = clangd_request(self, "textDocument/definition", params);
    json_node_unref(params);

    if (response == NULL)
        return NULL;

    parser = json_parser_new();
    if (json_parser_load_from_data(parser, response, -1, NULL))
    {
        obj = json_node_get_object(json_parser_get_root(parser));
        if (json_object_has_member(obj, "result"))
        {
            result = json_node_copy(
                json_object_get_member(obj, "result"));
            translate_definition_uris(self, result);
        }
    }

    g_object_unref(parser);
    g_free(response);

    return result;
}

/* ── public: flush diagnostics ────────────────────────────────────── */

GSList *
crispy_lsp_clangd_flush_diagnostics(CrispyLspClangd *self)
{
    GSList *translated = NULL;
    GSList *l;

    for (l = self->pending_notifs; l != NULL; l = l->next)
    {
        gchar *raw  = l->data;
        gchar *xlat = translate_diagnostic_notif(self, raw);
        translated  = g_slist_append(translated, xlat);
        g_free(raw);
    }

    g_slist_free(self->pending_notifs);
    self->pending_notifs = NULL;

    return translated;
}
