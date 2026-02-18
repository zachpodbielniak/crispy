# Crispy Configuration

## Overview

Crispy supports a C-based configuration file that is compiled and loaded using crispy's own infrastructure. The config file is itself a C source file that gets compiled to a shared object, cached by content hash, and loaded before any script runs.

This gives configuration files full access to GLib/GObject APIs and any libraries linked via `CRISPY_PARAMS`, making crispy's config system as powerful as its scripting system.

## Quick Start

Generate the default config template:

```bash
mkdir -p ~/.config/crispy
crispy --generate-c-config > ~/.config/crispy/config.c
```

Edit the config to enable features, then run any script normally -- the config compiles automatically on first use and is cached by SHA256 for subsequent runs.

## Config File Search Order

Crispy searches for a config file in the following order (first found wins):

1. `$CRISPY_CONFIG_FILE` environment variable
2. `-c`/`--config PATH` CLI argument
3. `~/.config/crispy/config.c`
4. `/etc/crispy/config.c`
5. `/usr/share/crispy/config.c`

## Bypassing Configuration

To skip config loading entirely:

```bash
# CLI flag
crispy --no-config script.c

# Environment variable (any value)
NO_CRISPY_CONFIG=1 crispy script.c
```

## Config File Structure

A config file must include `<crispy.h>` and export a `crispy_config_init` function:

```c
#include <crispy.h>

G_MODULE_EXPORT gboolean
crispy_config_init(CrispyConfigContext *ctx)
{
    /* configuration here */
    return TRUE;
}
```

The function receives a `CrispyConfigContext` pointer with setter/getter functions for all configurable aspects. Returning `TRUE` applies the configuration; returning `FALSE` skips it (crispy falls back to defaults).

### CRISPY_PARAMS in Config Files

Config files can define `CRISPY_PARAMS` just like regular scripts to link against additional libraries needed by the config itself:

```c
#define CRISPY_PARAMS "$(pkg-config --cflags --libs json-glib-1.0)"

#include <crispy.h>
#include <json-glib/json-glib.h>

G_MODULE_EXPORT gboolean
crispy_config_init(CrispyConfigContext *ctx)
{
    /* can now use json-glib in config logic */
    return TRUE;
}
```

## Configuration Options

### Compiler Flags

Crispy uses a three-tier compiler flag system. The precedence is based on gcc's last-wins behavior for conflicting flags:

| Tier | Source | Priority | Description |
|------|--------|----------|-------------|
| 1 | `extra_flags` | Lowest | Config defaults, prepended first |
| 2 | `CRISPY_PARAMS` | Middle | Per-script overrides from `#define CRISPY_PARAMS` |
| 3 | Plugin flags | Higher | Injected by plugins at PRE_COMPILE hook |
| 4 | `override_flags` | Highest | Config forced overrides, appended last |

**extra_flags** -- default flags prepended before everything else. Scripts can override these via their own `CRISPY_PARAMS`:

```c
/* link libm by default for all scripts */
crispy_config_context_set_extra_flags(ctx, "-lm");

/* append more flags (space-separated) */
crispy_config_context_append_extra_flags(ctx,
    "$(pkg-config --cflags --libs json-glib-1.0)");
```

**override_flags** -- forced flags appended after everything else. These cannot be overridden by scripts or plugins:

```c
/* force all scripts to compile with warnings as errors */
crispy_config_context_set_override_flags(ctx, "-Wall -Werror");

/* append more overrides */
crispy_config_context_append_override_flags(ctx, "-Wno-unused-variable");
```

### Per-Script Conditionals

Use `crispy_config_context_get_script_path()` to apply different settings per script:

```c
G_MODULE_EXPORT gboolean
crispy_config_init(CrispyConfigContext *ctx)
{
    const gchar *script = crispy_config_context_get_script_path(ctx);

    if (script != NULL && g_str_has_suffix(script, "unsafe.c"))
    {
        /* force sanitizers for specific scripts */
        crispy_config_context_set_override_flags(ctx,
            "-fsanitize=address -fsanitize=undefined");
    }

    return TRUE;
}
```

The script path is `NULL` for inline (`-i`) and stdin (`-`) modes.

### Plugin Loading

Plugins specified in the config file load before CLI-specified plugins (`-P`):

```c
crispy_config_context_add_plugin(ctx,
    "/usr/lib/crispy/plugins/timing.so");
crispy_config_context_add_plugin(ctx,
    "/usr/lib/crispy/plugins/source-guard.so");
```

### Plugin Configuration Data

Set key-value data that plugins can access via `crispy_plugin_engine_get_data()`:

```c
crispy_config_context_set_plugin_data(ctx,
    "source-guard.blocked", "system,exec,popen");
```

### CrispyFlags

Set default flags. CLI flags are OR'd on top (CLI always wins):

```c
/* set base flags */
crispy_config_context_set_flags(ctx, CRISPY_FLAG_NONE);

/* add individual flags (OR'd with existing) */
crispy_config_context_add_flags(ctx, CRISPY_FLAG_PRESERVE_SOURCE);
```

### Cache Directory

Override the default cache directory (`~/.cache/crispy/`):

```c
crispy_config_context_set_cache_dir(ctx, "/tmp/crispy-cache");
```

Note: if the CLI `--cache-dir` option is also provided, the CLI value takes precedence.

### Script Arguments

Inspect and optionally replace the script's argument vector:

```c
gint argc = crispy_config_context_get_script_argc(ctx);
gchar **argv = crispy_config_context_get_script_argv(ctx);

/* inspect args */
for (gint i = 0; i < argc; i++)
    g_debug("script argv[%d] = %s", i, argv[i]);

/* replace args entirely (context takes ownership of new_argv) */
gchar **new_argv = g_new0(gchar *, 3);
new_argv[0] = g_strdup("replaced.c");
new_argv[1] = g_strdup("--custom-flag");
new_argv[2] = NULL;
crispy_config_context_set_script_argv(ctx, 2, new_argv);
```

### Crispy's Own Arguments

Access crispy's full original command line (read-only):

```c
gint c_argc = crispy_config_context_get_crispy_argc(ctx);
const gchar * const *c_argv = crispy_config_context_get_crispy_argv(ctx);
```

## Execution Order

The config file runs early in the startup sequence:

1. Parse CLI arguments
2. Handle early exits (`--version`, `--license`, `--generate-c-config`)
3. Create compiler and cache
4. **Load and execute config file** (if not bypassed)
5. Apply config results (cache dir, flags, argv)
6. Handle `--clean-cache` (with possibly-updated cache)
7. Build flags (config defaults | CLI overrides)
8. Preload library (`-p`)
9. Load config-specified plugins
10. Load CLI-specified plugins (`-P`)
11. Inject config plugin data into engine
12. Create and configure script
13. Execute script

## Caching

Config files are cached using the same SHA256 content-hash mechanism as scripts. Editing the config file automatically triggers recompilation on the next run. The compiled `.so` is kept loaded for the duration of the process.

## Full Example

```c
/*
 * My crispy config
 * Install: crispy --generate-c-config > ~/.config/crispy/config.c
 */
#include <crispy.h>

G_MODULE_EXPORT gboolean
crispy_config_init(CrispyConfigContext *ctx)
{
    const gchar *script = crispy_config_context_get_script_path(ctx);

    /* always link libm and libpthread */
    crispy_config_context_set_extra_flags(ctx, "-lm -lpthread");

    /* force strict warnings for all scripts */
    crispy_config_context_set_override_flags(ctx, "-Wall -Wextra");

    /* auto-load timing plugin */
    crispy_config_context_add_plugin(ctx,
        "/usr/lib/crispy/plugins/timing.so");

    /* enable sanitizers for scripts in ~/debug/ */
    if (script != NULL && strstr(script, "/debug/") != NULL)
    {
        crispy_config_context_set_override_flags(ctx,
            "-Wall -Wextra -fsanitize=address -fsanitize=undefined");
    }

    return TRUE;
}
```

## API Reference

See [api.md](api.md) for the complete `CrispyConfigContext` API reference.
