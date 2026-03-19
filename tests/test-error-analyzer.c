/* test-error-analyzer.c - Tests for GCC error analysis and package suggestion */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "../src/core/crispy-error-analyzer-private.h"

#include <glib.h>
#include <string.h>

/* test: json-glib header missing → suggests "json-glib-1.0" */
static void
test_suggest_json_glib(void)
{
    const gchar *error_output =
        "foo.c:3:10: fatal error: json-glib/json-glib.h: No such file or directory\n"
        "    3 | #include <json-glib/json-glib.h>\n"
        "      |          ^~~~~~~~~~~~~~~~~~~~~~~\n"
        "compilation terminated.\n";
    GPtrArray *suggestions;

    suggestions = crispy_error_analyzer_suggest_packages(error_output);

    g_assert_nonnull(suggestions);
    g_assert_cmpuint(suggestions->len, >=, 1);

    /* at least one suggestion must mention json-glib */
    {
        gboolean found = FALSE;
        guint i;

        for (i = 0; i < suggestions->len; i++) {
            if (strstr((const gchar *)suggestions->pdata[i], "json-glib") != NULL) {
                found = TRUE;
                break;
            }
        }
        g_assert_true(found);
    }

    g_ptr_array_free(suggestions, TRUE);
}

/* test: libsoup header missing → suggests "libsoup-3.0" */
static void
test_suggest_libsoup(void)
{
    const gchar *error_output =
        "foo.c:2:10: fatal error: libsoup/soup.h: No such file or directory\n"
        "compilation terminated.\n";
    GPtrArray *suggestions;

    suggestions = crispy_error_analyzer_suggest_packages(error_output);

    g_assert_nonnull(suggestions);

    {
        gboolean found = FALSE;
        guint i;

        for (i = 0; i < suggestions->len; i++) {
            if (strstr((const gchar *)suggestions->pdata[i], "libsoup") != NULL ||
                strstr((const gchar *)suggestions->pdata[i], "soup")    != NULL) {
                found = TRUE;
                break;
            }
        }
        g_assert_true(found);
    }

    g_ptr_array_free(suggestions, TRUE);
}

/* test: sqlite3.h missing → suggests "sqlite3" */
static void
test_suggest_sqlite(void)
{
    const gchar *error_output =
        "foo.c:1:10: fatal error: sqlite3.h: No such file or directory\n"
        "compilation terminated.\n";
    GPtrArray *suggestions;

    suggestions = crispy_error_analyzer_suggest_packages(error_output);

    g_assert_nonnull(suggestions);

    {
        gboolean found = FALSE;
        guint i;

        for (i = 0; i < suggestions->len; i++) {
            if (strstr((const gchar *)suggestions->pdata[i], "sqlite") != NULL) {
                found = TRUE;
                break;
            }
        }
        g_assert_true(found);
    }

    g_ptr_array_free(suggestions, TRUE);
}

/* test: unknown header returns NULL */
static void
test_suggest_unknown(void)
{
    const gchar *error_output =
        "foo.c:1:10: fatal error: totally-unknown-xyz123.h: No such file or directory\n"
        "compilation terminated.\n";
    GPtrArray *suggestions;

    suggestions = crispy_error_analyzer_suggest_packages(error_output);

    /* NULL or an empty array are both acceptable */
    if (suggestions != NULL) {
        g_assert_cmpuint(suggestions->len, ==, 0);
        g_ptr_array_free(suggestions, TRUE);
    }
}

/* test: error output with two missing headers produces two suggestions */
static void
test_suggest_multiple(void)
{
    const gchar *error_output =
        "foo.c:2:10: fatal error: json-glib/json-glib.h: No such file or directory\n"
        "foo.c:3:10: fatal error: libsoup/soup.h: No such file or directory\n"
        "compilation terminated.\n";
    GPtrArray *suggestions;

    suggestions = crispy_error_analyzer_suggest_packages(error_output);

    g_assert_nonnull(suggestions);
    g_assert_cmpuint(suggestions->len, >=, 2);

    g_ptr_array_free(suggestions, TRUE);
}

/* test: NULL input returns NULL */
static void
test_suggest_null(void)
{
    GPtrArray *suggestions;

    suggestions = crispy_error_analyzer_suggest_packages(NULL);

    g_assert_null(suggestions);
}

/* test: warning-only output returns NULL */
static void
test_suggest_no_errors(void)
{
    const gchar *error_output =
        "foo.c:5:9: warning: unused variable 'x' [-Wunused-variable]\n"
        "    5 |     int x;\n";
    GPtrArray *suggestions;

    suggestions = crispy_error_analyzer_suggest_packages(error_output);

    if (suggestions != NULL) {
        g_assert_cmpuint(suggestions->len, ==, 0);
        g_ptr_array_free(suggestions, TRUE);
    }
}

/* test: format_suggestions produces a non-empty hint string */
static void
test_format_suggestions(void)
{
    GPtrArray *suggestions;
    g_autofree gchar *hint = NULL;

    suggestions = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(suggestions, g_strdup("json-glib-1.0"));
    g_ptr_array_add(suggestions, g_strdup("libsoup-3.0"));

    hint = crispy_error_analyzer_format_suggestions(suggestions);

    g_assert_nonnull(hint);
    g_assert_cmpuint(strlen(hint), >, 0);
    g_assert_true(strstr(hint, "json-glib-1.0") != NULL);
    g_assert_true(strstr(hint, "libsoup-3.0")   != NULL);

    g_ptr_array_free(suggestions, TRUE);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/error-analyzer/suggest-json-glib",
                    test_suggest_json_glib);
    g_test_add_func("/error-analyzer/suggest-libsoup",
                    test_suggest_libsoup);
    g_test_add_func("/error-analyzer/suggest-sqlite",
                    test_suggest_sqlite);
    g_test_add_func("/error-analyzer/suggest-unknown",
                    test_suggest_unknown);
    g_test_add_func("/error-analyzer/suggest-multiple",
                    test_suggest_multiple);
    g_test_add_func("/error-analyzer/suggest-null",
                    test_suggest_null);
    g_test_add_func("/error-analyzer/suggest-no-errors",
                    test_suggest_no_errors);
    g_test_add_func("/error-analyzer/format-suggestions",
                    test_format_suggestions);

    return g_test_run();
}
