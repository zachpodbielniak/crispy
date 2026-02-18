/* crispy-config-loader.c - Config file compilation and loading */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Compiles a user-written C configuration file into a shared object
 * and loads it at runtime.  Uses CrispyCompiler and CrispyCacheProvider
 * interfaces directly (not CrispyScript, since the config has a
 * different entry point and should not trigger plugin hooks).
 *
 * The config source may define CRISPY_PARAMS to pass extra compiler
 * flags.  The compiled .so must export a `crispy_config_init` symbol.
 *
 * Follows the same pattern as gst's gst-config-compiler.c.
 */

#define CRISPY_COMPILATION
#include "crispy-config-loader.h"
#include "crispy-source-utils-private.h"
#include "crispy-config-context.h"
#include "../interfaces/crispy-compiler.h"
#include "../interfaces/crispy-cache-provider.h"
#include "../crispy-types.h"

#include <glib.h>
#include <gmodule.h>
#include <string.h>

/* --- Internal helpers --- */

/**
 * get_crispy_include_flags:
 *
 * Attempts to get crispy's include flags. In development mode,
 * uses CRISPY_DEV_INCLUDE_DIR. When installed, uses pkg-config.
 *
 * Returns: (transfer full): include flags string; free with g_free()
 */
static gchar *
get_crispy_include_flags(void)
{
#ifdef CRISPY_DEV_INCLUDE_DIR
    /*
     * Development mode: point to the source directory so that
     * config files can #include <crispy.h>
     */
    if (g_file_test(CRISPY_DEV_INCLUDE_DIR "/crispy.h",
                    G_FILE_TEST_IS_REGULAR))
    {
        return g_strdup("-I" CRISPY_DEV_INCLUDE_DIR);
    }
#endif

    /* installed case: query pkg-config for crispy */
    {
        g_autofree gchar *cmd = NULL;
        g_autofree gchar *stdout_output = NULL;
        g_autofree gchar *stderr_output = NULL;
        gint exit_status;

        cmd = g_strdup("pkg-config --cflags crispy");
        if (g_spawn_command_line_sync(cmd, &stdout_output,
                                      &stderr_output,
                                      &exit_status, NULL) &&
            g_spawn_check_wait_status(exit_status, NULL))
        {
            g_strstrip(stdout_output);
            return g_steal_pointer(&stdout_output);
        }
    }

    return g_strdup("");
}

/* --- Public API --- */

gchar *
crispy_config_loader_find(
    const gchar *explicit_path
){
    const gchar *env_path;
    gchar *path;

    /* 1. CRISPY_CONFIG_FILE env var (highest priority) */
    env_path = g_getenv("CRISPY_CONFIG_FILE");
    if (env_path != NULL && env_path[0] != '\0')
    {
        if (g_file_test(env_path, G_FILE_TEST_IS_REGULAR))
            return g_strdup(env_path);
    }

    /* 2. explicit path from -c/--config */
    if (explicit_path != NULL && explicit_path[0] != '\0')
    {
        if (g_file_test(explicit_path, G_FILE_TEST_IS_REGULAR))
            return g_strdup(explicit_path);
    }

    /* 3. ~/.config/crispy/config.c */
    path = g_build_filename(g_get_user_config_dir(),
                            "crispy", "config.c", NULL);
    if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
        return path;
    g_free(path);

    /* 4. /etc/crispy/config.c */
#ifdef CRISPY_SYSCONFDIR
    path = g_build_filename(CRISPY_SYSCONFDIR,
                            "crispy", "config.c", NULL);
    if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
        return path;
    g_free(path);
#endif

    /* 5. /usr/share/crispy/config.c */
#ifdef CRISPY_DATADIR
    path = g_build_filename(CRISPY_DATADIR,
                            "crispy", "config.c", NULL);
    if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
        return path;
    g_free(path);
#endif

    return NULL;
}

gboolean
crispy_config_loader_compile_and_load(
    const gchar         *config_path,
    CrispyCompiler      *compiler,
    CrispyCacheProvider *cache,
    CrispyConfigContext *ctx,
    GError             **error
){
    g_autofree gchar *source_content = NULL;
    g_autofree gchar *raw_params = NULL;
    g_autofree gchar *expanded_params = NULL;
    g_autofree gchar *crispy_flags = NULL;
    g_autofree gchar *extra_flags = NULL;
    g_autofree gchar *hash = NULL;
    g_autofree gchar *so_path = NULL;
    const gchar *compiler_version;
    GModule *module;
    gpointer symbol;
    CrispyConfigInitFunc config_init_fn;
    gboolean result;

    g_return_val_if_fail(config_path != NULL, FALSE);
    g_return_val_if_fail(compiler != NULL, FALSE);
    g_return_val_if_fail(cache != NULL, FALSE);
    g_return_val_if_fail(ctx != NULL, FALSE);

    /* read the config source file */
    if (!g_file_get_contents(config_path, &source_content, NULL, error))
        return FALSE;

    /* extract optional CRISPY_PARAMS from the config source */
    raw_params = crispy_source_extract_params(source_content);

    /* shell-expand CRISPY_PARAMS (supports $(pkg-config ...) etc.) */
    expanded_params = crispy_source_shell_expand(raw_params, error);
    if (expanded_params == NULL && raw_params != NULL)
        return FALSE;
    if (expanded_params == NULL)
        expanded_params = g_strdup("");

    /* get crispy include flags so config can #include <crispy.h> */
    crispy_flags = get_crispy_include_flags();

    /* build the combined extra_flags string for compilation */
    extra_flags = g_strdup_printf("%s %s", crispy_flags, expanded_params);

    /* compute content hash for caching */
    compiler_version = crispy_compiler_get_version(compiler);
    hash = crispy_cache_provider_compute_hash(
        cache, source_content, -1, extra_flags, compiler_version);

    /* get the cached .so path */
    so_path = crispy_cache_provider_get_path(cache, hash);

    /* check cache */
    if (!crispy_cache_provider_has_valid(cache, hash, config_path))
    {
        /* compile the config source to a shared object */
        g_debug("Config compile: %s -> %s", config_path, so_path);
        if (!crispy_compiler_compile_shared(
                compiler, config_path, so_path, extra_flags, error))
        {
            return FALSE;
        }
    }
    else
    {
        g_debug("Config cache hit: %s", so_path);
    }

    /* load the compiled shared object */
    module = g_module_open(so_path, G_MODULE_BIND_LAZY);
    if (module == NULL)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_CONFIG,
                    "Failed to load config module '%s': %s",
                    so_path, g_module_error());
        return FALSE;
    }

    /* resolve the crispy_config_init symbol */
    if (!g_module_symbol(module, "crispy_config_init", &symbol))
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_CONFIG,
                    "Symbol 'crispy_config_init' not found in '%s': %s",
                    so_path, g_module_error());
        g_module_close(module);
        return FALSE;
    }

    /* call the config init function */
    config_init_fn = (CrispyConfigInitFunc)symbol;
    result = config_init_fn(ctx);

    if (!result)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_CONFIG,
                    "crispy_config_init() returned FALSE in '%s'",
                    config_path);
        g_module_close(module);
        return FALSE;
    }

    /* keep the module open so config symbols remain available */

    return TRUE;
}
