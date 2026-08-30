/* ptyxis-pane.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "config.h"
#include <adwaita.h>
#include <signal.h>
#include "ptyxis-enums.h"
#include "ptyxis-pane.h"
#include "ptyxis-tab-monitor.h"

struct _PtyxisPane
{
  GtkWidget parent_instance;
  PtyxisProfile *profile;
  GSignalGroup *profile_signals;
  PtyxisIpcProcess *process;
  PtyxisTabMonitor *monitor;
  PtyxisTerminal *terminal;
  GtkWidget *banner;
  GtkScrolledWindow *scrolled_window;
  GBindingGroup *terminal_bindings;
  PtyxisZoomLevel zoom;
  char *uuid;
  char **command;
  char *initial_working_directory_uri;
  char *previous_working_directory_uri;
  PtyxisIpcContainer *container;
  char *initial_title;
  char *title_prefix;
  char *command_line;
  char *program_name;
  GPid foreground_pid;
  PtyxisProcessLeaderKind leader_kind : 3;
  PtyxisPaneState state : 3;
  gint64 respawn_time;
  gint64 last_focus_time;
  guint forced_exit : 1;
  guint ignore_osc_title : 1;
  guint inhibit_cookie;
  guint has_foreground_process : 1;
};

enum {
  PROP_0,
  PROP_COMMAND_LINE,
  PROP_HAS_FOREGROUND_PROCESS,
  PROP_IGNORE_OSC_TITLE,
  PROP_PROCESS_LEADER_KIND,
  PROP_PROFILE,
  PROP_PROCESS,
  PROP_MONITOR,
  PROP_READ_ONLY,
  PROP_UUID,
  PROP_ZOOM,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum {
  FOCUS_ENTERED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static gboolean
ptyxis_pane_force_quit_in_idle (gpointer data)
{
  PtyxisPane *self = data;

  if (self->process != NULL)
    ptyxis_ipc_process_call_send_signal (self->process, SIGKILL, NULL, NULL, NULL);
  return G_SOURCE_REMOVE;
}

static void
ptyxis_pane_focus_changed_cb (PtyxisPane               *self,
                              GParamSpec               *pspec,
                              GtkEventControllerFocus  *controller)
{
  g_assert (PTYXIS_IS_PANE (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_FOCUS (controller));

  if (gtk_event_controller_focus_contains_focus (controller))
    {
      self->last_focus_time = g_get_monotonic_time ();
      g_signal_emit (self, signals[FOCUS_ENTERED], 0);
    }
}

G_DEFINE_FINAL_TYPE (PtyxisPane, ptyxis_pane, GTK_TYPE_WIDGET)

static void
ptyxis_pane_dispose (GObject *object)
{
  PtyxisPane *self = PTYXIS_PANE (object);
  GtkWidget *child;

  self->terminal = NULL;
  self->banner = NULL;
  self->scrolled_window = NULL;
  g_clear_object (&self->profile);
  g_clear_object (&self->profile_signals);
  g_clear_object (&self->terminal_bindings);
  g_clear_object (&self->process);
  g_clear_object (&self->monitor);
  g_clear_object (&self->container);
  g_clear_pointer (&self->command, g_strfreev);
  g_clear_pointer (&self->initial_working_directory_uri, g_free);
  g_clear_pointer (&self->previous_working_directory_uri, g_free);
  g_clear_pointer (&self->initial_title, g_free);
  g_clear_pointer (&self->title_prefix, g_free);
  g_clear_pointer (&self->command_line, g_free);
  g_clear_pointer (&self->program_name, g_free);
  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  G_OBJECT_CLASS (ptyxis_pane_parent_class)->dispose (object);
}

static void
ptyxis_pane_finalize (GObject *object)
{
  PtyxisPane *self = PTYXIS_PANE (object);

  g_clear_pointer (&self->uuid, g_free);

  G_OBJECT_CLASS (ptyxis_pane_parent_class)->finalize (object);
}

static void
ptyxis_pane_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  PtyxisPane *self = PTYXIS_PANE (object);

  switch (prop_id)
    {
    case PROP_COMMAND_LINE:
      g_value_set_string (value, self->command_line);
      break;

    case PROP_HAS_FOREGROUND_PROCESS:
      g_value_set_boolean (value, self->has_foreground_process);
      break;

    case PROP_IGNORE_OSC_TITLE:
      g_value_set_boolean (value, self->ignore_osc_title);
      break;

    case PROP_PROCESS_LEADER_KIND:
      g_value_set_enum (value, self->leader_kind);
      break;

    case PROP_PROFILE:
      g_value_set_object (value, self->profile);
      break;

    case PROP_PROCESS:
      g_value_set_object (value, self->process);
      break;

    case PROP_READ_ONLY:
      g_value_set_boolean (value, ptyxis_pane_get_read_only (self));
      break;

    case PROP_UUID:
      g_value_set_string (value, self->uuid);
      break;

    case PROP_MONITOR:
      g_value_set_object (value, self->monitor);
      break;

    case PROP_ZOOM:
      g_value_set_enum (value, self->zoom);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ptyxis_pane_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  PtyxisPane *self = PTYXIS_PANE (object);

  switch (prop_id)
    {
    case PROP_IGNORE_OSC_TITLE:
      ptyxis_pane_set_ignore_osc_title (self, g_value_get_boolean (value));
      break;

    case PROP_PROFILE:
      ptyxis_pane_set_profile (self, g_value_get_object (value));
      break;

    case PROP_READ_ONLY:
      ptyxis_pane_set_read_only (self, g_value_get_boolean (value));
      break;

    case PROP_ZOOM:
      ptyxis_pane_set_zoom (self, g_value_get_enum (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ptyxis_pane_class_init (PtyxisPaneClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = ptyxis_pane_dispose;
  object_class->finalize = ptyxis_pane_finalize;
  object_class->get_property = ptyxis_pane_get_property;
  object_class->set_property = ptyxis_pane_set_property;

  properties[PROP_COMMAND_LINE] =
    g_param_spec_string ("command-line", NULL, NULL, NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  properties[PROP_HAS_FOREGROUND_PROCESS] =
    g_param_spec_boolean ("has-foreground-process", NULL, NULL, FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  properties[PROP_IGNORE_OSC_TITLE] =
    g_param_spec_boolean ("ignore-osc-title", NULL, NULL, FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));
  properties[PROP_PROCESS_LEADER_KIND] =
    g_param_spec_enum ("process-leader-kind", NULL, NULL,
                       PTYXIS_TYPE_PROCESS_LEADER_KIND,
                       PTYXIS_PROCESS_LEADER_KIND_UNKNOWN,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_PROFILE] =
    g_param_spec_object ("profile", NULL, NULL,
                         PTYXIS_TYPE_PROFILE,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));
  properties[PROP_PROCESS] =
    g_param_spec_object ("process", NULL, NULL,
                         PTYXIS_IPC_TYPE_PROCESS,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));
  properties[PROP_MONITOR] =
    g_param_spec_object ("monitor", NULL, NULL,
                         PTYXIS_TYPE_TAB_MONITOR,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));
  properties[PROP_READ_ONLY] =
    g_param_spec_boolean ("read-only", NULL, NULL, FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));
  properties[PROP_UUID] =
    g_param_spec_string ("uuid", NULL, NULL, NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  properties[PROP_ZOOM] =
    g_param_spec_enum ("zoom", NULL, NULL,
                       PTYXIS_TYPE_ZOOM_LEVEL,
                       PTYXIS_ZOOM_LEVEL_DEFAULT,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));
  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals[FOCUS_ENTERED] =
    g_signal_new ("focus-entered",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "ptyxis-pane");
  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_GROUP);
}

static void
ptyxis_pane_init (PtyxisPane *self)
{
  GtkEventController *focus;
  GtkWidget *box;

  self->zoom = PTYXIS_ZOOM_LEVEL_DEFAULT;
  self->uuid = g_uuid_string_random ();
  self->foreground_pid = -1;
  self->state = PTYXIS_PANE_STATE_INITIAL;
  self->profile_signals = g_signal_group_new (PTYXIS_TYPE_PROFILE);
  self->terminal_bindings = g_binding_group_new ();

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  self->banner = g_object_new (ADW_TYPE_BANNER,
                               "revealed", TRUE,
                               "visible", FALSE,
                               NULL);
  self->scrolled_window = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
                                        "propagate-natural-width", TRUE,
                                        "propagate-natural-height", TRUE,
                                        "hscrollbar-policy", GTK_POLICY_NEVER,
                                        "vexpand", TRUE,
                                        NULL);
  self->terminal = g_object_new (PTYXIS_TYPE_TERMINAL,
                                 "enable-fallback-scrolling", FALSE,
                                 "scroll-unit-is-pixels", TRUE,
                                 NULL);
  gtk_scrolled_window_set_child (self->scrolled_window, GTK_WIDGET (self->terminal));
  gtk_box_append (GTK_BOX (box), self->banner);
  gtk_box_append (GTK_BOX (box), GTK_WIDGET (self->scrolled_window));
  gtk_widget_set_parent (box, GTK_WIDGET (self));

  g_binding_group_bind (self->terminal_bindings, "palette",
                        self->terminal, "palette", G_BINDING_SYNC_CREATE);
  g_binding_group_bind (self->terminal_bindings, "scroll-on-keystroke",
                        self->terminal, "scroll-on-keystroke", G_BINDING_SYNC_CREATE);
  g_binding_group_bind (self->terminal_bindings, "scroll-on-output",
                        self->terminal, "scroll-on-output", G_BINDING_SYNC_CREATE);
  g_binding_group_bind (self->terminal_bindings, "backspace-binding",
                        self->terminal, "backspace-binding", G_BINDING_SYNC_CREATE);
  g_binding_group_bind (self->terminal_bindings, "delete-binding",
                        self->terminal, "delete-binding", G_BINDING_SYNC_CREATE);
  g_binding_group_bind (self->terminal_bindings, "cjk-ambiguous-width",
                        self->terminal, "cjk-ambiguous-width", G_BINDING_SYNC_CREATE);
  g_binding_group_bind (self->terminal_bindings, "bold-is-bright",
                        self->terminal, "bold-is-bright", G_BINDING_SYNC_CREATE);

  focus = gtk_event_controller_focus_new ();
  g_signal_connect_object (focus,
                           "notify::contains-focus",
                           G_CALLBACK (ptyxis_pane_focus_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_widget_add_controller (GTK_WIDGET (self), focus);
}

PtyxisZoomLevel
ptyxis_pane_get_zoom (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), PTYXIS_ZOOM_LEVEL_DEFAULT);
  return self->zoom;
}

void
ptyxis_pane_set_zoom (PtyxisPane      *self,
                      PtyxisZoomLevel  zoom)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  g_return_if_fail (zoom >= PTYXIS_ZOOM_LEVEL_MINUS_14 &&
                    zoom <= PTYXIS_ZOOM_LEVEL_PLUS_14);

  if (self->zoom != zoom)
    {
      self->zoom = zoom;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ZOOM]);
    }
}

PtyxisIpcProcess *
ptyxis_pane_get_process (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->process;
}

void
ptyxis_pane_set_process (PtyxisPane       *self,
                         PtyxisIpcProcess *process)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  g_return_if_fail (process == NULL || PTYXIS_IPC_IS_PROCESS (process));

  if (g_set_object (&self->process, process))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROCESS]);
}

PtyxisTabMonitor *
ptyxis_pane_get_monitor (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->monitor;
}

void
ptyxis_pane_set_monitor (PtyxisPane       *self,
                         PtyxisTabMonitor *monitor)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  g_return_if_fail (monitor == NULL || PTYXIS_IS_TAB_MONITOR (monitor));

  if (g_set_object (&self->monitor, monitor))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MONITOR]);
}

const char *
ptyxis_pane_get_uuid (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->uuid;
}

gint64
ptyxis_pane_get_last_focus_time (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), 0);
  return self->last_focus_time;
}

const char *const *
ptyxis_pane_get_command (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return (const char *const *)self->command;
}

void
ptyxis_pane_set_command (PtyxisPane       *self,
                         const char *const *command)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  g_strfreev (self->command);
  self->command = g_strdupv ((char **)command);
}

const char *
ptyxis_pane_get_initial_working_directory_uri (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->initial_working_directory_uri;
}

void
ptyxis_pane_set_initial_working_directory_uri (PtyxisPane *self,
                                               const char *uri)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  g_set_str (&self->initial_working_directory_uri, uri);
}

const char *
ptyxis_pane_get_previous_working_directory_uri (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->previous_working_directory_uri;
}

void
ptyxis_pane_set_previous_working_directory_uri (PtyxisPane *self,
                                                const char *uri)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  g_set_str (&self->previous_working_directory_uri, uri);
}

PtyxisIpcContainer *
ptyxis_pane_dup_container (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->container ? g_object_ref (self->container) : NULL;
}

void
ptyxis_pane_set_container (PtyxisPane         *self,
                           PtyxisIpcContainer *container)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  g_return_if_fail (container == NULL || PTYXIS_IPC_IS_CONTAINER (container));
  g_set_object (&self->container, container);
}

const char *
ptyxis_pane_get_initial_title (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->initial_title;
}

void
ptyxis_pane_set_initial_title (PtyxisPane *self,
                               const char *title)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  g_set_str (&self->initial_title, title);
}

const char *
ptyxis_pane_get_title_prefix (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->title_prefix;
}

void
ptyxis_pane_set_title_prefix (PtyxisPane *self,
                              const char *prefix)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  g_set_str (&self->title_prefix, prefix);
}

GPid
ptyxis_pane_get_foreground_pid (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), -1);
  return self->foreground_pid;
}

void
ptyxis_pane_set_foreground_pid (PtyxisPane *self,
                                GPid         pid)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  self->foreground_pid = pid;
}

gboolean
ptyxis_pane_get_has_foreground_process (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), FALSE);
  return self->has_foreground_process;
}

void
ptyxis_pane_set_has_foreground_process (PtyxisPane *self,
                                        gboolean    has_foreground_process)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  has_foreground_process = !!has_foreground_process;
  if (self->has_foreground_process != has_foreground_process)
    {
      self->has_foreground_process = has_foreground_process;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HAS_FOREGROUND_PROCESS]);
    }
}

const char *
ptyxis_pane_get_command_line (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->command_line;
}

void
ptyxis_pane_set_command_line (PtyxisPane *self,
                              const char *command_line)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  if (g_set_str (&self->command_line, command_line))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMMAND_LINE]);
}

const char *
ptyxis_pane_get_program_name (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->program_name;
}

void
ptyxis_pane_set_program_name (PtyxisPane *self,
                              const char *program_name)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  g_set_str (&self->program_name, program_name);
}

PtyxisProcessLeaderKind
ptyxis_pane_get_process_leader_kind (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), PTYXIS_PROCESS_LEADER_KIND_UNKNOWN);
  return self->leader_kind;
}

void
ptyxis_pane_set_process_leader_kind (PtyxisPane              *self,
                                     PtyxisProcessLeaderKind  kind)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  g_return_if_fail (kind >= PTYXIS_PROCESS_LEADER_KIND_UNKNOWN &&
                    kind <= PTYXIS_PROCESS_LEADER_KIND_CONTAINER);
  if (self->leader_kind != kind)
    {
      self->leader_kind = kind;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROCESS_LEADER_KIND]);
    }
}

PtyxisPaneState
ptyxis_pane_get_state (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), PTYXIS_PANE_STATE_FAILED);
  return self->state;
}

void
ptyxis_pane_set_state (PtyxisPane      *self,
                       PtyxisPaneState  state)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  g_return_if_fail (state >= PTYXIS_PANE_STATE_INITIAL && state <= PTYXIS_PANE_STATE_FAILED);
  self->state = state;
}

gint64
ptyxis_pane_get_respawn_time (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), 0);
  return self->respawn_time;
}

void
ptyxis_pane_set_respawn_time (PtyxisPane *self,
                              gint64      respawn_time)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  self->respawn_time = respawn_time;
}

gboolean
ptyxis_pane_get_forced_exit (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), FALSE);
  return self->forced_exit;
}

void
ptyxis_pane_set_forced_exit (PtyxisPane *self,
                             gboolean    forced_exit)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  self->forced_exit = !!forced_exit;
}

void
ptyxis_pane_force_quit (PtyxisPane *self)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));

  self->forced_exit = TRUE;
  if (self->process == NULL)
    return;

  ptyxis_ipc_process_call_send_signal (self->process, SIGHUP, NULL, NULL, NULL);
  g_timeout_add_full (G_PRIORITY_HIGH, 50,
                      ptyxis_pane_force_quit_in_idle,
                      g_object_ref (self), g_object_unref);
}

gboolean
ptyxis_pane_get_read_only (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), FALSE);
  return self->terminal != NULL &&
         !vte_terminal_get_input_enabled (VTE_TERMINAL (self->terminal));
}

void
ptyxis_pane_set_read_only (PtyxisPane *self,
                           gboolean    read_only)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));

  if (self->terminal != NULL && ptyxis_pane_get_read_only (self) != !!read_only)
    {
      vte_terminal_set_input_enabled (VTE_TERMINAL (self->terminal), !read_only);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_READ_ONLY]);
    }
}

gboolean
ptyxis_pane_get_ignore_osc_title (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), FALSE);
  return self->ignore_osc_title;
}

void
ptyxis_pane_set_ignore_osc_title (PtyxisPane *self,
                                  gboolean    ignore_osc_title)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));

  ignore_osc_title = !!ignore_osc_title;
  if (self->ignore_osc_title != ignore_osc_title)
    {
      self->ignore_osc_title = ignore_osc_title;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IGNORE_OSC_TITLE]);
    }
}

GSignalGroup *
ptyxis_pane_get_profile_signals (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->profile_signals;
}

guint
ptyxis_pane_get_inhibit_cookie (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), 0);
  return self->inhibit_cookie;
}

void
ptyxis_pane_set_inhibit_cookie (PtyxisPane *self,
                                guint       inhibit_cookie)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  self->inhibit_cookie = inhibit_cookie;
}

PtyxisPane *
ptyxis_pane_new (void)
{
  return g_object_new (PTYXIS_TYPE_PANE, NULL);
}

PtyxisPane *
ptyxis_pane_new_for_split (PtyxisPane *source)
{
  PtyxisPane *self;
  g_autofree char *cwd_uri = NULL;

  g_return_val_if_fail (PTYXIS_IS_PANE (source), NULL);

  self = ptyxis_pane_new ();
  ptyxis_pane_set_profile (self, ptyxis_pane_get_profile (source));
  ptyxis_pane_set_zoom (self, ptyxis_pane_get_zoom (source));

  self->container = ptyxis_pane_dup_container (source);
  cwd_uri = ptyxis_terminal_dup_current_directory_uri (source->terminal);
  if (cwd_uri == NULL)
    cwd_uri = g_strdup (ptyxis_pane_get_previous_working_directory_uri (source));
  ptyxis_pane_set_initial_working_directory_uri (self, cwd_uri);

  return self;
}

PtyxisTerminal *
ptyxis_pane_get_terminal (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->terminal;
}

GtkWidget *
ptyxis_pane_get_banner (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->banner;
}

GtkScrolledWindow *
ptyxis_pane_get_scrolled_window (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->scrolled_window;
}

PtyxisProfile *
ptyxis_pane_get_profile (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->profile;
}

void
ptyxis_pane_set_profile (PtyxisPane    *self,
                         PtyxisProfile *profile)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  g_return_if_fail (profile == NULL || PTYXIS_IS_PROFILE (profile));

  if (g_set_object (&self->profile, profile))
    {
      g_binding_group_set_source (self->terminal_bindings, profile);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROFILE]);
    }
}
