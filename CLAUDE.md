# Crispy - Project Instructions

## Overview

Crispy (Crispy Really Is Super Powerful Yo) is a GLib/GObject/GIO-native C scripting tool. It compiles C scripts to shared objects on-the-fly, caches them by content hash (SHA256), and loads/executes them via GModule. It is both a library (`libcrispy`) and a CLI (`crispy`).

## Build System

Three-file GNU Make split (same pattern as `../gst`):

- `config.mk` -- all configurable variables (version, paths, flags, deps)
- `rules.mk` -- pattern rules, install/uninstall, generated files
- `Makefile` -- source lists, targets, test runner

### Common Commands

```bash
make                    # Release build -> build/release/
make DEBUG=1            # Debug build -> build/debug/
make DEBUG=1 ASAN=1     # Debug + AddressSanitizer
make test               # Build and run all 35 GTest tests
make clean              # Clean current build type
make clean-all          # Clean everything including generated headers
make install-deps       # Install Fedora build deps (glib2-devel, gcc, etc.)
make show-config        # Print current build configuration
```

### Build Output

Build artifacts go to `build/{debug,release}/`. The executable links with `-Wl,-rpath,'$ORIGIN'` so it finds `libcrispy.so` without installation.

## Project Layout

```
src/
  crispy.h                          # Umbrella header (include this, not individual headers)
  crispy-types.h                    # Forward declarations, CrispyFlags, CrispyError, CrispyMainFunc
  crispy-version.h.in               # Version template (sed-generated -> crispy-version.h)
  interfaces/
    crispy-compiler.h/.c            # CrispyCompiler GInterface
    crispy-cache-provider.h/.c      # CrispyCacheProvider GInterface
  core/
    crispy-gcc-compiler.h/.c        # CrispyGccCompiler (implements CrispyCompiler)
    crispy-file-cache.h/.c          # CrispyFileCache (implements CrispyCacheProvider)
    crispy-script.h/.c              # CrispyScript (final type, orchestrator)
  main.c                            # CLI entry point (GOptionContext)
tests/
  test-gcc-compiler.c               # 9 tests
  test-file-cache.c                 # 11 tests
  test-script.c                     # 8 tests (end-to-end, needs gcc at runtime)
  test-interfaces.c                 # 7 tests
examples/
  hello.c, args.c, file-io.c, math.c
docs/
  scripting.md, architecture.md, api.md
```

## Architecture

Two GInterfaces define the extensible contracts:

- **CrispyCompiler** -- compilation backend (get_version, get_base_flags, compile_shared, compile_executable)
- **CrispyCacheProvider** -- caching backend (compute_hash, get_path, has_valid, purge)

Concrete implementations:

- **CrispyGccCompiler** -- `G_DECLARE_FINAL_TYPE`, implements CrispyCompiler via gcc
- **CrispyFileCache** -- `G_DECLARE_FINAL_TYPE`, implements CrispyCacheProvider via `~/.cache/crispy/`
- **CrispyScript** -- `G_DECLARE_FINAL_TYPE`, takes interfaces as constructor args, orchestrates the full pipeline

All final types use `G_DEFINE_FINAL_TYPE_WITH_CODE` with `G_ADD_PRIVATE` and `G_IMPLEMENT_INTERFACE`. The instance struct is defined in the `.c` file (not the header) since `G_DECLARE_FINAL_TYPE` hides it.

## Coding Conventions

- **C standard**: `gnu89` (`-std=gnu89`), compiled with `gcc`
- **Indentation**: TAB characters, 4-space width
- **Comments**: `/* */` only, never `//`. GObject Introspection compatible annotations on all public functions.
- **Naming**: `UPPERCASE_SNAKE_CASE` for macros/defines, `PascalCase` for types, `lowercase_snake_case` for variables/functions
- **Memory**: Use `g_autoptr()`, `g_autofree`, and `g_steal_pointer()` for automatic cleanup
- **Error handling**: GError with `CRISPY_ERROR` quark and `CrispyError` codes
- **No camelCase**: ever

## Key Patterns

### Adding a new interface method

1. Add the vfunc pointer to the interface struct in the `.h` file
2. Add the public wrapper function declaration in the `.h` file (with GIR annotations)
3. Implement the public wrapper in the `.c` file (dispatch through the vfunc)
4. Implement the vfunc in each concrete type's `_iface_init()` function
5. Add tests

### Adding a new concrete type

1. Create `src/core/crispy-foo.h` with `G_DECLARE_FINAL_TYPE`
2. Create `src/core/crispy-foo.c` with:
   - `struct _CrispyFoo { GObject parent_instance; };` (before private struct)
   - Private data struct
   - `G_DEFINE_FINAL_TYPE_WITH_CODE(...)` with `G_ADD_PRIVATE` and any `G_IMPLEMENT_INTERFACE` calls
3. Add forward declaration to `src/crispy-types.h` (type only, NOT the Class typedef)
4. Include in `src/crispy.h` umbrella header
5. Add `.c` to `LIB_SRCS` and `.h` to `LIB_HDRS` in `Makefile`
6. Add tests and update docs

### Important gotchas

- **No `*Class` forward declarations** for final types in `crispy-types.h` -- `G_DECLARE_FINAL_TYPE` creates those automatically and they will conflict.
- **Instance struct must be in the `.c` file** before `G_DEFINE_FINAL_TYPE_WITH_CODE` so sizeof is known.
- Use `g_spawn_check_wait_status()` not the deprecated `g_spawn_check_exit_status()`.
- `src/crispy-version.h` is generated from `src/crispy-version.h.in` by sed. It's in `.gitignore`. On a clean first build, make may fail once then succeed (ordering race). Running `make` again always works.

## Commits

All commits MUST use the [Conventional Commits](https://www.conventionalcommits.org/) format:

```
<type>(<scope>): <description>

[optional body]

[optional footer(s)]
```

### Types

- `feat` -- new feature or functionality
- `fix` -- bug fix
- `refactor` -- code restructuring without behavior change
- `docs` -- documentation only changes
- `test` -- adding or updating tests
- `build` -- changes to the build system or dependencies (Makefile, config.mk, rules.mk)
- `chore` -- maintenance tasks (gitignore, CI, tooling)
- `perf` -- performance improvements
- `style` -- formatting, whitespace, comment adjustments (no logic change)

### Scopes

Use the component name as scope when applicable:

- `compiler` -- CrispyCompiler interface or CrispyGccCompiler
- `cache` -- CrispyCacheProvider interface or CrispyFileCache
- `script` -- CrispyScript
- `cli` -- main.c / CLI behavior
- `types` -- crispy-types.h (flags, error codes, typedefs)

Scope is optional for broad changes that span multiple components.

### Examples

```
feat(script): add stdin mode for piped input
fix(cache): correct mtime comparison for stale entries
refactor(compiler): extract flag building into helper function
docs: update API reference for new purge options
test(cache): add test for purge on empty directory
build: add ASAN support to debug builds
chore: update gitignore for core dump files
```

### Breaking changes

Append `!` after the type/scope and include a `BREAKING CHANGE:` footer:

```
feat(cache)!: change compute_hash to accept GBytes instead of raw string

BREAKING CHANGE: compute_hash signature changed; all callers must wrap
source content in GBytes.
```

## Testing

All tests use the GLib GTest framework. Run with `make test`. Each test binary is standalone and linked against `libcrispy.so`.

Tests that compile C code at runtime (test-gcc-compiler, test-script) require gcc to be installed.

## Documentation

Docs live in `docs/` as markdown. Keep them in sync with code changes:

- `docs/scripting.md` -- user-facing script authoring guide
- `docs/architecture.md` -- library design, interfaces, extension points
- `docs/api.md` -- complete API reference for all public types and functions
