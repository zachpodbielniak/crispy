/* main.c - Crispy CLI entry point */

#define CRISPY_COMPILATION
#include "crispy.h"

#include <glib.h>
#include <glib-unix.h>
#include <gmodule.h>
#include <stdlib.h>
#include <string.h>

#define CRISPY_LICENSE_TEXT \
    "Crispy - Crispy Really Is Super Powerful Yo\n" \
    "Copyright (C) 2025 Zach Podbielniak\n" \
    "\n" \
    "This program is free software: you can redistribute it and/or modify\n" \
    "it under the terms of the GNU Affero General Public License as\n" \
    "published by the Free Software Foundation, either version 3 of the\n" \
    "License, or (at your option) any later version.\n" \
    "\n" \
    "This program is distributed in the hope that it will be useful,\n" \
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n" \
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n" \
    "GNU Affero General Public License for more details.\n" \
    "\n" \
    "You should have received a copy of the GNU Affero General Public\n" \
    "License along with this program. If not, see\n" \
    "<https://www.gnu.org/licenses/>.\n"

/* global state for signal cleanup */
static gchar *g_temp_source_path = NULL;

/* --- CLI option variables --- */
static gchar    *opt_inline       = NULL;
static gchar    *opt_include      = NULL;
static gchar    *opt_preload      = NULL;
static gboolean  opt_no_cache     = FALSE;
static gboolean  opt_preserve     = FALSE;
static gboolean  opt_gdb          = FALSE;
static gboolean  opt_dry_run      = FALSE;
static gboolean  opt_clean_cache  = FALSE;
static gboolean  opt_version      = FALSE;
static gboolean  opt_license      = FALSE;
static gchar   **opt_remaining    = NULL;

static GOptionEntry entries[] =
{
    {
        "inline", 'i', 0, G_OPTION_ARG_STRING, &opt_inline,
        "Execute inline C code", "CODE"
    },
    {
        "include", 'I', 0, G_OPTION_ARG_STRING, &opt_include,
        "Additional headers (semicolon-separated)", "HEADERS"
    },
    {
        "preload", 'p', 0, G_OPTION_ARG_STRING, &opt_preload,
        "Preload a shared library", "LIBNAME"
    },
    {
        "no-cache", 'n', 0, G_OPTION_ARG_NONE, &opt_no_cache,
        "Force recompilation (skip cache)", NULL
    },
    {
        "source-preserve", 'S', 0, G_OPTION_ARG_NONE, &opt_preserve,
        "Keep modified temp source files", NULL
    },
    {
        "gdb", 0, 0, G_OPTION_ARG_NONE, &opt_gdb,
        "Launch script under gdb with debug symbols", NULL
    },
    {
        "dry-run", 0, 0, G_OPTION_ARG_NONE, &opt_dry_run,
        "Show compilation command without executing", NULL
    },
    {
        "clean-cache", 0, 0, G_OPTION_ARG_NONE, &opt_clean_cache,
        "Purge the cache directory and exit", NULL
    },
    {
        "version", 'v', 0, G_OPTION_ARG_NONE, &opt_version,
        "Show version information", NULL
    },
    {
        "license", 0, 0, G_OPTION_ARG_NONE, &opt_license,
        "Show license (AGPLv3)", NULL
    },
    {
        G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY,
        &opt_remaining, NULL, "[SCRIPT] [SCRIPT_ARGS...]"
    },
    { NULL }
};

/* --- signal handler for cleanup --- */
static gboolean
on_signal(
    gpointer user_data
){
    (void)user_data;

    if (g_temp_source_path != NULL)
    {
        g_unlink(g_temp_source_path);
        g_free(g_temp_source_path);
        g_temp_source_path = NULL;
    }

    exit(130); /* 128 + SIGINT */
    return G_SOURCE_REMOVE;
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_autoptr(GOptionContext) context = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autoptr(CrispyScript) script = NULL;
    CrispyFlags flags;
    GModule *preloaded_lib;
    gint script_argc;
    gchar **script_argv;
    gint exit_code;
    gboolean is_stdin;

    preloaded_lib = NULL;
    exit_code = 0;

    /* set up option context */
    context = g_option_context_new("[SCRIPT] [ARGS...] - GLib-native C scripting");
    g_option_context_set_summary(context,
        "Crispy Really Is Super Powerful Yo\n"
        "Compile and run C scripts with GLib/GObject/GIO support.\n"
        "\n"
        "Examples:\n"
        "  crispy script.c\n"
        "  crispy script.c arg1 arg2\n"
        "  crispy -i 'g_print(\"hello\\n\"); return 0;'\n"
        "  echo 'g_print(\"hello\\n\"); return 0;' | crispy -\n"
        "  crispy --gdb script.c\n"
        "  chmod +x script.c && ./script.c  (with #!/usr/bin/crispy shebang)");
    g_option_context_add_main_entries(context, entries, NULL);

    if (!g_option_context_parse(context, &argc, &argv, &error))
    {
        g_printerr("Error: %s\n", error->message);
        return 1;
    }

    /* handle early-exit options */
    if (opt_version)
    {
        g_print("crispy %s\n", CRISPY_VERSION_STRING);
        return 0;
    }

    if (opt_license)
    {
        g_print("%s", CRISPY_LICENSE_TEXT);
        return 0;
    }

    /* create compiler and cache */
    compiler = crispy_gcc_compiler_new(&error);
    if (compiler == NULL)
    {
        g_printerr("Error: %s\n", error->message);
        return 1;
    }

    cache = crispy_file_cache_new();

    /* handle --clean-cache */
    if (opt_clean_cache)
    {
        if (!crispy_cache_provider_purge(CRISPY_CACHE_PROVIDER(cache), &error))
        {
            g_printerr("Error: %s\n", error->message);
            return 1;
        }
        return 0;
    }

    /* build flags bitmask */
    flags = CRISPY_FLAG_NONE;
    if (opt_no_cache)
        flags |= CRISPY_FLAG_FORCE_COMPILE;
    if (opt_preserve)
        flags |= CRISPY_FLAG_PRESERVE_SOURCE;
    if (opt_dry_run)
        flags |= CRISPY_FLAG_DRY_RUN;
    if (opt_gdb)
        flags |= CRISPY_FLAG_GDB;

    /* preload library if requested */
    if (opt_preload != NULL)
    {
        preloaded_lib = g_module_open(opt_preload, G_MODULE_BIND_LAZY);
        if (preloaded_lib == NULL)
        {
            g_printerr("Error: Failed to preload '%s': %s\n",
                        opt_preload, g_module_error());
            return 1;
        }
    }

    /* set up signal handlers for cleanup */
    g_unix_signal_add(SIGINT, on_signal, NULL);
    g_unix_signal_add(SIGTERM, on_signal, NULL);

    /* determine mode and create script */
    is_stdin = FALSE;
    if (opt_remaining != NULL && opt_remaining[0] != NULL &&
        strcmp(opt_remaining[0], "-") == 0)
    {
        is_stdin = TRUE;
    }

    if (opt_inline != NULL)
    {
        /* inline mode: -i "code" */
        script = crispy_script_new_from_inline(
            opt_inline, opt_include,
            CRISPY_COMPILER(compiler),
            CRISPY_CACHE_PROVIDER(cache),
            flags, &error);

        /* script args are all of opt_remaining */
        script_argc = 0;
        script_argv = NULL;
        if (opt_remaining != NULL)
        {
            script_argc = (gint)g_strv_length(opt_remaining);
            script_argv = opt_remaining;
        }
    }
    else if (is_stdin)
    {
        /* stdin mode: crispy - */
        script = crispy_script_new_from_stdin(
            CRISPY_COMPILER(compiler),
            CRISPY_CACHE_PROVIDER(cache),
            flags, &error);

        /* script args are opt_remaining[1..] */
        script_argc = 0;
        script_argv = NULL;
        if (opt_remaining != NULL && g_strv_length(opt_remaining) > 1)
        {
            script_argc = (gint)g_strv_length(opt_remaining) - 1;
            script_argv = &opt_remaining[1];
        }
    }
    else
    {
        /* file mode: crispy script.c [args...] */
        if (opt_remaining == NULL || opt_remaining[0] == NULL)
        {
            g_printerr("Error: No script file specified.\n"
                        "Try 'crispy --help' for usage information.\n");
            exit_code = 1;
            goto cleanup;
        }

        script = crispy_script_new_from_file(
            opt_remaining[0],
            CRISPY_COMPILER(compiler),
            CRISPY_CACHE_PROVIDER(cache),
            flags, &error);

        /* script args: argv[0] = script name, then remaining args */
        if (g_strv_length(opt_remaining) > 1)
        {
            script_argc = (gint)g_strv_length(opt_remaining);
            script_argv = opt_remaining;
        }
        else
        {
            script_argc = 1;
            script_argv = opt_remaining;
        }
    }

    if (script == NULL)
    {
        g_printerr("Error: %s\n", error->message);
        exit_code = 1;
        goto cleanup;
    }

    /* track temp source path for signal cleanup */
    g_temp_source_path = g_strdup(crispy_script_get_temp_source_path(script));

    /* execute the script */
    exit_code = crispy_script_execute(script, script_argc, script_argv, &error);
    if (exit_code < 0 && error != NULL)
    {
        g_printerr("Error: %s\n", error->message);
        exit_code = 1;
    }

    /* report preserved temp source if applicable */
    if (opt_preserve && crispy_script_get_temp_source_path(script) != NULL)
    {
        g_printerr("Temp source preserved: %s\n",
                    crispy_script_get_temp_source_path(script));
    }

cleanup:
    if (preloaded_lib != NULL)
        g_module_close(preloaded_lib);

    g_free(g_temp_source_path);
    g_temp_source_path = NULL;

    g_strfreev(opt_remaining);
    g_free(opt_inline);
    g_free(opt_include);
    g_free(opt_preload);

    return exit_code;
}
