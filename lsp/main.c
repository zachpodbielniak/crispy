/*
 * Copyright (C) 2026
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/* main.c - crispy-language-server entry point */

#include "crispy-lsp-server.h"

#include <glib.h>
#include <locale.h>
#include <stdio.h>

#define VERSION "0.1.0"

int
main(int argc, char **argv)
{
    GOptionContext  *ctx;
    GError          *error = NULL;
    gboolean         show_version = FALSE;
    CrispyLspServer *server;
    gint             ret;

    GOptionEntry entries[] = {
        { "version", 'V', 0, G_OPTION_ARG_NONE, &show_version,
          "Show version", NULL },
        { NULL }
    };

    setlocale(LC_ALL, "");

    ctx = g_option_context_new("- Crispy Language Server");
    g_option_context_add_main_entries(ctx, entries, NULL);
    g_option_context_set_description(ctx,
        "LSP server for crispy C scripts.\n"
        "Proxies to clangd with crispy-aware shebang and\n"
        "CRISPY_PARAMS handling.\n\n"
        "License: AGPL-3.0-or-later");

    if (!g_option_context_parse(ctx, &argc, &argv, &error))
    {
        fprintf(stderr, "crispy-language-server: %s\n", error->message);
        g_error_free(error);
        g_option_context_free(ctx);
        return 1;
    }
    g_option_context_free(ctx);

    if (show_version)
    {
        fprintf(stdout, "crispy-language-server %s\n", VERSION);
        return 0;
    }

    server = crispy_lsp_server_new();
    ret    = crispy_lsp_server_run(server);
    crispy_lsp_server_free(server);

    return ret;
}
