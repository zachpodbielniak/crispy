/* test-script.c - End-to-end tests for CrispyScript */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "../src/core/crispy-temp-registry-private.h"
#include "crispy-test-cache.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

/* shared compiler and cache for all tests */
static CrispyGccCompiler *g_compiler = NULL;
static CrispyFileCache *g_cache = NULL;

/* helper: write a temp .c file and return its path */
static gchar *
write_temp_script(
    const gchar *source
){
    g_autofree gchar *tmpl = NULL;
    gint fd;

    tmpl = g_strdup("/tmp/crispy-test-script-XXXXXX.c");
    fd = g_mkstemp(tmpl);
    g_assert_cmpint(fd, >=, 0);
    write(fd, source, strlen(source));
    close(fd);

    return g_steal_pointer(&tmpl);
}

/* test: hello world script from file */
static void
test_script_from_file_hello(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;

    path = write_temp_script(
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv){\n"
        "    g_print(\"hello test\\n\");\n"
        "    return 0;\n"
        "}\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);
    g_assert_nonnull(script);

    exit_code = crispy_script_execute(script, 1, &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 0);

    g_unlink(path);
}

/* test: script exit code propagation */
static void
test_script_from_file_exit_code(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;

    path = write_temp_script(
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv){\n"
        "    return 42;\n"
        "}\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(script, 1, &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 42);
    g_assert_cmpint(crispy_script_get_exit_code(script), ==, 42);

    g_unlink(path);
}

/* test: inline mode */
static void
test_script_from_inline(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    gchar *fake_argv[] = { "crispy-inline", NULL };
    gint exit_code;

    script = crispy_script_new_from_inline(
        "g_print(\"inline test\\n\"); return 0;",
        NULL,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);
    g_assert_nonnull(script);

    exit_code = crispy_script_execute(script, 1, fake_argv, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 0);
}

/* test: CRISPY_PARAMS with -lm */
static void
test_script_crispy_params(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;

    path = write_temp_script(
        "#define CRISPY_PARAMS \"-lm\"\n"
        "#include <math.h>\n"
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv){\n"
        "    double val = sqrt(144.0);\n"
        "    return (val == 12.0) ? 0 : 1;\n"
        "}\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(script, 1, &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 0);

    g_unlink(path);
}

/* test: shebang is stripped and script compiles */
static void
test_script_shebang_strip(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;

    path = write_temp_script(
        "#!/usr/bin/crispy\n"
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv){\n"
        "    return 0;\n"
        "}\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(script, 1, &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 0);

    g_unlink(path);
}

/* test: compilation error produces GError */
static void
test_script_compile_error(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;

    path = write_temp_script("this is not valid C;\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(script, 1, &path, &error);
    g_assert_cmpint(exit_code, ==, -1);
    g_assert_error(error, CRISPY_ERROR, CRISPY_ERROR_COMPILE);

    g_unlink(path);
}

/* test: preserve source flag keeps temp file */
static void
test_script_preserve_source(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    const gchar *temp_path;

    path = write_temp_script(
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv){ return 0; }\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE | CRISPY_FLAG_PRESERVE_SOURCE,
        &error);
    g_assert_no_error(error);

    crispy_script_execute(script, 1, &path, &error);
    g_assert_no_error(error);

    temp_path = crispy_script_get_temp_source_path(script);
    g_assert_nonnull(temp_path);
    g_assert_true(g_file_test(temp_path, G_FILE_TEST_EXISTS));

    /* cleanup manually since preserve is set */
    g_unlink(temp_path);
    g_unlink(path);
}

/* test: CRISPY_PARAMS with quotes later in file (regression for strrchr bug).
 *
 * Previously the extractor used strrchr to find the closing quote, which
 * searched the entire file.  An apostrophe or quote character in a later
 * comment (e.g. "LibreClaw's") would be mistaken for the closing quote,
 * causing the entire source to be consumed as params.  The fix constrains
 * the search to the current line using memchr bounded by line_end.
 */
static void
test_script_crispy_params_quote_in_comment(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;

    path = write_temp_script(
        "#define CRISPY_PARAMS \"-lm\"\n"
        "#include <math.h>\n"
        "#include <glib.h>\n"
        "/* This is LibreClaw's test file -- apostrophe must not break params */\n"
        "gint main(gint argc, gchar **argv){\n"
        "    double val = sqrt(144.0);\n"
        "    return (val == 12.0) ? 0 : 1;\n"
        "}\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(script, 1, &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 0);

    g_unlink(path);
}

/* test: argument passing to script */
static void
test_script_arg_passing(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gchar *test_argv[] = { "test", "7", NULL };
    gint exit_code;

    path = write_temp_script(
        "#include <glib.h>\n"
        "#include <stdlib.h>\n"
        "gint main(gint argc, gchar **argv){\n"
        "    if (argc < 2) return -1;\n"
        "    return atoi(argv[1]);\n"
        "}\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(script, 2, test_argv, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 7);

    g_unlink(path);
}

/*
 * test: the stripped source registers itself for cleanup
 *
 * The CLI's signal cleanup read this path before execute() had created
 * the file, so it was always %NULL and an interrupted run left the
 * stripped source in the temp directory.  Registering it where it is
 * created is the fix; asserting the registration is what says the wire
 * exists at all.
 */
static void
test_script_registers_temp_source(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    const gchar *temp_source;
    gint exit_code;

    path = write_temp_script(
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv){ return 0; }\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE | CRISPY_FLAG_DRY_RUN,
        &error);
    g_assert_no_error(error);

    /* a dry run stops just after the temp source is written */
    exit_code = crispy_script_execute(script, 1, &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 0);

    temp_source = crispy_script_get_temp_source_path(script);
    g_assert_nonnull(temp_source);
    g_assert_true(crispy_temp_registry_contains(temp_source));

    g_unlink(path);
}

/*
 * test: a dry run on a warm cache still refuses to execute
 *
 * The DRY_RUN check sat inside the cache-miss branch, so it described a
 * cold cache and nothing else.  The second invocation of any script is a
 * warm one: the flag fell through, the cached module was loaded and the
 * script's main() ran -- the single thing --dry-run exists to prevent,
 * on the case that happens most.  A test that only compiles cold passes
 * against that bug, so this one primes the cache with a real run first.
 *
 * The script writes a marker file, which makes "did it execute" a
 * question about the filesystem rather than about output a test would
 * have to trap.
 */
static void
test_script_dry_run_warm_cache(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) warm = NULL;
    g_autoptr(CrispyScript) dry = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *marker = NULL;
    g_autofree gchar *source = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;

    tmpdir = g_dir_make_tmp("crispy-test-dryrun-XXXXXX", &error);
    g_assert_no_error(error);

    marker = g_build_filename(tmpdir, "it-ran", NULL);
    source = g_strdup_printf(
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv){\n"
        "    g_file_set_contents(\"%s\", \"ran\", -1, NULL);\n"
        "    return 0;\n"
        "}\n",
        marker);

    path = g_build_filename(tmpdir, "script.c", NULL);
    g_assert_true(g_file_set_contents(path, source, -1, NULL));

    /* first run: no flags, so the artifact lands in the cache */
    warm = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_NONE,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(warm, 1, &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 0);

    /* it really did run, so the marker means what the second half reads */
    g_assert_true(g_file_test(marker, G_FILE_TEST_EXISTS));
    g_assert_cmpint(g_unlink(marker), ==, 0);

    /* second run: same source, same flags -- a cache hit, and a dry one */
    dry = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_DRY_RUN,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(dry, 1, &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 0);

    g_assert_false(g_file_test(marker, G_FILE_TEST_EXISTS));

    g_unlink(path);
    g_rmdir(tmpdir);
}

/*
 * test: a script's own directory is on the include path
 *
 * Compilation happens on a stripped copy in the temp directory, so a
 * quoted include of a sibling header resolved for `crispy lint`, which
 * compiles in place, and for nothing that actually runs the script.
 */
static void
test_script_sibling_include(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *header = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;

    tmpdir = g_dir_make_tmp("crispy-test-sibling-XXXXXX", &error);
    g_assert_no_error(error);

    header = g_build_filename(tmpdir, "sibling.h", NULL);
    g_assert_true(g_file_set_contents(header, "#define SIBLING_RC 3\n",
                                      -1, NULL));

    path = g_build_filename(tmpdir, "script.c", NULL);
    g_assert_true(g_file_set_contents(
        path,
        "#include <glib.h>\n"
        "#include \"sibling.h\"\n"
        "gint main(gint argc, gchar **argv){ return SIBLING_RC; }\n",
        -1, NULL));

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(script, 1, &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 3);

    g_unlink(path);
    g_unlink(header);
    g_rmdir(tmpdir);
}

/*
 * test: CRISPY_USE is resolved on the path that runs a script
 *
 * lint, test and install each resolved it and the runner did not, so a
 * script that linted clean died on the header it had declared.  json-glib
 * is a build dependency of crispy itself, so it is present wherever this
 * suite runs.
 */
static void
test_script_crispy_use(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    gint exit_code;

    path = write_temp_script(
        "#define CRISPY_USE \"json-glib-1.0\"\n"
        "#include <glib.h>\n"
        "#include <json-glib/json-glib.h>\n"
        "gint main(gint argc, gchar **argv){\n"
        "    JsonBuilder *b = json_builder_new();\n"
        "    g_object_unref(b);\n"
        "    return 0;\n"
        "}\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_FORCE_COMPILE,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(script, 1, &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 0);

    g_unlink(path);
}

/*
 * The body that runs in the trapped subprocess for the --profile test.
 * It chdirs into the directory the parent made, because glibc writes
 * gmon.out into the working directory at exit and the suite must not
 * scatter one through the source tree.
 */
static void
test_script_profile_subprocess(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(CrispyScript) script = NULL;
    g_autofree gchar *path = NULL;
    const gchar *rundir;
    gint exit_code;

    rundir = g_getenv("CRISPY_TEST_PROFILE_DIR");
    g_assert_nonnull(rundir);
    g_assert_cmpint(g_chdir(rundir), ==, 0);

    path = write_temp_script(
        "#include <glib.h>\n"
        "gint main(gint argc, gchar **argv){ return 0; }\n");

    script = crispy_script_new_from_file(
        path,
        CRISPY_COMPILER(g_compiler),
        CRISPY_CACHE_PROVIDER(g_cache),
        CRISPY_FLAG_PROFILE,
        &error);
    g_assert_no_error(error);

    exit_code = crispy_script_execute(script, 1, &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(exit_code, ==, 0);

    g_unlink(path);
}

/*
 * test: --profile actually profiles
 *
 * The flag used to be read in one place, to print "Profiling complete.
 * gmon.out generated in current directory."  No -pg was passed and no
 * gmon.out was written, so the only thing --profile did was say it had
 * worked.  A gprof report on stdout cannot be produced without both.
 */
static void
test_script_profile(void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *gprof = NULL;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *gmon = NULL;

    gprof = g_find_program_in_path("gprof");
    if (gprof == NULL)
    {
        g_test_skip("gprof is not installed");
        return;
    }

    tmpdir = g_dir_make_tmp("crispy-test-profile-XXXXXX", &error);
    g_assert_no_error(error);

    g_setenv("CRISPY_TEST_PROFILE_DIR", tmpdir, TRUE);

    g_test_trap_subprocess("/script/profile/subprocess",
                           120 * G_USEC_PER_SEC, 0);
    g_test_trap_assert_passed();
    g_test_trap_assert_stdout("*Flat profile*");

    /* the profile data belongs to the run, not to the user's directory */
    gmon = g_build_filename(tmpdir, "gmon.out", NULL);
    g_assert_false(g_file_test(gmon, G_FILE_TEST_EXISTS));

    g_unsetenv("CRISPY_TEST_PROFILE_DIR");
    g_rmdir(tmpdir);
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

    /* set up shared fixtures */
    g_compiler = crispy_gcc_compiler_new(&error);
    g_assert_no_error(error);
    g_cache = crispy_file_cache_new();

    g_test_add_func("/script/from-file-hello",
                    test_script_from_file_hello);
    g_test_add_func("/script/from-file-exit-code",
                    test_script_from_file_exit_code);
    g_test_add_func("/script/from-inline",
                    test_script_from_inline);
    g_test_add_func("/script/crispy-params",
                    test_script_crispy_params);
    g_test_add_func("/script/crispy-params-quote-in-comment",
                    test_script_crispy_params_quote_in_comment);
    g_test_add_func("/script/shebang-strip",
                    test_script_shebang_strip);
    g_test_add_func("/script/compile-error",
                    test_script_compile_error);
    g_test_add_func("/script/preserve-source",
                    test_script_preserve_source);
    g_test_add_func("/script/arg-passing",
                    test_script_arg_passing);
    g_test_add_func("/script/registers-temp-source",
                    test_script_registers_temp_source);
    g_test_add_func("/script/dry-run-warm-cache",
                    test_script_dry_run_warm_cache);
    g_test_add_func("/script/sibling-include",
                    test_script_sibling_include);
    g_test_add_func("/script/crispy-use",
                    test_script_crispy_use);
    g_test_add_func("/script/profile",
                    test_script_profile);
    g_test_add_func("/script/profile/subprocess",
                    test_script_profile_subprocess);

    return g_test_run();
}
