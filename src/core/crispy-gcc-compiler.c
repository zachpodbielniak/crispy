/* crispy-gcc-compiler.c - GCC-based CrispyCompiler implementation */

#define CRISPY_COMPILATION
#include "crispy-gcc-compiler.h"
#include "../interfaces/crispy-compiler.h"
#include "../crispy-types.h"

#include <glib.h>
#include <string.h>

/**
 * SECTION:crispy-gcc-compiler
 * @title: CrispyGccCompiler
 * @short_description: GCC implementation of the CrispyCompiler interface
 *
 * #CrispyGccCompiler is the default compiler backend for crispy. It
 * uses gcc to compile C source files into shared objects or standalone
 * executables.
 *
 * On construction, it probes `gcc --version` and caches the output of
 * `pkg-config --cflags --libs glib-2.0 gobject-2.0 gio-2.0 gmodule-2.0`
 * so that these do not need to be re-evaluated on every compilation.
 */

struct _CrispyGccCompiler
{
    GObject parent_instance;
};

typedef struct
{
    gchar *gcc_version;     /* first line of gcc --version */
    gchar *base_flags;      /* cached pkg-config output */
} CrispyGccCompilerPrivate;

static void crispy_gcc_compiler_compiler_init (CrispyCompilerInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE(
    CrispyGccCompiler,
    crispy_gcc_compiler,
    G_TYPE_OBJECT,
    G_ADD_PRIVATE(CrispyGccCompiler)
    G_IMPLEMENT_INTERFACE(CRISPY_TYPE_COMPILER,
                          crispy_gcc_compiler_compiler_init)
)

/* --- helper: run a command and capture stdout --- */
static gchar *
run_command_stdout(
    const gchar  *cmd,
    GError      **error
){
    gchar *std_out;
    gchar *std_err;
    gint exit_status;

    std_out = NULL;
    std_err = NULL;
    exit_status = 0;

    if (!g_spawn_command_line_sync(cmd, &std_out, &std_err, &exit_status, error))
    {
        g_free(std_out);
        g_free(std_err);
        return NULL;
    }

    g_free(std_err);

    if (!g_spawn_check_wait_status(exit_status, error))
    {
        g_free(std_out);
        return NULL;
    }

    return std_out;
}

/* --- helper: extract first line from a string --- */
static gchar *
first_line(
    const gchar *text
){
    const gchar *nl;

    if (text == NULL)
        return NULL;

    nl = strchr(text, '\n');
    if (nl != NULL)
        return g_strndup(text, (gsize)(nl - text));

    return g_strdup(text);
}

/* --- helper: build and run a gcc command --- */
static gboolean
run_gcc(
    CrispyGccCompilerPrivate  *priv,
    const gchar               *mode_flags,
    const gchar               *source_path,
    const gchar               *output_path,
    const gchar               *extra_flags,
    GError                   **error
){
    g_autofree gchar *cmd = NULL;
    gchar *std_out;
    gchar *std_err;
    gint exit_status;

    std_out = NULL;
    std_err = NULL;
    exit_status = 0;

    /* build the compilation command */
    cmd = g_strdup_printf("gcc -std=gnu89 %s %s %s -o %s %s",
                          mode_flags,
                          priv->base_flags,
                          extra_flags != NULL ? extra_flags : "",
                          output_path,
                          source_path);

    if (!g_spawn_command_line_sync(cmd, &std_out, &std_err, &exit_status, error))
    {
        g_free(std_out);
        g_free(std_err);
        return FALSE;
    }

    g_free(std_out);

    if (!g_spawn_check_wait_status(exit_status, NULL))
    {
        /* report gcc stderr as the error message */
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_COMPILE,
                    "Compilation failed:\n%s\nCommand: %s",
                    std_err != NULL ? std_err : "(no output)",
                    cmd);
        g_free(std_err);
        return FALSE;
    }

    g_free(std_err);
    return TRUE;
}

/* --- CrispyCompiler interface implementation --- */

static const gchar *
gcc_compiler_get_version(
    CrispyCompiler *self
){
    CrispyGccCompilerPrivate *priv;

    priv = crispy_gcc_compiler_get_instance_private(CRISPY_GCC_COMPILER(self));
    return priv->gcc_version;
}

static const gchar *
gcc_compiler_get_base_flags(
    CrispyCompiler *self
){
    CrispyGccCompilerPrivate *priv;

    priv = crispy_gcc_compiler_get_instance_private(CRISPY_GCC_COMPILER(self));
    return priv->base_flags;
}

static gboolean
gcc_compiler_compile_shared(
    CrispyCompiler  *self,
    const gchar     *source_path,
    const gchar     *output_path,
    const gchar     *extra_flags,
    GError         **error
){
    CrispyGccCompilerPrivate *priv;

    priv = crispy_gcc_compiler_get_instance_private(CRISPY_GCC_COMPILER(self));
    return run_gcc(priv, "-shared -fPIC", source_path, output_path,
                   extra_flags, error);
}

static gboolean
gcc_compiler_compile_executable(
    CrispyCompiler  *self,
    const gchar     *source_path,
    const gchar     *output_path,
    const gchar     *extra_flags,
    GError         **error
){
    CrispyGccCompilerPrivate *priv;

    priv = crispy_gcc_compiler_get_instance_private(CRISPY_GCC_COMPILER(self));
    return run_gcc(priv, "-g -O0", source_path, output_path,
                   extra_flags, error);
}

static void
crispy_gcc_compiler_compiler_init(
    CrispyCompilerInterface *iface
){
    iface->get_version        = gcc_compiler_get_version;
    iface->get_base_flags     = gcc_compiler_get_base_flags;
    iface->compile_shared     = gcc_compiler_compile_shared;
    iface->compile_executable = gcc_compiler_compile_executable;
}

/* --- GObject lifecycle --- */

static void
crispy_gcc_compiler_finalize(
    GObject *object
){
    CrispyGccCompilerPrivate *priv;

    priv = crispy_gcc_compiler_get_instance_private(CRISPY_GCC_COMPILER(object));

    g_free(priv->gcc_version);
    g_free(priv->base_flags);

    G_OBJECT_CLASS(crispy_gcc_compiler_parent_class)->finalize(object);
}

static void
crispy_gcc_compiler_class_init(
    CrispyGccCompilerClass *klass
){
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = crispy_gcc_compiler_finalize;
}

static void
crispy_gcc_compiler_init(
    CrispyGccCompiler *self
){
    /* instance init -- fields zeroed by GObject */
    (void)self;
}

/* --- public constructor --- */

CrispyGccCompiler *
crispy_gcc_compiler_new(
    GError **error
){
    CrispyGccCompiler *self;
    CrispyGccCompilerPrivate *priv;
    g_autofree gchar *raw_version = NULL;
    g_autofree gchar *raw_flags = NULL;

    /* probe gcc version */
    raw_version = run_command_stdout("gcc --version", error);
    if (raw_version == NULL)
    {
        if (error != NULL && *error != NULL)
        {
            /* wrap the error with our domain */
            GError *orig;

            orig = g_steal_pointer(error);
            g_set_error(error,
                        CRISPY_ERROR,
                        CRISPY_ERROR_GCC_NOT_FOUND,
                        "Failed to probe gcc: %s", orig->message);
            g_error_free(orig);
        }
        return NULL;
    }

    /* probe pkg-config for base flags */
    raw_flags = run_command_stdout(
        "pkg-config --cflags --libs glib-2.0 gobject-2.0 gio-2.0 gmodule-2.0",
        error);
    if (raw_flags == NULL)
        return NULL;

    /* strip trailing newline from flags */
    g_strstrip(raw_flags);

    self = g_object_new(CRISPY_TYPE_GCC_COMPILER, NULL);
    priv = crispy_gcc_compiler_get_instance_private(self);

    priv->gcc_version = first_line(raw_version);
    priv->base_flags = g_strdup(raw_flags);

    return self;
}
