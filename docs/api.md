# Crispy API Reference

## Including

All public API is accessible through the umbrella header:

```c
#include <crispy.h>
```

Individual headers cannot be included directly (they will error).

---

## Enumerations

### CrispyFlags

```c
typedef enum
{
    CRISPY_FLAG_NONE            = 0,
    CRISPY_FLAG_FORCE_COMPILE   = 1 << 0,
    CRISPY_FLAG_PRESERVE_SOURCE = 1 << 1,
    CRISPY_FLAG_DRY_RUN         = 1 << 2,
    CRISPY_FLAG_GDB             = 1 << 3
} CrispyFlags;
```

Bitmask flags controlling script compilation and execution behavior.

| Value | Description |
|-------|-------------|
| `CRISPY_FLAG_NONE` | No special flags |
| `CRISPY_FLAG_FORCE_COMPILE` | Skip cache, force recompilation |
| `CRISPY_FLAG_PRESERVE_SOURCE` | Keep temp source files in /tmp |
| `CRISPY_FLAG_DRY_RUN` | Show compilation command without executing |
| `CRISPY_FLAG_GDB` | Compile as executable with debug symbols, launch under gdb |

### CrispyError

```c
typedef enum
{
    CRISPY_ERROR_COMPILE,
    CRISPY_ERROR_LOAD,
    CRISPY_ERROR_NO_MAIN,
    CRISPY_ERROR_IO,
    CRISPY_ERROR_PARAMS,
    CRISPY_ERROR_CACHE,
    CRISPY_ERROR_GCC_NOT_FOUND,
    CRISPY_ERROR_PLUGIN
} CrispyError;
```

Error codes for the `CRISPY_ERROR` domain.

| Code | Description |
|------|-------------|
| `CRISPY_ERROR_COMPILE` | Compilation failed |
| `CRISPY_ERROR_LOAD` | Module loading failed |
| `CRISPY_ERROR_NO_MAIN` | No main() symbol found in compiled module |
| `CRISPY_ERROR_IO` | File I/O error |
| `CRISPY_ERROR_PARAMS` | Error parsing CRISPY_PARAMS |
| `CRISPY_ERROR_CACHE` | Cache operation failed |
| `CRISPY_ERROR_GCC_NOT_FOUND` | gcc binary not found |
| `CRISPY_ERROR_PLUGIN` | Plugin load or hook failure |

### CrispyHookPoint

```c
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
```

Hook points in the script execution pipeline.

| Value | Description |
|-------|-------------|
| `CRISPY_HOOK_SOURCE_LOADED` | After source parsed, shebang/params stripped |
| `CRISPY_HOOK_PARAMS_EXPANDED` | After CRISPY_PARAMS shell expansion |
| `CRISPY_HOOK_HASH_COMPUTED` | After SHA256 cache key computed |
| `CRISPY_HOOK_CACHE_CHECKED` | After cache lookup (hit or miss) |
| `CRISPY_HOOK_PRE_COMPILE` | Before gcc invocation (cache miss only) |
| `CRISPY_HOOK_POST_COMPILE` | After successful compilation |
| `CRISPY_HOOK_MODULE_LOADED` | After g_module_open() of compiled .so |
| `CRISPY_HOOK_PRE_EXECUTE` | Before calling main() |
| `CRISPY_HOOK_POST_EXECUTE` | After main() returns |
| `CRISPY_HOOK_POINT_COUNT` | Total number of hook points (not a valid hook) |

### CrispyHookResult

```c
typedef enum
{
    CRISPY_HOOK_CONTINUE = 0,
    CRISPY_HOOK_ABORT,
    CRISPY_HOOK_FORCE_RECOMPILE
} CrispyHookResult;
```

Return value from hook functions indicating how the pipeline should proceed.

| Value | Description |
|-------|-------------|
| `CRISPY_HOOK_CONTINUE` | Proceed normally to the next phase |
| `CRISPY_HOOK_ABORT` | Stop the pipeline (plugin should set `ctx->error`) |
| `CRISPY_HOOK_FORCE_RECOMPILE` | Force recompilation even on cache hit (only meaningful from `CACHE_CHECKED`) |

---

## Type Definitions

### CrispyMainFunc

```c
typedef gint (*CrispyMainFunc)(gint argc, gchar **argv);
```

Function pointer type for the script's `main()` entry point.

### CrispyPluginInfo

```c
typedef struct
{
    const gchar *name;
    const gchar *description;
    const gchar *version;
    const gchar *author;
    const gchar *license;
} CrispyPluginInfo;
```

Metadata descriptor that every plugin must export as the symbol `crispy_plugin_info`. Use `CRISPY_PLUGIN_DEFINE()` to generate it.

### CrispyHookContext

```c
typedef struct _CrispyHookContext CrispyHookContext;
```

Context structure passed to every hook function. Contains both read-only pipeline state and mutable fields that plugins can modify to alter execution behavior. See [docs/plugins.md](plugins.md) for full field reference.

### CrispyPluginHookFunc

```c
typedef CrispyHookResult (*CrispyPluginHookFunc)(CrispyHookContext *ctx);
```

Function signature for all hook callbacks. Plugins export functions named `crispy_plugin_on_<hook_name>` matching this signature.

### CrispyPluginInitFunc

```c
typedef gpointer (*CrispyPluginInitFunc)(void);
```

Optional plugin initialization function. Called once when the plugin is loaded. The return value is stored as `plugin_data` and passed to every hook invocation.

### CrispyPluginShutdownFunc

```c
typedef void (*CrispyPluginShutdownFunc)(gpointer plugin_data);
```

Optional plugin shutdown function. Called once when the plugin engine is finalized.

---

## Macros

### CRISPY_ERROR

```c
#define CRISPY_ERROR (crispy_error_quark())
```

The GQuark for the crispy error domain. Use with `g_error_matches()` and `g_assert_error()`.

### CRISPY_HASH_ALGO

```c
#define CRISPY_HASH_ALGO (G_CHECKSUM_SHA256)
```

The hash algorithm used for cache keys.

### CRISPY_MAX_PARAMS_LEN

```c
#define CRISPY_MAX_PARAMS_LEN (8192)
```

Maximum length of the CRISPY_PARAMS define value.

### CRISPY_PLUGIN_DEFINE

```c
#define CRISPY_PLUGIN_DEFINE(name, description, version, author, license)
```

Convenience macro that generates the mandatory `crispy_plugin_info` symbol with the given metadata fields. Every plugin must use this macro exactly once.

### Version Macros

```c
#define CRISPY_VERSION_MAJOR  /* major version */
#define CRISPY_VERSION_MINOR  /* minor version */
#define CRISPY_VERSION_MICRO  /* micro version */
#define CRISPY_VERSION_STRING /* "major.minor.micro" */
#define CRISPY_CHECK_VERSION(major, minor, micro) /* version check */
```

---

## CrispyCompiler (GInterface)

**Type macro:** `CRISPY_TYPE_COMPILER`

**Check macro:** `CRISPY_IS_COMPILER(obj)`

**Cast macro:** `CRISPY_COMPILER(obj)`

### crispy_compiler_get_version

```c
const gchar *
crispy_compiler_get_version(CrispyCompiler *self);
```

Returns the compiler version string. Used as part of the cache key to invalidate cached artifacts when the compiler changes.

**Parameters:**
- `self` -- a CrispyCompiler

**Returns:** (transfer none) the compiler version string

### crispy_compiler_get_base_flags

```c
const gchar *
crispy_compiler_get_base_flags(CrispyCompiler *self);
```

Returns the base compiler and linker flags for the default libraries (glib-2.0, gobject-2.0, gio-2.0, gmodule-2.0).

**Parameters:**
- `self` -- a CrispyCompiler

**Returns:** (transfer none) the base flags string

### crispy_compiler_compile_shared

```c
gboolean
crispy_compiler_compile_shared(CrispyCompiler  *self,
                               const gchar     *source_path,
                               const gchar     *output_path,
                               const gchar     *extra_flags,
                               GError         **error);
```

Compiles the source file to a shared object suitable for loading with `g_module_open()`.

**Parameters:**
- `self` -- a CrispyCompiler
- `source_path` -- path to the C source file
- `output_path` -- path for the output .so file
- `extra_flags` -- (nullable) additional compiler flags from CRISPY_PARAMS
- `error` -- return location for a GError, or NULL

**Returns:** TRUE on success, FALSE on error

### crispy_compiler_compile_executable

```c
gboolean
crispy_compiler_compile_executable(CrispyCompiler  *self,
                                   const gchar     *source_path,
                                   const gchar     *output_path,
                                   const gchar     *extra_flags,
                                   GError         **error);
```

Compiles the source file to a standalone executable with debug symbols. Used for `--gdb` mode.

**Parameters:**
- `self` -- a CrispyCompiler
- `source_path` -- path to the C source file
- `output_path` -- path for the output executable
- `extra_flags` -- (nullable) additional compiler flags from CRISPY_PARAMS
- `error` -- return location for a GError, or NULL

**Returns:** TRUE on success, FALSE on error

---

## CrispyCacheProvider (GInterface)

**Type macro:** `CRISPY_TYPE_CACHE_PROVIDER`

**Check macro:** `CRISPY_IS_CACHE_PROVIDER(obj)`

**Cast macro:** `CRISPY_CACHE_PROVIDER(obj)`

### crispy_cache_provider_compute_hash

```c
gchar *
crispy_cache_provider_compute_hash(CrispyCacheProvider *self,
                                   const gchar         *source_content,
                                   gssize               source_len,
                                   const gchar         *extra_flags,
                                   const gchar         *compiler_version);
```

Computes a hash key from the source content, extra flags, and compiler version.

**Parameters:**
- `self` -- a CrispyCacheProvider
- `source_content` -- the source file contents
- `source_len` -- length of source content, or -1 if NUL-terminated
- `extra_flags` -- (nullable) expanded CRISPY_PARAMS string
- `compiler_version` -- compiler version string

**Returns:** (transfer full) hex digest string, free with `g_free()`

### crispy_cache_provider_get_path

```c
gchar *
crispy_cache_provider_get_path(CrispyCacheProvider *self,
                               const gchar         *hash);
```

Returns the full path to the cached artifact for a given hash.

**Parameters:**
- `self` -- a CrispyCacheProvider
- `hash` -- the hash key

**Returns:** (transfer full) file path string, free with `g_free()`

### crispy_cache_provider_has_valid

```c
gboolean
crispy_cache_provider_has_valid(CrispyCacheProvider *self,
                                const gchar         *hash,
                                const gchar         *source_path);
```

Checks if a valid cached artifact exists for the given hash. If `source_path` is provided, also verifies the cached artifact is not stale.

**Parameters:**
- `self` -- a CrispyCacheProvider
- `hash` -- the hash key
- `source_path` -- (nullable) original source file path for freshness check

**Returns:** TRUE if a valid cache entry exists

### crispy_cache_provider_purge

```c
gboolean
crispy_cache_provider_purge(CrispyCacheProvider *self,
                            GError             **error);
```

Purges all cached artifacts managed by this provider.

**Parameters:**
- `self` -- a CrispyCacheProvider
- `error` -- return location for a GError, or NULL

**Returns:** TRUE on success

---

## CrispyGccCompiler (Final Type)

**Type macro:** `CRISPY_TYPE_GCC_COMPILER`

**Check macros:** `CRISPY_IS_GCC_COMPILER(obj)`, `CRISPY_IS_COMPILER(obj)`

**Cast macro:** `CRISPY_GCC_COMPILER(obj)`

**Implements:** CrispyCompiler

### crispy_gcc_compiler_new

```c
CrispyGccCompiler *
crispy_gcc_compiler_new(GError **error);
```

Creates a new CrispyGccCompiler instance. On construction, probes gcc for its version and caches the pkg-config output for default GLib libraries.

**Parameters:**
- `error` -- return location for a GError, or NULL

**Returns:** (transfer full) a new CrispyGccCompiler, or NULL on error (e.g., gcc not found)

---

## CrispyFileCache (Final Type)

**Type macro:** `CRISPY_TYPE_FILE_CACHE`

**Check macros:** `CRISPY_IS_FILE_CACHE(obj)`, `CRISPY_IS_CACHE_PROVIDER(obj)`

**Cast macro:** `CRISPY_FILE_CACHE(obj)`

**Implements:** CrispyCacheProvider

### crispy_file_cache_new

```c
CrispyFileCache *
crispy_file_cache_new(void);
```

Creates a new CrispyFileCache instance. The cache directory (`~/.cache/crispy/`) is created if it does not exist.

**Returns:** (transfer full) a new CrispyFileCache

### crispy_file_cache_get_dir

```c
const gchar *
crispy_file_cache_get_dir(CrispyFileCache *self);
```

Returns the path to the cache directory.

**Parameters:**
- `self` -- a CrispyFileCache

**Returns:** (transfer none) the cache directory path

---

## CrispyPluginEngine (Final Type)

**Type macro:** `CRISPY_TYPE_PLUGIN_ENGINE`

**Check macro:** `CRISPY_IS_PLUGIN_ENGINE(obj)`

**Cast macro:** `CRISPY_PLUGIN_ENGINE(obj)`

### crispy_plugin_engine_new

```c
CrispyPluginEngine *
crispy_plugin_engine_new(void);
```

Creates a new empty plugin engine with no plugins loaded.

**Returns:** (transfer full) a new CrispyPluginEngine

### crispy_plugin_engine_load

```c
gboolean
crispy_plugin_engine_load(CrispyPluginEngine  *self,
                          const gchar         *path,
                          GError             **error);
```

Loads a single plugin from the given path. The plugin must export a `crispy_plugin_info` symbol. If the plugin exports a `crispy_plugin_init` function, it is called immediately.

**Parameters:**
- `self` -- a CrispyPluginEngine
- `path` -- path to a plugin .so file
- `error` -- return location for a GError, or NULL

**Returns:** TRUE on success, FALSE on error

### crispy_plugin_engine_load_paths

```c
gboolean
crispy_plugin_engine_load_paths(CrispyPluginEngine  *self,
                                const gchar         *paths,
                                GError             **error);
```

Splits `paths` on `:` and `,` delimiters, then loads each path via `crispy_plugin_engine_load()`. Stops on the first failure.

**Parameters:**
- `self` -- a CrispyPluginEngine
- `paths` -- colon-or-comma-separated list of plugin .so paths
- `error` -- return location for a GError, or NULL

**Returns:** TRUE if all plugins loaded, FALSE on first error

### crispy_plugin_engine_get_plugin_count

```c
guint
crispy_plugin_engine_get_plugin_count(CrispyPluginEngine *self);
```

Returns the number of currently loaded plugins.

**Parameters:**
- `self` -- a CrispyPluginEngine

**Returns:** the plugin count

### crispy_plugin_engine_set_data

```c
void
crispy_plugin_engine_set_data(CrispyPluginEngine *self,
                              const gchar        *key,
                              gpointer            data,
                              GDestroyNotify      destroy);
```

Stores arbitrary data in the engine's shared data store, keyed by `key`. This allows inter-plugin communication. If `key` already exists, the old value is freed via its destroy notify.

**Parameters:**
- `self` -- a CrispyPluginEngine
- `key` -- a string key for the data
- `data` -- (nullable) the data to store
- `destroy` -- (nullable) destroy notify for `data`

### crispy_plugin_engine_get_data

```c
gpointer
crispy_plugin_engine_get_data(CrispyPluginEngine *self,
                              const gchar        *key);
```

Retrieves data previously stored with `crispy_plugin_engine_set_data()`.

**Parameters:**
- `self` -- a CrispyPluginEngine
- `key` -- a string key to look up

**Returns:** (transfer none) (nullable) the stored data, or NULL

---

## CrispyScript (Final Type)

**Type macro:** `CRISPY_TYPE_SCRIPT`

**Check macro:** `CRISPY_IS_SCRIPT(obj)`

**Cast macro:** `CRISPY_SCRIPT(obj)`

### crispy_script_new_from_file

```c
CrispyScript *
crispy_script_new_from_file(const gchar          *path,
                            CrispyCompiler       *compiler,
                            CrispyCacheProvider  *cache,
                            CrispyFlags           flags,
                            GError              **error);
```

Creates a new CrispyScript from a source file. Reads the file, extracts CRISPY_PARAMS, strips the shebang, and prepares for execution.

**Parameters:**
- `path` -- path to the C source file
- `compiler` -- a CrispyCompiler implementation
- `cache` -- a CrispyCacheProvider implementation
- `flags` -- CrispyFlags controlling behavior
- `error` -- return location for a GError, or NULL

**Returns:** (transfer full) a new CrispyScript, or NULL on error

### crispy_script_new_from_inline

```c
CrispyScript *
crispy_script_new_from_inline(const gchar          *code,
                              const gchar          *extra_includes,
                              CrispyCompiler       *compiler,
                              CrispyCacheProvider  *cache,
                              CrispyFlags           flags,
                              GError              **error);
```

Creates a new CrispyScript from inline code. The code is wrapped in a `main()` function with default GLib includes.

**Parameters:**
- `code` -- the inline C code (body of main())
- `extra_includes` -- (nullable) semicolon-separated additional headers
- `compiler` -- a CrispyCompiler implementation
- `cache` -- a CrispyCacheProvider implementation
- `flags` -- CrispyFlags controlling behavior
- `error` -- return location for a GError, or NULL

**Returns:** (transfer full) a new CrispyScript, or NULL on error

### crispy_script_new_from_stdin

```c
CrispyScript *
crispy_script_new_from_stdin(CrispyCompiler       *compiler,
                             CrispyCacheProvider  *cache,
                             CrispyFlags           flags,
                             GError              **error);
```

Creates a new CrispyScript by reading C code from stdin until EOF. The code is wrapped in a `main()` function.

**Parameters:**
- `compiler` -- a CrispyCompiler implementation
- `cache` -- a CrispyCacheProvider implementation
- `flags` -- CrispyFlags controlling behavior
- `error` -- return location for a GError, or NULL

**Returns:** (transfer full) a new CrispyScript, or NULL on error

### crispy_script_execute

```c
gint
crispy_script_execute(CrispyScript  *self,
                      gint           argc,
                      gchar        **argv,
                      GError       **error);
```

Executes the full pipeline: hash, cache check, compile (if needed), load, run main(). The script's exit code is stored and can be retrieved with `crispy_script_get_exit_code()`.

**Parameters:**
- `self` -- a CrispyScript
- `argc` -- argument count for the script
- `argv` -- (array length=argc) argument vector for the script
- `error` -- return location for a GError, or NULL

**Returns:** the script's exit code, or -1 on error

### crispy_script_get_exit_code

```c
gint
crispy_script_get_exit_code(CrispyScript *self);
```

Returns the exit code from the last execution.

**Parameters:**
- `self` -- a CrispyScript

**Returns:** the exit code

### crispy_script_get_temp_source_path

```c
const gchar *
crispy_script_get_temp_source_path(CrispyScript *self);
```

Returns the path to the temporary modified source file. Only meaningful with `CRISPY_FLAG_PRESERVE_SOURCE`.

**Parameters:**
- `self` -- a CrispyScript

**Returns:** (transfer none) (nullable) the temp source path

### crispy_script_set_plugin_engine

```c
void
crispy_script_set_plugin_engine(CrispyScript       *self,
                                CrispyPluginEngine *engine);
```

Sets the plugin engine for this script. When set, hook functions from loaded plugins are dispatched at each phase of the execution pipeline. If `engine` is NULL, no hooks fire (default behavior). This must be called before `crispy_script_execute()`.

**Parameters:**
- `self` -- a CrispyScript
- `engine` -- (transfer none) (nullable) a CrispyPluginEngine, or NULL

---

## pkg-config

After `make install`, use pkg-config to get compiler/linker flags:

```bash
pkg-config --cflags crispy
pkg-config --libs crispy
```

The `.pc` file is installed to `$(LIBDIR)/pkgconfig/crispy.pc`.
