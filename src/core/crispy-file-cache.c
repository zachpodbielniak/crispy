/* crispy-file-cache.c - Filesystem CrispyCacheProvider implementation */

#ifndef CRISPY_COMPILATION
#define CRISPY_COMPILATION
#endif
#include "crispy-file-cache.h"
#include "crispy-header-tracker-private.h"
#include "../interfaces/crispy-cache-provider.h"
#include "../crispy-types.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <elf.h>
#include <string.h>
#include <sys/stat.h>

/**
 * SECTION:crispy-file-cache
 * @title: CrispyFileCache
 * @short_description: Filesystem implementation of CrispyCacheProvider
 *
 * #CrispyFileCache stores compiled shared objects in `~/.cache/crispy/`
 * using SHA256 content hashes as filenames. An entry is valid when it is
 * a complete ELF shared object, is no older than the script it was built
 * from, and none of the headers recorded in its dependency file have
 * changed since.
 */

/*
 * The byte order this process can load.  elf.h names the two encodings
 * but not which one is ours, and a module in the other one is unloadable
 * whatever else is right about it.
 */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define CRISPY_ELF_NATIVE_DATA ELFDATA2LSB
#else
#define CRISPY_ELF_NATIVE_DATA ELFDATA2MSB
#endif

struct _CrispyFileCache
{
    GObject parent_instance;
};

typedef struct
{
    gchar *cache_dir;
} CrispyFileCachePrivate;

static void crispy_file_cache_provider_init (CrispyCacheProviderInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE(
    CrispyFileCache,
    crispy_file_cache,
    G_TYPE_OBJECT,
    G_ADD_PRIVATE(CrispyFileCache)
    G_IMPLEMENT_INTERFACE(CRISPY_TYPE_CACHE_PROVIDER,
                          crispy_file_cache_provider_init)
)

/* --- CrispyCacheProvider interface implementation --- */

static gchar *
file_cache_compute_hash(
    CrispyCacheProvider *self,
    const gchar         *source_content,
    gssize               source_len,
    const gchar         *extra_flags,
    const gchar         *compiler_version
){
    GChecksum *checksum;
    const gchar *hex;
    gchar *result;
    gchar separator;

    (void)self;

    if (source_len < 0)
        source_len = (gssize)strlen(source_content);

    separator = '\0';

    checksum = g_checksum_new(CRISPY_HASH_ALGO);

    /* hash source content */
    g_checksum_update(checksum, (const guchar *)source_content,
                      (gssize)source_len);

    /* NUL separator */
    g_checksum_update(checksum, (const guchar *)&separator, 1);

    /* hash extra flags (or empty string) */
    if (extra_flags != NULL)
        g_checksum_update(checksum, (const guchar *)extra_flags,
                          (gssize)strlen(extra_flags));

    /* NUL separator */
    g_checksum_update(checksum, (const guchar *)&separator, 1);

    /* hash compiler version */
    g_checksum_update(checksum, (const guchar *)compiler_version,
                      (gssize)strlen(compiler_version));

    hex = g_checksum_get_string(checksum);
    result = g_strdup(hex);

    g_checksum_free(checksum);
    return result;
}

static gchar *
file_cache_get_path(
    CrispyCacheProvider *self,
    const gchar         *hash
){
    CrispyFileCachePrivate *priv;
    g_autofree gchar *filename = NULL;

    priv = crispy_file_cache_get_instance_private(CRISPY_FILE_CACHE(self));
    filename = g_strdup_printf("%s.so", hash);
    return g_build_filename(priv->cache_dir, filename, NULL);
}

/*
 * artifact_is_complete_module:
 * @path: path to a cached artifact
 *
 * Answers whether @path holds a whole ELF shared object rather than the
 * front of one.
 *
 * Existence was the whole test before, and a file that exists is not a
 * file that loads: a compile killed part-way, or read by another process
 * while the linker was still writing, leaves a stub that is newer than
 * the script and therefore "valid" for ever.  Every later run then died
 * with "file too short" or "invalid ELF header" from g_module_open(),
 * and only --no-cache got past it, which nothing said.  Rejecting the
 * stub here lets the next run rebuild it.
 *
 * The section header table sits at the end of a linked object, so
 * requiring the file to reach past it is what catches truncation.
 *
 * Returns: %TRUE if @path is a loadable shared object
 */
static gboolean
artifact_is_complete_module(
    const gchar *path
){
    guchar header[sizeof(Elf64_Ehdr)];
    GStatBuf st;
    FILE *fp;
    gsize want;
    gsize read_len;
    gint64 needed;
    gboolean ok;

    if (g_stat(path, &st) != 0)
        return FALSE;

    fp = g_fopen(path, "rb");
    if (fp == NULL)
        return FALSE;

    memset(header, 0, sizeof(header));
    read_len = fread(header, 1, sizeof(header), fp);
    fclose(fp);

    if (read_len < EI_NIDENT)
        return FALSE;

    if (memcmp(header, ELFMAG, SELFMAG) != 0)
        return FALSE;

    /*
     * A module built for another word size or byte order is not one this
     * process can load, and its e_shoff cannot be read with the native
     * struct either -- so refuse it rather than measuring it wrongly.
     */
    if (header[EI_DATA] != CRISPY_ELF_NATIVE_DATA)
        return FALSE;

    ok = FALSE;
    needed = 0;

    if (header[EI_CLASS] == ELFCLASS64)
    {
        Elf64_Ehdr ehdr;

        want = sizeof(Elf64_Ehdr);
        if (read_len < want)
            return FALSE;

        memcpy(&ehdr, header, want);
        if (ehdr.e_type != ET_DYN && ehdr.e_type != ET_EXEC)
            return FALSE;

        needed = (gint64)ehdr.e_shoff +
                 (gint64)ehdr.e_shnum * (gint64)ehdr.e_shentsize;
        ok = TRUE;
    }
    else if (header[EI_CLASS] == ELFCLASS32)
    {
        Elf32_Ehdr ehdr;

        want = sizeof(Elf32_Ehdr);
        if (read_len < want)
            return FALSE;

        memcpy(&ehdr, header, want);
        if (ehdr.e_type != ET_DYN && ehdr.e_type != ET_EXEC)
            return FALSE;

        needed = (gint64)ehdr.e_shoff +
                 (gint64)ehdr.e_shnum * (gint64)ehdr.e_shentsize;
        ok = TRUE;
    }

    if (!ok)
        return FALSE;

    return (gint64)st.st_size >= needed;
}

/*
 * headers_still_fresh:
 * @so_path: path to the cached artifact
 * @reference_time: the artifact's own modification time
 *
 * Answers whether every header the artifact was compiled against is
 * older than the artifact itself.
 *
 * An artifact with no dependency file beside it says nothing about its
 * headers, which is the answer for a backend that records none; it is
 * treated as fresh so an unknown backend behaves as it did before.
 *
 * Returns: %TRUE if no recorded header is newer than @reference_time
 */
static gboolean
headers_still_fresh(
    const gchar *so_path,
    gint64       reference_time
){
    g_autofree gchar *depfile = NULL;
    GPtrArray *deps;
    gboolean stale;

    depfile = crispy_header_tracker_get_depfile_path(so_path);

    if (!g_file_test(depfile, G_FILE_TEST_IS_REGULAR))
        return TRUE;

    deps = NULL;
    if (!crispy_header_tracker_parse_depfile(depfile, &deps, NULL))
        return TRUE;

    stale = crispy_header_tracker_check_stale(deps, reference_time);
    g_ptr_array_unref(deps);

    return !stale;
}

static gboolean
file_cache_has_valid(
    CrispyCacheProvider *self,
    const gchar         *hash,
    const gchar         *source_path
){
    g_autofree gchar *so_path = NULL;
    GStatBuf so_stat;
    GStatBuf src_stat;

    so_path = file_cache_get_path(self, hash);

    /* check if the cached .so exists */
    if (!g_file_test(so_path, G_FILE_TEST_IS_REGULAR))
        return FALSE;

    /* a truncated or half-written entry is not a cache hit */
    if (!artifact_is_complete_module(so_path))
        return FALSE;

    if (g_stat(so_path, &so_stat) != 0)
        return FALSE;

    /*
     * Headers are checked whether or not there is a script on disk: an
     * inline snippet or a REPL line can include a header of its own, and
     * the hash covers the text that named it, never the file it named.
     */
    if (!headers_still_fresh(so_path, (gint64)so_stat.st_mtime))
        return FALSE;

    /* if no source path, the artifact stands on its own (inline/stdin) */
    if (source_path == NULL)
        return TRUE;

    /*
     * Compare mtime: the cached artifact must be at least as new as the
     * source.
     *
     * st_mtime is whole seconds, so an edit made in the same second as
     * the compile is not seen -- by this check or by the header one
     * above, which uses the same clock deliberately rather than a finer
     * one, so the two cannot disagree about the same edit.  Editing and
     * re-running inside one second reuses the cache; `--no-cache` is the
     * escape, and a second later the change is picked up normally.
     */
    if (g_stat(source_path, &src_stat) != 0)
        return FALSE;

    return so_stat.st_mtime >= src_stat.st_mtime;
}

static gboolean
file_cache_purge(
    CrispyCacheProvider *self,
    GError             **error
){
    CrispyFileCachePrivate *priv;
    GDir *dir;
    const gchar *entry;
    gint count;

    priv = crispy_file_cache_get_instance_private(CRISPY_FILE_CACHE(self));

    dir = g_dir_open(priv->cache_dir, 0, error);
    if (dir == NULL)
        return FALSE;

    count = 0;
    while ((entry = g_dir_read_name(dir)) != NULL)
    {
        /*
         * Dependency files and the staging names a killed compile left
         * behind belong to the cache too; purging only the objects would
         * leave the directory growing after every interrupted run.
         */
        if (g_str_has_suffix(entry, ".so") ||
            g_str_has_suffix(entry, ".d") ||
            g_str_has_prefix(entry, ".crispy-stage-"))
        {
            g_autofree gchar *path = NULL;

            path = g_build_filename(priv->cache_dir, entry, NULL);
            if (g_unlink(path) == 0)
                count++;
        }
    }

    g_dir_close(dir);

    g_message("Purged %d cached file(s) from %s", count, priv->cache_dir);
    return TRUE;
}

static void
crispy_file_cache_provider_init(
    CrispyCacheProviderInterface *iface
){
    iface->compute_hash = file_cache_compute_hash;
    iface->get_path     = file_cache_get_path;
    iface->has_valid    = file_cache_has_valid;
    iface->purge        = file_cache_purge;
}

/* --- GObject lifecycle --- */

static void
crispy_file_cache_finalize(
    GObject *object
){
    CrispyFileCachePrivate *priv;

    priv = crispy_file_cache_get_instance_private(CRISPY_FILE_CACHE(object));
    g_free(priv->cache_dir);

    G_OBJECT_CLASS(crispy_file_cache_parent_class)->finalize(object);
}

static void
crispy_file_cache_class_init(
    CrispyFileCacheClass *klass
){
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = crispy_file_cache_finalize;
}

static void
crispy_file_cache_init(
    CrispyFileCache *self
){
    (void)self;
}

/* --- public API --- */

CrispyFileCache *
crispy_file_cache_new(void)
{
    return crispy_file_cache_new_with_dir(NULL);
}

CrispyFileCache *
crispy_file_cache_new_with_dir(
    const gchar *cache_dir
){
    CrispyFileCache *self;
    CrispyFileCachePrivate *priv;

    self = g_object_new(CRISPY_TYPE_FILE_CACHE, NULL);
    priv = crispy_file_cache_get_instance_private(self);

    /* use custom dir if provided, otherwise default to ~/.cache/crispy */
    if (cache_dir != NULL)
        priv->cache_dir = g_strdup(cache_dir);
    else
        priv->cache_dir = g_build_filename(g_get_user_cache_dir(), "crispy", NULL);

    /* attempt to create the cache directory; warn on failure rather than
     * aborting — the cache will simply miss every time if this fails */
    if (g_mkdir_with_parents(priv->cache_dir, 0755) != 0)
        g_warning("Failed to create cache directory '%s'", priv->cache_dir);

    return self;
}

const gchar *
crispy_file_cache_get_dir(
    CrispyFileCache *self
){
    CrispyFileCachePrivate *priv;

    g_return_val_if_fail(CRISPY_IS_FILE_CACHE(self), NULL);

    priv = crispy_file_cache_get_instance_private(self);
    return priv->cache_dir;
}
