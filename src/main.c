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

/**
 * split_argv:
 * @argc: original argument count
 * @argv: original argument vector
 * @crispy_argc: (out): argument count for crispy options (including argv[0])
 * @crispy_argv: (out) (transfer full): new argv for crispy options
 * @script_argc: (out): argument count for the script (script path + its args)
 * @script_argv: (out): pointer into original argv at the script path
 *
 * Scans argv to find the first non-option argument (the script path).
 * Everything before it is crispy's options; everything from it onward
 * is the script's argv. This ensures that flags after the script path
 * (e.g., `crispy script.c -f blah`) are passed to the script, not
 * interpreted by crispy.
 *
 * A non-option argument is one that does not start with '-', or is
 * literally "-" (stdin mode). Options that take a value argument
 * (-i, -I, -p) consume the next argv entry as well.
 */
static void
split_argv(
    gint     argc,
    gchar  **argv,
    gint    *crispy_argc,
    gchar ***crispy_argv,
    gint    *script_argc,
    gchar ***script_argv
){
    gint split_pos;
    gint i;
    gint j;

    split_pos = argc; /* default: no script found, everything is crispy's */

    for (i = 1; i < argc; i++)
    {
        /* literal "-" is stdin mode -- it's the script arg */
        if (strcmp(argv[i], "-") == 0)
        {
            split_pos = i;
            break;
        }

        /* "--" ends option parsing, next arg is the script */
        if (strcmp(argv[i], "--") == 0)
        {
            split_pos = i + 1;
            break;
        }

        /* not an option -- this is the script path */
        if (argv[i][0] != '-')
        {
            split_pos = i;
            break;
        }

        /* options that consume the next argument as their value */
        if (strcmp(argv[i], "-i") == 0 ||
            strcmp(argv[i], "--inline") == 0 ||
            strcmp(argv[i], "-I") == 0 ||
            strcmp(argv[i], "--include") == 0 ||
            strcmp(argv[i], "-p") == 0 ||
            strcmp(argv[i], "--preload") == 0)
        {
            i++; /* skip the value argument */
        }
    }

    /* build crispy's argv: argv[0] + everything before split_pos */
    *crispy_argc = split_pos;
    *crispy_argv = g_new0(gchar *, split_pos + 1);
    for (j = 0; j < split_pos; j++)
        (*crispy_argv)[j] = g_strdup(argv[j]);
    (*crispy_argv)[split_pos] = NULL;

    /* script's argv: everything from split_pos onward */
    if (split_pos < argc)
    {
        *script_argc = argc - split_pos;
        *script_argv = &argv[split_pos];
    }
    else
    {
        *script_argc = 0;
        *script_argv = NULL;
    }
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
    gint crispy_argc;
    gchar **crispy_argv;
    gint script_argc;
    gchar **script_argv;
    gint exit_code;
    gboolean is_stdin;

    preloaded_lib = NULL;
    exit_code = 0;

    /*
     * Split argv before GOptionContext sees it. Everything after the
     * script path (first non-option arg) belongs to the script, not
     * to crispy. This means `crispy script.c -f blah` passes "-f blah"
     * to the script, while `crispy -n script.c` gives crispy the -n flag.
     */
    split_argv(argc, argv, &crispy_argc, &crispy_argv,
               &script_argc, &script_argv);

    /* set up option context */
    context = g_option_context_new("[SCRIPT] [ARGS...] - GLib-native C scripting");
    g_option_context_set_summary(context,
        "Crispy Really Is Super Powerful Yo\n"
        "Compile and run C scripts with GLib/GObject/GIO support.\n"
        "\n"
        "Arguments after the script path are passed to the script, not crispy.\n"
        "\n"
        "Examples:\n"
        "  crispy script.c\n"
        "  crispy script.c arg1 arg2\n"
        "  crispy script.c -f blah        (script sees -f blah)\n"
        "  crispy -n script.c             (crispy gets -n, script sees no args)\n"
        "  crispy -i 'g_print(\"hello\\n\"); return 0;'\n"
        "  echo 'g_print(\"hello\\n\"); return 0;' | crispy -\n"
        "  crispy --gdb script.c\n"
        "  chmod +x script.c && ./script.c  (with #!/usr/bin/crispy shebang)");
    g_option_context_add_main_entries(context, entries, NULL);

    if (!g_option_context_parse(context, &crispy_argc, &crispy_argv, &error))
    {
        g_printerr("Error: %s\n", error->message);
        g_strfreev(crispy_argv);
        return 1;
    }

    /* handle early-exit options */
    if (opt_version)
    {
        g_print("crispy %s\n", CRISPY_VERSION_STRING);
        g_strfreev(crispy_argv);
        return 0;
    }

    if (opt_license)
    {
        g_print("%s", CRISPY_LICENSE_TEXT);
        g_strfreev(crispy_argv);
        return 0;
    }

    /* create compiler and cache */
    compiler = crispy_gcc_compiler_new(&error);
    if (compiler == NULL)
    {
        g_printerr("Error: %s\n", error->message);
        g_strfreev(crispy_argv);
        return 1;
    }

    cache = crispy_file_cache_new();

    /* handle --clean-cache */
    if (opt_clean_cache)
    {
        if (!crispy_cache_provider_purge(CRISPY_CACHE_PROVIDER(cache), &error))
        {
            g_printerr("Error: %s\n", error->message);
            g_strfreev(crispy_argv);
            return 1;
        }
        g_strfreev(crispy_argv);
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
            g_strfreev(crispy_argv);
            return 1;
        }
    }

    /* set up signal handlers for cleanup */
    g_unix_signal_add(SIGINT, on_signal, NULL);
    g_unix_signal_add(SIGTERM, on_signal, NULL);

    /* determine mode and create script */
    is_stdin = (script_argc > 0 && strcmp(script_argv[0], "-") == 0);

    if (opt_inline != NULL)
    {
        /* inline mode: -i "code" */
        script = crispy_script_new_from_inline(
            opt_inline, opt_include,
            CRISPY_COMPILER(compiler),
            CRISPY_CACHE_PROVIDER(cache),
            flags, &error);

        /* all split-off args go to the script */
    }
    else if (is_stdin)
    {
        /* stdin mode: crispy - [args...] */
        script = crispy_script_new_from_stdin(
            CRISPY_COMPILER(compiler),
            CRISPY_CACHE_PROVIDER(cache),
            flags, &error);

        /* skip the "-" itself, pass remaining to script */
        script_argv++;
        script_argc--;
    }
    else
    {
        /* file mode: crispy [opts] script.c [script_args...] */
        if (script_argc == 0)
        {
            g_printerr("Error: No script file specified.\n"
                        "Try 'crispy --help' for usage information.\n");
            exit_code = 1;
            goto cleanup;
        }

        script = crispy_script_new_from_file(
            script_argv[0],
            CRISPY_COMPILER(compiler),
            CRISPY_CACHE_PROVIDER(cache),
            flags, &error);
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

    g_strfreev(crispy_argv);
    g_free(opt_inline);
    g_free(opt_include);
    g_free(opt_preload);

    return exit_code;
}
