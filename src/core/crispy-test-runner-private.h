/* crispy-test-runner-private.h - Test function discovery and harness runner */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Discovers and runs test_*() functions found in crispy scripts.
 * Generates a GTest harness main(), compiles it as a shared library,
 * loads it, and calls g_test_run() via the symbol.  Implements the
 * `crispy test` subcommand.  This header is NOT installed or included
 * in the public umbrella header.
 */

#ifndef CRISPY_TEST_RUNNER_PRIVATE_H
#define CRISPY_TEST_RUNNER_PRIVATE_H

#include <glib.h>

G_BEGIN_DECLS

/* Forward declarations — full types defined in their own headers */
typedef struct _CrispyCompiler      CrispyCompiler;
typedef struct _CrispyCacheProvider CrispyCacheProvider;

/**
 * crispy_test_runner_discover:
 * @source_path: path to the C source file
 * @out_tests: (out) (transfer full) (array zero-terminated=1): discovered test function names
 * @error: return location for a #GError, or %NULL
 *
 * Scans a source file for functions matching the pattern test_*().
 * Uses simple text scanning (looks for lines like "void test_something("
 * or "int test_something(" or "gint test_something(").
 *
 * Returns: %TRUE on success (even if no tests found)
 */
gboolean crispy_test_runner_discover (const gchar   *source_path,
                                      gchar       ***out_tests,
                                      GError       **error);

/**
 * crispy_test_runner_generate_harness:
 * @source_content: original source text (with shebang/CRISPY_PARAMS stripped)
 * @test_names: (array length=test_count): array of test function names
 * @test_count: number of test functions
 *
 * Generates a modified source file that replaces the user's main() (if any)
 * with a test harness main() that calls g_test_init() and registers each
 * test function with g_test_add_func().
 *
 * Returns: (transfer full): the generated source text
 */
gchar *crispy_test_runner_generate_harness (const gchar  *source_content,
                                            gchar       **test_names,
                                            gint          test_count);

/**
 * crispy_test_runner_run:
 * @source_path: path to the C source file containing test_* functions
 * @compiler: a #CrispyCompiler
 * @cache: a #CrispyCacheProvider
 * @extra_flags: (nullable): additional compiler flags
 * @error: return location for a #GError, or %NULL
 *
 * Discovers test functions, generates a test harness main() that calls
 * g_test_init() and registers each test_* function, compiles the result
 * as a shared library, loads it, and runs g_test_run().
 *
 * Returns: number of test failures (0 = all passed), or -1 on error
 */
gint crispy_test_runner_run (const gchar          *source_path,
                             CrispyCompiler       *compiler,
                             CrispyCacheProvider  *cache,
                             const gchar          *extra_flags,
                             GError              **error);

G_END_DECLS

#endif /* CRISPY_TEST_RUNNER_PRIVATE_H */
