/* crispy-dependency-info.c - Resolved dependency information boxed type */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of CrispyDependencyInfo, a GLib boxed type that holds
 * the result of resolving a pkg-config dependency: name, cflags, ldflags,
 * version, and a resolved flag.  Uses g_slice_new0/g_slice_free for
 * allocation and g_strdup/g_free for all string fields.
 */

#ifndef CRISPY_COMPILATION
#define CRISPY_COMPILATION
#endif
#include "crispy-dependency-info.h"

#include <glib.h>

/**
 * SECTION:crispy-dependency-info
 * @title: CrispyDependencyInfo
 * @short_description: Resolved pkg-config dependency record
 *
 * #CrispyDependencyInfo is a lightweight boxed type (not a #GObject)
 * that captures the result of resolving a single pkg-config dependency.
 * It stores the package name, compiler flags, linker flags, optional
 * version string, and a boolean indicating whether resolution succeeded.
 *
 * Instances are created with crispy_dependency_info_new(), deep-copied
 * with crispy_dependency_info_copy(), and released with
 * crispy_dependency_info_free().  The type is registered with the GLib
 * type system as %CRISPY_TYPE_DEPENDENCY_INFO so it can be used as a
 * #GValue, stored in #GPtrArray with the correct free function, or
 * exposed via GObject Introspection.
 */

struct _CrispyDependencyInfo
{
    gchar    *name;
    gchar    *cflags;
    gchar    *ldflags;
    gchar    *version;    /* nullable */
    gboolean  resolved;
};

G_DEFINE_BOXED_TYPE(CrispyDependencyInfo,
                    crispy_dependency_info,
                    crispy_dependency_info_copy,
                    crispy_dependency_info_free)

/* --- public API --- */

CrispyDependencyInfo *
crispy_dependency_info_new(
    const gchar *name,
    const gchar *cflags,
    const gchar *ldflags,
    const gchar *version,
    gboolean     resolved
){
    CrispyDependencyInfo *info;

    info = g_slice_new0(CrispyDependencyInfo);
    info->name     = g_strdup(name);
    info->cflags   = g_strdup(cflags);
    info->ldflags  = g_strdup(ldflags);
    info->version  = g_strdup(version);
    info->resolved = resolved;

    return info;
}

CrispyDependencyInfo *
crispy_dependency_info_copy(
    CrispyDependencyInfo *info
){
    CrispyDependencyInfo *copy;

    g_return_val_if_fail(info != NULL, NULL);

    copy = g_slice_new0(CrispyDependencyInfo);
    copy->name     = g_strdup(info->name);
    copy->cflags   = g_strdup(info->cflags);
    copy->ldflags  = g_strdup(info->ldflags);
    copy->version  = g_strdup(info->version);
    copy->resolved = info->resolved;

    return copy;
}

void
crispy_dependency_info_free(
    CrispyDependencyInfo *info
){
    if (info == NULL)
        return;

    g_free(info->name);
    g_free(info->cflags);
    g_free(info->ldflags);
    g_free(info->version);

    g_slice_free(CrispyDependencyInfo, info);
}

const gchar *
crispy_dependency_info_get_name(
    CrispyDependencyInfo *info
){
    g_return_val_if_fail(info != NULL, NULL);
    return info->name;
}

const gchar *
crispy_dependency_info_get_cflags(
    CrispyDependencyInfo *info
){
    g_return_val_if_fail(info != NULL, NULL);
    return info->cflags;
}

const gchar *
crispy_dependency_info_get_ldflags(
    CrispyDependencyInfo *info
){
    g_return_val_if_fail(info != NULL, NULL);
    return info->ldflags;
}

const gchar *
crispy_dependency_info_get_version(
    CrispyDependencyInfo *info
){
    g_return_val_if_fail(info != NULL, NULL);
    return info->version;
}

gboolean
crispy_dependency_info_is_resolved(
    CrispyDependencyInfo *info
){
    g_return_val_if_fail(info != NULL, FALSE);
    return info->resolved;
}

gchar *
crispy_dependency_info_get_combined_flags(
    CrispyDependencyInfo *info
){
    g_return_val_if_fail(info != NULL, NULL);
    return g_strdup_printf("%s %s",
                           info->cflags  != NULL ? info->cflags  : "",
                           info->ldflags != NULL ? info->ldflags : "");
}
