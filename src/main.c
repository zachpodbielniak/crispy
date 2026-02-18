/* main.c - Crispy CLI entry point */

#define CRISPY_COMPILATION
#include "crispy.h"
#include "core/crispy-config-loader.h"
#include "crispy-default-config.h"

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
static gchar    *opt_plugins      = NULL;
static gchar    *opt_cache_dir    = NULL;
static gchar    *opt_config       = NULL;
static gboolean  opt_no_config    = FALSE;
static gboolean  opt_gen_config   = FALSE;
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
        "plugins", 'P', 0, G_OPTION_ARG_STRING, &opt_plugins,
        "Load plugins (colon-or-comma-separated .so paths)", "PATHS"
    },
    {
        "cache-dir", 0, 0, G_OPTION_ARG_STRING, &opt_cache_dir,
        "Override cache directory (default: ~/.cache/crispy)", "PATH"
    },
    {
        "config", 'c', 0, G_OPTION_ARG_STRING, &opt_config,
        "Explicit config file path", "PATH"
    },
    {
        "no-config", 0, 0, G_OPTION_ARG_NONE, &opt_no_config,
        "Skip config file loading", NULL
    },
    {
        "generate-c-config", 0, 0, G_OPTION_ARG_NONE, &opt_gen_config,
        "Print default C config to stdout and exit", NULL
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
 * (-i, -I, -p, -c) consume the next argv entry as well.
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
            strcmp(argv[i], "--preload") == 0 ||
            strcmp(argv[i], "-P") == 0 ||
            strcmp(argv[i], "--plugins") == 0 ||
            strcmp(argv[i], "--cache-dir") == 0 ||
            strcmp(argv[i], "-c") == 0 ||
            strcmp(argv[i], "--config") == 0)
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
    g_autoptr(CrispyPluginEngine) engine = NULL;
    g_autoptr(CrispyScript) script = NULL;
    CrispyConfigContext config_ctx;
    CrispyFlags flags;
    GModule *preloaded_lib;
    gint crispy_argc;
    gchar **crispy_argv;
    gint script_argc;
    gchar **script_argv;
    gint exit_code;
    gboolean is_stdin;
    gboolean config_loaded;
    g_autofree gchar *config_path = NULL;
    const gchar *config_extra_flags;
    const gchar *config_override_flags;

    preloaded_lib = NULL;
    exit_code = 0;
    config_loaded = FALSE;

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
    g_option_context_set_description(context,
        "Config file search order (first found wins):\n"
        "  1. $CRISPY_CONFIG_FILE environment variable\n"
        "  2. -c/--config PATH argument\n"
        "  3. ~/.config/crispy/config.c\n"
        "  4. /etc/crispy/config.c\n"
        "  5. /usr/share/crispy/config.c\n"
        "\n"
        "Config bypass:\n"
        "  --no-config flag or $NO_CRISPY_CONFIG environment variable (if set)\n"
        "\n"
        "Config generation:\n"
        "  crispy --generate-c-config > ~/.config/crispy/config.c\n"
        "\n"
        "Compiler flag precedence (last wins for conflicting flags):\n"
        "  1. Config extra_flags    (defaults, lowest priority)\n"
        "  2. Script CRISPY_PARAMS  (script-level overrides)\n"
        "  3. Plugin extra_flags    (from PRE_COMPILE hook)\n"
        "  4. Config override_flags (forced overrides, highest priority)\n"
        "\n"
        "Plugin loading order:\n"
        "  1. Plugins specified in config file (via crispy_config_context_add_plugin)\n"
        "  2. Plugins specified via -P/--plugins CLI flag");
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

    if (opt_gen_config)
    {
        g_print("%s", default_c_config);
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

    cache = crispy_file_cache_new_with_dir(opt_cache_dir);

    /*
     * CONFIG LOADING
     *
     * Load the C config file if not bypassed. The config runs before
     * plugins, scripts, or anything else. It can set defaults, load
     * plugins, inject compiler flags, override cache dir, and modify
     * the script's argv.
     */
    if (!opt_no_config && g_getenv("NO_CRISPY_CONFIG") == NULL)
    {
        config_path = crispy_config_loader_find(opt_config);

        if (config_path != NULL)
        {
            /* determine script path for the config context */
            const gchar *ctx_script_path;

            ctx_script_path = NULL;
            if (script_argc > 0 && script_argv != NULL)
            {
                /* "-" is stdin, not a file path */
                if (strcmp(script_argv[0], "-") != 0)
                    ctx_script_path = script_argv[0];
            }

            /* init config context with current state */
            crispy_config_context_init_internal(
                &config_ctx,
                argc, (const gchar **)argv,
                script_argc, script_argv,
                ctx_script_path);

            /* compile, load, and call the config's init function */
            if (!crispy_config_loader_compile_and_load(
                    config_path,
                    CRISPY_COMPILER(compiler),
                    CRISPY_CACHE_PROVIDER(cache),
                    &config_ctx,
                    &error))
            {
                g_printerr("Warning: Config load failed: %s\n",
                            error->message);
                g_clear_error(&error);
                crispy_config_context_clear_internal(&config_ctx);
            }
            else
            {
                config_loaded = TRUE;

                /*
                 * Apply config results:
                 * - cache_dir override -> recreate cache
                 * - script argv replacement -> use new argv
                 */

                /* override cache directory if config requested it */
                {
                    const gchar *cfg_cache_dir;

                    cfg_cache_dir = crispy_config_context_get_cache_dir_internal(
                        &config_ctx);
                    if (cfg_cache_dir != NULL && opt_cache_dir == NULL)
                    {
                        /* config sets cache dir, CLI didn't override */
                        g_clear_object(&cache);
                        cache = crispy_file_cache_new_with_dir(cfg_cache_dir);
                    }
                }

                /* use config's script argv if it was replaced */
                {
                    gint new_argc;
                    gchar **new_argv;

                    new_argc = crispy_config_context_get_script_argc(&config_ctx);
                    new_argv = crispy_config_context_get_script_argv(&config_ctx);

                    /*
                     * If the config replaced argv, the new values differ
                     * from what we passed in. We can detect this by
                     * checking if the pointer changed.
                     */
                    if (new_argv != script_argv)
                    {
                        script_argc = new_argc;
                        script_argv = new_argv;
                    }
                }
            }
        }
    }

    /* handle --clean-cache (with possibly-updated cache from config) */
    if (opt_clean_cache)
    {
        if (!crispy_cache_provider_purge(CRISPY_CACHE_PROVIDER(cache), &error))
        {
            g_printerr("Error: %s\n", error->message);
            if (config_loaded)
                crispy_config_context_clear_internal(&config_ctx);
            g_strfreev(crispy_argv);
            return 1;
        }
        if (config_loaded)
            crispy_config_context_clear_internal(&config_ctx);
        g_strfreev(crispy_argv);
        return 0;
    }

    /* build flags bitmask: config defaults OR'd with CLI flags */
    flags = CRISPY_FLAG_NONE;
    if (config_loaded)
    {
        gboolean cfg_flags_set;
        guint cfg_flags;

        cfg_flags = crispy_config_context_get_flags_internal(
            &config_ctx, &cfg_flags_set);
        if (cfg_flags_set)
            flags = (CrispyFlags)cfg_flags;
    }

    /* CLI flags always win (OR'd on top of config) */
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
            if (config_loaded)
                crispy_config_context_clear_internal(&config_ctx);
            g_strfreev(crispy_argv);
            return 1;
        }
    }

    /*
     * Plugin loading: config plugins first, then CLI plugins.
     * This ensures config-specified plugins run their hooks before
     * CLI-specified plugins.
     */
    if (config_loaded)
    {
        GPtrArray *cfg_plugin_paths;

        cfg_plugin_paths = crispy_config_context_get_plugin_paths_internal(
            &config_ctx);
        if (cfg_plugin_paths != NULL && cfg_plugin_paths->len > 0)
        {
            guint pi;

            if (engine == NULL)
                engine = crispy_plugin_engine_new();

            for (pi = 0; pi < cfg_plugin_paths->len; pi++)
            {
                const gchar *ppath;

                ppath = (const gchar *)g_ptr_array_index(cfg_plugin_paths, pi);
                if (!crispy_plugin_engine_load(engine, ppath, &error))
                {
                    g_printerr("Warning: Config plugin '%s' failed: %s\n",
                                ppath, error->message);
                    g_clear_error(&error);
                }
            }
        }
    }

    /* load CLI-specified plugins second */
    if (opt_plugins != NULL)
    {
        if (engine == NULL)
            engine = crispy_plugin_engine_new();

        if (!crispy_plugin_engine_load_paths(engine, opt_plugins, &error))
        {
            g_printerr("Error: %s\n", error->message);
            if (config_loaded)
                crispy_config_context_clear_internal(&config_ctx);
            g_strfreev(crispy_argv);
            if (preloaded_lib != NULL)
                g_module_close(preloaded_lib);
            return 1;
        }
    }

    /* inject config plugin data into the engine's shared data store */
    if (config_loaded && engine != NULL)
    {
        GHashTable *cfg_plugin_data;
        GHashTableIter iter;
        gpointer key;
        gpointer value;

        cfg_plugin_data = crispy_config_context_get_plugin_data_internal(
            &config_ctx);
        if (cfg_plugin_data != NULL)
        {
            g_hash_table_iter_init(&iter, cfg_plugin_data);
            while (g_hash_table_iter_next(&iter, &key, &value))
            {
                crispy_plugin_engine_set_data(
                    engine,
                    (const gchar *)key,
                    g_strdup((const gchar *)value),
                    g_free);
            }
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

    /* inject config compiler flags into the script */
    if (config_loaded)
    {
        config_extra_flags =
            crispy_config_context_get_extra_flags_internal(&config_ctx);
        config_override_flags =
            crispy_config_context_get_override_flags_internal(&config_ctx);

        if (config_extra_flags != NULL)
            crispy_script_set_extra_flags(script, config_extra_flags);
        if (config_override_flags != NULL)
            crispy_script_set_override_flags(script, config_override_flags);
    }

    /* attach plugin engine if loaded */
    if (engine != NULL)
        crispy_script_set_plugin_engine(script, engine);

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
    if (config_loaded)
        crispy_config_context_clear_internal(&config_ctx);

    if (preloaded_lib != NULL)
        g_module_close(preloaded_lib);

    g_free(g_temp_source_path);
    g_temp_source_path = NULL;

    g_strfreev(crispy_argv);
    g_free(opt_inline);
    g_free(opt_include);
    g_free(opt_preload);
    g_free(opt_plugins);
    g_free(opt_cache_dir);
    g_free(opt_config);

    return exit_code;
}
