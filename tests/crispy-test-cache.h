/* crispy-test-cache.h - give a test binary a cache of its own */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * crispy_file_cache_new() resolves g_get_user_cache_dir(), so every test
 * that builds a default cache compiled into the developer's real
 * ~/.cache/crispy -- and test-file-cache's two purge tests emptied it
 * outright.  A suite that deletes the entries the next `crispy
 * script.c` would have loaded does not merely observe the machine it
 * runs on, it changes it, and the damage lands after the suite is green
 * and long away from anything that would explain it.
 *
 * Pointing XDG_CACHE_HOME at a fresh temporary directory gives each
 * binary a cache nothing else shares.  g_get_user_cache_dir() caches its
 * answer on the first call, so the redirect has to happen before
 * anything in main() can ask -- g_test_init() included.
 */

#ifndef CRISPY_TEST_CACHE_H
#define CRISPY_TEST_CACHE_H

#include <glib.h>
#include <glib/gstdio.h>

#include <stdlib.h>

/* the directory crispy_test_use_temp_cache() made, or %NULL before it ran */
static gchar *crispy_test_cache_home = NULL;

/*
 * Remove @path and everything under it.
 *
 * A symlink is unlinked rather than descended into: the tree is one this
 * process created, and following a link out of it would delete something
 * it did not.
 */
static void
crispy_test_cache_remove_tree(
    const gchar *path
){
    GDir *dir;
    const gchar *name;

    if (g_file_test(path, G_FILE_TEST_IS_SYMLINK) ||
        !g_file_test(path, G_FILE_TEST_IS_DIR))
    {
        g_unlink(path);
        return;
    }

    dir = g_dir_open(path, 0, NULL);
    if (dir != NULL)
    {
        while ((name = g_dir_read_name(dir)) != NULL)
        {
            gchar *child;

            child = g_build_filename(path, name, NULL);
            crispy_test_cache_remove_tree(child);
            g_free(child);
        }

        g_dir_close(dir);
    }

    g_rmdir(path);
}

/*
 * atexit() hook: take the temporary cache back out of the temp directory.
 *
 * A failing test aborts before this runs, which is deliberate -- the
 * compiled artifacts of a run that failed are evidence.
 */
static void
crispy_test_cache_cleanup(void)
{
    if (crispy_test_cache_home == NULL)
        return;

    crispy_test_cache_remove_tree(crispy_test_cache_home);
    g_free(crispy_test_cache_home);
    crispy_test_cache_home = NULL;
}

/**
 * crispy_test_use_temp_cache:
 *
 * Redirect XDG_CACHE_HOME at a temporary directory for the rest of this
 * process.  Call it first thing in main(), before g_test_init().
 *
 * Failing to get a temporary directory is fatal on purpose: carrying on
 * means compiling into, and purging, the developer's own cache, which is
 * the whole thing this exists to prevent.
 */
static void
crispy_test_use_temp_cache(void)
{
    GError *error = NULL;
    gchar *dir;

    dir = g_dir_make_tmp("crispy-test-cache-XXXXXX", &error);
    if (dir == NULL)
    {
        g_error("Cannot create a temporary cache directory: %s",
                error != NULL ? error->message : "unknown error");
    }

    g_setenv("XDG_CACHE_HOME", dir, TRUE);
    crispy_test_cache_home = dir;

    atexit(crispy_test_cache_cleanup);
}

/**
 * crispy_test_cache_dir:
 *
 * Only test-file-cache asks, so the attribute is what keeps the other
 * five includers warning-free.
 *
 * Returns: the temporary XDG_CACHE_HOME this binary is using, or %NULL
 *   if crispy_test_use_temp_cache() has not run.
 */
G_GNUC_UNUSED static const gchar *
crispy_test_cache_dir(void)
{
    return crispy_test_cache_home;
}

#endif /* CRISPY_TEST_CACHE_H */
