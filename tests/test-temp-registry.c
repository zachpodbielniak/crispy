/* test-temp-registry.c - Tests for the temp file cleanup registry */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "../src/core/crispy-temp-registry-private.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <signal.h>

/* test: a path can be added, found, and forgotten */
static void
test_registry_add_remove(void)
{
    crispy_temp_registry_add("/tmp/crispy-registry-probe.c");
    g_assert_true(crispy_temp_registry_contains(
        "/tmp/crispy-registry-probe.c"));

    crispy_temp_registry_remove("/tmp/crispy-registry-probe.c");
    g_assert_false(crispy_temp_registry_contains(
        "/tmp/crispy-registry-probe.c"));

    /* forgetting something never added is not an error */
    crispy_temp_registry_remove("/tmp/crispy-registry-never-added.c");
}

/* test: unlink_all removes every registered file and only those */
static void
test_registry_unlink_all(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *registered = NULL;
    g_autofree gchar *untouched = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-registry-XXXXXX", &error);
    g_assert_no_error(error);

    registered = g_build_filename(tmpdir, "registered.c", NULL);
    untouched = g_build_filename(tmpdir, "untouched.c", NULL);
    g_assert_true(g_file_set_contents(registered, "x", -1, NULL));
    g_assert_true(g_file_set_contents(untouched, "x", -1, NULL));

    crispy_temp_registry_add(registered);
    crispy_temp_registry_unlink_all();

    g_assert_false(g_file_test(registered, G_FILE_TEST_EXISTS));
    g_assert_true(g_file_test(untouched, G_FILE_TEST_EXISTS));

    crispy_temp_registry_remove(registered);
    g_unlink(untouched);
    g_rmdir(tmpdir);
}

/*
 * The body that runs in the trapped subprocess: register a file, install
 * the handlers, and send ourselves a signal with no main loop anywhere.
 *
 * That last part is the whole point.  g_unix_signal_add() puts the
 * handler on a GLib source, which only runs while something iterates the
 * context it was attached to -- and a crispy run has no main loop
 * outside --watch.  So the old handler replaced the default disposition
 * with nothing: the process carried on with SIGINT and SIGTERM caught
 * and never dispatched, and the file stayed behind.
 */
static void
test_registry_signal_subprocess(void)
{
    const gchar *path;

    path = g_getenv("CRISPY_TEST_VICTIM_PATH");
    g_assert_nonnull(path);

    g_assert_true(g_file_set_contents(path, "x", -1, NULL));
    crispy_temp_registry_add(path);

    crispy_temp_registry_install_signal_handlers();

    raise(SIGTERM);

    /* only reachable if the signal was swallowed */
    g_print("signal was absorbed\n");
}

/* test: a signal with no main loop running still cleans up and kills us */
static void
test_registry_signal_cleanup(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *victim = NULL;

    tmpdir = g_dir_make_tmp("crispy-test-registry-XXXXXX", &error);
    g_assert_no_error(error);

    victim = g_build_filename(tmpdir, "victim.c", NULL);
    g_setenv("CRISPY_TEST_VICTIM_PATH", victim, TRUE);

    g_test_trap_subprocess("/temp-registry/signal-cleanup/subprocess",
                           10 * G_USEC_PER_SEC, 0);

    g_test_trap_assert_failed();
    g_test_trap_assert_stdout_unmatched("*signal was absorbed*");

    g_assert_false(g_file_test(victim, G_FILE_TEST_EXISTS));

    g_unsetenv("CRISPY_TEST_VICTIM_PATH");
    g_rmdir(tmpdir);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/temp-registry/add-remove",
                    test_registry_add_remove);
    g_test_add_func("/temp-registry/unlink-all",
                    test_registry_unlink_all);
    g_test_add_func("/temp-registry/signal-cleanup",
                    test_registry_signal_cleanup);
    g_test_add_func("/temp-registry/signal-cleanup/subprocess",
                    test_registry_signal_subprocess);

    return g_test_run();
}
