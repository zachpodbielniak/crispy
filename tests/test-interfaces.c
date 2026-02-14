/* test-interfaces.c - Interface contract tests */

#define CRISPY_COMPILATION
#include "../src/crispy.h"

#include <glib.h>

/* test: CrispyCompiler interface type is valid */
static void
test_compiler_interface_type(void)
{
    GType type;

    type = CRISPY_TYPE_COMPILER;
    g_assert_true(G_TYPE_IS_INTERFACE(type));
}

/* test: CrispyCacheProvider interface type is valid */
static void
test_cache_provider_interface_type(void)
{
    GType type;

    type = CRISPY_TYPE_CACHE_PROVIDER;
    g_assert_true(G_TYPE_IS_INTERFACE(type));
}

/* test: GccCompiler type is a final GObject type */
static void
test_gcc_compiler_is_final(void)
{
    GType type;

    type = CRISPY_TYPE_GCC_COMPILER;
    g_assert_true(G_TYPE_IS_OBJECT(type));
    g_assert_true(G_TYPE_IS_FINAL(type));
}

/* test: FileCache type is a final GObject type */
static void
test_file_cache_is_final(void)
{
    GType type;

    type = CRISPY_TYPE_FILE_CACHE;
    g_assert_true(G_TYPE_IS_OBJECT(type));
    g_assert_true(G_TYPE_IS_FINAL(type));
}

/* test: Script type is a final GObject type */
static void
test_script_is_final(void)
{
    GType type;

    type = CRISPY_TYPE_SCRIPT;
    g_assert_true(G_TYPE_IS_OBJECT(type));
    g_assert_true(G_TYPE_IS_FINAL(type));
}

/* test: GccCompiler conforms to CrispyCompiler interface */
static void
test_gcc_compiler_conforms(void)
{
    gboolean conforms;

    conforms = g_type_is_a(CRISPY_TYPE_GCC_COMPILER, CRISPY_TYPE_COMPILER);
    g_assert_true(conforms);
}

/* test: FileCache conforms to CrispyCacheProvider interface */
static void
test_file_cache_conforms(void)
{
    gboolean conforms;

    conforms = g_type_is_a(CRISPY_TYPE_FILE_CACHE, CRISPY_TYPE_CACHE_PROVIDER);
    g_assert_true(conforms);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/interfaces/compiler-type",
                    test_compiler_interface_type);
    g_test_add_func("/interfaces/cache-provider-type",
                    test_cache_provider_interface_type);
    g_test_add_func("/interfaces/gcc-compiler-is-final",
                    test_gcc_compiler_is_final);
    g_test_add_func("/interfaces/file-cache-is-final",
                    test_file_cache_is_final);
    g_test_add_func("/interfaces/script-is-final",
                    test_script_is_final);
    g_test_add_func("/interfaces/gcc-compiler-conforms",
                    test_gcc_compiler_conforms);
    g_test_add_func("/interfaces/file-cache-conforms",
                    test_file_cache_conforms);

    return g_test_run();
}
