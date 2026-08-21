/*
 * ptyxis-tab.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <glib/gi18n.h>

#include <cairo.h>

#ifdef __linux__
# include <libportal/portal.h>
# include <libportal-gtk4/portal-gtk4.h>
#endif

#include "ptyxis-agent-ipc.h"
#include "ptyxis-application.h"
#include "ptyxis-enums.h"
#include "ptyxis-inspector.h"
#include "ptyxis-pane.h"
#include "ptyxis-split-node.h"
#include "ptyxis-tab-monitor.h"
#include "ptyxis-tab-notify.h"
#include "ptyxis-tab-private.h"
#include "ptyxis-terminal.h"
#include "ptyxis-util.h"
#include "ptyxis-window.h"

struct _PtyxisTab
{
  GtkWidget                parent_instance;

  GdkTexture              *cached_texture;
  AdwBanner               *banner;
  PtyxisPane              *pane;
  PtyxisPane              *active_pane;
  PtyxisSplitNode         *split_root;
  GtkScrolledWindow       *scrolled_window;
  PtyxisTerminal          *terminal;
  PtyxisTabNotify          notify;

  guint                    ignore_snapshot : 1;

};

enum {
  PROP_0,
  PROP_ACTIVE_PANE,
  PROP_COMMAND_LINE,
  PROP_ICON,
  PROP_IGNORE_OSC_TITLE,
  PROP_INDICATOR_ICON,
  PROP_PROCESS_LEADER_KIND,
  PROP_PROFILE,
  PROP_PROGRESS,
  PROP_PROGRESS_FRACTION,
  PROP_READ_ONLY,
  PROP_SUBTITLE,
  PROP_TITLE,
  PROP_TITLE_PREFIX,
  PROP_UUID,
  PROP_ZOOM,
  PROP_ZOOM_LABEL,
  N_PROPS
};

enum {
  BELL,
  COMMIT,
  N_SIGNALS
};

static void ptyxis_tab_respawn (PtyxisTab *self);
static void ptyxis_tab_profile_signals_bind_cb (PtyxisTab     *self,
                                                PtyxisProfile *profile,
                                                GSignalGroup  *group);

static void
ptyxis_tab_focus_relative_action (GtkWidget  *widget,
                                  const char *action_name,
                                  GVariant   *params)
{
  PtyxisTab *self = PTYXIS_TAB (widget);
  PtyxisSplitNode *active;
  PtyxisSplitNode *target;

  active = ptyxis_split_node_find_pane (self->split_root, G_OBJECT (self->active_pane));
  if (g_str_equal (action_name, "tab.focus-pane-next"))
    target = ptyxis_split_node_get_next_leaf (self->split_root, active, TRUE);
  else
    target = ptyxis_split_node_get_previous_leaf (self->split_root, active, TRUE);

  if (target != NULL && target != active)
    {
      PtyxisPane *pane = PTYXIS_PANE (ptyxis_split_node_get_pane (target));

      ptyxis_tab_set_active_pane (self, pane);
      gtk_widget_grab_focus (GTK_WIDGET (ptyxis_pane_get_terminal (pane)));
    }
}

G_DEFINE_FINAL_TYPE (PtyxisTab, ptyxis_tab, GTK_TYPE_WIDGET)

#ifdef __linux__
static XdpPortal *portal;
#endif

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];
static double zoom_font_scales[] = {
  0,

  /* MINUS_14 through MINUS_1: each step is 1.2^(1/2) ≈ 1.095445 */
  1.0 / (1.2 * 1.2 * 1.2 * 1.2 * 1.2 * 1.2 * 1.2),                     /* MINUS_14: 1.2^(-7) */
  1.0 / (1.2 * 1.2 * 1.2 * 1.2 * 1.2 * 1.2 * 1.2) * 1.095445115010332, /* MINUS_13: 1.2^(-6.5) */
  1.0 / (1.2 * 1.2 * 1.2 * 1.2 * 1.2 * 1.2),                           /* MINUS_12: 1.2^(-6) */
  1.0 / (1.2 * 1.2 * 1.2 * 1.2 * 1.2 * 1.2) * 1.095445115010332,       /* MINUS_11: 1.2^(-5.5) */
  1.0 / (1.2 * 1.2 * 1.2 * 1.2 * 1.2),                                 /* MINUS_10: 1.2^(-5) */
  1.0 / (1.2 * 1.2 * 1.2 * 1.2 * 1.2) * 1.095445115010332,             /* MINUS_9: 1.2^(-4.5) */
  1.0 / (1.2 * 1.2 * 1.2 * 1.2),                                       /* MINUS_8: 1.2^(-4) */
  1.0 / (1.2 * 1.2 * 1.2 * 1.2) * 1.095445115010332,                   /* MINUS_7: 1.2^(-3.5) */
  1.0 / (1.2 * 1.2 * 1.2),                                             /* MINUS_6: 1.2^(-3) */
  1.0 / (1.2 * 1.2 * 1.2) * 1.095445115010332,                         /* MINUS_5: 1.2^(-2.5) */
  1.0 / (1.2 * 1.2),                                                   /* MINUS_4: 1.2^(-2) */
  1.0 / (1.2 * 1.2) * 1.095445115010332,                               /* MINUS_3: 1.2^(-1.5) */
  1.0 / (1.2),                                                         /* MINUS_2: 1.2^(-1) */
  1.0 / (1.2) * 1.095445115010332,                                     /* MINUS_1: 1.2^(-0.5) */
  1.0,                                                                 /* DEFAULT: 1.2^0 */

  /* PLUS_1 through PLUS_14: each step is 1.2^(1/2) ≈ 1.095445 */
  1.0 * 1.095445115010332,                                             /* PLUS_1: 1.2^0.5 */
  1.0 * 1.2,                                                           /* PLUS_2: 1.2^1 */
  1.0 * 1.2 * 1.095445115010332,                                       /* PLUS_3: 1.2^1.5 */
  1.0 * 1.2 * 1.2,                                                     /* PLUS_4: 1.2^2 */
  1.0 * 1.2 * 1.2 * 1.095445115010332,                                 /* PLUS_5: 1.2^2.5 */
  1.0 * 1.2 * 1.2 * 1.2,                                               /* PLUS_6: 1.2^3 */
  1.0 * 1.2 * 1.2 * 1.2 * 1.095445115010332,                           /* PLUS_7: 1.2^3.5 */
  1.0 * 1.2 * 1.2 * 1.2 * 1.2,                                         /* PLUS_8: 1.2^4 */
  1.0 * 1.2 * 1.2 * 1.2 * 1.2 * 1.095445115010332,                     /* PLUS_9: 1.2^4.5 */
  1.0 * 1.2 * 1.2 * 1.2 * 1.2 * 1.2,                                   /* PLUS_10: 1.2^5 */
  1.0 * 1.2 * 1.2 * 1.2 * 1.2 * 1.2 * 1.095445115010332,               /* PLUS_11: 1.2^5.5 */
  1.0 * 1.2 * 1.2 * 1.2 * 1.2 * 1.2 * 1.2,                             /* PLUS_12: 1.2^6 */
  1.0 * 1.2 * 1.2 * 1.2 * 1.2 * 1.2 * 1.2 * 1.095445115010332,         /* PLUS_13: 1.2^6.5 */
  1.0 * 1.2 * 1.2 * 1.2 * 1.2 * 1.2 * 1.2 * 1.2,                       /* PLUS_14: 1.2^7 */
};

static gboolean
on_scroll_scrolled_cb (GtkEventControllerScroll *scroll,
                       double                    dx,
                       double                    dy,
                       PtyxisTab                *self)
{
  GdkModifierType mods;

  g_assert (GTK_IS_EVENT_CONTROLLER_SCROLL (scroll));
  g_assert (PTYXIS_IS_TAB (self));

  mods = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (scroll));

  if ((mods & GDK_CONTROL_MASK) != 0)
    {
      PtyxisSettings *settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);

      if (ptyxis_settings_get_enable_zoom_scroll_ctrl(settings))
        {
          if (dy < 0)
            ptyxis_tab_zoom_in (self);
          else if (dy > 0)
            ptyxis_tab_zoom_out (self);
	}

      return TRUE;
    }

  return FALSE;
}

static void
on_scroll_begin_cb (GtkEventControllerScroll *scroll,
                    PtyxisTab                *self)
{
  GdkModifierType state;

  g_assert (GTK_IS_EVENT_CONTROLLER_SCROLL (scroll));
  g_assert (PTYXIS_IS_TAB (self));

  state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (scroll));

  if ((state & GDK_CONTROL_MASK) != 0)
    gtk_event_controller_scroll_set_flags (scroll,
                                           GTK_EVENT_CONTROLLER_SCROLL_VERTICAL |
                                           GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
}

static void
on_scroll_end_cb (GtkEventControllerScroll *scroll,
                  PtyxisTab                *self)
{
  g_assert (GTK_IS_EVENT_CONTROLLER_SCROLL (scroll));
  g_assert (PTYXIS_IS_TAB (self));

  gtk_event_controller_scroll_set_flags (scroll, GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
}

static void
ptyxis_tab_send_signal (PtyxisTab *self,
                        int        signum)
{
  g_autofree char *title = NULL;

  g_assert (PTYXIS_IS_TAB (self));

  if (ptyxis_tab_get_process (self) == NULL)
    {
      g_debug ("Cannot send signal %d to tab, process is gone.", signum);
      return;
    }

  title = ptyxis_tab_dup_title (self);
  g_debug ("Sending signal %d to tab \"%s\"", signum, title);

  ptyxis_ipc_process_call_send_signal (ptyxis_tab_get_process (self), signum, NULL, NULL, NULL);
}

static gboolean
ptyxis_tab_is_active (PtyxisTab *self)
{
  GtkWidget *window;

  g_assert (PTYXIS_IS_TAB (self));

  if ((window = gtk_widget_get_ancestor (GTK_WIDGET (self), PTYXIS_TYPE_WINDOW)))
    return ptyxis_window_get_active_tab (PTYXIS_WINDOW (window)) == self;

  return FALSE;
}

static void
ptyxis_tab_update_scrollback_lines (PtyxisTab *self)
{
  long scrollback_lines = -1;

  g_assert (PTYXIS_IS_TAB (self));

  if (ptyxis_profile_get_limit_scrollback (ptyxis_tab_get_profile (self)))
    scrollback_lines = ptyxis_profile_get_scrollback_lines (ptyxis_tab_get_profile (self));

  vte_terminal_set_scrollback_lines (VTE_TERMINAL (self->terminal), scrollback_lines);
}

static void
ptyxis_tab_update_cell_height_scale (PtyxisTab *self)
{
  double cell_height_scale = 1.0;

  g_assert (PTYXIS_IS_TAB (self));

  if (ptyxis_profile_get_cell_height_scale (ptyxis_tab_get_profile (self)))
    cell_height_scale = ptyxis_profile_get_cell_height_scale (ptyxis_tab_get_profile (self));

  vte_terminal_set_cell_height_scale (VTE_TERMINAL (self->terminal), cell_height_scale);
}

static void
ptyxis_tab_update_cell_width_scale (PtyxisTab *self)
{
  double cell_width_scale = 1.0;

  g_assert (PTYXIS_IS_TAB (self));

  if (ptyxis_profile_get_cell_width_scale (ptyxis_tab_get_profile (self)))
    cell_width_scale = ptyxis_profile_get_cell_width_scale (ptyxis_tab_get_profile (self));

  vte_terminal_set_cell_width_scale (VTE_TERMINAL (self->terminal), cell_width_scale);
}

static void
ptyxis_tab_update_custom_links (PtyxisTab *self)
{
  g_autoptr(GListModel) custom_links_list = NULL;

  g_assert (PTYXIS_IS_TAB (self));

  custom_links_list = ptyxis_profile_list_custom_links(ptyxis_tab_get_profile (self));
  ptyxis_terminal_update_custom_links_list(self->terminal, custom_links_list);
}

static void
ptyxis_tab_update_inhibit (PtyxisTab *self)
{
  PtyxisSettings *settings;
  gboolean inhibit = FALSE;
  GtkWidget *window;

  g_assert (PTYXIS_IS_TAB (self));

  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);

  /* Clear if the user has disabled logout inhibition */
  if (!ptyxis_settings_get_inhibit_logout (settings))
    {
      if (ptyxis_pane_get_inhibit_cookie (self->pane))
        {
          gtk_application_uninhibit (GTK_APPLICATION (PTYXIS_APPLICATION_DEFAULT),
                                     ptyxis_pane_get_inhibit_cookie (self->pane));
          ptyxis_pane_set_inhibit_cookie (self->pane, 0);
        }

      return;
    }

  /* Only inhibit if there's a foreground process running and it's not a shell */
  if (ptyxis_pane_get_has_foreground_process (self->pane) &&
      ptyxis_pane_get_program_name (self->pane) != NULL &&
      !ptyxis_is_shell (ptyxis_pane_get_program_name (self->pane)))
    inhibit = TRUE;

  /* Check if we need to change the inhibit state */
  if ((inhibit && ptyxis_pane_get_inhibit_cookie (self->pane) != 0) ||
      (!inhibit && ptyxis_pane_get_inhibit_cookie (self->pane) == 0))
    return;

  /* Get the window to use for the inhibit call */
  window = gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);

  if (inhibit)
    {
      /* Only inhibit if we have a valid window reference */
      if (window != NULL)
        {
          ptyxis_pane_set_inhibit_cookie (
            self->pane,
            gtk_application_inhibit (GTK_APPLICATION (PTYXIS_APPLICATION_DEFAULT),
                                     GTK_WINDOW (window),
                                     GTK_APPLICATION_INHIBIT_LOGOUT,
                                     _("A foreground process is running")));
        }
    }
  else
    {
      gtk_application_uninhibit (GTK_APPLICATION (PTYXIS_APPLICATION_DEFAULT),
                                 ptyxis_pane_get_inhibit_cookie (self->pane));
      ptyxis_pane_set_inhibit_cookie (self->pane, 0);
    }
}

static void
ptyxis_tab_wait_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  PtyxisApplication *app = (PtyxisApplication *)object;
  g_autoptr(PtyxisTab) self = user_data;
  g_autoptr(GError) error = NULL;
  PtyxisExitAction exit_action;
  PtyxisWindow *window;
  AdwTabPage *page = NULL;
  GtkWidget *tab_view;
  gboolean is_front = FALSE;
  int exit_code;

  g_assert (PTYXIS_IS_APPLICATION (app));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (ptyxis_pane_get_state (self->pane) == PTYXIS_PANE_STATE_RUNNING);

  ptyxis_pane_set_process (self->pane, NULL);

  /* Update inhibit state when process exits */
  ptyxis_tab_update_inhibit (self);

  exit_code = ptyxis_application_wait_finish (app, result, &error);

  g_debug ("Process completed with exit-code 0x%x %s",
           exit_code,
           error ? error->message : "");

  if (error == NULL && WIFEXITED (exit_code) && WEXITSTATUS (exit_code) == 0)
    ptyxis_pane_set_state (self->pane, PTYXIS_PANE_STATE_EXITED);
  else
    ptyxis_pane_set_state (self->pane, PTYXIS_PANE_STATE_FAILED);

  if (ptyxis_pane_get_forced_exit (self->pane))
    return;

  if ((window = PTYXIS_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), PTYXIS_TYPE_WINDOW))))
    is_front = self == ptyxis_window_get_active_tab (window);

  if (WIFSIGNALED (exit_code))
    {
      g_autofree char *title = NULL;

      title = g_strdup_printf (_("Process Exited from Signal %d"), WTERMSIG (exit_code));

      adw_banner_set_title (self->banner, title);
      adw_banner_set_button_label (self->banner, _("_Restart"));
      gtk_actionable_set_action_name (GTK_ACTIONABLE (self->banner), "tab.respawn");
      gtk_widget_set_visible (GTK_WIDGET (self->banner), TRUE);
      return;
    }

  exit_action = ptyxis_profile_get_exit_action (ptyxis_tab_get_profile (self));
  tab_view = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_TAB_VIEW);

  /* If this was started with something like ptyxis_window_new_for_command()
   * then we just want to exit the application (so allow tab to close).
   */
  if (ptyxis_pane_get_command (self->pane) != NULL)
    exit_action = PTYXIS_EXIT_ACTION_CLOSE;

  if (ADW_IS_TAB_VIEW (tab_view))
    page = adw_tab_view_get_page (ADW_TAB_VIEW (tab_view), GTK_WIDGET (self));

  /* Always prepare the banner even if we don't show it because we may
   * display it again if the tab is removed from the parking lot and
   * restored into the window.
   */
  adw_banner_set_title (self->banner, _("Process Exited"));
  adw_banner_set_button_label (self->banner, _("_Restart"));
  gtk_actionable_set_action_name (GTK_ACTIONABLE (self->banner), "tab.respawn");

  /* If we took less than .5 a second to spawn and no key has been
   * pressed in the terminal, then treat this as a failed spawn. Don't
   * allow ourselves to auto-close in that case as it's likely an error
   * the user would want to see.
   */
  if ((ptyxis_pane_get_command (self->pane) == NULL || ptyxis_pane_get_state (self->pane) == PTYXIS_PANE_STATE_FAILED) &&
      (g_get_monotonic_time () - ptyxis_pane_get_respawn_time (self->pane)) < (G_USEC_PER_SEC/2) &&
      !ptyxis_tab_monitor_get_has_pressed_key (ptyxis_pane_get_monitor (self->pane)))
    exit_action = PTYXIS_EXIT_ACTION_NONE;

  switch (exit_action)
    {
    case PTYXIS_EXIT_ACTION_RESTART:
      ptyxis_tab_respawn (self);
      break;

    case PTYXIS_EXIT_ACTION_CLOSE:
      if (ADW_IS_TAB_VIEW (tab_view) && ADW_IS_TAB_PAGE (page))
        {
          if (adw_tab_page_get_pinned (page))
            adw_tab_view_set_page_pinned (ADW_TAB_VIEW (tab_view), page, FALSE);
          adw_tab_view_close_page (ADW_TAB_VIEW (tab_view), page);
        }
      break;

    case PTYXIS_EXIT_ACTION_NONE:
      gtk_widget_set_visible (GTK_WIDGET (self->banner), TRUE);
      if (is_front)
        gtk_widget_child_focus (GTK_WIDGET (self->banner), GTK_DIR_TAB_FORWARD);
      break;

    default:
      g_assert_not_reached ();
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

static void
ptyxis_tab_spawn_cb (GObject      *object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  PtyxisApplication *app = (PtyxisApplication *)object;
  g_autoptr(PtyxisIpcProcess) process = NULL;
  g_autoptr(PtyxisTab) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (PTYXIS_IS_TAB (self));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (ptyxis_pane_get_state (self->pane) == PTYXIS_PANE_STATE_SPAWNING);

  if (!(process = ptyxis_application_spawn_finish (app, result, &error)))
    {
      const char *profile_uuid = ptyxis_profile_get_uuid (ptyxis_tab_get_profile (self));

      ptyxis_pane_set_state (self->pane, PTYXIS_PANE_STATE_FAILED);

      vte_terminal_feed (VTE_TERMINAL (self->terminal), error->message, -1);
      vte_terminal_feed (VTE_TERMINAL (self->terminal), "\r\n", -1);

      adw_banner_set_title (self->banner, _("Failed to launch terminal"));
      adw_banner_set_button_label (self->banner, _("Edit Profile"));
      gtk_actionable_set_action_target (GTK_ACTIONABLE (self->banner), "s", profile_uuid);
      gtk_actionable_set_action_name (GTK_ACTIONABLE (self->banner), "app.edit-profile");
      gtk_widget_set_visible (GTK_WIDGET (self->banner), TRUE);

      return;
    }

  ptyxis_pane_set_state (self->pane, PTYXIS_PANE_STATE_RUNNING);
  ptyxis_pane_set_respawn_time (self->pane, g_get_monotonic_time ());

  ptyxis_pane_set_process (self->pane, process);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ICON]);

  ptyxis_application_wait_async (app,
                                 process,
                                 NULL,
                                 ptyxis_tab_wait_cb,
                                 g_object_ref (self));
}

static void
ptyxis_tab_respawn (PtyxisTab *self)
{
  g_autofree char *default_container = NULL;
  g_autoptr(PtyxisIpcContainer) container = NULL;
  g_autoptr(PtyxisIpcContainer) container_at_creation = NULL;
  g_autoptr(VtePty) new_pty = NULL;
  PtyxisApplication *app;
  const char *profile_uuid;
  const char *cwd_uri;
  VtePty *pty;

  g_assert (PTYXIS_IS_TAB (self));
  g_assert (ptyxis_pane_get_state (self->pane) == PTYXIS_PANE_STATE_INITIAL ||
            ptyxis_pane_get_state (self->pane) == PTYXIS_PANE_STATE_EXITED ||
            ptyxis_pane_get_state (self->pane) == PTYXIS_PANE_STATE_FAILED);

  gtk_widget_set_visible (GTK_WIDGET (self->banner), FALSE);

  app = PTYXIS_APPLICATION_DEFAULT;
  profile_uuid = ptyxis_profile_get_uuid (ptyxis_tab_get_profile (self));
  default_container = ptyxis_profile_dup_default_container (ptyxis_tab_get_profile (self));

  container_at_creation = ptyxis_pane_dup_container (self->pane);
  if (container_at_creation != NULL)
    container = g_object_ref (container_at_creation);
  else
    container = ptyxis_application_lookup_container (app, default_container);

  if (container == NULL)
    {
      g_autofree char *title = NULL;

      ptyxis_pane_set_state (self->pane, PTYXIS_PANE_STATE_FAILED);

      title = g_strdup_printf (_("Cannot locate container “%s”"), default_container);
      adw_banner_set_title (self->banner, title);
      adw_banner_set_button_label (self->banner, _("Edit Profile"));
      gtk_actionable_set_action_target (GTK_ACTIONABLE (self->banner), "s", profile_uuid);
      gtk_actionable_set_action_name (GTK_ACTIONABLE (self->banner), "app.edit-profile");
      gtk_widget_set_visible (GTK_WIDGET (self->banner), TRUE);

      return;
    }

  ptyxis_pane_set_state (self->pane, PTYXIS_PANE_STATE_SPAWNING);

  pty = vte_terminal_get_pty (VTE_TERMINAL (self->terminal));

  if (pty == NULL)
    {
      g_autoptr(GError) error = NULL;

      new_pty = ptyxis_application_create_pty (PTYXIS_APPLICATION_DEFAULT, &error);

      if (new_pty == NULL)
        {
          ptyxis_pane_set_state (self->pane, PTYXIS_PANE_STATE_FAILED);

          adw_banner_set_title (self->banner, _("Failed to create pseudo terminal device"));
          adw_banner_set_button_label (self->banner, NULL);
          gtk_actionable_set_action_name (GTK_ACTIONABLE (self->banner), NULL);
          gtk_widget_set_visible (GTK_WIDGET (self->banner), TRUE);

          return;
        }

      vte_terminal_set_pty (VTE_TERMINAL (self->terminal), new_pty);

      pty = new_pty;
    }

  cwd_uri = ptyxis_pane_get_previous_working_directory_uri (self->pane);
  if (ptyxis_pane_get_initial_working_directory_uri (self->pane))
    cwd_uri = ptyxis_pane_get_initial_working_directory_uri (self->pane);

  ptyxis_application_spawn_async (PTYXIS_APPLICATION_DEFAULT,
                                  container,
                                  ptyxis_tab_get_profile (self),
                                  cwd_uri,
                                  pty,
                                  ptyxis_pane_get_command (self->pane),
                                  NULL,
                                  ptyxis_tab_spawn_cb,
                                  g_object_ref (self));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

static void
ptyxis_tab_respawn_action (GtkWidget  *widget,
                           const char *action_name,
                           GVariant   *params)
{
  PtyxisTab *self = (PtyxisTab *)widget;

  g_assert (PTYXIS_IS_TAB (self));

  if (ptyxis_pane_get_state (self->pane) == PTYXIS_PANE_STATE_FAILED ||
      ptyxis_pane_get_state (self->pane) == PTYXIS_PANE_STATE_EXITED)
    ptyxis_tab_respawn (self);
}


static void
ptyxis_tab_inspect_action (GtkWidget  *widget,
                           const char *action_name,
                           GVariant   *params)
{
  PtyxisTab *self = (PtyxisTab *)widget;
  PtyxisInspector *inspector;
  GtkRoot *root;

  g_assert (PTYXIS_IS_TAB (self));

  inspector = ptyxis_inspector_new (self);
  root = gtk_widget_get_root (GTK_WIDGET (self));

  gtk_window_set_transient_for (GTK_WINDOW (inspector), GTK_WINDOW (root));
  gtk_window_set_modal (GTK_WINDOW (inspector), FALSE);
  gtk_window_present (GTK_WINDOW (inspector));
}

static void
ptyxis_tab_map (GtkWidget *widget)
{
  PtyxisTab *self = (PtyxisTab *)widget;

  g_assert (PTYXIS_IS_TAB (widget));

  GTK_WIDGET_CLASS (ptyxis_tab_parent_class)->map (widget);

  if (ptyxis_pane_get_state (self->pane) == PTYXIS_PANE_STATE_INITIAL)
    ptyxis_tab_respawn (self);
}

static void
ptyxis_tab_notify_contains_focus_cb (PtyxisTab               *self,
                                     GParamSpec              *pspec,
                                     GtkEventControllerFocus *focus)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_FOCUS (focus));

  if (gtk_event_controller_focus_contains_focus (focus))
    {
      ptyxis_tab_set_needs_attention (self, FALSE);
      g_application_withdraw_notification (G_APPLICATION (PTYXIS_APPLICATION_DEFAULT),
                                           ptyxis_pane_get_uuid (self->pane));
    }
}

static void
ptyxis_tab_notify_window_title_cb (PtyxisTab      *self,
                                   GParamSpec     *pspec,
                                   PtyxisTerminal *terminal)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_TERMINAL (terminal));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

static void
ptyxis_tab_notify_window_subtitle_cb (PtyxisTab      *self,
                                      PtyxisTerminal *terminal)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_TERMINAL (terminal));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBTITLE]);
}

static void
ptyxis_tab_increase_font_size_cb (PtyxisTab      *self,
                                  PtyxisTerminal *terminal)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_TERMINAL (terminal));

  ptyxis_tab_zoom_in (self);
}

static void
ptyxis_tab_decrease_font_size_cb (PtyxisTab      *self,
                                  PtyxisTerminal *terminal)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_TERMINAL (terminal));

  ptyxis_tab_zoom_out (self);
}

static void
ptyxis_tab_bell_cb (PtyxisTab      *self,
                    PtyxisTerminal *terminal)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_TERMINAL (terminal));

  g_signal_emit (self, signals[BELL], 0);
}

static PtyxisIpcContainer *
ptyxis_tab_discover_container (PtyxisTab *self)
{
  const char *current_container_name = ptyxis_terminal_get_current_container_name (self->terminal);
  const char *current_container_runtime = ptyxis_terminal_get_current_container_runtime (self->terminal);

  return ptyxis_application_find_container_by_name (PTYXIS_APPLICATION_DEFAULT,
                                                    current_container_runtime,
                                                    current_container_name);
}

static GIcon *
ptyxis_tab_dup_icon (PtyxisTab *self)
{
  PtyxisProcessLeaderKind kind;

  g_assert (PTYXIS_IS_TAB (self));

  kind = ptyxis_pane_get_process_leader_kind (self->pane);

  switch (kind)
    {
    default:
    case PTYXIS_PROCESS_LEADER_KIND_REMOTE:
      return g_themed_icon_new ("process-remote-symbolic");

    case PTYXIS_PROCESS_LEADER_KIND_SUPERUSER:
      return g_themed_icon_new ("process-superuser-symbolic");

    case PTYXIS_PROCESS_LEADER_KIND_CONTAINER:
    case PTYXIS_PROCESS_LEADER_KIND_UNKNOWN:
      {
        g_autoptr(PtyxisIpcContainer) container = NULL;
        const char *icon_name;

        if (!(container = ptyxis_tab_discover_container (self)))
          {
            if (!(container = ptyxis_pane_dup_container (self->pane)))
              {
                if (ptyxis_tab_get_profile (self) != NULL)
                {
                  g_autofree char *profile_uuid = ptyxis_profile_dup_default_container (ptyxis_tab_get_profile (self));

                  container = ptyxis_application_lookup_container (PTYXIS_APPLICATION_DEFAULT, profile_uuid);
                }
              }
          }

        if (container != NULL &&
            (icon_name = ptyxis_ipc_container_get_icon_name (container)) &&
            icon_name[0] != 0)
          return g_themed_icon_new (icon_name);
      }
      return NULL;
    }
}

static void
ptyxis_tab_invalidate_thumbnail (PtyxisTab *self)
{
  GtkWidget *view;
  AdwTabPage *page;

  g_assert (PTYXIS_IS_TAB (self));

  g_clear_object (&self->cached_texture);

  gtk_widget_queue_draw (GTK_WIDGET (self));

  if ((view = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_TAB_VIEW)) &&
      (page = adw_tab_view_get_page (ADW_TAB_VIEW (view), GTK_WIDGET (self))))
    adw_tab_page_invalidate_thumbnail (page);
}

static void
ptyxis_tab_notify_palette_cb (PtyxisTab      *self,
                              GParamSpec     *pspec,
                              PtyxisTerminal *terminal)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_TERMINAL (terminal));

  ptyxis_tab_invalidate_thumbnail (self);
}

static void
ptyxis_tab_update_scrollbar_policy (PtyxisTab *self)
{
  PtyxisSettings *settings;
  PtyxisScrollbarPolicy policy;

  g_assert (PTYXIS_IS_TAB (self));

  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);
  policy = ptyxis_settings_get_scrollbar_policy (settings);

  switch (policy)
    {
    case PTYXIS_SCROLLBAR_POLICY_NEVER:
      gtk_scrolled_window_set_overlay_scrolling (self->scrolled_window, FALSE);
      gtk_scrolled_window_set_policy (self->scrolled_window, GTK_POLICY_NEVER, GTK_POLICY_EXTERNAL);
      break;

    case PTYXIS_SCROLLBAR_POLICY_ALWAYS:
      gtk_scrolled_window_set_overlay_scrolling (self->scrolled_window, FALSE);
      gtk_scrolled_window_set_policy (self->scrolled_window, GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
      break;

    case PTYXIS_SCROLLBAR_POLICY_SYSTEM:
      if (ptyxis_application_get_overlay_scrollbars (PTYXIS_APPLICATION_DEFAULT))
        {
          gtk_scrolled_window_set_overlay_scrolling (self->scrolled_window, TRUE);
          gtk_scrolled_window_set_policy (self->scrolled_window, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        }
      else
        {
          gtk_scrolled_window_set_overlay_scrolling (self->scrolled_window, FALSE);
          gtk_scrolled_window_set_policy (self->scrolled_window, GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
        }

      break;

    default:
      g_assert_not_reached ();
    }
}

static void
ptyxis_tab_update_padding_cb (PtyxisTab      *self,
                              GParamSpec     *pspec,
                              PtyxisSettings *settings)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_SETTINGS (settings));

  if (ptyxis_settings_get_disable_padding (settings))
    gtk_widget_remove_css_class (GTK_WIDGET (self->terminal), "padded");
  else
    gtk_widget_add_css_class (GTK_WIDGET (self->terminal), "padded");
}

static void
ptyxis_tab_update_word_char_exceptions (PtyxisTab      *self,
                                        GParamSpec     *pspec,
                                        PtyxisSettings *settings)
{
  g_autofree char *word_char_exceptions = NULL;

  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_SETTINGS (settings));

  word_char_exceptions = ptyxis_settings_dup_word_char_exceptions (settings);
  vte_terminal_set_word_char_exceptions (VTE_TERMINAL (self->terminal), word_char_exceptions);
}

static void
ptyxis_tab_constructed (GObject *object)
{
  PtyxisTab *self = (PtyxisTab *)object;
  PtyxisSettings *settings;

  G_OBJECT_CLASS (ptyxis_tab_parent_class)->constructed (object);

  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);
  g_object_bind_property (settings, "audible-bell",
                          self->terminal, "audible-bell",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (settings, "cursor-shape",
                          self->terminal, "cursor-shape",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (settings, "cursor-blink-mode",
                          self->terminal, "cursor-blink-mode",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (settings, "enable-a11y",
                          self->terminal, "enable-a11y",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (settings, "font-desc",
                          self->terminal, "font-desc",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (settings, "text-blink-mode",
                          self->terminal, "text-blink-mode",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (settings, "ignore-osc-title",
                          self, "ignore-osc-title",
                          G_BINDING_SYNC_CREATE);

  g_signal_connect_object (settings,
                           "notify::disable-padding",
                           G_CALLBACK (ptyxis_tab_update_padding_cb),
                           self,
                           G_CONNECT_SWAPPED);
  ptyxis_tab_update_padding_cb (self, NULL, settings);

  g_signal_connect_object (PTYXIS_APPLICATION_DEFAULT,
                           "notify::overlay-scrollbars",
                           G_CALLBACK (ptyxis_tab_update_scrollbar_policy),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (settings,
                           "notify::scrollbar-policy",
                           G_CALLBACK (ptyxis_tab_update_scrollbar_policy),
                           self,
                           G_CONNECT_SWAPPED);
  ptyxis_tab_update_scrollbar_policy (self);

  /* Set up signal group for profile signals */
  g_signal_connect_object (ptyxis_pane_get_profile_signals (self->pane),
                           "bind",
                           G_CALLBACK (ptyxis_tab_profile_signals_bind_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_group_connect_object (ptyxis_pane_get_profile_signals (self->pane),
                                 "notify::limit-scrollback",
                                 G_CALLBACK (ptyxis_tab_update_scrollback_lines),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (ptyxis_pane_get_profile_signals (self->pane),
                                 "notify::scrollback-lines",
                                 G_CALLBACK (ptyxis_tab_update_scrollback_lines),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (ptyxis_pane_get_profile_signals (self->pane),
                                 "notify::cell-height-scale",
                                 G_CALLBACK (ptyxis_tab_update_cell_height_scale),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (ptyxis_pane_get_profile_signals (self->pane),
                                 "notify::cell-width-scale",
                                 G_CALLBACK (ptyxis_tab_update_cell_width_scale),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (ptyxis_pane_get_profile_signals (self->pane),
                                 "custom-links-changed",
                                 G_CALLBACK (ptyxis_tab_update_custom_links),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_set_target (ptyxis_pane_get_profile_signals (self->pane), ptyxis_tab_get_profile (self));

  g_signal_connect_object (settings,
                           "notify::word-char-exceptions",
                           G_CALLBACK (ptyxis_tab_update_word_char_exceptions),
                           self,
                           G_CONNECT_SWAPPED);
  ptyxis_tab_update_word_char_exceptions (self, NULL, settings);

  g_signal_connect_object (settings,
                           "notify::inhibit-logout",
                           G_CALLBACK (ptyxis_tab_update_inhibit),
                           self,
                           G_CONNECT_SWAPPED);
  ptyxis_tab_update_inhibit (self);

  {
    g_autoptr(PtyxisTabMonitor) monitor = ptyxis_tab_monitor_new (self);

    ptyxis_pane_set_monitor (self->pane, monitor);
  }
}

static void
ptyxis_tab_profile_signals_bind_cb (PtyxisTab     *self,
                                    PtyxisProfile *profile,
                                    GSignalGroup  *group)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_PROFILE (profile));
  g_assert (G_IS_SIGNAL_GROUP (group));

  /* Trigger all update functions when profile changes */
  ptyxis_tab_update_scrollback_lines (self);
  ptyxis_tab_update_cell_height_scale (self);
  ptyxis_tab_update_cell_width_scale (self);
  ptyxis_tab_update_custom_links (self);
}

static void
ptyxis_tab_snapshot (GtkWidget   *widget,
                     GtkSnapshot *snapshot)
{
  PtyxisTab *self = (PtyxisTab *)widget;
  PtyxisWindow *window;
  GdkRGBA bg;
  gboolean animating;
  int width;
  int height;

  g_assert (PTYXIS_IS_TAB (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  if (self->ignore_snapshot)
    return;

  window = PTYXIS_WINDOW (gtk_widget_get_root (widget));
  animating = ptyxis_window_is_animating (window);
  width = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);

  vte_terminal_get_color_background_for_draw (VTE_TERMINAL (self->terminal), &bg);

  if (animating &&
      ptyxis_window_get_active_tab (window) == self)
    {

      if (self->cached_texture == NULL)
        {
          GtkSnapshot *sub_snapshot = gtk_snapshot_new ();
          int scale_factor = gtk_widget_get_scale_factor (widget);
          g_autoptr(GskRenderNode) node = NULL;
          graphene_matrix_t matrix;
          GskRenderer *renderer;

          gtk_snapshot_scale (sub_snapshot, scale_factor, scale_factor);
          gtk_snapshot_append_color (sub_snapshot,
                                     &bg,
                                     &GRAPHENE_RECT_INIT (0, 0, width, height));

          if (gtk_widget_compute_transform (GTK_WIDGET (self->terminal),
                                            GTK_WIDGET (self),
                                            &matrix))
            {
              gtk_snapshot_transform_matrix (sub_snapshot, &matrix);
              GTK_WIDGET_GET_CLASS (self->terminal)->snapshot (GTK_WIDGET (self->terminal), sub_snapshot);
            }

          node = gtk_snapshot_free_to_node (sub_snapshot);
          renderer = gtk_native_get_renderer (GTK_NATIVE (window));

          self->cached_texture = gsk_renderer_render_texture (renderer,
                                                              node,
                                                              &GRAPHENE_RECT_INIT (0,
                                                                                   0,
                                                                                   width * scale_factor,
                                                                                   height * scale_factor));
        }

      gtk_snapshot_append_texture (snapshot,
                                   self->cached_texture,
                                   &GRAPHENE_RECT_INIT (0, 0, width, height));
    }
  else
    {
      g_clear_object (&self->cached_texture);

      if (animating)
        gtk_snapshot_append_color (snapshot,
                                   &bg,
                                   &GRAPHENE_RECT_INIT (0, 0, width, height));

      GTK_WIDGET_CLASS (ptyxis_tab_parent_class)->snapshot (widget, snapshot);
    }
}

static void
ptyxis_tab_size_allocate (GtkWidget *widget,
                          int        width,
                          int        height,
                          int        baseline)
{
  PtyxisTab *self = (PtyxisTab *)widget;

  g_assert (PTYXIS_IS_TAB (self));

  GTK_WIDGET_CLASS (ptyxis_tab_parent_class)->size_allocate (widget, width, height, baseline);

  g_clear_object (&self->cached_texture);
}

static void
ptyxis_tab_invalidate_icon (PtyxisTab *self)
{
  g_assert (PTYXIS_IS_TAB (self));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ICON]);
}

static void
ptyxis_tab_invalidate_progress (PtyxisTab *self)
{
  g_assert (PTYXIS_IS_TAB (self));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS_FRACTION]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INDICATOR_ICON]);
}

static gboolean
ptyxis_tab_match_clicked_cb (PtyxisTab       *self,
                             double           x,
                             double           y,
                             int              button,
                             GdkModifierType  state,
                             const char      *match,
                             PtyxisTerminal  *terminal)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (match != NULL);
  g_assert (PTYXIS_IS_TERMINAL (terminal));

  if (!ptyxis_str_empty0 (match))
    {
      ptyxis_tab_open_uri (self, match);
      return TRUE;
    }

  return FALSE;
}

static void
ptyxis_tab_root (GtkWidget *widget)
{
  PtyxisTab *self = PTYXIS_TAB (widget);

  /* Clear our ignore_snapshot bit in case we've had our tab restored
   * from the parking lot.
   */
  self->ignore_snapshot = FALSE;

  GTK_WIDGET_CLASS (ptyxis_tab_parent_class)->root (widget);
}

static void
ptyxis_tab_unroot (GtkWidget *widget)
{
  PtyxisTab *self = PTYXIS_TAB (widget);

  /* Clear inhibit cookie when widget is unrooted since the window
   * reference may no longer be valid.
   */
  if (self->pane != NULL &&
      ptyxis_pane_get_inhibit_cookie (self->pane) != 0)
    {
      gtk_application_uninhibit (GTK_APPLICATION (PTYXIS_APPLICATION_DEFAULT),
                                 ptyxis_pane_get_inhibit_cookie (self->pane));
      ptyxis_pane_set_inhibit_cookie (self->pane, 0);
    }

  GTK_WIDGET_CLASS (ptyxis_tab_parent_class)->unroot (widget);
}

static void
ptyxis_tab_commit_cb (PtyxisTab      *self,
                      const char     *str,
                      guint           length,
                      PtyxisTerminal *terminal)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_TERMINAL (terminal));

  g_signal_emit (self, signals[COMMIT], 0, str);
}

static void
ptyxis_tab_dispose (GObject *object)
{
  PtyxisTab *self = (PtyxisTab *)object;
  GtkWidget *child;

  g_debug ("Disposing tab");

  ptyxis_tab_notify_destroy (&self->notify);

  ptyxis_tab_force_quit (self);

  /* Template disposal clears self->pane, so release application state that
   * is keyed by the pane before disposing template children.
   */
  if (self->pane != NULL &&
      ptyxis_pane_get_inhibit_cookie (self->pane) != 0)
    {
      gtk_application_uninhibit (GTK_APPLICATION (PTYXIS_APPLICATION_DEFAULT),
                                 ptyxis_pane_get_inhibit_cookie (self->pane));
      ptyxis_pane_set_inhibit_cookie (self->pane, 0);
    }

  gtk_widget_dispose_template (GTK_WIDGET (self), PTYXIS_TYPE_TAB);

  self->active_pane = NULL;
  g_clear_pointer (&self->split_root, ptyxis_split_node_unref);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  g_clear_object (&self->cached_texture);
  G_OBJECT_CLASS (ptyxis_tab_parent_class)->dispose (object);
}

static void
ptyxis_tab_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  PtyxisTab *self = PTYXIS_TAB (object);

  switch (prop_id)
    {
    case PROP_ACTIVE_PANE:
      g_value_set_object (value, self->active_pane);
      break;

    case PROP_COMMAND_LINE:
      g_value_set_string (value, ptyxis_pane_get_command_line (self->pane));
      break;

    case PROP_ICON:
      g_value_take_object (value, ptyxis_tab_dup_icon (self));
      break;

    case PROP_IGNORE_OSC_TITLE:
      g_value_set_boolean (value, ptyxis_tab_get_ignore_osc_title (self));
      break;

    case PROP_INDICATOR_ICON:
      g_value_take_object (value, ptyxis_tab_dup_indicator_icon (self));
      break;

    case PROP_PROCESS_LEADER_KIND:
      g_value_set_enum (value, ptyxis_pane_get_process_leader_kind (self->pane));
      break;

    case PROP_PROGRESS:
      g_value_set_enum (value, ptyxis_tab_get_progress (self));
      break;

    case PROP_PROGRESS_FRACTION:
      g_value_set_double (value, ptyxis_tab_get_progress_fraction (self));
      break;

    case PROP_PROFILE:
      g_value_set_object (value, ptyxis_tab_get_profile (self));
      break;

    case PROP_READ_ONLY:
      g_value_set_boolean (value, ptyxis_pane_get_read_only (self->pane));
      break;

    case PROP_SUBTITLE:
      g_value_take_string (value, ptyxis_tab_dup_subtitle (self));
      break;

    case PROP_TITLE:
      g_value_take_string (value, ptyxis_tab_dup_title (self));
      break;

    case PROP_TITLE_PREFIX:
      g_value_set_string (value, ptyxis_tab_get_title_prefix (self));
      break;

    case PROP_UUID:
      g_value_set_string (value, ptyxis_tab_get_uuid (self));
      break;

    case PROP_ZOOM:
      g_value_set_enum (value, ptyxis_tab_get_zoom (self));
      break;

    case PROP_ZOOM_LABEL:
      g_value_take_string (value, ptyxis_tab_dup_zoom_label (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ptyxis_tab_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  PtyxisTab *self = PTYXIS_TAB (object);

  switch (prop_id)
    {
    case PROP_IGNORE_OSC_TITLE:
      ptyxis_tab_set_ignore_osc_title (self, g_value_get_boolean (value));
      break;

    case PROP_PROFILE:
      ptyxis_pane_set_profile (self->pane, g_value_get_object (value));
      break;

    case PROP_READ_ONLY:
      ptyxis_pane_set_read_only (self->pane, g_value_get_boolean (value));
      break;

    case PROP_TITLE_PREFIX:
      ptyxis_tab_set_title_prefix (self, g_value_get_string (value));
      break;

    case PROP_ZOOM:
      ptyxis_tab_set_zoom (self, g_value_get_enum (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ptyxis_tab_class_init (PtyxisTabClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = ptyxis_tab_constructed;
  object_class->dispose = ptyxis_tab_dispose;
  object_class->get_property = ptyxis_tab_get_property;
  object_class->set_property = ptyxis_tab_set_property;

  widget_class->map = ptyxis_tab_map;
  widget_class->snapshot = ptyxis_tab_snapshot;
  widget_class->size_allocate = ptyxis_tab_size_allocate;
  widget_class->root = ptyxis_tab_root;
  widget_class->unroot = ptyxis_tab_unroot;

  properties[PROP_ACTIVE_PANE] =
    g_param_spec_object ("active-pane", NULL, NULL,
                         PTYXIS_TYPE_PANE,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_COMMAND_LINE] =
    g_param_spec_string ("command-line", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_ICON] =
    g_param_spec_object ("icon", NULL, NULL,
                         G_TYPE_ICON,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_IGNORE_OSC_TITLE] =
    g_param_spec_boolean ("ignore-osc-title", NULL, NULL,
                         FALSE,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_INDICATOR_ICON] =
    g_param_spec_object ("indicator-icon", NULL, NULL,
                         G_TYPE_ICON,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_PROCESS_LEADER_KIND] =
    g_param_spec_enum ("process-leader-kind", NULL, NULL,
                       PTYXIS_TYPE_PROCESS_LEADER_KIND,
                       PTYXIS_PROCESS_LEADER_KIND_UNKNOWN,
                       (G_PARAM_READABLE |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_PROFILE] =
    g_param_spec_object ("profile", NULL, NULL,
                         PTYXIS_TYPE_PROFILE,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_PROGRESS] =
    g_param_spec_enum ("progress", NULL, NULL,
                       PTYXIS_TYPE_TAB_PROGRESS,
                       PTYXIS_TAB_PROGRESS_INDETERMINATE,
                       (G_PARAM_READABLE |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_PROGRESS_FRACTION] =
    g_param_spec_double ("progress-fraction", NULL, NULL,
                         0, 1, 0,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_READ_ONLY] =
    g_param_spec_boolean ("read-only", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_SUBTITLE] =
    g_param_spec_string ("subtitle", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_TITLE_PREFIX] =
    g_param_spec_string ("title-prefix", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_UUID] =
    g_param_spec_string ("uuid", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_ZOOM] =
    g_param_spec_enum ("zoom", NULL, NULL,
                       PTYXIS_TYPE_ZOOM_LEVEL,
                       PTYXIS_ZOOM_LEVEL_DEFAULT,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_ZOOM_LABEL] =
    g_param_spec_string ("zoom-label", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals[BELL] =
    g_signal_new_class_handler ("bell",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST,
                                NULL,
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 0);

  signals[COMMIT] =
    g_signal_new_class_handler ("commit",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST,
                                NULL,
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE,
                                1,
                                G_TYPE_STRING | G_SIGNAL_TYPE_STATIC_SCOPE);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Ptyxis/ptyxis-tab.ui");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "ptyxistab");

  gtk_widget_class_bind_template_child (widget_class, PtyxisTab, banner);
  gtk_widget_class_bind_template_child (widget_class, PtyxisTab, pane);
  gtk_widget_class_bind_template_child (widget_class, PtyxisTab, terminal);
  gtk_widget_class_bind_template_child (widget_class, PtyxisTab, scrolled_window);

  gtk_widget_class_bind_template_callback (widget_class, ptyxis_tab_notify_contains_focus_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_tab_notify_window_title_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_tab_notify_window_subtitle_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_tab_increase_font_size_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_tab_decrease_font_size_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_tab_notify_palette_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_tab_bell_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_tab_invalidate_icon);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_tab_invalidate_progress);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_tab_match_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_tab_commit_cb);

  gtk_widget_class_install_action (widget_class, "tab.respawn", NULL, ptyxis_tab_respawn_action);
  gtk_widget_class_install_action (widget_class, "tab.inspect", NULL, ptyxis_tab_inspect_action);
  gtk_widget_class_install_action (widget_class, "tab.focus-pane-next", NULL,
                                   ptyxis_tab_focus_relative_action);
  gtk_widget_class_install_action (widget_class, "tab.focus-pane-previous", NULL,
                                   ptyxis_tab_focus_relative_action);

  g_type_ensure (PTYXIS_TYPE_TERMINAL);
  g_type_ensure (PTYXIS_TYPE_PANE);
}

static void
ptyxis_tab_init (PtyxisTab *self)
{
  GtkEventController *controller;


  gtk_widget_init_template (GTK_WIDGET (self));

  ptyxis_pane_set_terminal (self->pane, self->terminal);
  self->split_root = ptyxis_split_node_new_leaf (G_OBJECT (self->pane));
  self->active_pane = self->pane;

  ptyxis_tab_notify_init (&self->notify, self);

  controller = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
  g_signal_connect (controller,
                    "scroll",
                    G_CALLBACK (on_scroll_scrolled_cb),
                    self);
  g_signal_connect (controller,
                    "scroll-begin",
                    G_CALLBACK (on_scroll_begin_cb),
                    self);
  g_signal_connect (controller,
                    "scroll-end",
                    G_CALLBACK (on_scroll_end_cb),
                    self);
  gtk_widget_add_controller (GTK_WIDGET (self), controller);

  /* Ensure we redraw when the dark-mode changes so that if the user
   * goes to the tab-overview all the tabs look correct.
   */
  g_signal_connect_object (adw_style_manager_get_default (),
                           "notify::dark",
                           G_CALLBACK (ptyxis_tab_invalidate_thumbnail),
                           self,
                           G_CONNECT_SWAPPED);
}

PtyxisTab *
ptyxis_tab_new (PtyxisProfile *profile)
{
  g_return_val_if_fail (PTYXIS_IS_PROFILE (profile), NULL);

  return g_object_new (PTYXIS_TYPE_TAB,
                       "profile", profile,
                       NULL);
}

PtyxisPane *
ptyxis_tab_get_active_pane (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);
  return self->active_pane;
}

void
ptyxis_tab_set_active_pane (PtyxisTab  *self,
                            PtyxisPane *pane)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));
  g_return_if_fail (PTYXIS_IS_PANE (pane));
  g_return_if_fail (ptyxis_split_node_find_pane (self->split_root, G_OBJECT (pane)) != NULL);

  if (self->active_pane != pane)
    {
      self->active_pane = pane;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_PANE]);
    }
}

PtyxisSplitNode *
ptyxis_tab_get_split_root (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);
  return self->split_root;
}

/**
 * ptyxis_tab_get_profile:
 * @self: a #PtyxisTab
 *
 * Gets the profile used by the tab.
 *
 * Returns: (transfer none) (not nullable): a #PtyxisProfile
 */
PtyxisProfile *
ptyxis_tab_get_profile (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  /* GtkBuilder may evaluate bindings in the terminal subtree before the
   * template child pointer for the containing pane has been assigned.
   * The construct-only profile property is applied after instance init and
   * will notify the binding once the pane is available.
   */
  if (self->active_pane == NULL)
    return NULL;

  return ptyxis_pane_get_profile (self->active_pane);
}

/**
 * ptyxis_tab_apply_profile:
 * @self: a #PtyxisTab
 * @new_profile: a #PtyxisProfile to apply
 *
 * Applies a profile to the tab by replacing the tab's profile reference
 * with @new_profile. The tab will share the profile with other tabs,
 * so when the profile is edited in preferences, all tabs using it will
 * be updated automatically.
 */
void
ptyxis_tab_apply_profile (PtyxisTab     *self,
                          PtyxisProfile *new_profile)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));
  g_return_if_fail (PTYXIS_IS_PROFILE (new_profile));

  /* Don't do anything if it's already the same profile */
  if (ptyxis_tab_get_profile (self) == new_profile)
    return;

  /* Replace the profile with the selected one. */
  ptyxis_pane_set_profile (self->active_pane, new_profile);
  g_signal_group_set_target (ptyxis_pane_get_profile_signals (self->active_pane), new_profile);

  /* Notify that the profile property changed */
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROFILE]);
}

const char *
ptyxis_tab_get_title_prefix (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  return ptyxis_pane_get_title_prefix (self->pane) ?: "";
}

void
ptyxis_tab_set_title_prefix (PtyxisTab  *self,
                             const char *title_prefix)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  if (ptyxis_str_empty0 (title_prefix))
    title_prefix = NULL;

  if (g_strcmp0 (ptyxis_pane_get_title_prefix (self->pane), title_prefix) != 0)
    {
      ptyxis_pane_set_title_prefix (self->pane, title_prefix);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE_PREFIX]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
    }
}

char *
ptyxis_tab_dup_title (PtyxisTab *self)
{
  GString *gstr;

  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  gstr = g_string_new (ptyxis_pane_get_title_prefix (self->pane));

  if (!ptyxis_pane_get_ignore_osc_title (self->pane))
    {
      const char *window_title;

      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        window_title = vte_terminal_get_window_title (VTE_TERMINAL (self->terminal));
      G_GNUC_END_IGNORE_DEPRECATIONS

      if (window_title && window_title[0])
        g_string_append (gstr, window_title);
      else if (ptyxis_pane_get_command (self->pane) != NULL &&
               ptyxis_pane_get_command (self->pane)[0] != NULL)
        g_string_append (gstr, ptyxis_pane_get_command (self->pane)[0]);
      else if (ptyxis_pane_get_initial_title (self->pane) != NULL)
        g_string_append (gstr, ptyxis_pane_get_initial_title (self->pane));
    }

  if (gstr->len == 0)
    g_string_append (gstr, _("Terminal"));

  if (ptyxis_pane_get_state (self->pane) == PTYXIS_PANE_STATE_EXITED)
    g_string_append_printf (gstr, " (%s)", _("Exited"));
  else if (ptyxis_pane_get_state (self->pane) == PTYXIS_PANE_STATE_FAILED)
    g_string_append_printf (gstr, " (%s)", _("Failed"));
  else if (ptyxis_pane_get_has_foreground_process (self->pane) &&
           !ptyxis_str_empty0 (ptyxis_pane_get_command_line (self->pane)) &&
           !ptyxis_str_empty0 (ptyxis_pane_get_program_name (self->pane)) &&
           !ptyxis_is_shell (ptyxis_pane_get_program_name (self->pane)))
    g_string_append_printf (gstr, " — %s", ptyxis_pane_get_command_line (self->pane));

  return g_string_free (gstr, FALSE);
}

static char *
ptyxis_tab_collapse_uri (const char *uri)
{
  g_autoptr(GFile) file = NULL;

  if (uri == NULL)
    return NULL;

  if (!(file = g_file_new_for_uri (uri)))
    return NULL;

  if (g_file_is_native (file))
    return ptyxis_path_collapse (g_file_peek_path (file));

  return strdup (uri);
}

char *
ptyxis_tab_dup_subtitle (PtyxisTab *self)
{
  g_autofree char *current_directory_uri = NULL;
  g_autofree char *current_file_uri = NULL;

  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  current_file_uri = ptyxis_terminal_dup_current_file_uri (self->terminal);
  if (current_file_uri != NULL && current_file_uri[0] != 0)
    return ptyxis_tab_collapse_uri (current_file_uri);

  current_directory_uri = ptyxis_terminal_dup_current_directory_uri (self->terminal);
  if (current_directory_uri != NULL && current_directory_uri[0] != 0)
    return ptyxis_tab_collapse_uri (current_directory_uri);

  return g_strdup ("");
}

char *
ptyxis_tab_dup_current_directory_uri (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  return ptyxis_terminal_dup_current_directory_uri (self->terminal);
}

void
ptyxis_tab_set_initial_working_directory_uri (PtyxisTab  *self,
                                              const char *initial_working_directory_uri)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  ptyxis_pane_set_initial_working_directory_uri (self->pane, initial_working_directory_uri);
}

char *
ptyxis_tab_dup_previous_working_directory_uri (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  return g_strdup (ptyxis_pane_get_previous_working_directory_uri (self->pane));
}


void
ptyxis_tab_set_previous_working_directory_uri (PtyxisTab  *self,
                                               const char *previous_working_directory_uri)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  ptyxis_pane_set_previous_working_directory_uri (self->pane, previous_working_directory_uri);
}

static void
ptyxis_tab_apply_zoom (PtyxisTab *self)
{
  g_assert (PTYXIS_IS_TAB (self));

  vte_terminal_set_font_scale (VTE_TERMINAL (ptyxis_pane_get_terminal (self->active_pane)),
                               zoom_font_scales[ptyxis_pane_get_zoom (self->active_pane)]);
}

PtyxisZoomLevel
ptyxis_tab_get_zoom (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), 0);

  return ptyxis_pane_get_zoom (self->active_pane);
}

void
ptyxis_tab_set_zoom (PtyxisTab       *self,
                     PtyxisZoomLevel  zoom)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));
  g_return_if_fail (zoom >= PTYXIS_ZOOM_LEVEL_MINUS_14 &&
                    zoom <= PTYXIS_ZOOM_LEVEL_PLUS_14);

  if (zoom != ptyxis_pane_get_zoom (self->active_pane))
    {
      ptyxis_pane_set_zoom (self->active_pane, zoom);
      ptyxis_tab_apply_zoom (self);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ZOOM]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ZOOM_LABEL]);
    }
}

void
ptyxis_tab_zoom_in (PtyxisTab *self)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  if (ptyxis_tab_get_zoom (self) < PTYXIS_ZOOM_LEVEL_PLUS_14)
    ptyxis_tab_set_zoom (self, ptyxis_tab_get_zoom (self) + 1);
}

void
ptyxis_tab_zoom_out (PtyxisTab *self)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  if (ptyxis_tab_get_zoom (self) > PTYXIS_ZOOM_LEVEL_MINUS_14)
    ptyxis_tab_set_zoom (self, ptyxis_tab_get_zoom (self) - 1);
}

PtyxisTerminal *
ptyxis_tab_get_terminal (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  return ptyxis_pane_get_terminal (self->active_pane);
}

void
ptyxis_tab_raise (PtyxisTab *self)
{
  AdwTabView *tab_view;
  AdwTabPage *tab_page;

  g_return_if_fail (PTYXIS_IS_TAB (self));

  if ((tab_view = ADW_TAB_VIEW (gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_TAB_VIEW))) &&
      (tab_page = adw_tab_view_get_page (tab_view, GTK_WIDGET (self))))
    adw_tab_view_set_selected_page (tab_view, tab_page);
}

typedef struct _Wait
{
  GMainContext *context;
  gboolean completed;
  gboolean success;
} Wait;

static void
ptyxis_tab_poll_agent_sync_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  PtyxisTab *self = (PtyxisTab *)object;
  Wait *wait = user_data;

  g_assert (PTYXIS_IS_TAB (self));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (wait != NULL);

  wait->completed = TRUE;
  wait->success = ptyxis_tab_poll_agent_finish (self, result, NULL);

  g_main_context_wakeup (wait->context);
}

static gboolean
ptyxis_tab_poll_agent (PtyxisTab *self)
{
  Wait wait;

  g_return_val_if_fail (PTYXIS_IS_TAB (self), FALSE);

  wait.context = g_main_context_get_thread_default ();
  wait.completed = FALSE;
  wait.success = FALSE;

  ptyxis_tab_poll_agent_async (self,
                               NULL,
                               ptyxis_tab_poll_agent_sync_cb,
                               &wait);

  while (!wait.completed)
    g_main_context_iteration (wait.context, TRUE);

  return wait.success;
}

/**
 * ptyxis_tab_is_running:
 * @self: a #PtyxisTab
 * @cmdline: (out) (nullable): a location for the command line
 *
 * Returns: %TRUE if there is a command running
 */
gboolean
ptyxis_tab_is_running (PtyxisTab  *self,
                       char      **cmdline)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), FALSE);

  ptyxis_tab_poll_agent (self);

  if (cmdline != NULL)
    *cmdline = g_strdup (ptyxis_pane_get_command_line (self->pane));

  if (ptyxis_pane_get_has_foreground_process (self->pane) && ptyxis_pane_get_program_name (self->pane) != NULL)
    return !ptyxis_is_shell (ptyxis_pane_get_program_name (self->pane));

  return FALSE;
}

static gboolean
ptyxis_tab_force_quit_in_idle (gpointer data)
{
  PtyxisTab *self = data;

  g_assert (PTYXIS_IS_TAB (self));

  if (ptyxis_tab_get_process (self) != NULL)
    ptyxis_tab_send_signal (self, SIGKILL);

  return G_SOURCE_REMOVE;
}

void
ptyxis_tab_force_quit (PtyxisTab *self)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  g_debug ("Forcing tab to quit");

  ptyxis_pane_set_forced_exit (self->pane, TRUE);

  if (ptyxis_tab_get_process (self) == NULL)
    return;

  /* First we try to send SIGHUP so that shells like bash will save their
   * history (See #308).
   */
  ptyxis_tab_send_signal (self, SIGHUP);

  /* In case this was not enough for the process to actually exit, we setup
   * a short timer to send SIGKILL afterwards.
   */
  g_timeout_add_full (G_PRIORITY_HIGH,
                      50,
                      ptyxis_tab_force_quit_in_idle,
                      g_object_ref (self),
                      g_object_unref);
}

PtyxisIpcProcess *
ptyxis_tab_get_process (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  if (self->active_pane == NULL)
    return NULL;

  return ptyxis_pane_get_process (self->active_pane);
}

char *
ptyxis_tab_dup_zoom_label (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), 0);

  if (ptyxis_tab_get_zoom (self) == PTYXIS_ZOOM_LEVEL_DEFAULT)
    return g_strdup ("100%");

  return g_strdup_printf ("%.0lf%%", zoom_font_scales[ptyxis_tab_get_zoom (self)] * 100.0);
}

void
ptyxis_tab_show_banner (PtyxisTab *self)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  gtk_widget_set_visible (GTK_WIDGET (self->banner), TRUE);
}

void
ptyxis_tab_set_needs_attention (PtyxisTab *self,
                                gboolean   needs_attention)
{
  GtkWidget *tab_view;
  AdwTabPage *page;

  g_return_if_fail (PTYXIS_IS_TAB (self));

  if ((tab_view = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_TAB_VIEW)) &&
      (page = adw_tab_view_get_page (ADW_TAB_VIEW (tab_view), GTK_WIDGET (self))))
    adw_tab_page_set_needs_attention (page, needs_attention);
}

const char *
ptyxis_tab_get_uuid (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  return ptyxis_pane_get_uuid (self->pane);
}

PtyxisIpcContainer *
ptyxis_tab_dup_container (PtyxisTab *self)
{
  g_autoptr(PtyxisIpcContainer) container = NULL;
  const char *runtime;
  const char *name;

  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  if ((runtime = ptyxis_terminal_get_current_container_runtime (self->terminal)) &&
      (name = ptyxis_terminal_get_current_container_name (self->terminal)))
    container = ptyxis_application_find_container_by_name (PTYXIS_APPLICATION_DEFAULT, runtime, name);

  if (container == NULL)
    container = ptyxis_pane_dup_container (self->pane);

  return g_steal_pointer (&container);
}

void
ptyxis_tab_set_container (PtyxisTab          *self,
                          PtyxisIpcContainer *container)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));
  g_return_if_fail (!container || PTYXIS_IPC_IS_CONTAINER (container));

  ptyxis_pane_set_container (self->pane, container);
}

static void
ptyxis_tab_poll_agent_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  PtyxisIpcProcess *process = (PtyxisIpcProcess *)object;
  g_autoptr(GTask) task = user_data;
  g_autofree char *the_cmdline = NULL;
  g_autofree char *the_leader_kind = NULL;
  PtyxisProcessLeaderKind leader_kind;
  gboolean has_foreground_process;
  gboolean changed = FALSE;
  gboolean inhibit_changed = FALSE;
  PtyxisTab *self;
  GPid the_pid;

  g_assert (PTYXIS_IPC_IS_PROCESS (process));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);

  g_assert (PTYXIS_IS_TAB (self));

  ptyxis_ipc_process_call_has_foreground_process_finish (process,
                                                         &has_foreground_process,
                                                         &the_pid,
                                                         &the_cmdline,
                                                         &the_leader_kind,
                                                         NULL,
                                                         result,
                                                         NULL);

  if (ptyxis_pane_get_foreground_pid (self->pane) != the_pid)
    {
      changed = TRUE;
      ptyxis_pane_set_foreground_pid (self->pane, the_pid);
    }

  if (ptyxis_pane_get_has_foreground_process (self->pane) != has_foreground_process)
    {
      changed = TRUE;
      inhibit_changed = TRUE;
      ptyxis_pane_set_has_foreground_process (self->pane, has_foreground_process);
    }

  if (g_strcmp0 (the_leader_kind, "superuser") == 0)
    leader_kind = PTYXIS_PROCESS_LEADER_KIND_SUPERUSER;
  else if (g_strcmp0 (the_leader_kind, "container") == 0)
    leader_kind = PTYXIS_PROCESS_LEADER_KIND_CONTAINER;
  else if (g_strcmp0 (the_leader_kind, "remote") == 0)
    leader_kind = PTYXIS_PROCESS_LEADER_KIND_REMOTE;
  else
    leader_kind = PTYXIS_PROCESS_LEADER_KIND_UNKNOWN;

  if (ptyxis_pane_get_process_leader_kind (self->pane) != leader_kind)
    {
      changed = TRUE;
      ptyxis_pane_set_process_leader_kind (self->pane, leader_kind);

      if (!ptyxis_tab_is_active (self))
        ptyxis_tab_set_needs_attention (self, TRUE);

      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROCESS_LEADER_KIND]);
    }

  if (g_strcmp0 (ptyxis_pane_get_command_line (self->pane), the_cmdline) != 0)
    {
      g_autofree char *program_name = NULL;
      const char *space;

      changed = TRUE;
      ptyxis_pane_set_command_line (self->pane, the_cmdline);

      if (the_cmdline != NULL && (space = strchr (the_cmdline, ' ')))
        program_name = g_strndup (the_cmdline, space - the_cmdline);

      if (g_strcmp0 (ptyxis_pane_get_program_name (self->pane), program_name) != 0)
        {
          ptyxis_pane_set_program_name (self->pane, program_name);
          inhibit_changed = TRUE;
        }

      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMMAND_LINE]);
    }

  if (changed)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);

  if (inhibit_changed)
    ptyxis_tab_update_inhibit (self);

  g_task_return_boolean (task, changed);
}

void
ptyxis_tab_poll_agent_async (PtyxisTab           *self,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  g_autoptr(GUnixFDList) fd_list = NULL;
  g_autoptr(GTask) task = NULL;
  VtePty *pty;
  int handle;
  int pty_fd;

  g_assert (PTYXIS_IS_TAB (self));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, ptyxis_tab_poll_agent_async);

  if (ptyxis_tab_get_process (self) == NULL)
    {
      ptyxis_pane_set_has_foreground_process (self->pane, FALSE);
      ptyxis_pane_set_foreground_pid (self->pane, -1);

      if (ptyxis_pane_get_command_line (self->pane) != NULL)
        {
          ptyxis_pane_set_command_line (self->pane, NULL);
          g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMMAND_LINE]);
        }

      if (ptyxis_pane_get_process_leader_kind (self->pane) != PTYXIS_PROCESS_LEADER_KIND_UNKNOWN)
        {
          ptyxis_pane_set_process_leader_kind (self->pane, PTYXIS_PROCESS_LEADER_KIND_UNKNOWN);
          g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROCESS_LEADER_KIND]);
        }

      g_task_return_boolean (task, FALSE);

      return;
    }

  pty = vte_terminal_get_pty (VTE_TERMINAL (self->terminal));
  pty_fd = vte_pty_get_fd (pty);
  fd_list = g_unix_fd_list_new ();
  handle = g_unix_fd_list_append (fd_list, pty_fd, NULL);

  ptyxis_ipc_process_call_has_foreground_process (ptyxis_tab_get_process (self),
                                                  g_variant_new_handle (handle),
                                                  fd_list,
                                                  cancellable,
                                                  ptyxis_tab_poll_agent_cb,
                                                  g_steal_pointer (&task));


}

gboolean
ptyxis_tab_poll_agent_finish (PtyxisTab     *self,
                              GAsyncResult  *result,
                              GError       **error)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

gboolean
ptyxis_tab_has_foreground_process (PtyxisTab  *self,
                                   GPid       *pid,
                                   char      **cmdline)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), FALSE);

  ptyxis_tab_poll_agent (self);

  if (pid != NULL)
    *pid = ptyxis_pane_get_foreground_pid (self->pane);

  if (cmdline != NULL)
    *cmdline = g_strdup (ptyxis_pane_get_command_line (self->pane));

  return ptyxis_pane_get_has_foreground_process (self->pane);
}

void
ptyxis_tab_set_command (PtyxisTab          *self,
                        const char * const *command)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  if (command != NULL && command[0] == NULL)
    command = NULL;

  ptyxis_pane_set_command (self->pane, command);
}

const char *
ptyxis_tab_get_initial_title (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  return ptyxis_pane_get_initial_title (self->pane);
}

void
ptyxis_tab_set_initial_title (PtyxisTab  *self,
                              const char *initial_title)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  ptyxis_pane_set_initial_title (self->pane, initial_title);
}

const char *
ptyxis_tab_get_command_line (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  return ptyxis_pane_get_command_line (self->pane);
}

#ifdef __linux__
static void
ptyxis_tab_toast (PtyxisTab  *self,
                  int         timeout,
                  const char *title)
{
  GtkWidget *overlay = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_TOAST_OVERLAY);
  AdwToast *toast;

  if (overlay == NULL)
    return;

  toast = g_object_new (ADW_TYPE_TOAST,
                        "title", title,
                        "timeout", timeout,
                        NULL);
  adw_toast_overlay_add_toast (ADW_TOAST_OVERLAY (overlay), toast);
}

static void
ptyxis_tab_open_uri_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr(PtyxisTab) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (XDP_IS_PORTAL (object));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (PTYXIS_IS_TAB (self));

  if (!xdp_portal_open_uri_finish (XDP_PORTAL (object), result, &error) &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    ptyxis_tab_toast (self, 3, _("Failed to open link"));
}

void
ptyxis_tab_open_uri (PtyxisTab  *self,
                     const char *uri)
{
  g_autofree char *translated = NULL;
  GtkWindow *window;
  XdpParent *parent;

  g_return_if_fail (PTYXIS_IS_TAB (self));
  g_return_if_fail (uri != NULL);

  window = GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (self)));

  if (g_str_has_prefix (uri, "file://"))
    {
      g_autoptr(PtyxisIpcContainer) container = ptyxis_tab_dup_container (self);
      g_autoptr(GUri) guri = NULL;

      if (container == NULL)
        {
          g_autofree char *default_container = ptyxis_profile_dup_default_container (ptyxis_tab_get_profile (self));
          container = ptyxis_application_lookup_container (PTYXIS_APPLICATION_DEFAULT, default_container);
        }

      if (container != NULL)
        {
          if (ptyxis_ipc_container_call_translate_uri_sync (container, uri, &translated, NULL, NULL))
            uri = translated;
        }

      if (ptyxis_get_process_kind () == PTYXIS_PROCESS_KIND_FLATPAK &&
          (guri = g_uri_parse (uri, 0, NULL)) &&
          !g_str_has_prefix (g_uri_get_path (guri), g_get_home_dir ()))
        {
          const char *path = g_uri_get_path (guri);
          g_autofree char *new_path = g_build_filename ("/var/run/host", path, NULL);
          g_autoptr(GUri) rewritten = NULL;

          rewritten = g_uri_build (0,
                                   "file",
                                   g_uri_get_userinfo (guri),
                                   g_uri_get_host (guri),
                                   g_uri_get_port (guri),
                                   new_path,
                                   g_uri_get_query (guri),
                                   g_uri_get_fragment (guri));

          g_clear_pointer (&translated, g_free);
          uri = translated = g_uri_to_string (rewritten);
        }
    }
  else if (!g_utf8_strchr (uri, -1, ':') && g_utf8_strchr (uri, -1, '@'))
    {
      uri = translated = g_strconcat ("mailto:", uri, NULL);
    }

  if (portal == NULL)
    portal = xdp_portal_new ();

  parent = xdp_parent_new_gtk (window);
  xdp_portal_open_uri (portal,
                       parent,
                       uri,
                       XDP_OPEN_URI_FLAG_NONE,
                       NULL,
                       ptyxis_tab_open_uri_cb,
                       g_object_ref (self));
  xdp_parent_free (parent);
}
#else
void
ptyxis_tab_open_uri (PtyxisTab  *self,
                     const char *uri)
{
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  gtk_show_uri (GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (self))), uri, 0);
  G_GNUC_END_IGNORE_DEPRECATIONS
}
#endif

char *
ptyxis_tab_query_working_directory_from_agent (PtyxisTab *self)
{
  g_autofree char *path = NULL;
  g_autoptr(GUnixFDList) fd_list = NULL;
  VtePty *pty;
  int pty_fd;
  int handle;

  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  if (ptyxis_tab_get_process (self) == NULL)
    return NULL;

  pty = vte_terminal_get_pty (VTE_TERMINAL (self->terminal));
  pty_fd = vte_pty_get_fd (pty);
  fd_list = g_unix_fd_list_new ();
  handle = g_unix_fd_list_append (fd_list, pty_fd, NULL);

  if (ptyxis_ipc_process_call_get_working_directory_sync (ptyxis_tab_get_process (self),
                                                          g_variant_new_handle (handle),
                                                          fd_list,
                                                          &path,
                                                          NULL, NULL, NULL))
    return g_steal_pointer (&path);

  return NULL;
}

PtyxisTabProgress
ptyxis_tab_get_progress (PtyxisTab *self)
{
  gint64 state;

  g_return_val_if_fail (PTYXIS_IS_TAB (self), 0);

  if (vte_terminal_get_termprop_int_by_id (VTE_TERMINAL (self->terminal),
                                           VTE_PROPERTY_ID_PROGRESS_HINT,
                                           &state))
    {
      switch (state)
        {
        case VTE_PROGRESS_HINT_ACTIVE:
          return PTYXIS_TAB_PROGRESS_ACTIVE;

        case VTE_PROGRESS_HINT_ERROR:
          return PTYXIS_TAB_PROGRESS_ERROR;

        case VTE_PROGRESS_HINT_PAUSED:
        case VTE_PROGRESS_HINT_INDETERMINATE:
        default:
          return PTYXIS_TAB_PROGRESS_INDETERMINATE;
        }
    }

  return PTYXIS_TAB_PROGRESS_INDETERMINATE;
}

double
ptyxis_tab_get_progress_fraction (PtyxisTab *self)
{
  guint64 value;

  g_return_val_if_fail (PTYXIS_IS_TAB (self), .0);

  if (ptyxis_tab_get_progress (self) != PTYXIS_TAB_PROGRESS_ACTIVE ||
      !vte_terminal_get_termprop_uint_by_id (VTE_TERMINAL (self->terminal),
                                             VTE_PROPERTY_ID_PROGRESS_VALUE,
                                             &value))
    return .0;

  return MIN (value, 100) / 100.0;
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static void
draw_progress (cairo_t         *cr,
               GtkStyleContext *style_context,
               int              width,
               int              height,
               double           progress)
{
  GdkRGBA rgba;
  double alpha;

  g_assert (cr != NULL);
  g_assert (style_context != NULL);

  progress = CLAMP (progress, 0, 1);

  gtk_style_context_get_color (style_context, &rgba);

  alpha = rgba.alpha;
  rgba.alpha *= .15;
  gdk_cairo_set_source_rgba (cr, &rgba);

  cairo_arc (cr,
             width / 2,
             height / 2,
             width / 2,
             0.0,
             2 * M_PI);
  cairo_fill (cr);

  if (progress > 0.0)
    {
      rgba.alpha = alpha;
      gdk_cairo_set_source_rgba (cr, &rgba);

      cairo_arc (cr,
                 width / 2,
                 height / 2,
                 width / 2,
                 (-.5 * M_PI),
                 (2 * progress * M_PI) - (.5 * M_PI));

      if (progress != 1.0)
        {
          cairo_line_to (cr, width / 2, height / 2);
          cairo_line_to (cr, width / 2, 0);
        }

      cairo_fill (cr);
    }
}
G_GNUC_END_IGNORE_DEPRECATIONS

/**
 * ptyxis_tab_dup_indicator_icon:
 * @self: a #PtyxisTab
 *
 * Gets the progress indicator icon.
 *
 * Due to libadwaita not providing a way to do progress natively (as of 1.6)
 * this uses indicator icon to generate a progress icon using a drawing.
 *
 * Returns: (transfer full) (nullable): a #GIcon or %NULL
 */
GIcon *
ptyxis_tab_dup_indicator_icon (PtyxisTab *self)
{
  PtyxisTabProgress progress;

  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  progress = ptyxis_tab_get_progress (self);

  if (progress == PTYXIS_TAB_PROGRESS_ERROR)
    return g_themed_icon_new ("dialog-error-symbolic");

  if (progress == PTYXIS_TAB_PROGRESS_INDETERMINATE)
    return NULL;

  if (progress == PTYXIS_TAB_PROGRESS_ACTIVE)
    {
      g_autoptr(GdkTexture) texture = NULL;
      g_autoptr(GBytes) bytes = NULL;
      cairo_surface_t *surface;
      cairo_t *cr;
      double fraction;
      int stride;
      int scale;
      int width;
      int height;

      fraction = ptyxis_tab_get_progress_fraction (self);
      scale = gtk_widget_get_scale_factor (GTK_WIDGET (self));
      width = 16 * scale;
      height = 16 * scale;

      surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
      stride = cairo_image_surface_get_stride (surface);
      cr = cairo_create (surface);

      G_GNUC_BEGIN_IGNORE_DEPRECATIONS {
        GtkStyleContext *style_context = gtk_widget_get_style_context (GTK_WIDGET (self));
        draw_progress (cr, style_context, width, height, fraction);
      } G_GNUC_END_IGNORE_DEPRECATIONS

      cairo_destroy (cr);

      bytes = g_bytes_new (cairo_image_surface_get_data (surface), height * stride);
      texture = gdk_memory_texture_new (width, height, GDK_MEMORY_DEFAULT, bytes, stride);

      cairo_surface_destroy (surface);

      return G_ICON (g_steal_pointer (&texture));
    }

  return NULL;
}

gboolean
ptyxis_tab_get_ignore_osc_title (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), FALSE);

  return ptyxis_pane_get_ignore_osc_title (self->pane);
}

void
ptyxis_tab_set_ignore_osc_title (PtyxisTab *self,
                                 gboolean   ignore_osc_title)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  ignore_osc_title = !!ignore_osc_title;

  if (ignore_osc_title != ptyxis_pane_get_ignore_osc_title (self->pane))
    {
      ptyxis_pane_set_ignore_osc_title (self->pane, ignore_osc_title);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IGNORE_OSC_TITLE]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
    }
}

void
_ptyxis_tab_ignore_snapshot (PtyxisTab *self)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  self->ignore_snapshot = TRUE;
}

void
ptyxis_tab_grab_focus (PtyxisTab *self)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  gtk_widget_grab_focus (GTK_WIDGET (self->terminal));
}
