/* crispy-compiler.c - CrispyCompiler GInterface implementation */

#define CRISPY_COMPILATION
#include "crispy-compiler.h"
#include "../crispy-types.h"

/* error quark defined once for the whole library */
G_DEFINE_QUARK(crispy-error-quark, crispy_error)

/**
 * SECTION:crispy-compiler
 * @title: CrispyCompiler
 * @short_description: Interface for C compilation backends
 *
 * #CrispyCompiler is a GInterface that defines the contract for
 * compiling C source files into loadable shared objects or standalone
 * executables. The default implementation is #CrispyGccCompiler which
 * uses gcc.
 *
 * Custom implementations can be created to support other compilers
 * such as clang or tcc.
 */

G_DEFINE_INTERFACE(CrispyCompiler, crispy_compiler, G_TYPE_OBJECT)

static void
crispy_compiler_default_init(
    CrispyCompilerInterface *iface
){
    /* no default implementations or signals */
    (void)iface;
}

const gchar *
crispy_compiler_get_version(
    CrispyCompiler *self
){
    CrispyCompilerInterface *iface;

    g_return_val_if_fail(CRISPY_IS_COMPILER(self), NULL);

    iface = CRISPY_COMPILER_GET_IFACE(self);
    g_return_val_if_fail(iface->get_version != NULL, NULL);

    return iface->get_version(self);
}

const gchar *
crispy_compiler_get_base_flags(
    CrispyCompiler *self
){
    CrispyCompilerInterface *iface;

    g_return_val_if_fail(CRISPY_IS_COMPILER(self), NULL);

    iface = CRISPY_COMPILER_GET_IFACE(self);
    g_return_val_if_fail(iface->get_base_flags != NULL, NULL);

    return iface->get_base_flags(self);
}

gboolean
crispy_compiler_compile_shared(
    CrispyCompiler  *self,
    const gchar     *source_path,
    const gchar     *output_path,
    const gchar     *extra_flags,
    GError         **error
){
    CrispyCompilerInterface *iface;

    g_return_val_if_fail(CRISPY_IS_COMPILER(self), FALSE);
    g_return_val_if_fail(source_path != NULL, FALSE);
    g_return_val_if_fail(output_path != NULL, FALSE);
    g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

    iface = CRISPY_COMPILER_GET_IFACE(self);
    g_return_val_if_fail(iface->compile_shared != NULL, FALSE);

    return iface->compile_shared(self, source_path, output_path,
                                 extra_flags, error);
}

gboolean
crispy_compiler_compile_executable(
    CrispyCompiler  *self,
    const gchar     *source_path,
    const gchar     *output_path,
    const gchar     *extra_flags,
    GError         **error
){
    CrispyCompilerInterface *iface;

    g_return_val_if_fail(CRISPY_IS_COMPILER(self), FALSE);
    g_return_val_if_fail(source_path != NULL, FALSE);
    g_return_val_if_fail(output_path != NULL, FALSE);
    g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

    iface = CRISPY_COMPILER_GET_IFACE(self);
    g_return_val_if_fail(iface->compile_executable != NULL, FALSE);

    return iface->compile_executable(self, source_path, output_path,
                                     extra_flags, error);
}
