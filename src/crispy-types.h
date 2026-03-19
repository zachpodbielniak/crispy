/* crispy-types.h - Forward declarations and common types for Crispy */

#ifndef CRISPY_TYPES_H
#define CRISPY_TYPES_H

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/* Interface forward declarations */
typedef struct _CrispyCompiler          CrispyCompiler;
typedef struct _CrispyCompilerInterface CrispyCompilerInterface;
typedef struct _CrispyCacheProvider          CrispyCacheProvider;
typedef struct _CrispyCacheProviderInterface CrispyCacheProviderInterface;
typedef struct _CrispyDependencyResolver          CrispyDependencyResolver;
typedef struct _CrispyDependencyResolverInterface CrispyDependencyResolverInterface;

/* Concrete type forward declarations (no Class typedefs -- G_DECLARE_FINAL_TYPE creates those) */
typedef struct _CrispyGccCompiler        CrispyGccCompiler;
typedef struct _CrispyFileCache          CrispyFileCache;
typedef struct _CrispyScript             CrispyScript;
typedef struct _CrispyPluginEngine       CrispyPluginEngine;
typedef struct _CrispyPkgConfigResolver  CrispyPkgConfigResolver;
typedef struct _CrispyWatcher            CrispyWatcher;
typedef struct _CrispyRepl               CrispyRepl;

/* Boxed type forward declarations */
typedef struct _CrispyDependencyInfo     CrispyDependencyInfo;

/* Config context forward declaration (plain struct, not GObject) */
typedef struct _CrispyConfigContext    CrispyConfigContext;

/**
 * CrispyFlags:
 * @CRISPY_FLAG_NONE: No special flags.
 * @CRISPY_FLAG_FORCE_COMPILE: Skip cache, force recompilation (-n).
 * @CRISPY_FLAG_PRESERVE_SOURCE: Keep temp source files in /tmp (-S).
 * @CRISPY_FLAG_DRY_RUN: Show compilation command without executing (--dry-run).
 * @CRISPY_FLAG_GDB: Compile as executable with debug symbols, launch under gdb (--gdb).
 * @CRISPY_FLAG_PROFILE: Compile with profiling instrumentation (-pg) (--profile).
 * @CRISPY_FLAG_WATCH: Enable watch mode — recompile and re-execute on file changes.
 *
 * Flags controlling script compilation and execution behavior.
 */
typedef enum
{
    CRISPY_FLAG_NONE            = 0,
    CRISPY_FLAG_FORCE_COMPILE   = 1 << 0,
    CRISPY_FLAG_PRESERVE_SOURCE = 1 << 1,
    CRISPY_FLAG_DRY_RUN         = 1 << 2,
    CRISPY_FLAG_GDB             = 1 << 3,
    CRISPY_FLAG_PROFILE         = 1 << 4,
    CRISPY_FLAG_WATCH           = 1 << 5
} CrispyFlags;

/**
 * CrispyMainFunc:
 * @argc: argument count
 * @argv: argument vector
 *
 * Function pointer type for the script's main() entry point.
 *
 * Returns: exit code from the script
 */
typedef gint (*CrispyMainFunc)(gint argc, gchar **argv);

/**
 * CRISPY_MAX_PARAMS_LEN:
 *
 * Maximum length of the CRISPY_PARAMS define value.
 */
#define CRISPY_MAX_PARAMS_LEN (8192)

/**
 * CRISPY_HASH_ALGO:
 *
 * The hash algorithm used for cache keys.
 */
#define CRISPY_HASH_ALGO (G_CHECKSUM_SHA256)

/**
 * CRISPY_ERROR:
 *
 * Error domain for crispy operations.
 */
#define CRISPY_ERROR (crispy_error_quark())

GQuark crispy_error_quark (void);

/**
 * CrispyError:
 * @CRISPY_ERROR_COMPILE: Compilation failed.
 * @CRISPY_ERROR_LOAD: Module loading failed.
 * @CRISPY_ERROR_NO_MAIN: No main() symbol found in compiled module.
 * @CRISPY_ERROR_IO: File I/O error.
 * @CRISPY_ERROR_PARAMS: Error parsing CRISPY_PARAMS.
 * @CRISPY_ERROR_CACHE: Cache operation failed.
 * @CRISPY_ERROR_GCC_NOT_FOUND: gcc binary not found.
 * @CRISPY_ERROR_PLUGIN: Plugin operation failed.
 * @CRISPY_ERROR_CONFIG: Configuration file error.
 * @CRISPY_ERROR_DEPENDENCY: Dependency resolution failed.
 * @CRISPY_ERROR_SCAFFOLD: Scaffolding operation failed.
 * @CRISPY_ERROR_INSTALL: Installation failed.
 * @CRISPY_ERROR_TEST: Test runner error.
 * @CRISPY_ERROR_LINT: Linting operation failed.
 * @CRISPY_ERROR_REPL: REPL error.
 *
 * Error codes for the %CRISPY_ERROR domain.
 */
typedef enum
{
    CRISPY_ERROR_COMPILE,
    CRISPY_ERROR_LOAD,
    CRISPY_ERROR_NO_MAIN,
    CRISPY_ERROR_IO,
    CRISPY_ERROR_PARAMS,
    CRISPY_ERROR_CACHE,
    CRISPY_ERROR_GCC_NOT_FOUND,
    CRISPY_ERROR_PLUGIN,
    CRISPY_ERROR_CONFIG,
    CRISPY_ERROR_DEPENDENCY,
    CRISPY_ERROR_SCAFFOLD,
    CRISPY_ERROR_INSTALL,
    CRISPY_ERROR_TEST,
    CRISPY_ERROR_LINT,
    CRISPY_ERROR_REPL
} CrispyError;

G_END_DECLS

#endif /* CRISPY_TYPES_H */
