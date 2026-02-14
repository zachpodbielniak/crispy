# Crispy Script Authoring Guide

## What is Crispy?

Crispy (Crispy Really Is Super Powerful Yo) is a GLib-native C scripting tool. It compiles C source files to shared objects on-the-fly, caches them by content hash, and loads/executes them via GModule. Scripts link against glib-2.0, gobject-2.0, gio-2.0, and gmodule-2.0 by default -- no extra configuration needed.

The result is that you can write and execute C programs as if they were scripts, with instant startup on subsequent runs thanks to caching.

## Script Format

A crispy script is a standard C source file with an optional shebang and optional `CRISPY_PARAMS`:

```c
#!/usr/bin/crispy
#define CRISPY_PARAMS "`pkg-config --cflags --libs libsoup-3.0`"

#include <glib.h>
#include <gio/gio.h>

gint
main(
    gint    argc,
    gchar **argv
){
    g_print("Hello from crispy!\n");
    return 0;
}
```

### Shebang

The `#!/usr/bin/crispy` line is optional. When present, it allows the script to be executed directly:

```bash
chmod +x script.c
./script.c
```

Crispy automatically strips the shebang line before compilation so gcc does not see it.

### CRISPY_PARAMS

The `#define CRISPY_PARAMS "..."` line specifies additional compiler flags. The value is shell-expanded, so backticks and `$()` work:

```c
/* link against libmath */
#define CRISPY_PARAMS "-lm"

/* use pkg-config for a library */
#define CRISPY_PARAMS "`pkg-config --cflags --libs libsoup-3.0`"

/* multiple flags */
#define CRISPY_PARAMS "-lm `pkg-config --cflags --libs json-glib-1.0`"
```

The CRISPY_PARAMS line is extracted and removed from the source before compilation. The expanded value becomes part of the cache key, so different pkg-config outputs (e.g., on different systems) produce separate cached binaries.

### main() Signature

Scripts must define a `main()` function with the standard C signature:

```c
gint
main(
    gint    argc,
    gchar **argv
){
    /* your code here */
    return 0;
}
```

The return value propagates as the script's exit code.

## Default Available Libraries

Every crispy script links against these libraries automatically:

| Library | Provides |
|---------|----------|
| glib-2.0 | Core utilities, data structures, string handling, memory management |
| gobject-2.0 | GObject type system, signals, properties |
| gio-2.0 | File I/O, networking, D-Bus, settings |
| gmodule-2.0 | Dynamic module loading |

You can `#include` any header from these libraries without additional configuration:

```c
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gmodule.h>
```

## Adding Extra Dependencies

Use `CRISPY_PARAMS` with `pkg-config` to add libraries:

```c
/* JSON parsing */
#define CRISPY_PARAMS "`pkg-config --cflags --libs json-glib-1.0`"
#include <json-glib/json-glib.h>

/* HTTP client */
#define CRISPY_PARAMS "`pkg-config --cflags --libs libsoup-3.0`"
#include <libsoup/soup.h>

/* Math library (no pkg-config needed) */
#define CRISPY_PARAMS "-lm"
#include <math.h>

/* GTK GUI */
#define CRISPY_PARAMS "`pkg-config --cflags --libs gtk4`"
#include <gtk/gtk.h>
```

## Execution Modes

### File Mode

The most common mode. Pass a `.c` file path:

```bash
crispy script.c
crispy script.c arg1 arg2
```

### Inline Mode (-i)

Execute a C expression directly. The code is wrapped in a `main()` function with default GLib includes:

```bash
crispy -i 'g_print("hello\n"); return 0;'
```

Add extra includes with `-I` (semicolon-separated):

```bash
crispy -I "math.h;stdlib.h" -i 'g_print("%f\n", sqrt(2.0)); return 0;'
```

Note: inline mode does not support `CRISPY_PARAMS`.

### Stdin Mode (-)

Read full source from stdin:

```bash
cat <<'EOF' | crispy -
#include <glib.h>
gint main(gint argc, gchar **argv){
    g_print("from stdin!\n");
    return 0;
}
EOF
```

Or pipe from another command:

```bash
echo '#include <glib.h>
gint main(gint argc, gchar **argv){ return 42; }' | crispy -
echo $?  # prints 42
```

### Shebang Mode

Make the script executable:

```bash
chmod +x script.c
./script.c arg1 arg2
```

The kernel invokes `crispy script.c arg1 arg2` via the shebang.

## Argument Passing

Arguments after the script path are passed to the script's `main()`:

```bash
crispy script.c hello world
# In script: argc=3, argv={"script.c", "hello", "world"}
```

For inline mode, all remaining arguments are passed:

```bash
crispy -i 'g_print("arg1=%s\n", argv[1]); return 0;' hello
```

## Caching

Crispy caches compiled shared objects in `~/.cache/crispy/`. The cache key is a SHA256 hash of:

1. The full source content
2. The expanded CRISPY_PARAMS value
3. The compiler version string

### Cache Behavior

- **First run**: Source is compiled to a `.so` and cached. This takes a moment.
- **Subsequent runs**: If the cache entry exists and the source hasn't changed, the cached `.so` is loaded directly. Startup is nearly instant.
- **Source changes**: Any modification to the source, CRISPY_PARAMS, or compiler version invalidates the cache.

### Cache Management

```bash
# Force recompilation (skip cache)
crispy -n script.c

# Purge all cached artifacts
crispy --clean-cache
```

## Debugging with GDB

Use `--gdb` to compile the script as a standalone executable with debug symbols (`-g -O0`) and launch it under gdb:

```bash
crispy --gdb script.c
```

This compiles the source to a temporary executable (not a .so) and runs `gdb --args <executable> <script_args>`.

## Other Options

```bash
# Keep temp source files (useful for debugging preprocessed source)
crispy -S script.c

# Show what would be compiled without running
crispy --dry-run script.c

# Preload a shared library before execution
crispy -p libcustom.so script.c

# Show version
crispy -v

# Show license
crispy --license
```

## Performance Tips

- **Warm runs are fast**: After the first compilation, cached scripts load in milliseconds via `g_module_open()`.
- **Cold runs depend on gcc**: First compilation time depends on script complexity and linked libraries.
- **Cache is per-system**: Different compiler versions or pkg-config outputs produce separate cache entries. This is intentional -- binaries are not portable across systems.
- **Use `-n` sparingly**: Forcing recompilation on every run negates the caching benefit.

## Common Patterns

### GLib Data Structures

```c
#!/usr/bin/crispy
#include <glib.h>

gint
main(
    gint    argc,
    gchar **argv
){
    g_autoptr(GHashTable) table = NULL;
    g_autoptr(GPtrArray) array = NULL;
    gint i;

    /* hash table */
    table = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(table, "key1", "value1");
    g_hash_table_insert(table, "key2", "value2");
    g_print("table size: %u\n", g_hash_table_size(table));

    /* pointer array */
    array = g_ptr_array_new();
    for (i = 0; i < 5; i++)
        g_ptr_array_add(array, g_strdup_printf("item-%d", i));
    g_print("array length: %u\n", array->len);

    return 0;
}
```

### GIO File Operations

```c
#!/usr/bin/crispy
#include <glib.h>
#include <gio/gio.h>

gint
main(
    gint    argc,
    gchar **argv
){
    g_autoptr(GFile) file = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *contents = NULL;
    gsize length;

    if (argc < 2)
    {
        g_printerr("Usage: %s <file>\n", argv[0]);
        return 1;
    }

    file = g_file_new_for_path(argv[1]);
    if (!g_file_load_contents(file, NULL, &contents, &length, NULL, &error))
    {
        g_printerr("Error: %s\n", error->message);
        return 1;
    }
    g_print("%s", contents);

    return 0;
}
```

### Using g_autoptr

Crispy scripts fully support GLib's `g_autoptr()` for automatic resource management:

```c
g_autoptr(GError) error = NULL;       /* auto g_error_free */
g_autoptr(GHashTable) table = NULL;   /* auto g_hash_table_unref */
g_autoptr(GFile) file = NULL;         /* auto g_object_unref */
g_autofree gchar *str = NULL;         /* auto g_free */
```

This prevents resource leaks and makes scripts cleaner.
