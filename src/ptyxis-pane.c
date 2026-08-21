/* ptyxis-pane.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "config.h"
#include "ptyxis-enums.h"
#include "ptyxis-pane.h"
#include "ptyxis-tab-monitor.h"

struct _PtyxisPane
{
  GtkWidget parent_instance;
  PtyxisProfile *profile;
  PtyxisIpcProcess *process;
  PtyxisTabMonitor *monitor;
  PtyxisTerminal *terminal;
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
  guint forced_exit : 1;
  guint has_foreground_process : 1;
};

enum {
  PROP_0,
  PROP_PROFILE,
  PROP_PROCESS,
  PROP_MONITOR,
  PROP_ZOOM,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_FINAL_TYPE (PtyxisPane, ptyxis_pane, GTK_TYPE_WIDGET)

static void
ptyxis_pane_dispose (GObject *object)
{
  PtyxisPane *self = PTYXIS_PANE (object);
  GtkWidget *child;

  self->terminal = NULL;
  g_clear_object (&self->profile);
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
    case PROP_PROFILE:
      g_value_set_object (value, self->profile);
      break;

    case PROP_PROCESS:
      g_value_set_object (value, self->process);
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
    case PROP_PROFILE:
      ptyxis_pane_set_profile (self, g_value_get_object (value));
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
  properties[PROP_ZOOM] =
    g_param_spec_enum ("zoom", NULL, NULL,
                       PTYXIS_TYPE_ZOOM_LEVEL,
                       PTYXIS_ZOOM_LEVEL_DEFAULT,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));
  g_object_class_install_properties (object_class, N_PROPS, properties);
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "ptyxis-pane");
}

static void
ptyxis_pane_init (PtyxisPane *self)
{
  self->zoom = PTYXIS_ZOOM_LEVEL_DEFAULT;
  self->uuid = g_uuid_string_random ();
  self->foreground_pid = -1;
  self->state = PTYXIS_PANE_STATE_INITIAL;
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
  self->has_foreground_process = !!has_foreground_process;
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
  g_set_str (&self->command_line, command_line);
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
  self->leader_kind = kind;
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

PtyxisPane *
ptyxis_pane_new (void)
{
  return g_object_new (PTYXIS_TYPE_PANE, NULL);
}

PtyxisTerminal *
ptyxis_pane_get_terminal (PtyxisPane *self)
{
  g_return_val_if_fail (PTYXIS_IS_PANE (self), NULL);
  return self->terminal;
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
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROFILE]);
}

void
ptyxis_pane_set_terminal (PtyxisPane     *self,
                          PtyxisTerminal *terminal)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  g_return_if_fail (PTYXIS_IS_TERMINAL (terminal));
  g_return_if_fail (self->terminal == NULL || self->terminal == terminal);
  self->terminal = terminal;
}
