# Plugin Authoring Guide

Crispy supports a plugin system that lets you extend the script execution pipeline without modifying crispy itself. Plugins are shared libraries (`.so` files) loaded at runtime via GModule.

## Quick Start

```c
/* my-plugin.c */
#define CRISPY_COMPILATION
#include <crispy-plugin.h>

#include <glib.h>

CRISPY_PLUGIN_DEFINE("my-plugin", "Does something useful", "0.1.0", "Me", "AGPLv3");

CrispyHookResult
crispy_plugin_on_post_execute(CrispyHookContext *ctx)
{
    g_printerr("Script exited with code: %d\n", ctx->exit_code);
    return CRISPY_HOOK_CONTINUE;
}
```

Build:
```bash
gcc -std=gnu89 -shared -fPIC -o my-plugin.so my-plugin.c \
    -I/path/to/crispy/src $(pkg-config --cflags --libs glib-2.0)
```

Use:
```bash
crispy -P my-plugin.so script.c
crispy -P plugin1.so:plugin2.so script.c    # colon-separated
crispy -P plugin1.so,plugin2.so script.c    # comma-separated
```

## Plugin Contract

### Mandatory: Plugin Info

Every plugin **must** export a `CrispyPluginInfo` struct named `crispy_plugin_info`. Use the `CRISPY_PLUGIN_DEFINE()` macro:

```c
CRISPY_PLUGIN_DEFINE(name, description, version, author, license);
```

### Optional: Lifecycle Functions

```c
/* Called once when plugin is loaded. Return value stored as plugin_data. */
gpointer crispy_plugin_init(void);

/* Called once when the engine is finalized. Free your plugin_data here. */
void crispy_plugin_shutdown(gpointer plugin_data);
```

### Optional: Hook Functions

Export any combination of these functions to hook into the pipeline:

| Function | Hook Point | When |
|----------|-----------|------|
| `crispy_plugin_on_source_loaded` | SOURCE_LOADED | After source parsed, shebang/params stripped |
| `crispy_plugin_on_params_expanded` | PARAMS_EXPANDED | After CRISPY_PARAMS shell expansion |
| `crispy_plugin_on_hash_computed` | HASH_COMPUTED | After SHA256 cache key computed |
| `crispy_plugin_on_cache_checked` | CACHE_CHECKED | After cache lookup (hit or miss) |
| `crispy_plugin_on_pre_compile` | PRE_COMPILE | Before gcc invocation (cache miss only) |
| `crispy_plugin_on_post_compile` | POST_COMPILE | After successful compilation |
| `crispy_plugin_on_module_loaded` | MODULE_LOADED | After g_module_open() |
| `crispy_plugin_on_pre_execute` | PRE_EXECUTE | Before calling main() |
| `crispy_plugin_on_post_execute` | POST_EXECUTE | After main() returns |

All hook functions have the same signature:

```c
CrispyHookResult crispy_plugin_on_<hook_name>(CrispyHookContext *ctx);
```

## Hook Context

The `CrispyHookContext` struct is passed to every hook. It contains read-only pipeline state and mutable fields.

### Read-Only Fields

| Field | Type | Description |
|-------|------|-------------|
| `hook_point` | `CrispyHookPoint` | Which hook is firing |
| `source_path` | `const gchar*` | Original script path (NULL for inline/stdin) |
| `source_content` | `const gchar*` | Full original source text |
| `source_len` | `gsize` | Length of source_content |
| `crispy_params` | `const gchar*` | Raw CRISPY_PARAMS value |
| `expanded_params` | `const gchar*` | Shell-expanded CRISPY_PARAMS |
| `hash` | `const gchar*` | SHA256 cache key |
| `cached_so_path` | `const gchar*` | Path to cached .so |
| `compiler_version` | `const gchar*` | GCC version string |
| `temp_source_path` | `const gchar*` | Path to temp source file |
| `flags` | `guint` | CrispyFlags bitmask |
| `cache_hit` | `gboolean` | Whether cache had a valid entry |

### Mutable Fields

| Field | Type | Description |
|-------|------|-------------|
| `modified_source` | `gchar*` | Plugin-modified source (set at SOURCE_LOADED) |
| `modified_len` | `gsize` | Length of modified source |
| `extra_flags` | `gchar*` | Additional compiler flags (set at PRE_COMPILE) |
| `argc` | `gint` | Script argument count (modifiable at PRE_EXECUTE) |
| `argv` | `gchar**` | Script argument vector (modifiable at PRE_EXECUTE) |
| `force_recompile` | `gboolean` | Force recompilation (set at CACHE_CHECKED) |

### Timing Fields (microseconds)

| Field | Description |
|-------|-------------|
| `time_param_expand` | Time spent expanding CRISPY_PARAMS |
| `time_hash` | Time spent computing hash |
| `time_cache_check` | Time spent checking cache |
| `time_compile` | Time spent compiling |
| `time_module_load` | Time spent loading module |
| `time_execute` | Time spent executing script |
| `time_total` | Total elapsed time |

### Access Fields

| Field | Type | Description |
|-------|------|-------------|
| `plugin_data` | `gpointer` | Per-plugin state from init() |
| `engine` | `gpointer` | Plugin engine (cast to `CrispyPluginEngine*`) |
| `error` | `GError**` | Error location (set when returning ABORT) |
| `exit_code` | `gint` | Script exit code (available at POST_EXECUTE) |

## Hook Results

| Result | Value | Meaning |
|--------|-------|---------|
| `CRISPY_HOOK_CONTINUE` | 0 | Proceed normally |
| `CRISPY_HOOK_ABORT` | 1 | Stop pipeline (set `ctx->error` first) |
| `CRISPY_HOOK_FORCE_RECOMPILE` | 2 | Force recompilation (only at CACHE_CHECKED) |

## Inter-Plugin Communication

Plugins can share data via the engine's data store:

```c
CrispyPluginEngine *engine = (CrispyPluginEngine *)ctx->engine;

/* set data (any plugin) */
crispy_plugin_engine_set_data(engine, "my-key", my_data, g_free);

/* get data (another plugin) */
gpointer data = crispy_plugin_engine_get_data(engine, "my-key");
```

## Plugin Execution Order

- Plugins are called in the order they were loaded (left-to-right in the `-P` argument)
- If a plugin returns `CRISPY_HOOK_ABORT`, subsequent plugins are **not** called
- Each plugin's `plugin_data` is swapped in before its hook is called

## Use Cases

### Performance Monitoring

Hook `POST_EXECUTE` to report timing data (see `examples/plugins/plugin-timing.c`).

### Security Scanning

Hook `SOURCE_LOADED` to scan source for dangerous patterns (see `examples/plugins/plugin-source-guard.c`).

### Source Transformation

Hook `SOURCE_LOADED` to modify `ctx->modified_source` before compilation. For example, inject instrumentation code or macro expansions.

### Compiler Flag Injection

Hook `PRE_COMPILE` to set `ctx->extra_flags` with additional compiler flags (e.g., `-DDEBUG`, `-fsanitize=address`).

### Cache Control

Hook `CACHE_CHECKED` and return `CRISPY_HOOK_FORCE_RECOMPILE` or set `ctx->force_recompile = TRUE` to bypass the cache.

### Argument Manipulation

Hook `PRE_EXECUTE` to modify `ctx->argc` and `ctx->argv` before the script's main() is called.

## Example Plugins

| Plugin | Description |
|--------|-------------|
| `examples/plugins/plugin-timing.c` | Prints per-phase timing report to stderr |
| `examples/plugins/plugin-source-guard.c` | Rejects scripts calling system(), exec(), etc. |

Build them with:
```bash
make -C examples/plugins
```
