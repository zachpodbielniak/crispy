/* crispy-pkg-config-resolver.c - pkg-config CrispyDependencyResolver implementation */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef CRISPY_COMPILATION
#define CRISPY_COMPILATION
#endif
#include "crispy-pkg-config-resolver.h"
#include "crispy-dependency-info.h"
#include "../interfaces/crispy-dependency-resolver.h"
#include "../crispy-types.h"

#include <glib.h>
#include <string.h>

/**
 * SECTION:crispy-pkg-config-resolver
 * @title: CrispyPkgConfigResolver
 * @short_description: pkg-config implementation of the CrispyDependencyResolver interface
 *
 * #CrispyPkgConfigResolver is the default dependency resolution backend
 * for crispy. It delegates all resolution to the system pkg-config binary,
 * querying cflags, linker flags, and version information per package.
 *
 * On construction a probe of `pkg-config --version` is attempted so that
 * a missing tool is detected early. Per-package availability is checked
 * lazily via crispy_dependency_resolver_is_available().
 */

struct _CrispyPkgConfigResolver
{
    GObject parent_instance;
    gchar  *pkg_config_path;   /* path to pkg-config binary */
};

static void crispy_pkg_config_resolver_dep_resolver_init (CrispyDependencyResolverInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE(
    CrispyPkgConfigResolver,
    crispy_pkg_config_resolver,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(CRISPY_TYPE_DEPENDENCY_RESOLVER,
                          crispy_pkg_config_resolver_dep_resolver_init)
)

/* --- helper: run pkg-config with the given argument string --- */

/**
 * run_pkg_config:
 * @self: a #CrispyPkgConfigResolver
 * @args: argument string to append after the pkg-config binary path
 * @output: (out) (transfer full) (nullable): return location for stdout, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Builds a command string of the form "<binary> <args>" and executes it
 * via g_spawn_command_line_sync(). On success the captured stdout is
 * stored in @output (caller must free with g_free()). On spawn failure or
 * non-zero exit status the function returns %FALSE and sets @error.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
static gboolean
run_pkg_config(
    CrispyPkgConfigResolver  *self,
    const gchar              *args,
    gchar                   **output,
    GError                  **error
){
    g_autofree gchar *cmd = NULL;
    gchar *std_out;
    gchar *std_err;
    gint exit_status;

    std_out = NULL;
    std_err = NULL;
    exit_status = 0;

    cmd = g_strdup_printf("%s %s", self->pkg_config_path, args);

    if (!g_spawn_command_line_sync(cmd, &std_out, &std_err, &exit_status, error))
    {
        g_free(std_out);
        g_free(std_err);
        return FALSE;
    }

    g_free(std_err);

    if (!g_spawn_check_wait_status(exit_status, error))
    {
        g_free(std_out);
        return FALSE;
    }

    if (output != NULL)
        *output = std_out;
    else
        g_free(std_out);

    return TRUE;
}

/* --- CrispyDependencyResolver interface implementation --- */

static CrispyDependencyInfo *
resolve_impl(
    CrispyDependencyResolver  *resolver,
    const gchar               *name,
    GError                   **error
){
    CrispyPkgConfigResolver *self;
    g_autofree gchar *cflags_arg = NULL;
    g_autofree gchar *libs_arg = NULL;
    g_autofree gchar *ver_arg = NULL;
    gchar *raw_cflags = NULL;
    gchar *raw_libs = NULL;
    gchar *raw_version = NULL;

    self = CRISPY_PKG_CONFIG_RESOLVER(resolver);

    cflags_arg  = g_strdup_printf("--cflags %s", name);
    libs_arg    = g_strdup_printf("--libs %s", name);
    ver_arg     = g_strdup_printf("--modversion %s", name);

    if (!run_pkg_config(self, cflags_arg, &raw_cflags, NULL))
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_DEPENDENCY,
                    "pkg-config --cflags failed for '%s'", name);
        return NULL;
    }

    if (!run_pkg_config(self, libs_arg, &raw_libs, NULL))
    {
        g_free(raw_cflags);
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_DEPENDENCY,
                    "pkg-config --libs failed for '%s'", name);
        return NULL;
    }

    /* version is best-effort; ignore errors */
    run_pkg_config(self, ver_arg, &raw_version, NULL);

    g_strstrip(raw_cflags);
    g_strstrip(raw_libs);

    if (raw_version != NULL)
        g_strstrip(raw_version);

    {
        CrispyDependencyInfo *info;

        info = crispy_dependency_info_new(name, raw_cflags, raw_libs,
                                         raw_version, TRUE);
        g_free(raw_cflags);
        g_free(raw_libs);
        g_free(raw_version);

        return info;
    }
}

static gboolean
is_available_impl(
    CrispyDependencyResolver *resolver,
    const gchar              *name
){
    CrispyPkgConfigResolver *self;
    g_autofree gchar *exists_arg = NULL;
    g_autofree gchar *cmd = NULL;
    gchar *std_out;
    gchar *std_err;
    gint exit_status;

    self = CRISPY_PKG_CONFIG_RESOLVER(resolver);

    exists_arg = g_strdup_printf("--exists %s", name);
    cmd = g_strdup_printf("%s %s", self->pkg_config_path, exists_arg);

    std_out = NULL;
    std_err = NULL;
    exit_status = 0;

    if (!g_spawn_command_line_sync(cmd, &std_out, &std_err, &exit_status, NULL))
    {
        g_free(std_out);
        g_free(std_err);
        return FALSE;
    }

    g_free(std_out);
    g_free(std_err);

    return g_spawn_check_wait_status(exit_status, NULL);
}

static gchar *
get_version_impl(
    CrispyDependencyResolver *resolver,
    const gchar              *name
){
    CrispyPkgConfigResolver *self;
    g_autofree gchar *ver_arg = NULL;
    gchar *raw_version = NULL;

    self = CRISPY_PKG_CONFIG_RESOLVER(resolver);

    ver_arg = g_strdup_printf("--modversion %s", name);

    if (!run_pkg_config(self, ver_arg, &raw_version, NULL))
        return NULL;

    g_strstrip(raw_version);
    return raw_version;
}

static void
crispy_pkg_config_resolver_dep_resolver_init(
    CrispyDependencyResolverInterface *iface
){
    iface->resolve      = resolve_impl;
    iface->is_available = is_available_impl;
    iface->get_version  = get_version_impl;
}

/* --- GObject lifecycle --- */

static void
crispy_pkg_config_resolver_finalize(
    GObject *object
){
    CrispyPkgConfigResolver *self;

    self = CRISPY_PKG_CONFIG_RESOLVER(object);

    g_free(self->pkg_config_path);

    G_OBJECT_CLASS(crispy_pkg_config_resolver_parent_class)->finalize(object);
}

static void
crispy_pkg_config_resolver_class_init(
    CrispyPkgConfigResolverClass *klass
){
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = crispy_pkg_config_resolver_finalize;
}

static void
crispy_pkg_config_resolver_init(
    CrispyPkgConfigResolver *self
){
    self->pkg_config_path = g_strdup("pkg-config");
}

/* --- public constructors --- */

CrispyPkgConfigResolver *
crispy_pkg_config_resolver_new(
    void
){
    CrispyPkgConfigResolver *self;
    gchar *std_out;
    gchar *std_err;
    gint exit_status;

    self = g_object_new(CRISPY_TYPE_PKG_CONFIG_RESOLVER, NULL);

    /* probe availability -- ignore result, caller checks per-package */
    std_out = NULL;
    std_err = NULL;
    exit_status = 0;
    g_spawn_command_line_sync("pkg-config --version",
                              &std_out, &std_err, &exit_status, NULL);
    g_free(std_out);
    g_free(std_err);

    return self;
}

CrispyPkgConfigResolver *
crispy_pkg_config_resolver_new_with_path(
    const gchar *pkg_config_path
){
    CrispyPkgConfigResolver *self;
    g_autofree gchar *probe_cmd = NULL;
    gchar *std_out;
    gchar *std_err;
    gint exit_status;

    g_return_val_if_fail(pkg_config_path != NULL, NULL);

    self = g_object_new(CRISPY_TYPE_PKG_CONFIG_RESOLVER, NULL);

    g_free(self->pkg_config_path);
    self->pkg_config_path = g_strdup(pkg_config_path);

    /* probe availability with the custom binary -- ignore result */
    probe_cmd = g_strdup_printf("%s --version", pkg_config_path);

    std_out = NULL;
    std_err = NULL;
    exit_status = 0;
    g_spawn_command_line_sync(probe_cmd, &std_out, &std_err, &exit_status, NULL);
    g_free(std_out);
    g_free(std_err);

    return self;
}
