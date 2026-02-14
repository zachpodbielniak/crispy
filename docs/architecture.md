# Crispy Architecture

## Overview

Crispy is structured as a GObject library (`libcrispy`) with a thin CLI frontend. The library is built around two GInterfaces that define the compilation and caching contracts, making it easy to extend with custom backends.

```
                    ┌──────────────────┐
                    │    main.c (CLI)   │
                    └────────┬─────────┘
                             │ uses
                    ┌────────▼─────────┐
                    │  CrispyScript     │ (Final type - orchestrator)
                    │                   │
                    │  - source parsing │
                    │  - pipeline exec  │
                    │  - GModule load   │
                    └──┬───────────┬───┘
                       │           │
              uses     │           │  uses
          ┌────────────▼──┐   ┌───▼──────────────┐
          │ CrispyCompiler │   │ CrispyCacheProvider│
          │  (GInterface)  │   │   (GInterface)     │
          └────────┬───────┘   └──────┬─────────────┘
                   │                  │
          implements               implements
                   │                  │
     ┌─────────────▼──────┐  ┌───────▼──────────┐
     │ CrispyGccCompiler   │  │ CrispyFileCache   │
     │ (Final type)        │  │ (Final type)       │
     │                     │  │                    │
     │ - gcc invocation    │  │ - SHA256 hashing   │
     │ - pkg-config flags  │  │ - ~/.cache/crispy/ │
     │ - version detection │  │ - mtime validation │
     └────────────────────┘  └────────────────────┘
```

## Type Hierarchy

### Interfaces

Interfaces define contracts that concrete types implement. They are the primary extension points.

#### CrispyCompiler

Defined in `src/interfaces/crispy-compiler.h`.

The compilation contract. Any type implementing this interface can serve as a compilation backend.

**Virtual methods:**

| Method | Description |
|--------|-------------|
| `get_version()` | Returns compiler version string (cache key component) |
| `get_base_flags()` | Returns default pkg-config flags for GLib libraries |
| `compile_shared()` | Compiles source to a `.so` for dynamic loading |
| `compile_executable()` | Compiles source to an executable with debug symbols |

**Implementing a custom compiler backend:**

To add support for a different compiler (e.g., clang, tcc), create a new GObject final type that implements `CrispyCompiler`:

```c
/* crispy-clang-compiler.h */
G_DECLARE_FINAL_TYPE(CrispyClangCompiler, crispy_clang_compiler,
                     CRISPY, CLANG_COMPILER, GObject)

CrispyClangCompiler *crispy_clang_compiler_new(GError **error);
```

```c
/* crispy-clang-compiler.c */
struct _CrispyClangCompiler
{
    GObject parent_instance;
};

typedef struct
{
    gchar *clang_version;
    gchar *base_flags;
} CrispyClangCompilerPrivate;

static void
compiler_iface_init(CrispyCompilerInterface *iface)
{
    iface->get_version = clang_get_version;
    iface->get_base_flags = clang_get_base_flags;
    iface->compile_shared = clang_compile_shared;
    iface->compile_executable = clang_compile_executable;
}

G_DEFINE_FINAL_TYPE_WITH_CODE(CrispyClangCompiler,
    crispy_clang_compiler, G_TYPE_OBJECT,
    G_ADD_PRIVATE(CrispyClangCompiler)
    G_IMPLEMENT_INTERFACE(CRISPY_TYPE_COMPILER, compiler_iface_init))
```

Then pass it to `CrispyScript` like any other compiler:

```c
g_autoptr(CrispyClangCompiler) compiler = crispy_clang_compiler_new(&error);
script = crispy_script_new_from_file(path,
    CRISPY_COMPILER(compiler), cache, flags, &error);
```

#### CrispyCacheProvider

Defined in `src/interfaces/crispy-cache-provider.h`.

The caching contract. Any type implementing this interface can serve as a cache backend.

**Virtual methods:**

| Method | Description |
|--------|-------------|
| `compute_hash()` | Computes SHA256 from source + flags + compiler version |
| `get_path()` | Returns the filesystem path for a cached artifact |
| `has_valid()` | Checks if a valid (non-stale) cache entry exists |
| `purge()` | Removes all cached artifacts |

**Implementing a custom cache backend:**

For example, an in-memory cache for testing or short-lived processes:

```c
G_DECLARE_FINAL_TYPE(CrispyMemoryCache, crispy_memory_cache,
                     CRISPY, MEMORY_CACHE, GObject)
```

Or a network-shared cache using a key-value store. The interface makes no assumptions about the storage mechanism -- only that it can hash, store, check, and purge.

### Concrete Types

#### CrispyGccCompiler

Defined in `src/core/crispy-gcc-compiler.h/.c`. Implements `CrispyCompiler`.

On construction, probes the system for:
- gcc binary location and version (`gcc --version`)
- Base pkg-config flags (`pkg-config --cflags --libs glib-2.0 gobject-2.0 gio-2.0 gmodule-2.0`)

Compilation commands:
- **Shared object**: `gcc -std=gnu89 -shared -fPIC <base_flags> <extra_flags> -o <output> <source>`
- **Executable**: `gcc -std=gnu89 -g -O0 <base_flags> <extra_flags> -o <output> <source>`

All process spawning uses `g_spawn_command_line_sync()`. Compilation errors include gcc's stderr in the GError message.

#### CrispyFileCache

Defined in `src/core/crispy-file-cache.h/.c`. Implements `CrispyCacheProvider`.

- Cache directory: `~/.cache/crispy/` (via `g_get_user_cache_dir()`)
- Hash algorithm: SHA256 via `GChecksum`
- Hash inputs: source content + NUL + extra_flags + NUL + compiler_version
- Cached artifacts: `~/.cache/crispy/<sha256hex>.so`
- Freshness check: cached `.so` mtime >= source file mtime (when source_path is known)
- Purge: iterates directory, removes all `*.so` files

#### CrispyScript

Defined in `src/core/crispy-script.h/.c`. Final type -- not an interface.

The orchestrator that ties everything together. It takes a `CrispyCompiler` and `CrispyCacheProvider` via its constructors, making it fully decoupled from specific implementations.

**Construction modes:**
- `new_from_file()` -- reads source from a file path
- `new_from_inline()` -- wraps inline code in a main() function
- `new_from_stdin()` -- reads source from stdin, wraps in main()

**Execution pipeline:**

See the Execution Pipeline section below.

## Execution Pipeline

```
Source (file / -i / stdin)
  │
  ▼
[1] Read source content
  │
  ▼
[2] Strip shebang (if line 1 starts with #!)
  │
  ▼
[3] Parse & extract #define CRISPY_PARAMS "..."
  │  (remove that line from modified source)
  │
  ▼
[4] Shell-expand CRISPY_PARAMS via /bin/sh -c "printf '%s' <params>"
  │
  ▼
[5] Compute SHA256(modified_source + expanded_params + compiler_version)
  │
  ▼
[6] Check cache ──[HIT]──► skip to [9]
  │
 [MISS]
  │
  ▼
[7] Write modified source to /tmp/crispy-XXXXXX.c
  │
  ▼
[8] Compile:
  │  Normal: compile_shared() → ~/.cache/crispy/<hash>.so
  │  GDB:    compile_executable() → /tmp/crispy-dbg-XXXXXX
  │
  ▼
[9] g_module_open(cached_so_path, G_MODULE_BIND_LAZY)
  │  (GDB mode: execvp("gdb", "--args", executable, ...) instead)
  │
  ▼
[10] g_module_symbol(module, "main") → CrispyMainFunc pointer
  │
  ▼
[11] main_func(argc, argv)
  │
  ▼
[12] Cleanup: g_module_close(), unlink temp (unless -S)
  │
  ▼
Exit with script's return code
```

## Error Handling

All errors use the `CRISPY_ERROR` quark with specific error codes:

| Code | Meaning |
|------|---------|
| `CRISPY_ERROR_COMPILE` | gcc compilation failed (stderr included in message) |
| `CRISPY_ERROR_LOAD` | `g_module_open()` failed |
| `CRISPY_ERROR_NO_MAIN` | No `main` symbol found in compiled module |
| `CRISPY_ERROR_IO` | File read/write error |
| `CRISPY_ERROR_PARAMS` | CRISPY_PARAMS parsing or shell expansion failed |
| `CRISPY_ERROR_CACHE` | Cache operation failed |
| `CRISPY_ERROR_GCC_NOT_FOUND` | gcc binary not found on system |

## Flags

`CrispyFlags` is a bitmask enum:

| Flag | CLI | Effect |
|------|-----|--------|
| `CRISPY_FLAG_FORCE_COMPILE` | `-n` | Skip cache, always recompile |
| `CRISPY_FLAG_PRESERVE_SOURCE` | `-S` | Keep temp source files in /tmp |
| `CRISPY_FLAG_DRY_RUN` | `--dry-run` | Show compilation command only |
| `CRISPY_FLAG_GDB` | `--gdb` | Compile as executable, launch under gdb |

## Thread Safety

- `CrispyGccCompiler` and `CrispyFileCache` instances are safe to share across threads for read-only operations (get_version, get_base_flags, compute_hash, has_valid).
- Compilation and purge operations should be serialized if multiple threads share the same instances.
- `CrispyScript` instances should not be shared across threads. Create separate instances per thread.

## Building and Linking

To use libcrispy in your own project:

```bash
# Get compiler flags
pkg-config --cflags crispy

# Get linker flags
pkg-config --libs crispy
```

Or in a Makefile:

```makefile
CFLAGS += $(shell pkg-config --cflags crispy)
LDFLAGS += $(shell pkg-config --libs crispy)
```
