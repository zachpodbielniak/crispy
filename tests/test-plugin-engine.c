/* test-plugin-engine.c - Tests for the CrispyPluginEngine */

#define CRISPY_COMPILATION
#include "crispy.h"
#include "core/crispy-plugin-engine-private.h"

#include <glib.h>
#include <string.h>
#include <unistd.h>

/* path to the build output directory (set by test runner or default) */
static gchar *plugin_dir = NULL;

/* --- helper: get path to a test plugin --- */
static gchar *
get_test_plugin_path(
    const gchar *name
){
    return g_strdup_printf("%s/test-plugin-%s.so", plugin_dir, name);
}

/* --- tests --- */

/**
 * test_engine_new:
 *
 * Verify that a new engine has zero plugins.
 */
static void
test_engine_new(void)
{
    g_autoptr(CrispyPluginEngine) engine = NULL;

    engine = crispy_plugin_engine_new();
    g_assert_nonnull(engine);
    g_assert_true(CRISPY_IS_PLUGIN_ENGINE(engine));
    g_assert_cmpuint(crispy_plugin_engine_get_plugin_count(engine), ==, 0);
}

/**
 * test_engine_load_single:
 *
 * Load a single noop plugin and verify the count increases.
 */
static void
test_engine_load_single(void)
{
    g_autoptr(CrispyPluginEngine) engine = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = NULL;
    gboolean ret;

    engine = crispy_plugin_engine_new();
    path = get_test_plugin_path("noop");

    ret = crispy_plugin_engine_load(engine, path, &error);
    g_assert_no_error(error);
    g_assert_true(ret);
    g_assert_cmpuint(crispy_plugin_engine_get_plugin_count(engine), ==, 1);
}

/**
 * test_engine_load_missing:
 *
 * Loading a nonexistent plugin should fail with an error.
 */
static void
test_engine_load_missing(void)
{
    g_autoptr(CrispyPluginEngine) engine = NULL;
    g_autoptr(GError) error = NULL;
    gboolean ret;

    engine = crispy_plugin_engine_new();

    ret = crispy_plugin_engine_load(engine, "/nonexistent/plugin.so", &error);
    g_assert_false(ret);
    g_assert_error(error, CRISPY_ERROR, CRISPY_ERROR_PLUGIN);
}

/**
 * test_engine_load_paths:
 *
 * Load multiple plugins via colon-separated paths.
 */
static void
test_engine_load_paths(void)
{
    g_autoptr(CrispyPluginEngine) engine = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *noop_path = NULL;
    g_autofree gchar *hooks_path = NULL;
    g_autofree gchar *paths = NULL;
    gboolean ret;

    engine = crispy_plugin_engine_new();
    noop_path = get_test_plugin_path("noop");
    hooks_path = get_test_plugin_path("hooks");
    paths = g_strdup_printf("%s:%s", noop_path, hooks_path);

    ret = crispy_plugin_engine_load_paths(engine, paths, &error);
    g_assert_no_error(error);
    g_assert_true(ret);
    g_assert_cmpuint(crispy_plugin_engine_get_plugin_count(engine), ==, 2);
}

/**
 * test_engine_load_paths_comma:
 *
 * Load multiple plugins via comma-separated paths.
 */
static void
test_engine_load_paths_comma(void)
{
    g_autoptr(CrispyPluginEngine) engine = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *noop_path = NULL;
    g_autofree gchar *hooks_path = NULL;
    g_autofree gchar *paths = NULL;
    gboolean ret;

    engine = crispy_plugin_engine_new();
    noop_path = get_test_plugin_path("noop");
    hooks_path = get_test_plugin_path("hooks");
    paths = g_strdup_printf("%s,%s", noop_path, hooks_path);

    ret = crispy_plugin_engine_load_paths(engine, paths, &error);
    g_assert_no_error(error);
    g_assert_true(ret);
    g_assert_cmpuint(crispy_plugin_engine_get_plugin_count(engine), ==, 2);
}

/**
 * test_engine_data_store:
 *
 * Test the shared data store for inter-plugin communication.
 */
static void
test_engine_data_store(void)
{
    g_autoptr(CrispyPluginEngine) engine = NULL;
    gpointer val;

    engine = crispy_plugin_engine_new();

    /* initially empty */
    val = crispy_plugin_engine_get_data(engine, "key1");
    g_assert_null(val);

    /* set and get */
    crispy_plugin_engine_set_data(engine, "key1",
                                  g_strdup("hello"),
                                  g_free);
    val = crispy_plugin_engine_get_data(engine, "key1");
    g_assert_nonnull(val);
    g_assert_cmpstr((const gchar *)val, ==, "hello");

    /* overwrite */
    crispy_plugin_engine_set_data(engine, "key1",
                                  g_strdup("world"),
                                  g_free);
    val = crispy_plugin_engine_get_data(engine, "key1");
    g_assert_cmpstr((const gchar *)val, ==, "world");

    /* different key */
    val = crispy_plugin_engine_get_data(engine, "key2");
    g_assert_null(val);
}

/**
 * test_engine_dispatch_noop:
 *
 * Dispatching to the noop plugin should always return CONTINUE.
 */
static void
test_engine_dispatch_noop(void)
{
    g_autoptr(CrispyPluginEngine) engine = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = NULL;
    CrispyHookContext ctx;
    CrispyHookResult result;

    engine = crispy_plugin_engine_new();
    path = get_test_plugin_path("noop");
    crispy_plugin_engine_load(engine, path, &error);
    g_assert_no_error(error);

    memset(&ctx, 0, sizeof(ctx));
    ctx.error = &error;

    result = crispy_plugin_engine_dispatch(engine,
                                            CRISPY_HOOK_SOURCE_LOADED, &ctx);
    g_assert_cmpint(result, ==, CRISPY_HOOK_CONTINUE);
}

/**
 * test_engine_dispatch_no_plugins:
 *
 * Dispatching with no plugins should always return CONTINUE.
 */
static void
test_engine_dispatch_no_plugins(void)
{
    g_autoptr(CrispyPluginEngine) engine = NULL;
    CrispyHookContext ctx;
    CrispyHookResult result;
    gint i;

    engine = crispy_plugin_engine_new();
    memset(&ctx, 0, sizeof(ctx));

    for (i = 0; i < CRISPY_HOOK_POINT_COUNT; i++)
    {
        result = crispy_plugin_engine_dispatch(engine,
                                                (CrispyHookPoint)i, &ctx);
        g_assert_cmpint(result, ==, CRISPY_HOOK_CONTINUE);
    }
}

/**
 * test_engine_dispatch_abort:
 *
 * The abort plugin returns ABORT at PRE_EXECUTE.
 */
static void
test_engine_dispatch_abort(void)
{
    g_autoptr(CrispyPluginEngine) engine = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = NULL;
    CrispyHookContext ctx;
    CrispyHookResult result;

    engine = crispy_plugin_engine_new();
    path = get_test_plugin_path("abort");
    crispy_plugin_engine_load(engine, path, &error);
    g_assert_no_error(error);

    memset(&ctx, 0, sizeof(ctx));
    ctx.error = &error;

    /* other hooks should continue */
    result = crispy_plugin_engine_dispatch(engine,
                                            CRISPY_HOOK_SOURCE_LOADED, &ctx);
    g_assert_cmpint(result, ==, CRISPY_HOOK_CONTINUE);

    /* PRE_EXECUTE should abort */
    result = crispy_plugin_engine_dispatch(engine,
                                            CRISPY_HOOK_PRE_EXECUTE, &ctx);
    g_assert_cmpint(result, ==, CRISPY_HOOK_ABORT);
    g_assert_nonnull(error);
    g_assert_cmpstr(error->message, ==, "Aborted by test-abort plugin");
}

/**
 * test_script_with_plugins:
 *
 * End-to-end test: run a script with the hooks plugin loaded and
 * verify that all expected hooks were called.
 */
static void
test_script_with_plugins(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autoptr(CrispyPluginEngine) engine = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;
    gchar *argv[] = { "test", NULL };

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);
    cache = crispy_file_cache_new();
    engine = crispy_plugin_engine_new();

    path = get_test_plugin_path("hooks");
    crispy_plugin_engine_load(engine, path, &error);
    g_assert_no_error(error);

    script = crispy_script_new_from_inline(
        "return 42;", NULL,
        CRISPY_COMPILER(compiler),
        CRISPY_CACHE_PROVIDER(cache),
        CRISPY_FLAG_FORCE_COMPILE, &error);
    g_assert_no_error(error);
    g_assert_nonnull(script);

    crispy_script_set_plugin_engine(script, engine);

    exit_code = crispy_script_execute(script, 1, argv, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 42);
}

/**
 * test_script_plugin_abort:
 *
 * End-to-end test: verify that an aborting plugin stops execution.
 */
static void
test_script_plugin_abort(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyGccCompiler) compiler = NULL;
    g_autoptr(CrispyFileCache) cache = NULL;
    g_autoptr(CrispyPluginEngine) engine = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;
    gchar *argv[] = { "test", NULL };

    compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);
    cache = crispy_file_cache_new();
    engine = crispy_plugin_engine_new();

    path = get_test_plugin_path("abort");
    crispy_plugin_engine_load(engine, path, &error);
    g_assert_no_error(error);

    script = crispy_script_new_from_inline(
        "return 0;", NULL,
        CRISPY_COMPILER(compiler),
        CRISPY_CACHE_PROVIDER(cache),
        CRISPY_FLAG_FORCE_COMPILE, &error);
    g_assert_no_error(error);

    crispy_script_set_plugin_engine(script, engine);

    exit_code = crispy_script_execute(script, 1, argv, &error);
    g_assert_cmpint(exit_code, ==, -1);
    g_assert_nonnull(error);
    g_assert_cmpstr(error->message, ==, "Aborted by test-abort plugin");
}

/**
 * test_engine_is_final_type:
 *
 * Verify CrispyPluginEngine is a final type.
 */
static void
test_engine_is_final_type(void)
{
    g_assert_true(G_TYPE_IS_FINAL(CRISPY_TYPE_PLUGIN_ENGINE));
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    /* determine plugin directory from LD_LIBRARY_PATH or default */
    plugin_dir = g_strdup(g_getenv("LD_LIBRARY_PATH"));
    if (plugin_dir == NULL || plugin_dir[0] == '\0')
    {
        g_free(plugin_dir);
        plugin_dir = g_strdup("build/release");
    }

    g_test_add_func("/plugin-engine/new", test_engine_new);
    g_test_add_func("/plugin-engine/load-single", test_engine_load_single);
    g_test_add_func("/plugin-engine/load-missing", test_engine_load_missing);
    g_test_add_func("/plugin-engine/load-paths", test_engine_load_paths);
    g_test_add_func("/plugin-engine/load-paths-comma", test_engine_load_paths_comma);
    g_test_add_func("/plugin-engine/data-store", test_engine_data_store);
    g_test_add_func("/plugin-engine/dispatch-noop", test_engine_dispatch_noop);
    g_test_add_func("/plugin-engine/dispatch-no-plugins", test_engine_dispatch_no_plugins);
    g_test_add_func("/plugin-engine/dispatch-abort", test_engine_dispatch_abort);
    g_test_add_func("/plugin-engine/script-with-plugins", test_script_with_plugins);
    g_test_add_func("/plugin-engine/script-plugin-abort", test_script_plugin_abort);
    g_test_add_func("/plugin-engine/is-final-type", test_engine_is_final_type);

    return g_test_run();
}
