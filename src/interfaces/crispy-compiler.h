/* crispy-compiler.h - CrispyCompiler GInterface */

#ifndef CRISPY_COMPILER_H
#define CRISPY_COMPILER_H

#if !defined(CRISPY_INSIDE) && !defined(CRISPY_COMPILATION)
#error "Only <crispy.h> can be included directly."
#endif

#include <glib-object.h>

G_BEGIN_DECLS

#define CRISPY_TYPE_COMPILER (crispy_compiler_get_type())

G_DECLARE_INTERFACE(CrispyCompiler, crispy_compiler, CRISPY, COMPILER, GObject)

/**
 * CrispyCompilerInterface:
 * @parent_iface: the parent interface
 * @get_version: returns the compiler version string (used as part of cache key)
 * @get_base_flags: returns the base pkg-config flags for default libraries
 * @compile_shared: compiles source to a shared object (.so)
 * @compile_executable: compiles source to a standalone executable (for debugging)
 *
 * The virtual function table for the #CrispyCompiler interface.
 * Implementations provide a compilation backend (e.g., gcc, clang, tcc).
 */
struct _CrispyCompilerInterface
{
    GTypeInterface parent_iface;

    /* virtual methods */

    const gchar * (*get_version)        (CrispyCompiler *self);

    const gchar * (*get_base_flags)     (CrispyCompiler *self);

    gboolean      (*compile_shared)     (CrispyCompiler  *self,
                                         const gchar     *source_path,
                                         const gchar     *output_path,
                                         const gchar     *extra_flags,
                                         GError         **error);

    gboolean      (*compile_executable) (CrispyCompiler  *self,
                                         const gchar     *source_path,
                                         const gchar     *output_path,
                                         const gchar     *extra_flags,
                                         GError         **error);
};

/**
 * crispy_compiler_get_version:
 * @self: a #CrispyCompiler
 *
 * Returns the compiler version string. This is used as part of the
 * cache key to invalidate cached artifacts when the compiler changes.
 *
 * Returns: (transfer none): the compiler version string
 */
const gchar *crispy_compiler_get_version (CrispyCompiler *self);

/**
 * crispy_compiler_get_base_flags:
 * @self: a #CrispyCompiler
 *
 * Returns the base compiler and linker flags for the default libraries
 * (glib-2.0, gobject-2.0, gio-2.0, gmodule-2.0).
 *
 * Returns: (transfer none): the base flags string
 */
const gchar *crispy_compiler_get_base_flags (CrispyCompiler *self);

/**
 * crispy_compiler_compile_shared:
 * @self: a #CrispyCompiler
 * @source_path: path to the C source file
 * @output_path: path for the output .so file
 * @extra_flags: (nullable): additional compiler flags from CRISPY_PARAMS
 * @error: return location for a #GError, or %NULL
 *
 * Compiles the source file to a shared object suitable for loading
 * with g_module_open().
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean crispy_compiler_compile_shared (CrispyCompiler  *self,
                                         const gchar     *source_path,
                                         const gchar     *output_path,
                                         const gchar     *extra_flags,
                                         GError         **error);

/**
 * crispy_compiler_compile_executable:
 * @self: a #CrispyCompiler
 * @source_path: path to the C source file
 * @output_path: path for the output executable
 * @extra_flags: (nullable): additional compiler flags from CRISPY_PARAMS
 * @error: return location for a #GError, or %NULL
 *
 * Compiles the source file to a standalone executable with debug symbols.
 * Used for --gdb mode to allow debugging with gdb.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean crispy_compiler_compile_executable (CrispyCompiler  *self,
                                             const gchar     *source_path,
                                             const gchar     *output_path,
                                             const gchar     *extra_flags,
                                             GError         **error);

G_END_DECLS

#endif /* CRISPY_COMPILER_H */
