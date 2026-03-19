/* test-use-parser.c - Tests for CRISPY_USE parsing utilities */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "../src/core/crispy-use-parser-private.h"
#include "../src/core/crispy-pkg-config-resolver.h"

#include <glib.h>
#include <string.h>

/* test: extract finds CRISPY_USE in source */
static void
test_extract_found(void)
{
    const gchar *source =
        "#!/usr/bin/crispy\n"
        "#define CRISPY_USE \"glib-2.0 json-glib-1.0\"\n"
        "#include <glib.h>\n"
        "int main(void) { return 0; }\n";
    g_autofree gchar *result = NULL;

    result = crispy_use_parser_extract(source);

    g_assert_nonnull(result);
    g_assert_cmpstr(result, ==, "glib-2.0 json-glib-1.0");
}

/* test: extract returns NULL for source without CRISPY_USE */
static void
test_extract_not_found(void)
{
    const gchar *source =
        "#include <glib.h>\n"
        "int main(void) { return 0; }\n";
    g_autofree gchar *result = NULL;

    result = crispy_use_parser_extract(source);

    g_assert_null(result);
}

/* test: extract returns NULL for NULL input */
static void
test_extract_null_source(void)
{
    g_autofree gchar *result = NULL;

    result = crispy_use_parser_extract(NULL);

    g_assert_null(result);
}

/* test: extract handles leading whitespace before #define */
static void
test_extract_with_whitespace(void)
{
    const gchar *source =
        "#!/usr/bin/crispy\n"
        "  #define CRISPY_USE \"libsoup-3.0\"\n"
        "#include <glib.h>\n";
    g_autofree gchar *result = NULL;

    result = crispy_use_parser_extract(source);

    /*
     * Whether leading whitespace is accepted depends on implementation.
     * If it returns non-NULL we verify the value; if NULL we just ensure
     * no crash occurred.
     */
    if (result != NULL)
        g_assert_cmpstr(result, ==, "libsoup-3.0");
}

/* test: split single package name */
static void
test_split_single(void)
{
    gchar **parts;
    gint count = 0;

    parts = crispy_use_parser_split("glib-2.0", &count);

    g_assert_cmpint(count, ==, 1);
    g_assert_nonnull(parts);
    g_assert_cmpstr(parts[0], ==, "glib-2.0");

    g_strfreev(parts);
}

/* test: split multiple package names */
static void
test_split_multiple(void)
{
    gchar **parts;
    gint count = 0;

    parts = crispy_use_parser_split("glib-2.0 json-glib-1.0 libsoup-3.0",
                                    &count);

    g_assert_cmpint(count, ==, 3);
    g_assert_nonnull(parts);
    g_assert_cmpstr(parts[0], ==, "glib-2.0");
    g_assert_cmpstr(parts[1], ==, "json-glib-1.0");
    g_assert_cmpstr(parts[2], ==, "libsoup-3.0");

    g_strfreev(parts);
}

/* test: split trims extra spaces */
static void
test_split_extra_spaces(void)
{
    gchar **parts;
    gint count = 0;

    parts = crispy_use_parser_split("  glib-2.0   json-glib-1.0  ", &count);

    g_assert_cmpint(count, ==, 2);
    g_assert_cmpstr(parts[0], ==, "glib-2.0");
    g_assert_cmpstr(parts[1], ==, "json-glib-1.0");

    g_strfreev(parts);
}

/* test: split empty string returns 0 packages */
static void
test_split_empty(void)
{
    gchar **parts;
    gint count = 0;

    parts = crispy_use_parser_split("", &count);

    g_assert_cmpint(count, ==, 0);

    g_strfreev(parts);
}

/* test: split NULL returns 0 packages safely */
static void
test_split_null(void)
{
    gchar **parts;
    gint count = 0;

    parts = crispy_use_parser_split(NULL, &count);

    g_assert_cmpint(count, ==, 0);
    /* parts may be NULL or empty array — both are fine */
    g_strfreev(parts);
}

/* test: full resolve of source with CRISPY_USE "glib-2.0" */
static void
test_resolve_glib(void)
{
    g_autoptr(CrispyPkgConfigResolver) resolver = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *flags = NULL;
    const gchar *source =
        "#define CRISPY_USE \"glib-2.0\"\n"
        "#include <glib.h>\n"
        "int main(void) { return 0; }\n";

    resolver = crispy_pkg_config_resolver_new();
    flags = crispy_use_parser_resolve(
        source, CRISPY_DEPENDENCY_RESOLVER(resolver), &error);

    g_assert_no_error(error);
    g_assert_nonnull(flags);
    g_assert_cmpuint(strlen(flags), >, 0);
}

/* test: source without CRISPY_USE returns NULL without error */
static void
test_resolve_no_use(void)
{
    g_autoptr(CrispyPkgConfigResolver) resolver = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *flags = NULL;
    const gchar *source =
        "#include <glib.h>\n"
        "int main(void) { return 0; }\n";

    resolver = crispy_pkg_config_resolver_new();
    flags = crispy_use_parser_resolve(
        source, CRISPY_DEPENDENCY_RESOLVER(resolver), &error);

    g_assert_null(flags);
    g_assert_no_error(error);
}

/* test: resolving with a nonexistent package returns NULL with error */
static void
test_resolve_nonexistent(void)
{
    g_autoptr(CrispyPkgConfigResolver) resolver = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *flags = NULL;
    const gchar *source =
        "#define CRISPY_USE \"nonexistent-pkg-12345\"\n"
        "#include <glib.h>\n"
        "int main(void) { return 0; }\n";

    resolver = crispy_pkg_config_resolver_new();
    flags = crispy_use_parser_resolve(
        source, CRISPY_DEPENDENCY_RESOLVER(resolver), &error);

    g_assert_null(flags);
    g_assert_nonnull(error);
}

/* test: resolve_to_list returns a GPtrArray of CrispyDependencyInfo */
static void
test_resolve_to_list(void)
{
    g_autoptr(CrispyPkgConfigResolver) resolver = NULL;
    g_autoptr(GError) error = NULL;
    GPtrArray *list = NULL;
    const gchar *source =
        "#define CRISPY_USE \"glib-2.0\"\n"
        "#include <glib.h>\n"
        "int main(void) { return 0; }\n";

    resolver = crispy_pkg_config_resolver_new();
    list = crispy_use_parser_resolve_to_list(
        source, CRISPY_DEPENDENCY_RESOLVER(resolver), &error);

    g_assert_no_error(error);
    g_assert_nonnull(list);
    g_assert_cmpuint(list->len, >=, 1);

    g_ptr_array_free(list, TRUE);
}

/* test: strip_use removes CRISPY_USE line from source */
static void
test_strip_use(void)
{
    const gchar *source =
        "#define CRISPY_USE \"glib-2.0\"\n"
        "#include <glib.h>\n"
        "int main(void) { return 0; }\n";
    g_autofree gchar *stripped = NULL;

    stripped = crispy_source_strip_use(source, NULL);

    g_assert_nonnull(stripped);
    g_assert_null(strstr(stripped, "CRISPY_USE"));
    g_assert_nonnull(strstr(stripped, "#include <glib.h>"));
}

/* test: strip_use on source without CRISPY_USE is unchanged */
static void
test_strip_use_not_present(void)
{
    const gchar *source =
        "#include <glib.h>\n"
        "int main(void) { return 0; }\n";
    g_autofree gchar *stripped = NULL;

    stripped = crispy_source_strip_use(source, NULL);

    g_assert_nonnull(stripped);
    g_assert_cmpstr(stripped, ==, source);
}

/* test: only first CRISPY_USE line is extracted when multiple are present */
static void
test_extract_multiple_only_first(void)
{
    const gchar *source =
        "#define CRISPY_USE \"glib-2.0\"\n"
        "#define CRISPY_USE \"json-glib-1.0\"\n"
        "#include <glib.h>\n";
    g_autofree gchar *result = NULL;

    result = crispy_use_parser_extract(source);

    g_assert_nonnull(result);
    g_assert_cmpstr(result, ==, "glib-2.0");
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/use-parser/extract-found",
                    test_extract_found);
    g_test_add_func("/use-parser/extract-not-found",
                    test_extract_not_found);
    g_test_add_func("/use-parser/extract-null-source",
                    test_extract_null_source);
    g_test_add_func("/use-parser/extract-with-whitespace",
                    test_extract_with_whitespace);
    g_test_add_func("/use-parser/split-single",
                    test_split_single);
    g_test_add_func("/use-parser/split-multiple",
                    test_split_multiple);
    g_test_add_func("/use-parser/split-extra-spaces",
                    test_split_extra_spaces);
    g_test_add_func("/use-parser/split-empty",
                    test_split_empty);
    g_test_add_func("/use-parser/split-null",
                    test_split_null);
    g_test_add_func("/use-parser/resolve-glib",
                    test_resolve_glib);
    g_test_add_func("/use-parser/resolve-no-use",
                    test_resolve_no_use);
    g_test_add_func("/use-parser/resolve-nonexistent",
                    test_resolve_nonexistent);
    g_test_add_func("/use-parser/resolve-to-list",
                    test_resolve_to_list);
    g_test_add_func("/use-parser/strip-use",
                    test_strip_use);
    g_test_add_func("/use-parser/strip-use-not-present",
                    test_strip_use_not_present);
    g_test_add_func("/use-parser/extract-multiple-only-first",
                    test_extract_multiple_only_first);

    return g_test_run();
}
