/* test-config-context.c - Tests for CrispyConfigContext */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "../src/core/crispy-config-context.h"

#include <glib.h>
#include <string.h>

/* --- Helper: create and init a config context --- */
static void
init_test_ctx(
    CrispyConfigContext *ctx,
    gint                 script_argc,
    gchar              **script_argv
){
    const gchar *fake_crispy_argv[] = { "crispy", "-n", NULL };

    crispy_config_context_init_internal(
        ctx,
        2, (const gchar **)fake_crispy_argv,
        script_argc, script_argv,
        (script_argc > 0) ? script_argv[0] : NULL);
}

/* test: init sets crispy argc/argv correctly */
static void
test_config_context_init(void)
{
    CrispyConfigContext ctx;
    const gchar *fake_crispy_argv[] = { "crispy", "-v", NULL };
    gchar *script_argv[] = { "test.c", "--flag", NULL };

    crispy_config_context_init_internal(
        &ctx,
        2, (const gchar **)fake_crispy_argv,
        2, script_argv,
        "test.c");

    g_assert_cmpint(crispy_config_context_get_crispy_argc(&ctx), ==, 2);
    g_assert_nonnull(crispy_config_context_get_crispy_argv(&ctx));

    g_assert_cmpint(crispy_config_context_get_script_argc(&ctx), ==, 2);
    g_assert_nonnull(crispy_config_context_get_script_argv(&ctx));
    g_assert_cmpstr(crispy_config_context_get_script_argv(&ctx)[0], ==, "test.c");

    g_assert_cmpstr(crispy_config_context_get_script_path(&ctx), ==, "test.c");

    crispy_config_context_clear_internal(&ctx);
}

/* test: script path is NULL when not provided */
static void
test_config_context_null_script_path(void)
{
    CrispyConfigContext ctx;

    init_test_ctx(&ctx, 0, NULL);

    g_assert_null(crispy_config_context_get_script_path(&ctx));

    crispy_config_context_clear_internal(&ctx);
}

/* test: set and get extra_flags */
static void
test_config_context_extra_flags(void)
{
    CrispyConfigContext ctx;

    init_test_ctx(&ctx, 0, NULL);

    /* initially NULL */
    g_assert_null(crispy_config_context_get_extra_flags_internal(&ctx));

    /* set extra flags */
    crispy_config_context_set_extra_flags(&ctx, "-lm -lpthread");
    g_assert_cmpstr(
        crispy_config_context_get_extra_flags_internal(&ctx),
        ==, "-lm -lpthread");

    /* replace extra flags */
    crispy_config_context_set_extra_flags(&ctx, "-lz");
    g_assert_cmpstr(
        crispy_config_context_get_extra_flags_internal(&ctx),
        ==, "-lz");

    crispy_config_context_clear_internal(&ctx);
}

/* test: append extra_flags */
static void
test_config_context_append_extra_flags(void)
{
    CrispyConfigContext ctx;

    init_test_ctx(&ctx, 0, NULL);

    crispy_config_context_set_extra_flags(&ctx, "-lm");
    crispy_config_context_append_extra_flags(&ctx, "-lpthread");

    g_assert_cmpstr(
        crispy_config_context_get_extra_flags_internal(&ctx),
        ==, "-lm -lpthread");

    crispy_config_context_clear_internal(&ctx);
}

/* test: append to empty extra_flags */
static void
test_config_context_append_extra_flags_empty(void)
{
    CrispyConfigContext ctx;

    init_test_ctx(&ctx, 0, NULL);

    crispy_config_context_append_extra_flags(&ctx, "-lm");

    g_assert_cmpstr(
        crispy_config_context_get_extra_flags_internal(&ctx),
        ==, "-lm");

    crispy_config_context_clear_internal(&ctx);
}

/* test: set and get override_flags */
static void
test_config_context_override_flags(void)
{
    CrispyConfigContext ctx;

    init_test_ctx(&ctx, 0, NULL);

    g_assert_null(crispy_config_context_get_override_flags_internal(&ctx));

    crispy_config_context_set_override_flags(&ctx, "-Wall -Werror");
    g_assert_cmpstr(
        crispy_config_context_get_override_flags_internal(&ctx),
        ==, "-Wall -Werror");

    crispy_config_context_clear_internal(&ctx);
}

/* test: append override_flags */
static void
test_config_context_append_override_flags(void)
{
    CrispyConfigContext ctx;

    init_test_ctx(&ctx, 0, NULL);

    crispy_config_context_set_override_flags(&ctx, "-fsanitize=address");
    crispy_config_context_append_override_flags(&ctx, "-fsanitize=undefined");

    g_assert_cmpstr(
        crispy_config_context_get_override_flags_internal(&ctx),
        ==, "-fsanitize=address -fsanitize=undefined");

    crispy_config_context_clear_internal(&ctx);
}

/* test: add_plugin accumulates paths */
static void
test_config_context_add_plugin(void)
{
    CrispyConfigContext ctx;
    GPtrArray *paths;

    init_test_ctx(&ctx, 0, NULL);

    crispy_config_context_add_plugin(&ctx, "/usr/lib/foo.so");
    crispy_config_context_add_plugin(&ctx, "/usr/lib/bar.so");

    paths = crispy_config_context_get_plugin_paths_internal(&ctx);
    g_assert_nonnull(paths);
    g_assert_cmpuint(paths->len, ==, 2);
    g_assert_cmpstr(g_ptr_array_index(paths, 0), ==, "/usr/lib/foo.so");
    g_assert_cmpstr(g_ptr_array_index(paths, 1), ==, "/usr/lib/bar.so");

    crispy_config_context_clear_internal(&ctx);
}

/* test: plugin data key-value store */
static void
test_config_context_plugin_data(void)
{
    CrispyConfigContext ctx;
    GHashTable *data;

    init_test_ctx(&ctx, 0, NULL);

    crispy_config_context_set_plugin_data(&ctx, "key1", "value1");
    crispy_config_context_set_plugin_data(&ctx, "key2", "value2");

    data = crispy_config_context_get_plugin_data_internal(&ctx);
    g_assert_nonnull(data);
    g_assert_cmpstr(g_hash_table_lookup(data, "key1"), ==, "value1");
    g_assert_cmpstr(g_hash_table_lookup(data, "key2"), ==, "value2");

    /* overwrite key1 */
    crispy_config_context_set_plugin_data(&ctx, "key1", "new_value");
    g_assert_cmpstr(g_hash_table_lookup(data, "key1"), ==, "new_value");

    crispy_config_context_clear_internal(&ctx);
}

/* test: set_flags and add_flags */
static void
test_config_context_flags(void)
{
    CrispyConfigContext ctx;
    gboolean flags_set;
    guint flags;

    init_test_ctx(&ctx, 0, NULL);

    /* initially not set */
    flags = crispy_config_context_get_flags_internal(&ctx, &flags_set);
    g_assert_false(flags_set);
    g_assert_cmpuint(flags, ==, 0);

    /* set flags */
    crispy_config_context_set_flags(&ctx, CRISPY_FLAG_FORCE_COMPILE);
    flags = crispy_config_context_get_flags_internal(&ctx, &flags_set);
    g_assert_true(flags_set);
    g_assert_cmpuint(flags, ==, CRISPY_FLAG_FORCE_COMPILE);

    /* add flags (OR) */
    crispy_config_context_add_flags(&ctx, CRISPY_FLAG_PRESERVE_SOURCE);
    flags = crispy_config_context_get_flags_internal(&ctx, &flags_set);
    g_assert_cmpuint(flags, ==,
        CRISPY_FLAG_FORCE_COMPILE | CRISPY_FLAG_PRESERVE_SOURCE);

    crispy_config_context_clear_internal(&ctx);
}

/* test: cache_dir override */
static void
test_config_context_cache_dir(void)
{
    CrispyConfigContext ctx;

    init_test_ctx(&ctx, 0, NULL);

    g_assert_null(crispy_config_context_get_cache_dir_internal(&ctx));

    crispy_config_context_set_cache_dir(&ctx, "/tmp/my-cache");
    g_assert_cmpstr(
        crispy_config_context_get_cache_dir_internal(&ctx),
        ==, "/tmp/my-cache");

    crispy_config_context_clear_internal(&ctx);
}

/* test: set_script_argv replaces argv and takes ownership */
static void
test_config_context_set_script_argv(void)
{
    CrispyConfigContext ctx;
    gchar *orig_argv[] = { "orig.c", NULL };
    gchar **new_argv;

    init_test_ctx(&ctx, 1, orig_argv);
    g_assert_cmpstr(crispy_config_context_get_script_argv(&ctx)[0],
                     ==, "orig.c");

    /* replace with new argv (context takes ownership) */
    new_argv = g_new0(gchar *, 3);
    new_argv[0] = g_strdup("replaced.c");
    new_argv[1] = g_strdup("--new-arg");
    new_argv[2] = NULL;

    crispy_config_context_set_script_argv(&ctx, 2, new_argv);

    g_assert_cmpint(crispy_config_context_get_script_argc(&ctx), ==, 2);
    g_assert_cmpstr(crispy_config_context_get_script_argv(&ctx)[0],
                     ==, "replaced.c");
    g_assert_cmpstr(crispy_config_context_get_script_argv(&ctx)[1],
                     ==, "--new-arg");

    /* clear will free the owned argv */
    crispy_config_context_clear_internal(&ctx);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/config-context/init",
                     test_config_context_init);
    g_test_add_func("/config-context/null-script-path",
                     test_config_context_null_script_path);
    g_test_add_func("/config-context/extra-flags",
                     test_config_context_extra_flags);
    g_test_add_func("/config-context/append-extra-flags",
                     test_config_context_append_extra_flags);
    g_test_add_func("/config-context/append-extra-flags-empty",
                     test_config_context_append_extra_flags_empty);
    g_test_add_func("/config-context/override-flags",
                     test_config_context_override_flags);
    g_test_add_func("/config-context/append-override-flags",
                     test_config_context_append_override_flags);
    g_test_add_func("/config-context/add-plugin",
                     test_config_context_add_plugin);
    g_test_add_func("/config-context/plugin-data",
                     test_config_context_plugin_data);
    g_test_add_func("/config-context/flags",
                     test_config_context_flags);
    g_test_add_func("/config-context/cache-dir",
                     test_config_context_cache_dir);
    g_test_add_func("/config-context/set-script-argv",
                     test_config_context_set_script_argv);

    return g_test_run();
}
