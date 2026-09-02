/* test-compile-publish.c - Tests for atomic publishing of compiler output */

/*
 * Copyright (C) 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define CRISPY_COMPILATION
#include "../src/crispy.h"
#include "../src/core/crispy-header-tracker-private.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

/*
 * A CrispyCompiler that writes a fixed payload wherever it is pointed
 * and records the path it was handed.
 *
 * The property under test is where the backend's output lands, not what
 * gcc puts in it, so the backend is the thing to stand in for: a real
 * compile cannot be interrupted on demand at the moment the linker is
 * half done, and a test that tried would be a test that sometimes wins
 * a race rather than one that reads the mechanism.
 */

#define TEST_TYPE_STUB_COMPILER (test_stub_compiler_get_type())

G_DECLARE_FINAL_TYPE(TestStubCompiler, test_stub_compiler,
                     TEST, STUB_COMPILER, GObject)

struct _TestStubCompiler
{
    GObject parent_instance;
};

typedef struct
{
    gchar    *handed_path;      /* the output path the backend was given */
    gboolean  succeed;          /* what the backend reports */
    gboolean  write_depfile;    /* also drop a .d beside the output */
    gint      calls;
} TestStubCompilerPrivate;

static void test_stub_compiler_compiler_init (CrispyCompilerInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE(
    TestStubCompiler,
    test_stub_compiler,
    G_TYPE_OBJECT,
    G_ADD_PRIVATE(TestStubCompiler)
    G_IMPLEMENT_INTERFACE(CRISPY_TYPE_COMPILER,
                          test_stub_compiler_compiler_init)
)

#define STUB_PAYLOAD "complete-artifact"

static const gchar *
stub_get_version(
    CrispyCompiler *self
){
    return "stub 1.0";
}

static const gchar *
stub_get_base_flags(
    CrispyCompiler *self
){
    return "";
}

static gboolean
stub_compile(
    CrispyCompiler  *self,
    const gchar     *source_path,
    const gchar     *output_path,
    const gchar     *extra_flags,
    GError         **error
){
    TestStubCompilerPrivate *priv;

    priv = test_stub_compiler_get_instance_private(TEST_STUB_COMPILER(self));

    g_free(priv->handed_path);
    priv->handed_path = g_strdup(output_path);
    priv->calls++;

    if (priv->write_depfile)
    {
        g_autofree gchar *dep = NULL;

        dep = crispy_header_tracker_get_depfile_path(output_path);
        g_file_set_contents(dep, "out: in.c dep.h\n", -1, NULL);
    }

    if (!priv->succeed)
    {
        /* a failed link still leaves the front of a file behind */
        g_file_set_contents(output_path, "half", -1, NULL);
        g_set_error(error, CRISPY_ERROR, CRISPY_ERROR_COMPILE,
                    "stub failure");
        return FALSE;
    }

    g_file_set_contents(output_path, STUB_PAYLOAD, -1, NULL);
    return TRUE;
}

static void
test_stub_compiler_compiler_init(
    CrispyCompilerInterface *iface
){
    iface->get_version        = stub_get_version;
    iface->get_base_flags     = stub_get_base_flags;
    iface->compile_shared     = stub_compile;
    iface->compile_executable = stub_compile;
}

static void
test_stub_compiler_finalize(
    GObject *object
){
    TestStubCompilerPrivate *priv;

    priv = test_stub_compiler_get_instance_private(TEST_STUB_COMPILER(object));
    g_free(priv->handed_path);

    G_OBJECT_CLASS(test_stub_compiler_parent_class)->finalize(object);
}

static void
test_stub_compiler_class_init(
    TestStubCompilerClass *klass
){
    G_OBJECT_CLASS(klass)->finalize = test_stub_compiler_finalize;
}

static void
test_stub_compiler_init(
    TestStubCompiler *self
){
    TestStubCompilerPrivate *priv;

    priv = test_stub_compiler_get_instance_private(self);
    priv->succeed = TRUE;
}

/* helper: count directory entries whose name starts with @prefix */
static gint
count_entries_with_prefix(
    const gchar *dir_path,
    const gchar *prefix
){
    GDir *dir;
    const gchar *entry;
    gint count;

    dir = g_dir_open(dir_path, 0, NULL);
    g_assert_nonnull(dir);

    count = 0;
    while ((entry = g_dir_read_name(dir)) != NULL)
    {
        if (g_str_has_prefix(entry, prefix))
            count++;
    }

    g_dir_close(dir);
    return count;
}

/* test: the backend never writes to the name the caller asked for */
static void
test_publish_stages_then_renames(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(TestStubCompiler) stub = NULL;
    TestStubCompilerPrivate *priv;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *out = NULL;
    g_autofree gchar *handed_dir = NULL;
    g_autofree gchar *contents = NULL;
    gboolean ok;

    tmpdir = g_dir_make_tmp("crispy-test-publish-XXXXXX", &error);
    g_assert_no_error(error);

    out = g_build_filename(tmpdir, "abc123.so", NULL);

    stub = g_object_new(TEST_TYPE_STUB_COMPILER, NULL);
    priv = test_stub_compiler_get_instance_private(stub);

    ok = crispy_compiler_compile_shared(CRISPY_COMPILER(stub),
                                        "unused.c", out, NULL, &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    /*
     * The cache path is what other processes load from, so it must not
     * be what the compiler writes into.
     */
    g_assert_nonnull(priv->handed_path);
    g_assert_cmpstr(priv->handed_path, !=, out);

    /* ...and the staging name must share the destination's directory,
     * or the publishing rename is a copy across a filesystem */
    handed_dir = g_path_get_dirname(priv->handed_path);
    g_assert_cmpstr(handed_dir, ==, tmpdir);

    /* the finished artifact appears under the requested name */
    g_assert_true(g_file_get_contents(out, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, STUB_PAYLOAD);

    /* and nothing is left staged */
    g_assert_false(g_file_test(priv->handed_path, G_FILE_TEST_EXISTS));
    g_assert_cmpint(count_entries_with_prefix(tmpdir, ".crispy-stage-"),
                    ==, 0);

    g_unlink(out);
    g_rmdir(tmpdir);
}

/* test: a failed compile publishes nothing and leaves nothing behind */
static void
test_publish_failure_leaves_no_artifact(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(TestStubCompiler) stub = NULL;
    TestStubCompilerPrivate *priv;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *out = NULL;
    gboolean ok;

    tmpdir = g_dir_make_tmp("crispy-test-publish-XXXXXX", &error);
    g_assert_no_error(error);

    out = g_build_filename(tmpdir, "abc123.so", NULL);

    stub = g_object_new(TEST_TYPE_STUB_COMPILER, NULL);
    priv = test_stub_compiler_get_instance_private(stub);
    priv->succeed = FALSE;

    ok = crispy_compiler_compile_shared(CRISPY_COMPILER(stub),
                                        "unused.c", out, NULL, &error);
    g_assert_false(ok);
    g_assert_error(error, CRISPY_ERROR, CRISPY_ERROR_COMPILE);

    g_assert_false(g_file_test(out, G_FILE_TEST_EXISTS));
    g_assert_cmpint(count_entries_with_prefix(tmpdir, ".crispy-stage-"),
                    ==, 0);

    g_rmdir(tmpdir);
}

/* test: a failed compile does not damage the entry already published */
static void
test_publish_failure_keeps_previous(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(TestStubCompiler) stub = NULL;
    TestStubCompilerPrivate *priv;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *out = NULL;
    g_autofree gchar *contents = NULL;
    gboolean ok;

    tmpdir = g_dir_make_tmp("crispy-test-publish-XXXXXX", &error);
    g_assert_no_error(error);

    out = g_build_filename(tmpdir, "abc123.so", NULL);
    g_assert_true(g_file_set_contents(out, "previous-artifact", -1, NULL));

    stub = g_object_new(TEST_TYPE_STUB_COMPILER, NULL);
    priv = test_stub_compiler_get_instance_private(stub);
    priv->succeed = FALSE;

    ok = crispy_compiler_compile_shared(CRISPY_COMPILER(stub),
                                        "unused.c", out, NULL, &error);
    g_assert_false(ok);

    g_assert_true(g_file_get_contents(out, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "previous-artifact");

    g_unlink(out);
    g_rmdir(tmpdir);
}

/* test: a dependency file written beside the staged object is published too */
static void
test_publish_carries_depfile(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(TestStubCompiler) stub = NULL;
    TestStubCompilerPrivate *priv;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *out = NULL;
    g_autofree gchar *out_dep = NULL;
    g_autofree gchar *staged_dep = NULL;
    gboolean ok;

    tmpdir = g_dir_make_tmp("crispy-test-publish-XXXXXX", &error);
    g_assert_no_error(error);

    out = g_build_filename(tmpdir, "abc123.so", NULL);

    stub = g_object_new(TEST_TYPE_STUB_COMPILER, NULL);
    priv = test_stub_compiler_get_instance_private(stub);
    priv->write_depfile = TRUE;

    ok = crispy_compiler_compile_shared(CRISPY_COMPILER(stub),
                                        "unused.c", out, NULL, &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    /*
     * An artifact published without its dependency list is trusted with
     * no record of the headers it was built from, which is the stale
     * header cache all over again.
     */
    out_dep = crispy_header_tracker_get_depfile_path(out);
    g_assert_true(g_file_test(out_dep, G_FILE_TEST_IS_REGULAR));

    staged_dep = crispy_header_tracker_get_depfile_path(priv->handed_path);
    g_assert_false(g_file_test(staged_dep, G_FILE_TEST_EXISTS));

    g_unlink(out);
    g_unlink(out_dep);
    g_rmdir(tmpdir);
}

/* test: a failed compile does not leave its dependency file behind */
static void
test_publish_failure_removes_depfile(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(TestStubCompiler) stub = NULL;
    TestStubCompilerPrivate *priv;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *out = NULL;
    g_autofree gchar *out_dep = NULL;
    g_autofree gchar *staged_dep = NULL;
    gboolean ok;

    tmpdir = g_dir_make_tmp("crispy-test-publish-XXXXXX", &error);
    g_assert_no_error(error);

    out = g_build_filename(tmpdir, "abc123.so", NULL);

    stub = g_object_new(TEST_TYPE_STUB_COMPILER, NULL);
    priv = test_stub_compiler_get_instance_private(stub);
    priv->write_depfile = TRUE;
    priv->succeed = FALSE;

    ok = crispy_compiler_compile_shared(CRISPY_COMPILER(stub),
                                        "unused.c", out, NULL, &error);
    g_assert_false(ok);

    out_dep = crispy_header_tracker_get_depfile_path(out);
    staged_dep = crispy_header_tracker_get_depfile_path(priv->handed_path);

    g_assert_false(g_file_test(out_dep, G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(staged_dep, G_FILE_TEST_EXISTS));

    g_rmdir(tmpdir);
}

/* test: an executable is staged and published the same way */
static void
test_publish_executable_stages(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(TestStubCompiler) stub = NULL;
    TestStubCompilerPrivate *priv;
    g_autofree gchar *tmpdir = NULL;
    g_autofree gchar *out = NULL;
    gboolean ok;

    tmpdir = g_dir_make_tmp("crispy-test-publish-XXXXXX", &error);
    g_assert_no_error(error);

    out = g_build_filename(tmpdir, "crispy-dbg-1", NULL);

    stub = g_object_new(TEST_TYPE_STUB_COMPILER, NULL);
    priv = test_stub_compiler_get_instance_private(stub);

    ok = crispy_compiler_compile_executable(CRISPY_COMPILER(stub),
                                            "unused.c", out, NULL, &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    g_assert_cmpstr(priv->handed_path, !=, out);
    g_assert_true(g_file_test(out, G_FILE_TEST_IS_REGULAR));

    g_unlink(out);
    g_rmdir(tmpdir);
}

gint
main(
    gint    argc,
    gchar **argv
){
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/compile-publish/stages-then-renames",
                    test_publish_stages_then_renames);
    g_test_add_func("/compile-publish/failure-leaves-no-artifact",
                    test_publish_failure_leaves_no_artifact);
    g_test_add_func("/compile-publish/failure-keeps-previous",
                    test_publish_failure_keeps_previous);
    g_test_add_func("/compile-publish/carries-depfile",
                    test_publish_carries_depfile);
    g_test_add_func("/compile-publish/failure-removes-depfile",
                    test_publish_failure_removes_depfile);
    g_test_add_func("/compile-publish/executable-stages",
                    test_publish_executable_stages);

    return g_test_run();
}
