<p align="center">
  <img src="data/logo.jpg" alt="Crispy Logo" width="256">
</p>

# Crispy

**Crispy Really Is Super Powerful Yo**

A GLib-native C scripting tool. Write C scripts that compile on-the-fly, cache automatically, and run with full access to GLib, GObject, GIO, and GModule.

## Features

- **Instant startup** -- compiled scripts are cached by content hash; subsequent runs load the cached `.so` directly
- **GLib-native** -- scripts link against glib-2.0, gobject-2.0, gio-2.0, and gmodule-2.0 by default
- **CRISPY_PARAMS** -- add extra compiler flags via `#define CRISPY_PARAMS` with shell expansion (backticks, `$()`)
- **Multiple modes** -- file, inline (`-i`), stdin (`-`), and shebang (`#!/usr/bin/crispy`)
- **GDB support** -- `--gdb` compiles with debug symbols and launches under gdb
- **Extensible library** -- GObject interfaces for compiler and cache backends

## Quick Start

```bash
# Install build dependencies (Fedora 42+)
make install-deps

# Build
make

# Run an example
./build/release/crispy examples/hello.c

# Inline mode
./build/release/crispy -i 'g_print("hello\n"); return 0;'

# Make a script executable
chmod +x examples/hello.c
./examples/hello.c
```

## Script Format

```c
#!/usr/bin/crispy
#define CRISPY_PARAMS "-lm"

#include <math.h>
#include <glib.h>

gint
main(
    gint    argc,
    gchar **argv
){
    g_print("sqrt(2) = %f\n", sqrt(2.0));
    return 0;
}
```

## Usage

```
Usage: crispy [OPTIONS] SCRIPT [SCRIPT_ARGS...]
       crispy -i "CODE" [SCRIPT_ARGS...]
       crispy - [SCRIPT_ARGS...]

Options:
  -i, --inline CODE         Execute inline C code
  -I, --include HEADERS     Additional headers (semicolon-separated)
  -p, --preload LIBNAME     Preload a shared library
  -n, --no-cache            Force recompilation (skip cache)
  -S, --source-preserve     Keep temp source files in /tmp
      --gdb                 Compile with debug symbols, launch under gdb
      --dry-run             Show compilation command without executing
      --clean-cache         Purge ~/.cache/crispy/ and exit
  -v, --version             Show version
      --license             Show AGPLv3 license notice
  -h, --help                Show help
```

## Building

### Dependencies

Fedora 42+:

```bash
make install-deps
```

This installs: `gcc`, `make`, `pkgconf-pkg-config`, `glib2-devel`.

For GObject Introspection support, also install `gobject-introspection-devel`:

```bash
make install-deps BUILD_GIR=1
```

### Build Targets

```bash
make                # Release build (build/release/)
make DEBUG=1        # Debug build (build/debug/)
make DEBUG=1 ASAN=1 # Debug build with AddressSanitizer
make test           # Build and run all tests
make clean          # Clean current build type
make clean-all      # Clean all build artifacts
make install        # Install to /usr/local (or PREFIX=...)
make uninstall      # Remove installed files
make help           # Show all available targets
make show-config    # Show current build configuration
```

### Build Output

```
build/release/
├── crispy                          # CLI executable
├── libcrispy.a                     # Static library
├── libcrispy.so -> libcrispy.so.0  # Shared library symlinks
├── libcrispy.so.0 -> libcrispy.so.0.1.0
├── libcrispy.so.0.1.0
├── crispy.pc                       # pkg-config file
└── test-*                          # Test binaries
```

## Architecture

Crispy is built on GObject interfaces for extensibility:

```
CrispyCompiler (GInterface)     CrispyCacheProvider (GInterface)
       │                                │
       ▼                                ▼
CrispyGccCompiler (Final)      CrispyFileCache (Final)
       │                                │
       └──────────┬─────────────────────┘
                  ▼
          CrispyScript (Final)
```

- **CrispyCompiler** -- compilation contract (get_version, compile_shared, compile_executable)
- **CrispyCacheProvider** -- caching contract (compute_hash, get_path, has_valid, purge)
- **CrispyGccCompiler** -- gcc-based compiler implementation
- **CrispyFileCache** -- filesystem cache in `~/.cache/crispy/`
- **CrispyScript** -- orchestrator that ties compilation and caching together

Implement the interfaces to add custom compiler backends (clang, tcc) or cache strategies (in-memory, network-shared).

## Examples

| File | Description |
|------|-------------|
| `examples/hello.c` | Basic hello world with shebang |
| `examples/args.c` | Argument passing demonstration |
| `examples/file-io.c` | GIO file operations |
| `examples/math.c` | CRISPY_PARAMS demo with `-lm` |

## Tests

35 tests across 4 test binaries using GTest:

```bash
make test
```

| Test Binary | Tests | Coverage |
|-------------|-------|----------|
| test-gcc-compiler | 9 | Compiler construction, version, flags, shared/executable compilation, error handling |
| test-file-cache | 11 | Cache construction, hash determinism, path format, hit/miss, purge |
| test-script | 8 | File/inline execution, exit codes, CRISPY_PARAMS, shebang, compile errors, arg passing |
| test-interfaces | 7 | Interface types, final types, conformance checks |

## Documentation

- [Script Authoring Guide](docs/scripting.md) -- how to write and run crispy scripts
- [Architecture](docs/architecture.md) -- library design, interfaces, extension points
- [API Reference](docs/api.md) -- complete API for all types, interfaces, and functions

## Caching

Scripts are cached by SHA256 hash of the source content + CRISPY_PARAMS + compiler version. Cache location: `~/.cache/crispy/`.

```bash
# Force recompilation
crispy -n script.c

# Purge all cached artifacts
crispy --clean-cache
```

## License

AGPLv3. See [LICENSE](LICENSE).
