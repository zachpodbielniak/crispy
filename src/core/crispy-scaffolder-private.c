/* crispy-scaffolder-private.c - Script scaffolding utilities */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Generates new crispy script files from built-in templates.  Each
 * template provides a ready-to-run .c file with shebang, boilerplate
 * includes, and a minimal main() so the user can start coding
 * immediately without hand-writing the scaffolding themselves.
 */

#define CRISPY_COMPILATION
#include "crispy-scaffolder-private.h"
#include "../crispy-types.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Template strings                                                     */
/* ------------------------------------------------------------------ */

/*
 * "minimal" — bare minimum: shebang + glib include + empty main.
 * Useful when the user wants a clean slate without any boilerplate.
 */
static const gchar TEMPLATE_MINIMAL[] =
    "#!/usr/bin/env crispy\n"
    "#include <glib.h>\n"
    "\n"
    "int\n"
    "main (int argc, char **argv)\n"
    "{\n"
    "\treturn 0;\n"
    "}\n";

/*
 * "glib" (default) — shebang with CRISPY_PARAMS driven by pkg-config,
 * plus a GOptionContext-based main() that handles --help automatically.
 */
static const gchar TEMPLATE_GLIB[] =
    "#!/usr/bin/env crispy\n"
    "#define CRISPY_PARAMS \"$(pkg-config --cflags --libs glib-2.0 gobject-2.0 gio-2.0)\"\n"
    "#include <glib.h>\n"
    "#include <gio/gio.h>\n"
    "\n"
    "static GOptionEntry entries[] = {\n"
    "\t{ NULL }\n"
    "};\n"
    "\n"
    "int\n"
    "main (int argc, char **argv)\n"
    "{\n"
    "\tg_autoptr(GOptionContext) ctx = NULL;\n"
    "\tg_autoptr(GError) error = NULL;\n"
    "\n"
    "\tctx = g_option_context_new (NULL);\n"
    "\tg_option_context_add_main_entries (ctx, entries, NULL);\n"
    "\n"
    "\tif (!g_option_context_parse (ctx, &argc, &argv, &error))\n"
    "\t{\n"
    "\t\tg_printerr (\"error: %s\\n\", error->message);\n"
    "\t\treturn 1;\n"
    "\t}\n"
    "\n"
    "\treturn 0;\n"
    "}\n";

/*
 * "gtk" — shebang with CRISPY_USE for gtk4 and a minimal GtkApplication
 * pattern with activate signal.
 */
static const gchar TEMPLATE_GTK[] =
    "#!/usr/bin/env crispy\n"
    "#define CRISPY_USE \"gtk4\"\n"
    "#include <gtk/gtk.h>\n"
    "\n"
    "static void\n"
    "on_activate (GtkApplication *app,\n"
    "             gpointer        user_data)\n"
    "{\n"
    "\tGtkWidget *window;\n"
    "\n"
    "\twindow = gtk_application_window_new (app);\n"
    "\tgtk_window_set_title (GTK_WINDOW (window), \"My App\");\n"
    "\tgtk_window_set_default_size (GTK_WINDOW (window), 400, 300);\n"
    "\tgtk_window_present (GTK_WINDOW (window));\n"
    "}\n"
    "\n"
    "int\n"
    "main (int argc, char **argv)\n"
    "{\n"
    "\tg_autoptr(GtkApplication) app = NULL;\n"
    "\tint status;\n"
    "\n"
    "\tapp = gtk_application_new (\"org.example.MyApp\", G_APPLICATION_DEFAULT_FLAGS);\n"
    "\tg_signal_connect (app, \"activate\", G_CALLBACK (on_activate), NULL);\n"
    "\tstatus = g_application_run (G_APPLICATION (app), argc, argv);\n"
    "\n"
    "\treturn status;\n"
    "}\n";

/*
 * "cli" — GOptionContext with --help and --version, plus structured
 * error handling for command-line tools.
 */
static const gchar TEMPLATE_CLI[] =
    "#!/usr/bin/env crispy\n"
    "#define CRISPY_PARAMS \"$(pkg-config --cflags --libs glib-2.0 gio-2.0)\"\n"
    "#include <glib.h>\n"
    "#include <gio/gio.h>\n"
    "\n"
    "#define PROGRAM_NAME    \"my-tool\"\n"
    "#define PROGRAM_VERSION \"0.1.0\"\n"
    "\n"
    "static gboolean opt_version = FALSE;\n"
    "\n"
    "static GOptionEntry entries[] = {\n"
    "\t{ \"version\", 'V', 0, G_OPTION_ARG_NONE, &opt_version,\n"
    "\t  \"Print version information and exit\", NULL },\n"
    "\t{ NULL }\n"
    "};\n"
    "\n"
    "int\n"
    "main (int argc, char **argv)\n"
    "{\n"
    "\tg_autoptr(GOptionContext) ctx = NULL;\n"
    "\tg_autoptr(GError) error = NULL;\n"
    "\n"
    "\tctx = g_option_context_new (\"[OPTIONS]\");\n"
    "\tg_option_context_set_summary (ctx, \"A crispy command-line tool.\");\n"
    "\tg_option_context_add_main_entries (ctx, entries, NULL);\n"
    "\n"
    "\tif (!g_option_context_parse (ctx, &argc, &argv, &error))\n"
    "\t{\n"
    "\t\tg_printerr (\"%s: %s\\n\", PROGRAM_NAME, error->message);\n"
    "\t\tg_printerr (\"Try '%s --help' for more information.\\n\", PROGRAM_NAME);\n"
    "\t\treturn 1;\n"
    "\t}\n"
    "\n"
    "\tif (opt_version)\n"
    "\t{\n"
    "\t\tg_print (\"%s %s\\n\", PROGRAM_NAME, PROGRAM_VERSION);\n"
    "\t\treturn 0;\n"
    "\t}\n"
    "\n"
    "\treturn 0;\n"
    "}\n";

/* ------------------------------------------------------------------ */
/* Template table                                                       */
/* ------------------------------------------------------------------ */

static const gchar *TEMPLATE_NAMES[] = {
    "minimal",
    "glib",
    "gtk",
    "cli",
    NULL
};

static const gchar *TEMPLATE_BODIES[] = {
    TEMPLATE_MINIMAL,
    TEMPLATE_GLIB,
    TEMPLATE_GTK,
    TEMPLATE_CLI,
    NULL
};

/* ------------------------------------------------------------------ */
/* crispy_scaffolder_list_templates                                     */
/* ------------------------------------------------------------------ */

const gchar * const *
crispy_scaffolder_list_templates (void)
{
    return (const gchar * const *)TEMPLATE_NAMES;
}

/* ------------------------------------------------------------------ */
/* crispy_scaffolder_create                                             */
/* ------------------------------------------------------------------ */

/*
 * look_up_template:
 * @name: template name, or %NULL to use the default
 *
 * Returns the template body string for the given name.
 * Falls back to the "glib" template if @name is %NULL or unrecognised.
 */
static const gchar *
look_up_template (const gchar *name)
{
    gint i;

    if (name == NULL)
        name = "glib";

    for (i = 0; TEMPLATE_NAMES[i] != NULL; i++)
    {
        if (g_strcmp0(TEMPLATE_NAMES[i], name) == 0)
            return TEMPLATE_BODIES[i];
    }

    /* unrecognised name — fall back to "glib" */
    return TEMPLATE_GLIB;
}

gchar *
crispy_scaffolder_create (const gchar  *name,
                          const gchar  *directory,
                          const gchar  *template,
                          GError      **error)
{
    g_autofree gchar *filename = NULL;
    g_autofree gchar *path = NULL;
    const gchar *body;

    g_return_val_if_fail(name != NULL, NULL);

    /* Validate: name must not be empty and must not contain slashes. */
    if (name[0] == '\0')
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_SCAFFOLD,
                    "Script name must not be empty");
        return NULL;
    }

    if (strchr(name, '/') != NULL)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_SCAFFOLD,
                    "Script name must not contain directory separators: %s",
                    name);
        return NULL;
    }

    /* Build the output filename and full path. */
    if (g_str_has_suffix(name, ".c"))
        filename = g_strdup(name);
    else
        filename = g_strdup_printf("%s.c", name);
    path = g_build_filename(directory != NULL ? directory : ".", filename, NULL);

    /* Refuse to overwrite an existing file. */
    if (g_file_test(path, G_FILE_TEST_EXISTS))
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_SCAFFOLD,
                    "File already exists: %s",
                    path);
        return NULL;
    }

    /* Resolve the template body. */
    body = look_up_template(template);

    /* Write the file. */
    if (!g_file_set_contents(path, body, (gssize)strlen(body), error))
        return NULL;

    /* Set executable permissions: rwxr-xr-x (0755). */
    if (g_chmod(path, 0755) != 0)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_IO,
                    "Failed to set executable permissions on %s",
                    path);
        return NULL;
    }

    return g_steal_pointer(&path);
}
