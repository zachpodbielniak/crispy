/* crispy-test-runner-private.c - Test function discovery and harness runner */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * Implements test discovery via regex scanning, GTest harness generation,
 * and the full compile-load-run pipeline for `crispy test`.
 */

#ifndef CRISPY_COMPILATION
#define CRISPY_COMPILATION
#endif
#include "crispy-test-runner-private.h"
#include "crispy-source-utils-private.h"
#include "crispy-temp-registry-private.h"
#include "../interfaces/crispy-compiler.h"
#include "../interfaces/crispy-cache-provider.h"
#include "../crispy-types.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <gmodule.h>
#include <string.h>
#include <unistd.h>

/* Regex pattern: matches "void test_foo(", "int test_foo(", "gint test_foo(", etc. */
#define TEST_FUNC_PATTERN \
    "^\\s*(?:void|int|gint|gboolean)\\s+(test_\\w+)\\s*\\("

/* -------------------------------------------------------------------------
 * crispy_test_runner_discover
 * ---------------------------------------------------------------------- */

gboolean
crispy_test_runner_discover(
    const gchar   *source_path,
    gchar       ***out_tests,
    GError       **error
){
    gchar       *contents;
    gsize        len;
    GRegex      *regex;
    GMatchInfo  *match_info;
    GPtrArray   *results;
    gchar      **lines;
    gint         i;

    g_return_val_if_fail(source_path != NULL, FALSE);
    g_return_val_if_fail(out_tests != NULL, FALSE);

    *out_tests = NULL;

    if (!g_file_get_contents(source_path, &contents, &len, error))
        return FALSE;

    regex = g_regex_new(TEST_FUNC_PATTERN,
                        G_REGEX_MULTILINE,
                        0,
                        error);
    if (regex == NULL)
    {
        g_free(contents);
        return FALSE;
    }

    results = g_ptr_array_new_with_free_func(g_free);

    lines = g_strsplit(contents, "\n", -1);
    for (i = 0; lines[i] != NULL; i++)
    {
        match_info = NULL;
        if (g_regex_match(regex, lines[i], 0, &match_info))
        {
            gchar *name;

            name = g_match_info_fetch(match_info, 1);
            if (name != NULL && name[0] != '\0')
                g_ptr_array_add(results, name);
            else
                g_free(name);
        }
        if (match_info != NULL)
            g_match_info_free(match_info);
    }

    g_strfreev(lines);
    g_regex_unref(regex);
    g_free(contents);

    /* NULL-terminate and hand off */
    g_ptr_array_add(results, NULL);
    *out_tests = (gchar **)g_ptr_array_free(results, FALSE);

    return TRUE;
}

/* -------------------------------------------------------------------------
 * crispy_test_runner_generate_harness
 * ---------------------------------------------------------------------- */

gchar *
crispy_test_runner_generate_harness(
    const gchar  *source_content,
    gchar       **test_names,
    gint          test_count
){
    GString     *out;
    GRegex      *main_regex;
    gchar       *modified;
    gint         i;

    g_return_val_if_fail(source_content != NULL, NULL);
    g_return_val_if_fail(test_names != NULL || test_count == 0, NULL);

    /*
     * Rename any existing main() to _user_main() so it does not conflict
     * with the generated harness main.  We match:
     *   "\nmain("  or  "\nint\nmain("  or  "\nint main("
     * and rewrite to "_user_main(".
     */
    main_regex = g_regex_new(
        "\\b(int\\s+)?main\\s*(?=\\()",
        G_REGEX_MULTILINE,
        0,
        NULL);

    if (main_regex != NULL)
    {
        modified = g_regex_replace(main_regex,
                                   source_content, -1, 0,
                                   "_user_main",
                                   0, NULL);
        g_regex_unref(main_regex);
    }
    else
    {
        modified = g_strdup(source_content);
    }

    out = g_string_new(modified);
    g_free(modified);

    /* Forward-declare each test function */
    g_string_append(out, "\n/* --- generated test harness --- */\n");

    for (i = 0; i < test_count; i++)
    {
        g_string_append_printf(out,
                               "extern void %s (void);\n",
                               test_names[i]);
    }

    /* Generated main() */
    g_string_append(out,
                    "\nint\nmain(\n"
                    "    int    argc,\n"
                    "    char **argv\n"
                    "){\n"
                    "    g_test_init(&argc, &argv, NULL);\n");

    for (i = 0; i < test_count; i++)
    {
        g_string_append_printf(out,
                               "    g_test_add_func(\"/%s\", %s);\n",
                               test_names[i],
                               test_names[i]);
    }

    g_string_append(out, "    return g_test_run();\n}\n");

    return g_string_free(out, FALSE);
}

/* -------------------------------------------------------------------------
 * crispy_test_runner_run
 * ---------------------------------------------------------------------- */

gint
crispy_test_runner_run(
    const gchar          *source_path,
    CrispyCompiler       *compiler,
    CrispyCacheProvider  *cache,
    const gchar          *extra_flags,
    GError              **error
){
    gchar            *source_content;
    gsize             source_len;
    gchar            *crispy_params;
    gchar            *expanded_params;
    gchar            *modified_source;
    gchar            *harness_source;
    gchar           **test_names;
    gint              test_count;
    gchar            *temp_path;
    gchar            *so_path;
    gchar            *hash;
    gchar            *compile_flags;
    gchar            *include_flag;
    GModule          *module;
    CrispyMainFunc    main_func;
    gint              result;
    gint              fd;
    gint              i;
    const gchar      *compiler_version;

    g_return_val_if_fail(source_path != NULL, -1);
    g_return_val_if_fail(CRISPY_IS_COMPILER(compiler), -1);
    g_return_val_if_fail(CRISPY_IS_CACHE_PROVIDER(cache), -1);

    source_content   = NULL;
    modified_source  = NULL;
    harness_source   = NULL;
    test_names       = NULL;
    temp_path        = NULL;
    so_path          = NULL;
    hash             = NULL;
    compile_flags    = NULL;
    include_flag     = NULL;
    module           = NULL;
    expanded_params  = NULL;
    result           = -1;

    /* [1] Read source */
    if (!g_file_get_contents(source_path, &source_content, &source_len, error))
        goto cleanup;

    /* [2] Discover test functions */
    if (!crispy_test_runner_discover(source_path, &test_names, error))
        goto cleanup;

    test_count = 0;
    while (test_names[test_count] != NULL)
        test_count++;

    if (test_count == 0)
    {
        g_print("No test functions found in %s\n", source_path);
        result = 0;
        goto cleanup;
    }

    g_print("Found %d test function%s\n",
            test_count,
            test_count == 1 ? "" : "s");

    /* [3] Extract and expand CRISPY_PARAMS from source */
    crispy_params   = crispy_source_extract_params(source_content);
    modified_source = crispy_source_strip_header(source_content, NULL);

    expanded_params = crispy_source_shell_expand(crispy_params, error);
    g_free(crispy_params);
    if (expanded_params == NULL)
        goto cleanup;

    /* [4] Generate the harness source */
    harness_source = crispy_test_runner_generate_harness(
        modified_source, test_names, test_count);

    /*
     * The harness is compiled from the temp directory, not from where
     * the script lives, so a quoted include of a sibling header has to
     * be told where "beside the script" is.
     */
    include_flag = crispy_source_include_flag_for(source_path);

    /* [5] Compute cache hash over harness source + flags */
    {
        GString *hash_input;

        hash_input = g_string_new(NULL);

        if (include_flag != NULL)
        {
            g_string_append(hash_input, include_flag);
            g_string_append_c(hash_input, ' ');
        }

        if (extra_flags != NULL && extra_flags[0] != '\0')
        {
            g_string_append(hash_input, extra_flags);
            g_string_append_c(hash_input, ' ');
        }
        if (expanded_params != NULL && expanded_params[0] != '\0')
        {
            g_string_append(hash_input, expanded_params);
        }

        compiler_version = crispy_compiler_get_version(compiler);
        hash = crispy_cache_provider_compute_hash(
            cache,
            harness_source,
            (gssize)strlen(harness_source),
            hash_input->str,
            compiler_version);

        g_string_free(hash_input, TRUE);
    }

    so_path = crispy_cache_provider_get_path(cache, hash);

    /* [6] Write harness to temp file */
    temp_path = g_build_filename(g_get_tmp_dir(), "crispy-test-XXXXXX.c", NULL);
    fd = g_mkstemp(temp_path);
    if (fd >= 0)
        crispy_temp_registry_add(temp_path);
    if (fd < 0)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_IO,
                    "Failed to create temp file for test harness");
        goto cleanup;
    }

    {
        gsize harness_len;
        gssize written;

        harness_len = strlen(harness_source);
        written = write(fd, harness_source, harness_len);
        close(fd);

        if (written < 0 || (gsize)written != harness_len)
        {
            g_set_error(error,
                        CRISPY_ERROR,
                        CRISPY_ERROR_IO,
                        "Failed to write test harness to temp file");
            goto cleanup;
        }
    }

    /* [7] Build compile flags: extra_flags, then CRISPY_PARAMS */
    {
        GString *flags_buf;

        flags_buf = g_string_new(NULL);

        if (include_flag != NULL)
            g_string_append(flags_buf, include_flag);

        if (extra_flags != NULL && extra_flags[0] != '\0')
        {
            if (flags_buf->len > 0)
                g_string_append_c(flags_buf, ' ');
            g_string_append(flags_buf, extra_flags);
        }

        if (expanded_params != NULL && expanded_params[0] != '\0')
        {
            if (flags_buf->len > 0)
                g_string_append_c(flags_buf, ' ');
            g_string_append(flags_buf, expanded_params);
        }

        compile_flags = g_string_free(flags_buf, FALSE);
    }

    /* [8] Compile as shared library */
    if (!crispy_cache_provider_has_valid(cache, hash, source_path))
    {
        if (!crispy_compiler_compile_shared(compiler,
                                            temp_path,
                                            so_path,
                                            compile_flags,
                                            error))
        {
            goto cleanup;
        }
    }

    /* [9] Load and execute */
    module = g_module_open(so_path, G_MODULE_BIND_LAZY);
    if (module == NULL)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_LOAD,
                    "Failed to load test module: %s",
                    g_module_error());
        goto cleanup;
    }

    main_func = NULL;
    if (!g_module_symbol(module, "main", (gpointer *)&main_func) ||
        main_func == NULL)
    {
        g_set_error(error,
                    CRISPY_ERROR,
                    CRISPY_ERROR_NO_MAIN,
                    "No main() symbol found in test harness module");
        goto cleanup;
    }

    {
        gint    harness_argc;
        gchar  *harness_argv[2];

        harness_argv[0] = (gchar *)source_path;
        harness_argv[1] = NULL;
        harness_argc = 1;

        result = main_func(harness_argc, harness_argv);
    }

cleanup:
    if (module != NULL)
        g_module_close(module);

    if (temp_path != NULL)
    {
        crispy_temp_registry_remove(temp_path);
        g_unlink(temp_path);
        g_free(temp_path);
    }

    g_free(include_flag);

    if (test_names != NULL)
    {
        for (i = 0; test_names[i] != NULL; i++)
            g_free(test_names[i]);
        g_free(test_names);
    }

    g_free(source_content);
    g_free(modified_source);
    g_free(harness_source);
    g_free(expanded_params);
    g_free(compile_flags);
    g_free(so_path);
    g_free(hash);

    return result;
}
