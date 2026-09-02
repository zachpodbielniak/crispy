/* crispy-compiler.c - CrispyCompiler GInterface implementation */

#ifndef CRISPY_COMPILATION
#define CRISPY_COMPILATION
#endif
#include "crispy-compiler.h"
#include "../crispy-types.h"
#include "../core/crispy-header-tracker-private.h"

#include <glib/gstdio.h>
#include <errno.h>
#include <unistd.h>

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

/*
 * The vfunc signature shared by compile_shared and compile_executable,
 * so that one publishing helper can serve both.
 */
typedef gboolean (*CrispyCompileVFunc) (CrispyCompiler  *self,
                                        const gchar     *source_path,
                                        const gchar     *output_path,
                                        const gchar     *extra_flags,
                                        GError         **error);

/*
 * compile_and_publish:
 * @self: a #CrispyCompiler
 * @compile: the backend vfunc to run
 * @source_path: the C source to compile
 * @output_path: the name the finished artifact must appear under
 * @extra_flags: (nullable): additional compiler flags
 * @error: return location for a #GError, or %NULL
 *
 * Runs @compile into a staging name in @output_path's own directory and
 * renames the result into place only once the backend has succeeded.
 *
 * @output_path is a name other processes load from: crispy hands the
 * cache path straight to the compiler, and gcc's own output is visible
 * to a concurrent reader while the linker is still writing it.  Eight
 * concurrent runs of one script against a cold cache failed in four of
 * six rounds with "file too short" and "invalid ELF header", and a
 * compile killed part-way left a stub in the cache that every later run
 * loaded for ever.  rename() within one directory is atomic, so a reader
 * sees either the previous artifact or the finished one and never a
 * half-written file.  crispy_installer_install() already did this and
 * said why; the rule belonged here, where every caller and every backend
 * reaches it.
 *
 * Returns: %TRUE on success
 */
static gboolean
compile_and_publish(
    CrispyCompiler      *self,
    CrispyCompileVFunc   compile,
    const gchar         *source_path,
    const gchar         *output_path,
    const gchar         *extra_flags,
    GError             **error
){
    g_autofree gchar *dir = NULL;
    g_autofree gchar *basename = NULL;
    g_autofree gchar *staging_name = NULL;
    g_autofree gchar *staging_path = NULL;
    g_autofree gchar *staging_dep = NULL;
    g_autofree gchar *output_dep = NULL;
    gint fd;

    dir = g_path_get_dirname(output_path);
    basename = g_path_get_basename(output_path);

    /*
     * The staging name keeps @output_path's extension so that whatever
     * the backend writes beside its output -- a dependency file -- lands
     * under a name that maps back to the published one, and it stays in
     * @output_path's directory so the publishing rename cannot cross a
     * filesystem and fall back to a copy.
     */
    staging_name = g_strdup_printf(".crispy-stage-XXXXXX-%s", basename);
    staging_path = g_build_filename(dir, staging_name, NULL);

    fd = g_mkstemp(staging_path);
    if (fd < 0)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_IO,
                    "Failed to create a staging file in %s: %s",
                    dir, g_strerror(errno));
        return FALSE;
    }
    close(fd);

    /*
     * Drop the placeholder so the backend creates the file itself: the
     * 0600 g_mkstemp() gave it would otherwise survive a linker that
     * truncates its output rather than unlinking it, and a cached object
     * or an installed binary would come out with the wrong mode.  The
     * name stays reserved for as long as it takes the backend to open
     * it, which is the same trade crispy_installer_install() makes.
     */
    g_unlink(staging_path);

    staging_dep = crispy_header_tracker_get_depfile_path(staging_path);
    output_dep = crispy_header_tracker_get_depfile_path(output_path);

    if (!compile(self, source_path, staging_path, extra_flags, error))
    {
        g_unlink(staging_path);
        g_unlink(staging_dep);
        return FALSE;
    }

    /*
     * Publish the dependency file first.  A reader decides the artifact
     * exists before it looks for the dependency list, so an artifact
     * that appears without one would be trusted with no record of the
     * headers it was built from -- which is the stale-header bug itself.
     */
    if (g_file_test(staging_dep, G_FILE_TEST_IS_REGULAR) &&
        g_rename(staging_dep, output_dep) != 0)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_IO,
                    "Failed to publish dependency file %s as %s: %s",
                    staging_dep, output_dep, g_strerror(errno));
        g_unlink(staging_path);
        g_unlink(staging_dep);
        return FALSE;
    }

    if (g_rename(staging_path, output_path) != 0)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_IO,
                    "Failed to publish %s as %s: %s",
                    staging_path, output_path, g_strerror(errno));
        g_unlink(staging_path);
        return FALSE;
    }

    return TRUE;
}

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

    return compile_and_publish(self, iface->compile_shared,
                               source_path, output_path,
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

    return compile_and_publish(self, iface->compile_executable,
                               source_path, output_path,
                               extra_flags, error);
}
