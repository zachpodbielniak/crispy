/* test-script.c - End-to-end tests for CrispyScript */

#define CRISPY_COMPILATION
#include "../src/crispy.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

/* shared compiler and cache for all tests */
static CrispyGccCompiler *g_compiler = NULL;
static CrispyFileCache *g_cache = NULL;

/* helper: write a temp .c file and return its path */
static gchar *
write_temp_script(
    const gchar *source
){
    g_autofree gchar *tmpl = NULL;
    gint fd;

    tmpl = g_strdup("/tmp/crispy-test-script-XXXXXX.c");
    fd = g_mkstemp(tmpl);
    g_assert_cmpint(fd, >=, 0);
    write(fd, source, strlen(source));
    close(fd);

    return g_steal_pointer(&tmpl);
}

/* test: hello world script from file */
static void
test_script_from_file_hello(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;

    path = write_temp_script(
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv){\n"
        "    g_print(\"hello test\\n\");\n"
        "    return 0;\n"
        "}\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);
    g_assert_nonnull(script);

    exit_code = crispy_script_execute(script, 1, &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 0);

    g_unlink(path);
}

/* test: script exit code propagation */
static void
test_script_from_file_exit_code(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;

    path = write_temp_script(
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv){\n"
        "    return 42;\n"
        "}\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(script, 1, &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 42);
    g_assert_cmpint(crispy_script_get_exit_code(script), ==, 42);

    g_unlink(path);
}

/* test: inline mode */
static void
test_script_from_inline(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    gchar *fake_argv[] = { "crispy-inline", NULL };
    gint exit_code;

    script = crispy_script_new_from_inline(
        "g_print(\"inline test\\n\"); return 0;",
        NULL,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);
    g_assert_nonnull(script);

    exit_code = crispy_script_execute(script, 1, fake_argv, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 0);
}

/* test: CRISPY_PARAMS with -lm */
static void
test_script_crispy_params(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;

    path = write_temp_script(
        "#define CRISPY_PARAMS \"-lm\"\n"
        "#include <math.h>\n"
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv){\n"
        "    double val = sqrt(144.0);\n"
        "    return (val == 12.0) ? 0 : 1;\n"
        "}\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(script, 1, &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 0);

    g_unlink(path);
}

/* test: shebang is stripped and script compiles */
static void
test_script_shebang_strip(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;

    path = write_temp_script(
        "#!/usr/bin/crispy\n"
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv){\n"
        "    return 0;\n"
        "}\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(script, 1, &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 0);

    g_unlink(path);
}

/* test: compilation error produces GError */
static void
test_script_compile_error(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;

    path = write_temp_script("this is not valid C;\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(script, 1, &path, &error);
    g_assert_cmpint(exit_code, ==, -1);
    g_assert_error(error, CRISPY_ERROR, CRISPY_ERROR_COMPILE);

    g_unlink(path);
}

/* test: preserve source flag keeps temp file */
static void
test_script_preserve_source(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    const gchar *temp_path;

    path = write_temp_script(
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv){ return 0; }\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE | CRISPY_FLAG_PRESERVE_SOURCE,
        &error);
    g_assert_no_error(error);

    crispy_script_execute(script, 1, &path, &error);
    g_assert_no_error(error);

    temp_path = crispy_script_get_temp_source_path(script);
    g_assert_nonnull(temp_path);
    g_assert_true(g_file_test(temp_path, G_FILE_TEST_EXISTS));

    /* cleanup manually since preserve is set */
    g_unlink(temp_path);
    g_unlink(path);
}

/* test: argument passing to script */
static void
test_script_arg_passing(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gchar *test_argv[] = { "test", "7", NULL };
    gint exit_code;

    path = write_temp_script(
        "#include <glib.h>\n"
        "#include <stdlib.h>\n"
        "gint main(gint argc, gchar **argv){\n"
        "    if (argc < 2) return -1;\n"
        "    return atoi(argv[1]);\n"
        "}\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(script, 2, test_argv, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 7);

    g_unlink(path);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_autoptr(GError) error = NULL;

    g_test_init(&argc, &argv, NULL);

    /* set up shared fixtures */
    g_compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);
    g_cache = crispy_file_cache_new();

    g_test_add_func("/script/from-file-hello",
                    test_script_from_file_hello);
    g_test_add_func("/script/from-file-exit-code",
                    test_script_from_file_exit_code);
    g_test_add_func("/script/from-inline",
                    test_script_from_inline);
    g_test_add_func("/script/crispy-params",
                    test_script_crispy_params);
    g_test_add_func("/script/shebang-strip",
                    test_script_shebang_strip);
    g_test_add_func("/script/compile-error",
                    test_script_compile_error);
    g_test_add_func("/script/preserve-source",
                    test_script_preserve_source);
    g_test_add_func("/script/arg-passing",
                    test_script_arg_passing);

    return g_test_run();
}
