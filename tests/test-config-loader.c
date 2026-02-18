/* test-config-loader.c - Tests for config finding and compile-and-load */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "../src/core/crispy-config-loader.h"
#include "../src/core/crispy-config-context.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

/* --- Helpers --- */

/* write a config file to a temp directory and return the path */
static gchar *
write_temp_config(
    const gchar *content
){
    g_autofree gchar *dir = NULL;
    gchar *path;
    GError *error = NULL;

    dir = g_dir_make_tmp("crispy-test-config-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(dir);

    path = g_build_filename(dir, "config.c", NULL);
    g_assert_true(g_file_set_contents(path, content, -1, &error));
    g_assert_no_error(error);

    return path;
}

/* clean up temp config file and its directory */
static void
cleanup_temp_config(
    gchar *path
){
    g_autofree gchar *dir = NULL;

    dir = g_path_get_dirname(path);
    g_unlink(path);
    g_rmdir(dir);
    g_free(path);
}

/* --- Tests --- */

/* test: find returns NULL when no config exists */
static void
test_config_loader_find_none(void)
{
    g_autofree gchar *path = NULL;

    /* unset all env vars that could provide a config path */
    g_unsetenv("CRISPY_CONFIG_FILE");

    path = crispy_config_loader_find(NULL);
    /* may or may not be NULL depending on system state, but should not crash */
    (void)path;
}

/* test: find uses CRISPY_CONFIG_FILE env var */
static void
test_config_loader_find_env(void)
{
    g_autofree gchar *result = NULL;
    gchar *config_path;

    config_path = write_temp_config(
        "#include <crispy.h>\n"
        "G_MODULE_EXPORT gboolean\n"
        "crispy_config_init(CrispyConfigContext *ctx)\n"
        "{ (void)ctx; return TRUE; }\n");

    g_setenv("CRISPY_CONFIG_FILE", config_path, TRUE);
    result = crispy_config_loader_find(NULL);

    g_assert_cmpstr(result, ==, config_path);

    g_unsetenv("CRISPY_CONFIG_FILE");
    cleanup_temp_config(config_path);
}

/* test: find uses explicit path argument */
static void
test_config_loader_find_explicit(void)
{
    g_autofree gchar *result = NULL;
    gchar *config_path;

    g_unsetenv("CRISPY_CONFIG_FILE");

    config_path = write_temp_config(
        "#include <crispy.h>\n"
        "G_MODULE_EXPORT gboolean\n"
        "crispy_config_init(CrispyConfigContext *ctx)\n"
        "{ (void)ctx; return TRUE; }\n");

    result = crispy_config_loader_find(config_path);
    g_assert_cmpstr(result, ==, config_path);

    cleanup_temp_config(config_path);
}

/* test: find returns NULL for nonexistent explicit path */
static void
test_config_loader_find_explicit_missing(void)
{
    g_autofree gchar *result = NULL;

    g_unsetenv("CRISPY_CONFIG_FILE");

    result = crispy_config_loader_find("/tmp/nonexistent-crispy-config.c");
    /* should not find the nonexistent path */
    if (result != NULL)
        g_assert_cmpstr(result, !=, "/tmp/nonexistent-crispy-config.c");
}

/* test: env var takes precedence over explicit path */
static void
test_config_loader_find_env_precedence(void)
{
    g_autofree gchar *result = NULL;
    gchar *env_config;
    gchar *explicit_config;

    env_config = write_temp_config(
        "#include <crispy.h>\n"
        "G_MODULE_EXPORT gboolean\n"
        "crispy_config_init(CrispyConfigContext *ctx)\n"
        "{ (void)ctx; return TRUE; }\n");

    explicit_config = write_temp_config(
        "#include <crispy.h>\n"
        "G_MODULE_EXPORT gboolean\n"
        "crispy_config_init(CrispyConfigContext *ctx)\n"
        "{ (void)ctx; return TRUE; }\n");

    g_setenv("CRISPY_CONFIG_FILE", env_config, TRUE);
    result = crispy_config_loader_find(explicit_config);

    /* env var wins */
    g_assert_cmpstr(result, ==, env_config);

    g_unsetenv("CRISPY_CONFIG_FILE");
    cleanup_temp_config(env_config);
    cleanup_temp_config(explicit_config);
}

/* test: compile and load a trivial config that returns TRUE */
static void
test_config_loader_compile_and_load_trivial(void)
{
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autoptr(GError) error = NULL;
    CrispyConfigContext ctx;
    gchar *config_path;
    gboolean result;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);

    cache = crispy_file_cache_new();

    config_path = write_temp_config(
        "#include <crispy.h>\n"
        "G_MODULE_EXPORT gboolean\n"
        "crispy_config_init(CrispyConfigContext *ctx)\n"
        "{ (void)ctx; return TRUE; }\n");

    crispy_config_context_init_internal(
        &ctx, 0, NULL, 0, NULL, NULL);

    result = crispy_config_loader_compile_and_load(
        config_path,
        CRISPY_COMPILER(compiler),
        CRISPY_CACHE_PROVIDER(cache),
        &ctx,
        &error);

    g_assert_no_error(error);
    g_assert_true(result);

    crispy_config_context_clear_internal(&ctx);
    cleanup_temp_config(config_path);
}

/* test: compile and load a config that sets extra_flags */
static void
test_config_loader_sets_extra_flags(void)
{
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autoptr(GError) error = NULL;
    CrispyConfigContext ctx;
    gchar *config_path;
    gboolean result;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);

    cache = crispy_file_cache_new();

    config_path = write_temp_config(
        "#include <crispy.h>\n"
        "G_MODULE_EXPORT gboolean\n"
        "crispy_config_init(CrispyConfigContext *ctx)\n"
        "{\n"
        "    crispy_config_context_set_extra_flags(ctx, \"-lm -lpthread\");\n"
        "    return TRUE;\n"
        "}\n");

    crispy_config_context_init_internal(
        &ctx, 0, NULL, 0, NULL, NULL);

    result = crispy_config_loader_compile_and_load(
        config_path,
        CRISPY_COMPILER(compiler),
        CRISPY_CACHE_PROVIDER(cache),
        &ctx,
        &error);

    g_assert_no_error(error);
    g_assert_true(result);
    g_assert_cmpstr(
        crispy_config_context_get_extra_flags_internal(&ctx),
        ==, "-lm -lpthread");

    crispy_config_context_clear_internal(&ctx);
    cleanup_temp_config(config_path);
}

/* test: compile and load a config that returns FALSE */
static void
test_config_loader_returns_false(void)
{
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autoptr(GError) error = NULL;
    CrispyConfigContext ctx;
    gchar *config_path;
    gboolean result;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);

    cache = crispy_file_cache_new();

    config_path = write_temp_config(
        "#include <crispy.h>\n"
        "G_MODULE_EXPORT gboolean\n"
        "crispy_config_init(CrispyConfigContext *ctx)\n"
        "{ (void)ctx; return FALSE; }\n");

    crispy_config_context_init_internal(
        &ctx, 0, NULL, 0, NULL, NULL);

    result = crispy_config_loader_compile_and_load(
        config_path,
        CRISPY_COMPILER(compiler),
        CRISPY_CACHE_PROVIDER(cache),
        &ctx,
        &error);

    g_assert_false(result);
    g_assert_error(error, CRISPY_ERROR, CRISPY_ERROR_CONFIG);

    crispy_config_context_clear_internal(&ctx);
    cleanup_temp_config(config_path);
}

/* test: compile fails on syntax error */
static void
test_config_loader_compile_error(void)
{
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autoptr(GError) error = NULL;
    CrispyConfigContext ctx;
    gchar *config_path;
    gboolean result;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);

    cache = crispy_file_cache_new();

    config_path = write_temp_config(
        "#include <crispy.h>\n"
        "THIS IS NOT VALID C;\n");

    crispy_config_context_init_internal(
        &ctx, 0, NULL, 0, NULL, NULL);

    result = crispy_config_loader_compile_and_load(
        config_path,
        CRISPY_COMPILER(compiler),
        CRISPY_CACHE_PROVIDER(cache),
        &ctx,
        &error);

    g_assert_false(result);
    g_assert_nonnull(error);

    crispy_config_context_clear_internal(&ctx);
    cleanup_temp_config(config_path);
}

/* test: config that sets multiple fields at once */
static void
test_config_loader_sets_multiple_fields(void)
{
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autoptr(GError) error = NULL;
    CrispyConfigContext ctx;
    gchar *config_path;
    gboolean result;
    gboolean flags_set;
    guint flags;

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);

    cache = crispy_file_cache_new();

    config_path = write_temp_config(
        "#include <crispy.h>\n"
        "G_MODULE_EXPORT gboolean\n"
        "crispy_config_init(CrispyConfigContext *ctx)\n"
        "{\n"
        "    crispy_config_context_set_extra_flags(ctx, \"-lm\");\n"
        "    crispy_config_context_set_override_flags(ctx, \"-Wall\");\n"
        "    crispy_config_context_set_cache_dir(ctx, \"/tmp/test-cache\");\n"
        "    crispy_config_context_set_flags(ctx, 1);\n"
        "    crispy_config_context_set_plugin_data(ctx, \"key\", \"val\");\n"
        "    return TRUE;\n"
        "}\n");

    crispy_config_context_init_internal(
        &ctx, 0, NULL, 0, NULL, NULL);

    result = crispy_config_loader_compile_and_load(
        config_path,
        CRISPY_COMPILER(compiler),
        CRISPY_CACHE_PROVIDER(cache),
        &ctx,
        &error);

    g_assert_no_error(error);
    g_assert_true(result);

    g_assert_cmpstr(
        crispy_config_context_get_extra_flags_internal(&ctx),
        ==, "-lm");
    g_assert_cmpstr(
        crispy_config_context_get_override_flags_internal(&ctx),
        ==, "-Wall");
    g_assert_cmpstr(
        crispy_config_context_get_cache_dir_internal(&ctx),
        ==, "/tmp/test-cache");

    flags = crispy_config_context_get_flags_internal(&ctx, &flags_set);
    g_assert_true(flags_set);
    g_assert_cmpuint(flags, ==, 1);

    g_assert_cmpstr(
        g_hash_table_lookup(
            crispy_config_context_get_plugin_data_internal(&ctx), "key"),
        ==, "val");

    crispy_config_context_clear_internal(&ctx);
    cleanup_temp_config(config_path);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/config-loader/find-none",
                     test_config_loader_find_none);
    g_test_add_func("/config-loader/find-env",
                     test_config_loader_find_env);
    g_test_add_func("/config-loader/find-explicit",
                     test_config_loader_find_explicit);
    g_test_add_func("/config-loader/find-explicit-missing",
                     test_config_loader_find_explicit_missing);
    g_test_add_func("/config-loader/find-env-precedence",
                     test_config_loader_find_env_precedence);
    g_test_add_func("/config-loader/compile-and-load-trivial",
                     test_config_loader_compile_and_load_trivial);
    g_test_add_func("/config-loader/sets-extra-flags",
                     test_config_loader_sets_extra_flags);
    g_test_add_func("/config-loader/returns-false",
                     test_config_loader_returns_false);
    g_test_add_func("/config-loader/compile-error",
                     test_config_loader_compile_error);
    g_test_add_func("/config-loader/sets-multiple-fields",
                     test_config_loader_sets_multiple_fields);

    return g_test_run();
}
