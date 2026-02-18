/* crispy-config-context.c - Config context implementation */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the CrispyConfigContext struct and its
 * setter/getter functions.  The context is a plain struct
 * (not a GObject) that is stack-allocated in main(), populated
 * by the config init function, and read back to apply settings.
 */

#define CRISPY_COMPILATION
#include "crispy-config-context.h"
#include "../crispy-types.h"

#include <glib.h>
#include <string.h>

/*
 * The struct definition is in the header under #ifdef CRISPY_COMPILATION
 * so that main.c can stack-allocate it, while config .so files see only
 * the opaque forward declaration.
 */

/* --- Internal helpers --- */

void
crispy_config_context_init_internal(
    CrispyConfigContext *ctx,
    gint                 crispy_argc,
    const gchar        **crispy_argv,
    gint                 script_argc,
    gchar              **script_argv,
    const gchar         *script_path
){
    memset(ctx, 0, sizeof(*ctx));

    ctx->crispy_argc = crispy_argc;
    ctx->crispy_argv = crispy_argv;
    ctx->script_argc = script_argc;
    ctx->script_argv = script_argv;
    ctx->script_argv_owned = FALSE;
    ctx->script_path = script_path;

    ctx->extra_flags = NULL;
    ctx->override_flags = NULL;

    ctx->plugin_paths = g_ptr_array_new_with_free_func(g_free);
    ctx->plugin_data = g_hash_table_new_full(
        g_str_hash, g_str_equal, g_free, g_free);

    ctx->flags = 0;
    ctx->flags_set = FALSE;
    ctx->cache_dir = NULL;
}

void
crispy_config_context_clear_internal(
    CrispyConfigContext *ctx
){
    /* free owned script argv if it was replaced */
    if (ctx->script_argv_owned && ctx->script_argv != NULL)
        g_strfreev(ctx->script_argv);

    g_free(ctx->extra_flags);
    g_free(ctx->override_flags);
    g_free(ctx->cache_dir);

    if (ctx->plugin_paths != NULL)
        g_ptr_array_unref(ctx->plugin_paths);

    if (ctx->plugin_data != NULL)
        g_hash_table_unref(ctx->plugin_data);
}

/* --- Read-only accessors --- */

gint
crispy_config_context_get_crispy_argc(
    CrispyConfigContext *ctx
){
    return ctx->crispy_argc;
}

const gchar * const *
crispy_config_context_get_crispy_argv(
    CrispyConfigContext *ctx
){
    return (const gchar * const *)ctx->crispy_argv;
}

gint
crispy_config_context_get_script_argc(
    CrispyConfigContext *ctx
){
    return ctx->script_argc;
}

gchar **
crispy_config_context_get_script_argv(
    CrispyConfigContext *ctx
){
    return ctx->script_argv;
}

const gchar *
crispy_config_context_get_script_path(
    CrispyConfigContext *ctx
){
    return ctx->script_path;
}

/* --- Compiler flag setters --- */

void
crispy_config_context_set_extra_flags(
    CrispyConfigContext *ctx,
    const gchar         *flags
){
    g_free(ctx->extra_flags);
    ctx->extra_flags = g_strdup(flags);
}

void
crispy_config_context_append_extra_flags(
    CrispyConfigContext *ctx,
    const gchar         *flags
){
    if (flags == NULL || flags[0] == '\0')
        return;

    if (ctx->extra_flags == NULL || ctx->extra_flags[0] == '\0')
    {
        g_free(ctx->extra_flags);
        ctx->extra_flags = g_strdup(flags);
    }
    else
    {
        gchar *combined;

        combined = g_strdup_printf("%s %s", ctx->extra_flags, flags);
        g_free(ctx->extra_flags);
        ctx->extra_flags = combined;
    }
}

void
crispy_config_context_set_override_flags(
    CrispyConfigContext *ctx,
    const gchar         *flags
){
    g_free(ctx->override_flags);
    ctx->override_flags = g_strdup(flags);
}

void
crispy_config_context_append_override_flags(
    CrispyConfigContext *ctx,
    const gchar         *flags
){
    if (flags == NULL || flags[0] == '\0')
        return;

    if (ctx->override_flags == NULL || ctx->override_flags[0] == '\0')
    {
        g_free(ctx->override_flags);
        ctx->override_flags = g_strdup(flags);
    }
    else
    {
        gchar *combined;

        combined = g_strdup_printf("%s %s", ctx->override_flags, flags);
        g_free(ctx->override_flags);
        ctx->override_flags = combined;
    }
}

/* --- Plugin management --- */

void
crispy_config_context_add_plugin(
    CrispyConfigContext *ctx,
    const gchar         *plugin_path
){
    if (plugin_path == NULL || plugin_path[0] == '\0')
        return;

    g_ptr_array_add(ctx->plugin_paths, g_strdup(plugin_path));
}

void
crispy_config_context_set_plugin_data(
    CrispyConfigContext *ctx,
    const gchar         *key,
    const gchar         *value
){
    if (key == NULL)
        return;

    g_hash_table_replace(ctx->plugin_data,
                         g_strdup(key),
                         g_strdup(value));
}

/* --- CrispyFlags management --- */

void
crispy_config_context_set_flags(
    CrispyConfigContext *ctx,
    guint                flags
){
    ctx->flags = flags;
    ctx->flags_set = TRUE;
}

void
crispy_config_context_add_flags(
    CrispyConfigContext *ctx,
    guint                flags
){
    ctx->flags |= flags;
    ctx->flags_set = TRUE;
}

/* --- Cache configuration --- */

void
crispy_config_context_set_cache_dir(
    CrispyConfigContext *ctx,
    const gchar         *cache_dir
){
    g_free(ctx->cache_dir);
    ctx->cache_dir = g_strdup(cache_dir);
}

/* --- Internal result accessors (used by main.c) --- */

const gchar *
crispy_config_context_get_extra_flags_internal(
    CrispyConfigContext *ctx
){
    return ctx->extra_flags;
}

const gchar *
crispy_config_context_get_override_flags_internal(
    CrispyConfigContext *ctx
){
    return ctx->override_flags;
}

GPtrArray *
crispy_config_context_get_plugin_paths_internal(
    CrispyConfigContext *ctx
){
    return ctx->plugin_paths;
}

GHashTable *
crispy_config_context_get_plugin_data_internal(
    CrispyConfigContext *ctx
){
    return ctx->plugin_data;
}

guint
crispy_config_context_get_flags_internal(
    CrispyConfigContext *ctx,
    gboolean            *flags_set
){
    if (flags_set != NULL)
        *flags_set = ctx->flags_set;
    return ctx->flags;
}

const gchar *
crispy_config_context_get_cache_dir_internal(
    CrispyConfigContext *ctx
){
    return ctx->cache_dir;
}

/* --- Script argv management --- */

void
crispy_config_context_set_script_argv(
    CrispyConfigContext *ctx,
    gint                 argc,
    gchar              **argv
){
    /* free previous owned argv if any */
    if (ctx->script_argv_owned && ctx->script_argv != NULL)
        g_strfreev(ctx->script_argv);

    ctx->script_argc = argc;
    ctx->script_argv = argv;
    ctx->script_argv_owned = TRUE;
}
