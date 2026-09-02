/* test-watcher.c - Type-system tests for CrispyWatcher */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "crispy-test-cache.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

/* shared fixtures */
static CrispyGccCompiler *g_compiler = NULL;
static CrispyFileCache   *g_cache    = NULL;

/* test: CRISPY_TYPE_WATCHER is a valid, final GObject type */
static void
test_watcher_type(void)
{
    GType type;

    type = CRISPY_TYPE_WATCHER;
    g_assert_cmpuint(type, !=, G_TYPE_INVALID);
    g_assert_true(G_TYPE_IS_OBJECT(type));
    g_assert_true(G_TYPE_IS_FINAL(type));
}

/* test: creates successfully with a real compiler and cache */
static void
test_watcher_new(void)
{
    g_autoptr(CrispyWatcher) watcher = NULL;
    const gchar *fake_path = "/tmp/nonexistent-script.c";

    watcher = crispy_watcher_new(fake_path,
                                 CRISPY_COMPILER(g_compiler),
                                 CRISPY_CACHE_PROVIDER(g_cache),
                                 CRISPY_FLAG_NONE);

    g_assert_nonnull(watcher);
    g_assert_true(CRISPY_IS_WATCHER(watcher));
}

/* test: default debounce is 500 ms */
static void
test_watcher_default_debounce(void)
{
    g_autoptr(CrispyWatcher) watcher = NULL;
    guint debounce;

    watcher = crispy_watcher_new("/tmp/fake.c",
                                 CRISPY_COMPILER(g_compiler),
                                 CRISPY_CACHE_PROVIDER(g_cache),
                                 CRISPY_FLAG_NONE);

    debounce = crispy_watcher_get_debounce_ms(watcher);

    g_assert_cmpuint(debounce, ==, 500);
}

/* test: set_debounce_ms round-trips through get_debounce_ms */
static void
test_watcher_set_debounce(void)
{
    g_autoptr(CrispyWatcher) watcher = NULL;
    guint debounce;

    watcher = crispy_watcher_new("/tmp/fake.c",
                                 CRISPY_COMPILER(g_compiler),
                                 CRISPY_CACHE_PROVIDER(g_cache),
                                 CRISPY_FLAG_NONE);

    crispy_watcher_set_debounce_ms(watcher, 250);
    debounce = crispy_watcher_get_debounce_ms(watcher);

    g_assert_cmpuint(debounce, ==, 250);
}

/* test: is_running returns FALSE before start() is called */
static void
test_watcher_not_running(void)
{
    g_autoptr(CrispyWatcher) watcher = NULL;

    watcher = crispy_watcher_new("/tmp/fake.c",
                                 CRISPY_COMPILER(g_compiler),
                                 CRISPY_CACHE_PROVIDER(g_cache),
                                 CRISPY_FLAG_NONE);

    g_assert_false(crispy_watcher_is_running(watcher));
}

/* test: all three signals are registered on the type */
static void
test_watcher_signals_exist(void)
{
    guint id_changed;
    guint id_executed;
    guint id_error;

    id_changed  = g_signal_lookup("script-changed",  CRISPY_TYPE_WATCHER);
    id_executed = g_signal_lookup("script-executed",  CRISPY_TYPE_WATCHER);
    id_error    = g_signal_lookup("watch-error",      CRISPY_TYPE_WATCHER);

    g_assert_cmpuint(id_changed,  !=, 0);
    g_assert_cmpuint(id_executed, !=, 0);
    g_assert_cmpuint(id_error,    !=, 0);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_autoptr(GError) error = NULL;

    /*
     * Before g_test_init(), because g_get_user_cache_dir() caches
     * its first answer and this suite must not compile into -- or
     * purge -- the developer's own ~/.cache/crispy.
     */
    crispy_test_use_temp_cache();

    g_test_init(&argc, &argv, NULL);

    g_compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);
    g_cache = crispy_file_cache_new();

    g_test_add_func("/watcher/watcher-type",
                    test_watcher_type);
    g_test_add_func("/watcher/watcher-new",
                    test_watcher_new);
    g_test_add_func("/watcher/default-debounce",
                    test_watcher_default_debounce);
    g_test_add_func("/watcher/set-debounce",
                    test_watcher_set_debounce);
    g_test_add_func("/watcher/not-running",
                    test_watcher_not_running);
    g_test_add_func("/watcher/signals-exist",
                    test_watcher_signals_exist);

    return g_test_run();
}
