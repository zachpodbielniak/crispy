/* crispy-dependency-info.h - Resolved dependency information boxed type */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lightweight boxed type holding resolved pkg-config dependency data:
 * name, compiler flags, linker flags, version, and resolution status.
 * Not a GObject -- use crispy_dependency_info_copy() and
 * crispy_dependency_info_free() for ownership management.
 */

#ifndef CRISPY_DEPENDENCY_INFO_H
#define CRISPY_DEPENDENCY_INFO_H

#if !defined(CRISPY_INSIDE) && !defined(CRISPY_COMPILATION)
#error "Only <crispy.h> can be included directly."
#endif

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * CRISPY_TYPE_DEPENDENCY_INFO:
 *
 * The #GType for #CrispyDependencyInfo.
 */
#define CRISPY_TYPE_DEPENDENCY_INFO (crispy_dependency_info_get_type())

/**
 * CrispyDependencyInfo:
 *
 * Opaque boxed type holding resolved dependency information for a single
 * pkg-config package or library. Fields are populated after pkg-config
 * resolution and expose the name, compiler flags, linker flags, version
 * string, and whether resolution succeeded.
 *
 * Use crispy_dependency_info_new() to create instances,
 * crispy_dependency_info_copy() to clone, and
 * crispy_dependency_info_free() to release.
 */
typedef struct _CrispyDependencyInfo CrispyDependencyInfo;

GType                 crispy_dependency_info_get_type      (void) G_GNUC_CONST;

/**
 * crispy_dependency_info_new:
 * @name: the package or library name (e.g. "json-glib-1.0")
 * @cflags: resolved compiler flags from pkg-config --cflags
 * @ldflags: resolved linker flags from pkg-config --libs
 * @version: (nullable): resolved version string, or %NULL if unavailable
 * @resolved: %TRUE if resolution succeeded, %FALSE otherwise
 *
 * Allocates and initializes a new #CrispyDependencyInfo. All string
 * arguments are copied internally; the caller retains ownership of the
 * originals.
 *
 * Returns: (transfer full): a new #CrispyDependencyInfo
 */
CrispyDependencyInfo *crispy_dependency_info_new           (const gchar          *name,
                                                            const gchar          *cflags,
                                                            const gchar          *ldflags,
                                                            const gchar          *version,
                                                            gboolean              resolved);

/**
 * crispy_dependency_info_copy:
 * @info: a #CrispyDependencyInfo
 *
 * Returns a deep copy of @info. All string fields are duplicated.
 *
 * Returns: (transfer full): a new #CrispyDependencyInfo
 */
CrispyDependencyInfo *crispy_dependency_info_copy          (CrispyDependencyInfo *info);

/**
 * crispy_dependency_info_free:
 * @info: (transfer full): a #CrispyDependencyInfo to free
 *
 * Frees all string fields and the struct itself.
 * Passing %NULL is safe and has no effect.
 */
void                  crispy_dependency_info_free          (CrispyDependencyInfo *info);

/**
 * crispy_dependency_info_get_name:
 * @info: a #CrispyDependencyInfo
 *
 * Returns the package or library name (e.g. "json-glib-1.0").
 *
 * Returns: (transfer none): the dependency name
 */
const gchar          *crispy_dependency_info_get_name      (CrispyDependencyInfo *info);

/**
 * crispy_dependency_info_get_cflags:
 * @info: a #CrispyDependencyInfo
 *
 * Returns the resolved compiler flags from pkg-config --cflags.
 *
 * Returns: (transfer none): the compiler flags string
 */
const gchar          *crispy_dependency_info_get_cflags    (CrispyDependencyInfo *info);

/**
 * crispy_dependency_info_get_ldflags:
 * @info: a #CrispyDependencyInfo
 *
 * Returns the resolved linker flags from pkg-config --libs.
 *
 * Returns: (transfer none): the linker flags string
 */
const gchar          *crispy_dependency_info_get_ldflags   (CrispyDependencyInfo *info);

/**
 * crispy_dependency_info_get_version:
 * @info: a #CrispyDependencyInfo
 *
 * Returns the resolved version string, or %NULL if unavailable.
 *
 * Returns: (transfer none) (nullable): the version string
 */
const gchar          *crispy_dependency_info_get_version   (CrispyDependencyInfo *info);

/**
 * crispy_dependency_info_is_resolved:
 * @info: a #CrispyDependencyInfo
 *
 * Returns %TRUE if pkg-config resolution succeeded for this dependency.
 *
 * Returns: %TRUE if resolved, %FALSE otherwise
 */
gboolean              crispy_dependency_info_is_resolved   (CrispyDependencyInfo *info);

/**
 * crispy_dependency_info_get_combined_flags:
 * @info: a #CrispyDependencyInfo
 *
 * Returns a newly-allocated string combining the cflags and ldflags
 * fields separated by a single space. Useful for passing a single
 * flags string to a compiler invocation.
 *
 * Returns: (transfer full): a new string containing cflags + " " + ldflags
 */
gchar                *crispy_dependency_info_get_combined_flags (CrispyDependencyInfo *info);

G_END_DECLS

#endif /* CRISPY_DEPENDENCY_INFO_H */
