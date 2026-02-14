/* crispy-script.h - Script lifecycle orchestrator */

#ifndef CRISPY_SCRIPT_H
#define CRISPY_SCRIPT_H

#if !defined(CRISPY_INSIDE) && !defined(CRISPY_COMPILATION)
#error "Only <crispy.h> can be included directly."
#endif

#include <glib-object.h>
#include <gmodule.h>
#include "../crispy-types.h"
#include "../crispy-plugin.h"

G_BEGIN_DECLS

#define CRISPY_TYPE_SCRIPT (crispy_script_get_type())

G_DECLARE_FINAL_TYPE(CrispyScript, crispy_script, CRISPY, SCRIPT, GObject)

/**
 * crispy_script_new_from_file:
 * @path: path to the C source file
 * @compiler: a #CrispyCompiler implementation
 * @cache: a #CrispyCacheProvider implementation
 * @flags: #CrispyFlags controlling behavior
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #CrispyScript from a source file on disk.
 * The source is read, CRISPY_PARAMS extracted, and the script
 * is prepared for execution.
 *
 * Returns: (transfer full): a new #CrispyScript, or %NULL on error
 */
CrispyScript *crispy_script_new_from_file (const gchar          *path,
                                           CrispyCompiler       *compiler,
                                           CrispyCacheProvider  *cache,
                                           CrispyFlags           flags,
                                           GError              **error);

/**
 * crispy_script_new_from_inline:
 * @code: the inline C code (body of main())
 * @extra_includes: (nullable): semicolon-separated additional headers
 * @compiler: a #CrispyCompiler implementation
 * @cache: a #CrispyCacheProvider implementation
 * @flags: #CrispyFlags controlling behavior
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #CrispyScript from inline code. The code is wrapped
 * in a main() function with default GLib includes.
 *
 * Returns: (transfer full): a new #CrispyScript, or %NULL on error
 */
CrispyScript *crispy_script_new_from_inline (const gchar          *code,
                                             const gchar          *extra_includes,
                                             CrispyCompiler       *compiler,
                                             CrispyCacheProvider  *cache,
                                             CrispyFlags           flags,
                                             GError              **error);

/**
 * crispy_script_new_from_stdin:
 * @compiler: a #CrispyCompiler implementation
 * @cache: a #CrispyCacheProvider implementation
 * @flags: #CrispyFlags controlling behavior
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #CrispyScript by reading C code from stdin until EOF.
 * The code is wrapped in a main() function with default GLib includes.
 *
 * Returns: (transfer full): a new #CrispyScript, or %NULL on error
 */
CrispyScript *crispy_script_new_from_stdin (CrispyCompiler       *compiler,
                                            CrispyCacheProvider  *cache,
                                            CrispyFlags           flags,
                                            GError              **error);

/**
 * crispy_script_execute:
 * @self: a #CrispyScript
 * @argc: argument count for the script
 * @argv: (array length=argc): argument vector for the script
 * @error: return location for a #GError, or %NULL
 *
 * Executes the full pipeline: hash -> cache check -> compile (if needed)
 * -> load -> run main(). The script's exit code is stored and can be
 * retrieved with crispy_script_get_exit_code().
 *
 * Returns: the script's exit code, or -1 on error
 */
gint crispy_script_execute (CrispyScript  *self,
                            gint           argc,
                            gchar        **argv,
                            GError       **error);

/**
 * crispy_script_get_exit_code:
 * @self: a #CrispyScript
 *
 * Returns the exit code from the last execution of this script.
 *
 * Returns: the exit code
 */
gint crispy_script_get_exit_code (CrispyScript *self);

/**
 * crispy_script_get_temp_source_path:
 * @self: a #CrispyScript
 *
 * Returns the path to the temporary modified source file, if it
 * still exists (only meaningful with %CRISPY_FLAG_PRESERVE_SOURCE).
 *
 * Returns: (transfer none) (nullable): the temp source path
 */
const gchar *crispy_script_get_temp_source_path (CrispyScript *self);

/**
 * crispy_script_set_plugin_engine:
 * @self: a #CrispyScript
 * @engine: (transfer none) (nullable): a #CrispyPluginEngine, or %NULL
 *
 * Sets the plugin engine for this script. When set, hook functions
 * from loaded plugins are dispatched at each phase of the execution
 * pipeline. If @engine is %NULL, no hooks fire (default behavior).
 *
 * This must be called before crispy_script_execute().
 */
void crispy_script_set_plugin_engine (CrispyScript       *self,
                                      CrispyPluginEngine *engine);

G_END_DECLS

#endif /* CRISPY_SCRIPT_H */
