/* crispy-watcher.c - Script file watcher */

/*
 * Copyright (C) 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * Watches a script file for modifications using GFileMonitor and
 * re-executes it via CrispyScript on each change.  A debounce timer
 * coalesces rapid successive events (e.g. editor atomic-save sequences)
 * into a single recompile trigger.
 */

#ifndef CRISPY_COMPILATION
#define CRISPY_COMPILATION
#endif
#include "crispy-watcher.h"
#include "crispy-script.h"
#include "../interfaces/crispy-compiler.h"
#include "../interfaces/crispy-cache-provider.h"
#include "../crispy-types.h"
#include <gio/gio.h>

/**
 * SECTION:crispy-watcher
 * @title: CrispyWatcher
 * @short_description: Watches a script file and re-executes on change
 *
 * #CrispyWatcher monitors a C script file using #GFileMonitor and
 * recompiles and re-executes the script whenever the file is modified.
 * A debounce interval (default 500 ms) prevents multiple rapid change
 * events from triggering redundant compilations.
 *
 * Three signals are emitted during the watch loop: #CrispyWatcher::script-changed
 * fires when a file change is detected (before recompilation),
 * #CrispyWatcher::script-executed fires after each successful execution,
 * and #CrispyWatcher::watch-error fires if an error occurs during the
 * recompile/execute cycle.
 */

/* --- property enum --- */

enum
{
    PROP_0,
    PROP_SCRIPT_PATH,
    PROP_DEBOUNCE_MS,
    N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

/* --- signal enum --- */

enum
{
    SIGNAL_SCRIPT_CHANGED,
    SIGNAL_SCRIPT_EXECUTED,
    SIGNAL_WATCH_ERROR,
    N_SIGNALS
};

static guint obj_signals[N_SIGNALS];

/* --- private struct --- */

struct _CrispyWatcher
{
    GObject              parent_instance;

    gchar               *script_path;
    CrispyCompiler      *compiler;
    CrispyCacheProvider *cache;
    CrispyFlags          flags;

    GFileMonitor        *monitor;
    GMainLoop           *loop;
    guint                debounce_ms;
    guint                debounce_source_id;

    gint                 script_argc;
    gchar              **script_argv;

    gboolean             running;
};

G_DEFINE_FINAL_TYPE(CrispyWatcher, crispy_watcher, G_TYPE_OBJECT)

/* --- forward declarations --- */

static gboolean on_debounce_fire (gpointer user_data);
static void on_file_changed (GFileMonitor      *monitor,
                              GFile             *file,
                              GFile             *other_file,
                              GFileMonitorEvent  event_type,
                              gpointer           user_data);

/* --- GObject property accessors --- */

static void
crispy_watcher_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
){
    CrispyWatcher *self;

    self = CRISPY_WATCHER(object);

    switch (prop_id)
    {
    case PROP_SCRIPT_PATH:
        g_free(self->script_path);
        self->script_path = g_value_dup_string(value);
        break;
    case PROP_DEBOUNCE_MS:
        self->debounce_ms = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
crispy_watcher_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
){
    CrispyWatcher *self;

    self = CRISPY_WATCHER(object);

    switch (prop_id)
    {
    case PROP_SCRIPT_PATH:
        g_value_set_string(value, self->script_path);
        break;
    case PROP_DEBOUNCE_MS:
        g_value_set_uint(value, self->debounce_ms);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/* --- GObject finalize --- */

static void
crispy_watcher_finalize(
    GObject *object
){
    CrispyWatcher *self;

    self = CRISPY_WATCHER(object);

    /* cancel any pending debounce timer */
    if (self->debounce_source_id != 0)
    {
        g_source_remove(self->debounce_source_id);
        self->debounce_source_id = 0;
    }

    /* stop monitoring if still active */
    if (self->monitor != NULL)
    {
        g_file_monitor_cancel(self->monitor);
        g_clear_object(&self->monitor);
    }

    if (self->loop != NULL)
    {
        if (g_main_loop_is_running(self->loop))
            g_main_loop_quit(self->loop);
        g_clear_pointer(&self->loop, g_main_loop_unref);
    }

    g_clear_object(&self->compiler);
    g_clear_object(&self->cache);

    g_free(self->script_path);
    g_strfreev(self->script_argv);

    G_OBJECT_CLASS(crispy_watcher_parent_class)->finalize(object);
}

/* --- class init --- */

static void
crispy_watcher_class_init(
    CrispyWatcherClass *klass
){
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);

    object_class->finalize     = crispy_watcher_finalize;
    object_class->set_property = crispy_watcher_set_property;
    object_class->get_property = crispy_watcher_get_property;

    /**
     * CrispyWatcher:script-path:
     *
     * Path to the C source file being watched.
     */
    obj_props[PROP_SCRIPT_PATH] =
        g_param_spec_string("script-path",
                            "Script Path",
                            "Path to the C source file being watched",
                            NULL,
                            G_PARAM_READWRITE |
                            G_PARAM_CONSTRUCT_ONLY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * CrispyWatcher:debounce-ms:
     *
     * Debounce interval in milliseconds.  After a file change event is
     * received, the watcher waits this many milliseconds before triggering
     * recompilation.  Additional change events during the interval reset
     * the timer.  Default is 500.
     */
    obj_props[PROP_DEBOUNCE_MS] =
        g_param_spec_uint("debounce-ms",
                          "Debounce Ms",
                          "Milliseconds to wait after a change before recompiling",
                          0, G_MAXUINT, 500,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(object_class, N_PROPS, obj_props);

    /**
     * CrispyWatcher::script-changed:
     * @watcher: the #CrispyWatcher that received the signal
     *
     * Emitted when a file change event is detected on the watched script,
     * after the debounce interval has elapsed but before recompilation
     * begins.
     */
    obj_signals[SIGNAL_SCRIPT_CHANGED] =
        g_signal_new("script-changed",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     0);

    /**
     * CrispyWatcher::script-executed:
     * @watcher: the #CrispyWatcher that received the signal
     * @exit_code: the exit code returned by the script's main() function
     *
     * Emitted after the script has been recompiled and executed
     * successfully.
     */
    obj_signals[SIGNAL_SCRIPT_EXECUTED] =
        g_signal_new("script-executed",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    /**
     * CrispyWatcher::watch-error:
     * @watcher: the #CrispyWatcher that received the signal
     * @error: a #GError describing the failure
     *
     * Emitted when an error occurs during the recompile or execute cycle.
     * The watcher continues running after this signal; subsequent file
     * change events will trigger new recompile attempts.
     */
    obj_signals[SIGNAL_WATCH_ERROR] =
        g_signal_new("watch-error",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_ERROR);
}

/* --- instance init --- */

static void
crispy_watcher_init(
    CrispyWatcher *self
){
    self->debounce_ms = 500;
    self->running     = FALSE;
}

/* --- constructor --- */

CrispyWatcher *
crispy_watcher_new(
    const gchar         *script_path,
    CrispyCompiler      *compiler,
    CrispyCacheProvider *cache,
    CrispyFlags          flags
){
    CrispyWatcher *self;

    g_return_val_if_fail(script_path != NULL, NULL);
    g_return_val_if_fail(CRISPY_IS_COMPILER(compiler), NULL);
    g_return_val_if_fail(CRISPY_IS_CACHE_PROVIDER(cache), NULL);

    self = g_object_new(CRISPY_TYPE_WATCHER,
                        "script-path", script_path,
                        NULL);

    self->compiler = g_object_ref(compiler);
    self->cache    = g_object_ref(cache);
    self->flags    = flags;

    return self;
}

/* --- debounce fire callback --- */

/*
 * on_debounce_fire:
 * @user_data: the #CrispyWatcher
 *
 * Called when the debounce timer expires.  Emits "script-changed",
 * creates a fresh #CrispyScript, executes it, then emits either
 * "script-executed" or "watch-error" depending on the outcome.
 */
static gboolean
on_debounce_fire(
    gpointer user_data
){
    CrispyWatcher *self;
    CrispyScript  *script;
    GError        *error;
    gint           exit_code;

    self = CRISPY_WATCHER(user_data);
    self->debounce_source_id = 0;

    /* notify listeners that a change was detected */
    g_signal_emit(self, obj_signals[SIGNAL_SCRIPT_CHANGED], 0);

    error  = NULL;
    script = crispy_script_new_from_file(self->script_path,
                                         self->compiler,
                                         self->cache,
                                         self->flags,
                                         &error);
    if (script == NULL)
    {
        g_signal_emit(self, obj_signals[SIGNAL_WATCH_ERROR], 0, error);
        g_clear_error(&error);
        return G_SOURCE_REMOVE;
    }

    exit_code = crispy_script_execute(script,
                                       self->script_argc,
                                       self->script_argv,
                                       &error);
    g_object_unref(script);

    if (error != NULL)
    {
        g_signal_emit(self, obj_signals[SIGNAL_WATCH_ERROR], 0, error);
        g_clear_error(&error);
        return G_SOURCE_REMOVE;
    }

    g_signal_emit(self, obj_signals[SIGNAL_SCRIPT_EXECUTED], 0, exit_code);

    return G_SOURCE_REMOVE;
}

/* --- file monitor changed callback --- */

/*
 * on_file_changed:
 * @monitor: the #GFileMonitor that detected the event
 * @file: the file that changed
 * @other_file: second file (rename target, or %NULL)
 * @event_type: the type of change event
 * @user_data: the #CrispyWatcher
 *
 * Handles change events from the GFileMonitor.  Only acts on
 * G_FILE_MONITOR_EVENT_CHANGED and G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT
 * events; all other events are ignored.  Implements debouncing by
 * cancelling any pending timer and scheduling a new one.
 */
static void
on_file_changed(
    GFileMonitor      *monitor,
    GFile             *file,
    GFile             *other_file,
    GFileMonitorEvent  event_type,
    gpointer           user_data
){
    CrispyWatcher *self;

    (void)monitor;
    (void)file;
    (void)other_file;

    if (event_type != G_FILE_MONITOR_EVENT_CHANGED &&
        event_type != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
    {
        return;
    }

    self = CRISPY_WATCHER(user_data);

    /* cancel any pending debounce timer */
    if (self->debounce_source_id != 0)
    {
        g_source_remove(self->debounce_source_id);
        self->debounce_source_id = 0;
    }

    /* schedule a new debounce timer */
    self->debounce_source_id =
        g_timeout_add(self->debounce_ms, on_debounce_fire, self);
}

/* --- public API --- */

gboolean
crispy_watcher_start(
    CrispyWatcher  *self,
    GError        **error
){
    GFile *file;

    g_return_val_if_fail(CRISPY_IS_WATCHER(self), FALSE);
    g_return_val_if_fail(self->script_path != NULL, FALSE);
    g_return_val_if_fail(!self->running, FALSE);

    file = g_file_new_for_path(self->script_path);

    self->monitor = g_file_monitor_file(file,
                                        G_FILE_MONITOR_NONE,
                                        NULL,
                                        error);
    g_object_unref(file);

    if (self->monitor == NULL)
        return FALSE;

    g_signal_connect(self->monitor,
                     "changed",
                     G_CALLBACK(on_file_changed),
                     self);

    self->running = TRUE;
    self->loop    = g_main_loop_new(NULL, FALSE);

    /* blocks until crispy_watcher_stop() quits the loop */
    g_main_loop_run(self->loop);

    return TRUE;
}

void
crispy_watcher_stop(
    CrispyWatcher *self
){
    g_return_if_fail(CRISPY_IS_WATCHER(self));

    if (self->monitor != NULL)
    {
        g_file_monitor_cancel(self->monitor);
        g_clear_object(&self->monitor);
    }

    if (self->loop != NULL && g_main_loop_is_running(self->loop))
        g_main_loop_quit(self->loop);

    self->running = FALSE;
}

gboolean
crispy_watcher_is_running(
    CrispyWatcher *self
){
    g_return_val_if_fail(CRISPY_IS_WATCHER(self), FALSE);
    return self->running;
}

void
crispy_watcher_set_debounce_ms(
    CrispyWatcher *self,
    guint          ms
){
    g_return_if_fail(CRISPY_IS_WATCHER(self));
    self->debounce_ms = ms;
    g_object_notify_by_pspec(G_OBJECT(self), obj_props[PROP_DEBOUNCE_MS]);
}

guint
crispy_watcher_get_debounce_ms(
    CrispyWatcher *self
){
    g_return_val_if_fail(CRISPY_IS_WATCHER(self), 0);
    return self->debounce_ms;
}

void
crispy_watcher_set_script_argv(
    CrispyWatcher  *self,
    gint            argc,
    gchar         **argv
){
    gint i;

    g_return_if_fail(CRISPY_IS_WATCHER(self));

    g_strfreev(self->script_argv);
    self->script_argv = NULL;
    self->script_argc = 0;

    if (argc <= 0 || argv == NULL)
        return;

    self->script_argc = argc;
    self->script_argv = g_new0(gchar *, argc + 1);
    for (i = 0; i < argc; i++)
        self->script_argv[i] = g_strdup(argv[i]);
    self->script_argv[argc] = NULL;
}
