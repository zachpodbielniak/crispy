/* crispy-plugin.h - Public plugin author header for Crispy */

#ifndef CRISPY_PLUGIN_H
#define CRISPY_PLUGIN_H

#if !defined(CRISPY_INSIDE) && !defined(CRISPY_COMPILATION)
#error "Only <crispy.h> can be included directly."
#endif

#include <glib.h>

G_BEGIN_DECLS

/**
 * SECTION:crispy-plugin
 * @title: CrispyPlugin
 * @short_description: Plugin contract for extending Crispy
 *
 * This header defines the types and macros that plugin authors use
 * to create Crispy plugins. A plugin is a shared library (.so) that
 * exports well-known C symbols discovered at runtime via GModule.
 *
 * Every plugin must export a #CrispyPluginInfo descriptor named
 * `crispy_plugin_info`. Use the %CRISPY_PLUGIN_DEFINE macro to
 * generate this symbol. Plugins may also export optional lifecycle
 * and hook functions.
 *
 * Example plugin:
 * |[<!-- language="C" -->
 * #include <crispy.h>
 *
 * CRISPY_PLUGIN_DEFINE("my-plugin", "Does stuff", "0.1.0", "Me", "AGPLv3")
 *
 * CrispyHookResult
 * crispy_plugin_on_post_execute(CrispyHookContext *ctx)
 * {
 *     g_printerr("Script exited with code: %d\n", ctx->exit_code);
 *     return CRISPY_HOOK_CONTINUE;
 * }
 * ]|
 */

/* --- Hook points in the execution pipeline --- */

/**
 * CrispyHookPoint:
 * @CRISPY_HOOK_SOURCE_LOADED: After source parsed (shebang/params stripped).
 * @CRISPY_HOOK_PARAMS_EXPANDED: After CRISPY_PARAMS shell expansion.
 * @CRISPY_HOOK_HASH_COMPUTED: After SHA256 cache key computed.
 * @CRISPY_HOOK_CACHE_CHECKED: After cache lookup (hit or miss).
 * @CRISPY_HOOK_PRE_COMPILE: Before gcc invocation (cache miss only).
 * @CRISPY_HOOK_POST_COMPILE: After successful compilation.
 * @CRISPY_HOOK_MODULE_LOADED: After g_module_open() of compiled .so.
 * @CRISPY_HOOK_PRE_EXECUTE: Before calling main().
 * @CRISPY_HOOK_POST_EXECUTE: After main() returns.
 * @CRISPY_HOOK_POINT_COUNT: Total number of hook points (not a valid hook).
 *
 * Enumeration of the hook points in the script execution pipeline.
 */
typedef enum
{
    CRISPY_HOOK_SOURCE_LOADED = 0,
    CRISPY_HOOK_PARAMS_EXPANDED,
    CRISPY_HOOK_HASH_COMPUTED,
    CRISPY_HOOK_CACHE_CHECKED,
    CRISPY_HOOK_PRE_COMPILE,
    CRISPY_HOOK_POST_COMPILE,
    CRISPY_HOOK_MODULE_LOADED,
    CRISPY_HOOK_PRE_EXECUTE,
    CRISPY_HOOK_POST_EXECUTE,
    CRISPY_HOOK_POINT_COUNT
} CrispyHookPoint;

/* --- Hook result codes --- */

/**
 * CrispyHookResult:
 * @CRISPY_HOOK_CONTINUE: Proceed normally to the next phase.
 * @CRISPY_HOOK_ABORT: Stop the pipeline. Plugin should set ctx->error.
 * @CRISPY_HOOK_FORCE_RECOMPILE: Force recompilation even on cache hit
 *   (only meaningful from %CRISPY_HOOK_CACHE_CHECKED).
 *
 * Return value from hook functions indicating how the pipeline should proceed.
 */
typedef enum
{
    CRISPY_HOOK_CONTINUE = 0,
    CRISPY_HOOK_ABORT,
    CRISPY_HOOK_FORCE_RECOMPILE
} CrispyHookResult;

/* --- Hook context --- */

/**
 * CrispyHookContext:
 * @hook_point: which hook point is currently firing
 * @source_path: (nullable): original script path (NULL for inline/stdin)
 * @source_content: full original source text
 * @source_len: length of source_content in bytes
 * @crispy_params: (nullable): raw CRISPY_PARAMS value from source
 * @expanded_params: (nullable): shell-expanded CRISPY_PARAMS
 * @hash: (nullable): SHA256 cache key hex string
 * @cached_so_path: (nullable): path to cached .so file
 * @compiler_version: (nullable): compiler version string
 * @temp_source_path: (nullable): path to temp modified source
 * @flags: #CrispyFlags bitmask
 * @cache_hit: whether the cache had a valid entry
 * @modified_source: (nullable): plugin-modified source (mutable)
 * @modified_len: length of modified_source (mutable)
 * @extra_flags: (nullable): additional compiler flags injected by plugin (mutable)
 * @argc: argument count passed to script main() (mutable)
 * @argv: argument vector passed to script main() (mutable)
 * @force_recompile: set to TRUE to force recompilation (mutable)
 * @exit_code: script exit code (set after POST_EXECUTE)
 * @time_param_expand: microseconds spent expanding params
 * @time_hash: microseconds spent computing hash
 * @time_cache_check: microseconds spent checking cache
 * @time_compile: microseconds spent compiling
 * @time_module_load: microseconds spent loading module
 * @time_execute: microseconds spent executing script
 * @time_total: total microseconds from start to current hook
 * @plugin_data: (nullable): per-plugin opaque state from init
 * @engine: (nullable): the plugin engine (for shared data store)
 * @error: (nullable): location for error reporting on ABORT
 *
 * Context structure passed to every hook function. Contains both
 * read-only pipeline state and mutable fields that plugins can
 * modify to alter execution behavior.
 */
typedef struct _CrispyHookContext CrispyHookContext;

struct _CrispyHookContext
{
    /* which hook is firing */
    CrispyHookPoint  hook_point;

    /* read-only pipeline state */
    const gchar     *source_path;
    const gchar     *source_content;
    gsize            source_len;
    const gchar     *crispy_params;
    const gchar     *expanded_params;
    const gchar     *hash;
    const gchar     *cached_so_path;
    const gchar     *compiler_version;
    const gchar     *temp_source_path;
    guint            flags;
    gboolean         cache_hit;

    /* mutable fields (plugins may modify these) */
    gchar           *modified_source;
    gsize            modified_len;
    gchar           *extra_flags;
    gint             argc;
    gchar          **argv;
    gboolean         force_recompile;

    /* results */
    gint             exit_code;

    /* timing in microseconds (g_get_monotonic_time()) */
    gint64           time_param_expand;
    gint64           time_hash;
    gint64           time_cache_check;
    gint64           time_compile;
    gint64           time_module_load;
    gint64           time_execute;
    gint64           time_total;

    /* plugin access */
    gpointer         plugin_data;
    gpointer         engine;
    GError         **error;
};

/* --- Plugin info descriptor --- */

/**
 * CrispyPluginInfo:
 * @name: short plugin name (e.g. "timing")
 * @description: human-readable description
 * @version: plugin version string
 * @author: plugin author
 * @license: license identifier (e.g. "AGPLv3")
 *
 * Metadata descriptor that every plugin must export as the symbol
 * `crispy_plugin_info`. Use %CRISPY_PLUGIN_DEFINE to generate it.
 */
typedef struct
{
    const gchar *name;
    const gchar *description;
    const gchar *version;
    const gchar *author;
    const gchar *license;
} CrispyPluginInfo;

/* --- Function pointer types --- */

/**
 * CrispyPluginInitFunc:
 *
 * Optional plugin initialization function. Called once when the plugin
 * is loaded. The return value is stored as plugin_data and passed to
 * every hook invocation and to the shutdown function.
 *
 * Returns: (transfer full) (nullable): per-plugin opaque state
 */
typedef gpointer (*CrispyPluginInitFunc)(void);

/**
 * CrispyPluginShutdownFunc:
 * @plugin_data: the opaque state returned by init
 *
 * Optional plugin shutdown function. Called once when the plugin engine
 * is finalized. Plugins should free any resources allocated in init.
 */
typedef void (*CrispyPluginShutdownFunc)(gpointer plugin_data);

/**
 * CrispyPluginHookFunc:
 * @ctx: the hook context with pipeline state
 *
 * Function signature for all hook callbacks. Plugins export functions
 * named `crispy_plugin_on_<hook_name>` matching this signature.
 *
 * Returns: a #CrispyHookResult indicating how to proceed
 */
typedef CrispyHookResult (*CrispyPluginHookFunc)(CrispyHookContext *ctx);

/* --- Convenience macro --- */

/**
 * CRISPY_PLUGIN_DEFINE:
 * @_name: plugin name string
 * @_desc: plugin description string
 * @_ver: plugin version string
 * @_author: plugin author string
 * @_lic: plugin license string
 *
 * Convenience macro that generates the mandatory `crispy_plugin_info`
 * symbol with the given metadata fields.
 */
#define CRISPY_PLUGIN_DEFINE(_name, _desc, _ver, _author, _lic) \
    const CrispyPluginInfo crispy_plugin_info = { \
        .name        = (_name), \
        .description = (_desc), \
        .version     = (_ver), \
        .author      = (_author), \
        .license     = (_lic) \
    }

G_END_DECLS

#endif /* CRISPY_PLUGIN_H */
