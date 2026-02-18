/* crispy-config-context.h - Config context for C config files */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Plain struct and setter/getter functions passed to the config
 * file's crispy_config_init() entry point.  Config authors use
 * these to set defaults, load plugins, inject compiler flags,
 * and modify script arguments before execution.
 */

#ifndef CRISPY_CONFIG_CONTEXT_H
#define CRISPY_CONFIG_CONTEXT_H

#if !defined(CRISPY_INSIDE) && !defined(CRISPY_COMPILATION)
#error "Only <crispy.h> can be included directly."
#endif

#include <glib.h>
#include "../crispy-types.h"

G_BEGIN_DECLS

/**
 * CrispyConfigContext:
 *
 * Opaque context structure passed to crispy_config_init().
 * Config files use the setter/getter functions below to
 * inspect and modify crispy's behavior before script execution.
 *
 * This is NOT a GObject -- it is a short-lived struct allocated
 * on the stack in main(), populated before calling config init,
 * and read back afterwards.
 */

/*
 * The struct definition is exposed only to internal callers
 * (CRISPY_COMPILATION) so main.c can stack-allocate it.
 * Config .so files see only the opaque forward declaration
 * from crispy-types.h.
 */
#ifdef CRISPY_COMPILATION
struct _CrispyConfigContext
{
    /* read-only: crispy's own argv */
    gint           crispy_argc;
    const gchar  **crispy_argv;    /* not owned -- points into original argv */

    /* mutable: script argv */
    gint           script_argc;
    gchar        **script_argv;    /* not owned initially */
    gboolean       script_argv_owned; /* TRUE if set_script_argv was called */

    /* script source path (not owned) */
    const gchar   *script_path;

    /* compiler flags */
    gchar         *extra_flags;    /* prepended before CRISPY_PARAMS (defaults) */
    gchar         *override_flags; /* appended after CRISPY_PARAMS (overrides) */

    /* plugin config */
    GPtrArray     *plugin_paths;   /* of gchar*, paths to load */
    GHashTable    *plugin_data;    /* string -> string, key-value data */

    /* CrispyFlags */
    guint          flags;
    gboolean       flags_set;      /* TRUE if set_flags or add_flags was called */

    /* cache override */
    gchar         *cache_dir;
};
#endif /* CRISPY_COMPILATION */

/**
 * CrispyConfigInitFunc:
 * @ctx: the config context
 *
 * Function signature for the config file's entry point.
 * The compiled config .so must export this as `crispy_config_init`.
 *
 * Returns: %TRUE on success, %FALSE to skip config (use defaults)
 */
typedef gboolean (*CrispyConfigInitFunc)(CrispyConfigContext *ctx);

/* --- Read-only accessors --- */

/**
 * crispy_config_context_get_crispy_argc:
 * @ctx: a #CrispyConfigContext
 *
 * Returns crispy's own argument count (the full original command line).
 *
 * Returns: the argument count
 */
gint crispy_config_context_get_crispy_argc (CrispyConfigContext *ctx);

/**
 * crispy_config_context_get_crispy_argv:
 * @ctx: a #CrispyConfigContext
 *
 * Returns crispy's own argument vector (the full original command line).
 * The returned array is read-only and must not be modified or freed.
 *
 * Returns: (transfer none): the argument vector
 */
const gchar * const * crispy_config_context_get_crispy_argv (CrispyConfigContext *ctx);

/**
 * crispy_config_context_get_script_argc:
 * @ctx: a #CrispyConfigContext
 *
 * Returns the script's argument count (script path + its arguments).
 *
 * Returns: the argument count
 */
gint crispy_config_context_get_script_argc (CrispyConfigContext *ctx);

/**
 * crispy_config_context_get_script_argv:
 * @ctx: a #CrispyConfigContext
 *
 * Returns the script's argument vector. This may be modified by
 * calling crispy_config_context_set_script_argv().
 *
 * Returns: (transfer none): the argument vector
 */
gchar ** crispy_config_context_get_script_argv (CrispyConfigContext *ctx);

/**
 * crispy_config_context_get_script_path:
 * @ctx: a #CrispyConfigContext
 *
 * Returns the path of the script that is about to run. This is
 * %NULL for inline (-i) or stdin (-) modes. Useful for per-script
 * conditional logic in the config file.
 *
 * Returns: (transfer none) (nullable): the script source path
 */
const gchar * crispy_config_context_get_script_path (CrispyConfigContext *ctx);

/* --- Compiler flag setters --- */

/**
 * crispy_config_context_set_extra_flags:
 * @ctx: a #CrispyConfigContext
 * @flags: compiler flags string
 *
 * Sets default compiler flags that are **prepended** before the
 * script's own CRISPY_PARAMS during compilation. These have the
 * lowest priority -- scripts can override them via CRISPY_PARAMS.
 *
 * Replaces any previously set extra_flags.
 */
void crispy_config_context_set_extra_flags (CrispyConfigContext *ctx,
                                            const gchar         *flags);

/**
 * crispy_config_context_append_extra_flags:
 * @ctx: a #CrispyConfigContext
 * @flags: compiler flags to append
 *
 * Appends to the existing extra_flags string (space-separated).
 */
void crispy_config_context_append_extra_flags (CrispyConfigContext *ctx,
                                               const gchar         *flags);

/**
 * crispy_config_context_set_override_flags:
 * @ctx: a #CrispyConfigContext
 * @flags: compiler flags string
 *
 * Sets override compiler flags that are **appended** after the
 * script's CRISPY_PARAMS and plugin extra_flags. These have the
 * highest priority -- they override everything else. For gcc,
 * later flags win when there are conflicts.
 *
 * Use this for flags that must always apply regardless of what
 * individual scripts request (e.g. forcing -fsanitize=address
 * for certain programs).
 *
 * Replaces any previously set override_flags.
 */
void crispy_config_context_set_override_flags (CrispyConfigContext *ctx,
                                               const gchar         *flags);

/**
 * crispy_config_context_append_override_flags:
 * @ctx: a #CrispyConfigContext
 * @flags: compiler flags to append
 *
 * Appends to the existing override_flags string (space-separated).
 */
void crispy_config_context_append_override_flags (CrispyConfigContext *ctx,
                                                  const gchar         *flags);

/* --- Plugin management --- */

/**
 * crispy_config_context_add_plugin:
 * @ctx: a #CrispyConfigContext
 * @plugin_path: path to a plugin .so file
 *
 * Queues a plugin .so file for loading. Plugins specified here
 * are loaded before CLI-specified plugins (-P). Multiple calls
 * accumulate paths in order.
 */
void crispy_config_context_add_plugin (CrispyConfigContext *ctx,
                                       const gchar         *plugin_path);

/**
 * crispy_config_context_set_plugin_data:
 * @ctx: a #CrispyConfigContext
 * @key: a string key for the data
 * @value: the string value to store
 *
 * Sets a key-value pair in the plugin data store. After config
 * loading, these are injected into the plugin engine's shared
 * data store, accessible to plugins via
 * crispy_plugin_engine_get_data().
 */
void crispy_config_context_set_plugin_data (CrispyConfigContext *ctx,
                                            const gchar         *key,
                                            const gchar         *value);

/* --- CrispyFlags management --- */

/**
 * crispy_config_context_set_flags:
 * @ctx: a #CrispyConfigContext
 * @flags: a #CrispyFlags bitmask
 *
 * Sets the default CrispyFlags. CLI flags are OR'd on top of
 * these (CLI always wins).
 */
void crispy_config_context_set_flags (CrispyConfigContext *ctx,
                                      guint                flags);

/**
 * crispy_config_context_add_flags:
 * @ctx: a #CrispyConfigContext
 * @flags: a #CrispyFlags bitmask to OR with existing
 *
 * Adds flags to the existing bitmask (OR operation).
 */
void crispy_config_context_add_flags (CrispyConfigContext *ctx,
                                      guint                flags);

/* --- Cache configuration --- */

/**
 * crispy_config_context_set_cache_dir:
 * @ctx: a #CrispyConfigContext
 * @cache_dir: path to cache directory
 *
 * Overrides the cache directory. If set, a new CrispyFileCache
 * is created with this directory after config loading.
 */
void crispy_config_context_set_cache_dir (CrispyConfigContext *ctx,
                                          const gchar         *cache_dir);

/* --- Script argv management --- */

/**
 * crispy_config_context_set_script_argv:
 * @ctx: a #CrispyConfigContext
 * @argc: new argument count
 * @argv: (transfer full) (array length=argc): new argument vector
 *
 * Replaces the script's argv entirely. The context takes ownership
 * of @argv (it will be freed with g_strfreev() on cleanup).
 *
 * The previous argv is NOT freed -- it points into the original
 * process argv unless a previous set_script_argv call replaced it.
 */
void crispy_config_context_set_script_argv (CrispyConfigContext *ctx,
                                            gint                 argc,
                                            gchar              **argv);

/* --- Internal helpers (not for config authors) --- */

/**
 * crispy_config_context_init_internal:
 * @ctx: a #CrispyConfigContext to initialize
 * @crispy_argc: crispy's argument count
 * @crispy_argv: crispy's argument vector (not owned)
 * @script_argc: script's argument count
 * @script_argv: script's argument vector (not owned)
 * @script_path: (nullable): script source path
 *
 * Initializes the context before passing it to the config init
 * function.  This is called by main.c, not by config authors.
 */
void crispy_config_context_init_internal (CrispyConfigContext *ctx,
                                          gint                 crispy_argc,
                                          const gchar        **crispy_argv,
                                          gint                 script_argc,
                                          gchar              **script_argv,
                                          const gchar         *script_path);

/**
 * crispy_config_context_clear_internal:
 * @ctx: a #CrispyConfigContext to clean up
 *
 * Frees all accumulated resources in the context.  Called by
 * main.c after config results have been extracted.
 */
void crispy_config_context_clear_internal (CrispyConfigContext *ctx);

/**
 * crispy_config_context_get_extra_flags_internal:
 * @ctx: a #CrispyConfigContext
 *
 * Returns the accumulated extra_flags string (may be %NULL).
 * Used by main.c to inject into the script before execution.
 *
 * Returns: (transfer none) (nullable): the extra flags string
 */
const gchar * crispy_config_context_get_extra_flags_internal (CrispyConfigContext *ctx);

/**
 * crispy_config_context_get_override_flags_internal:
 * @ctx: a #CrispyConfigContext
 *
 * Returns the accumulated override_flags string (may be %NULL).
 * Used by main.c to inject into the script before execution.
 *
 * Returns: (transfer none) (nullable): the override flags string
 */
const gchar * crispy_config_context_get_override_flags_internal (CrispyConfigContext *ctx);

/**
 * crispy_config_context_get_plugin_paths_internal:
 * @ctx: a #CrispyConfigContext
 *
 * Returns the array of plugin paths accumulated via add_plugin().
 *
 * Returns: (transfer none): the plugin paths array
 */
GPtrArray * crispy_config_context_get_plugin_paths_internal (CrispyConfigContext *ctx);

/**
 * crispy_config_context_get_plugin_data_internal:
 * @ctx: a #CrispyConfigContext
 *
 * Returns the hash table of plugin data key-value pairs.
 *
 * Returns: (transfer none): the plugin data hash table
 */
GHashTable * crispy_config_context_get_plugin_data_internal (CrispyConfigContext *ctx);

/**
 * crispy_config_context_get_flags_internal:
 * @ctx: a #CrispyConfigContext
 * @flags_set: (out) (nullable): whether set_flags/add_flags was called
 *
 * Returns the accumulated CrispyFlags bitmask.
 *
 * Returns: the flags bitmask
 */
guint crispy_config_context_get_flags_internal (CrispyConfigContext *ctx,
                                                gboolean            *flags_set);

/**
 * crispy_config_context_get_cache_dir_internal:
 * @ctx: a #CrispyConfigContext
 *
 * Returns the overridden cache directory (may be %NULL).
 *
 * Returns: (transfer none) (nullable): the cache directory path
 */
const gchar * crispy_config_context_get_cache_dir_internal (CrispyConfigContext *ctx);

G_END_DECLS

#endif /* CRISPY_CONFIG_CONTEXT_H */
