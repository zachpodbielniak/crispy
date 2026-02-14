/* crispy-gcc-compiler.h - GCC-based CrispyCompiler implementation */

#ifndef CRISPY_GCC_COMPILER_H
#define CRISPY_GCC_COMPILER_H

#if !defined(CRISPY_INSIDE) && !defined(CRISPY_COMPILATION)
#error "Only <crispy.h> can be included directly."
#endif

#include <glib-object.h>

G_BEGIN_DECLS

#define CRISPY_TYPE_GCC_COMPILER (crispy_gcc_compiler_get_type())

G_DECLARE_FINAL_TYPE(CrispyGccCompiler, crispy_gcc_compiler, CRISPY, GCC_COMPILER, GObject)

/**
 * crispy_gcc_compiler_new:
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #CrispyGccCompiler instance. On construction, the
 * compiler probes gcc for its version and caches the pkg-config
 * output for the default GLib/GObject/GIO libraries.
 *
 * The created object implements the #CrispyCompiler interface.
 *
 * Returns: (transfer full): a new #CrispyGccCompiler, or %NULL on error
 */
CrispyGccCompiler *crispy_gcc_compiler_new (GError **error);

G_END_DECLS

#endif /* CRISPY_GCC_COMPILER_H */
