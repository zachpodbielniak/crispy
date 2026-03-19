/* crispy-pkg-config-resolver.h - pkg-config CrispyDependencyResolver implementation */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef CRISPY_PKG_CONFIG_RESOLVER_H
#define CRISPY_PKG_CONFIG_RESOLVER_H

#if !defined(CRISPY_INSIDE) && !defined(CRISPY_COMPILATION)
#error "Only <crispy.h> can be included directly."
#endif

#include <glib-object.h>

G_BEGIN_DECLS

#define CRISPY_TYPE_PKG_CONFIG_RESOLVER (crispy_pkg_config_resolver_get_type())

G_DECLARE_FINAL_TYPE(CrispyPkgConfigResolver, crispy_pkg_config_resolver, CRISPY, PKG_CONFIG_RESOLVER, GObject)

/**
 * crispy_pkg_config_resolver_new:
 *
 * Creates a new #CrispyPkgConfigResolver that uses the system pkg-config
 * binary. The binary is located by searching %PATH for "pkg-config".
 * A probe of `pkg-config --version` is performed at construction time
 * to verify that the tool is accessible; the object is returned regardless,
 * and per-package availability is checked lazily via
 * crispy_dependency_resolver_is_available().
 *
 * The created object implements the #CrispyDependencyResolver interface.
 *
 * Returns: (transfer full): a new #CrispyPkgConfigResolver
 */
CrispyPkgConfigResolver *crispy_pkg_config_resolver_new (void);

/**
 * crispy_pkg_config_resolver_new_with_path:
 * @pkg_config_path: path to the pkg-config binary to use
 *
 * Creates a new #CrispyPkgConfigResolver using a custom pkg-config binary.
 * Useful when pkg-config is not on %PATH or when a cross-compilation
 * wrapper (e.g. arm-linux-gnueabihf-pkg-config) is required.
 *
 * The created object implements the #CrispyDependencyResolver interface.
 *
 * Returns: (transfer full): a new #CrispyPkgConfigResolver
 */
CrispyPkgConfigResolver *crispy_pkg_config_resolver_new_with_path (const gchar *pkg_config_path);

G_END_DECLS

#endif /* CRISPY_PKG_CONFIG_RESOLVER_H */
