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
#include <math.h>

#ifdef __linux__
# include <libportal/portal.h>
# include <libportal-gtk4/portal-gtk4.h>
#endif

#include "ptyxis-agent-ipc.h"
#include "ptyxis-application.h"
#include "ptyxis-close-dialog.h"
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
  char                    *uuid;

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
static void ptyxis_tab_respawn_pane (PtyxisTab *self, PtyxisPane *pane);
static void ptyxis_tab_apply_zoom (PtyxisTab *self);
static void ptyxis_tab_remove_pane (PtyxisTab *self, PtyxisPane *pane);

static void
ptyxis_tab_update_pane_accessibility (PtyxisTab *self)
{
  guint n_panes;

  g_assert (PTYXIS_IS_TAB (self));

  n_panes = ptyxis_split_node_count_leaves (self->split_root);
  for (guint i = 0; i < n_panes; i++)
    {
      PtyxisSplitNode *leaf = ptyxis_split_node_get_nth_leaf (self->split_root, i);
      PtyxisPane *pane = PTYXIS_PANE (ptyxis_split_node_get_pane (leaf));
      g_autofree char *label = g_strdup_printf (_("Terminal Pane %u of %u"), i + 1, n_panes);

      gtk_accessible_update_property (GTK_ACCESSIBLE (pane),
                                      GTK_ACCESSIBLE_PROPERTY_LABEL, label,
                                      -1);
    }
}
static void ptyxis_tab_update_scrollback_lines (PtyxisTab *self);
static void ptyxis_tab_update_cell_height_scale (PtyxisTab *self);
static void ptyxis_tab_update_cell_width_scale (PtyxisTab *self);
static void ptyxis_tab_update_custom_links (PtyxisTab *self);
static gboolean ptyxis_tab_active_pane_is_running (PtyxisTab *self, char **cmdline);

static void
ptyxis_tab_update_split_sizing (PtyxisTab *self)
{
  guint n_panes;
  gboolean propagate_natural;

  g_assert (PTYXIS_IS_TAB (self));

  n_panes = ptyxis_split_node_count_leaves (self->split_root);
  propagate_natural = n_panes == 1;

  for (guint i = 0; i < n_panes; i++)
    {
      PtyxisSplitNode *leaf = ptyxis_split_node_get_nth_leaf (self->split_root, i);
      PtyxisPane *pane = PTYXIS_PANE (ptyxis_split_node_get_pane (leaf));
      GtkScrolledWindow *scroller = ptyxis_pane_get_scrolled_window (pane);

      gtk_scrolled_window_set_propagate_natural_width (scroller, propagate_natural);
      gtk_scrolled_window_set_propagate_natural_height (scroller, propagate_natural);
    }
}
static void ptyxis_tab_profile_signals_bind_cb (PtyxisTab     *self,
                                                PtyxisProfile *profile,
                                                GSignalGroup  *group);
static void ptyxis_tab_commit_cb (PtyxisTab *, const char *, guint, PtyxisTerminal *);
static void ptyxis_tab_notify_palette_cb (PtyxisTab *, GParamSpec *, PtyxisTerminal *);
static void ptyxis_tab_notify_window_title_cb (PtyxisTab *, GParamSpec *, PtyxisTerminal *);
static void ptyxis_tab_notify_window_subtitle_cb (PtyxisTab *, PtyxisTerminal *);
static void ptyxis_tab_decrease_font_size_cb (PtyxisTab *, PtyxisTerminal *);
static void ptyxis_tab_increase_font_size_cb (PtyxisTab *, PtyxisTerminal *);
static void ptyxis_tab_bell_cb (PtyxisTab *, PtyxisTerminal *);
static void ptyxis_tab_invalidate_icon (PtyxisTab *);
static void ptyxis_tab_invalidate_progress (PtyxisTab *);
static gboolean ptyxis_tab_match_clicked_cb (PtyxisTab *, double, double, int,
                                             GdkModifierType, const char *, PtyxisTerminal *);

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

static void
ptyxis_tab_focus_direction_action (GtkWidget  *widget,
                                   const char *action_name,
                                   GVariant   *params)
{
  PtyxisTab *self = PTYXIS_TAB (widget);
  graphene_rect_t active_bounds;
  PtyxisPane *best_pane = NULL;
  double best_score = G_MAXDOUBLE;
  double active_x;
  double active_y;
  guint n_panes;

  if (!gtk_widget_compute_bounds (GTK_WIDGET (self->active_pane),
                                  GTK_WIDGET (self),
                                  &active_bounds))
    return;

  active_x = active_bounds.origin.x + active_bounds.size.width / 2.;
  active_y = active_bounds.origin.y + active_bounds.size.height / 2.;
  n_panes = ptyxis_split_node_count_leaves (self->split_root);

  for (guint i = 0; i < n_panes; i++)
    {
      PtyxisSplitNode *leaf = ptyxis_split_node_get_nth_leaf (self->split_root, i);
      PtyxisPane *pane = PTYXIS_PANE (ptyxis_split_node_get_pane (leaf));
      graphene_rect_t bounds;
      double primary;
      double secondary;
      double x;
      double y;

      if (pane == self->active_pane ||
          !gtk_widget_compute_bounds (GTK_WIDGET (pane), GTK_WIDGET (self), &bounds))
        continue;

      x = bounds.origin.x + bounds.size.width / 2.;
      y = bounds.origin.y + bounds.size.height / 2.;

      if (g_str_equal (action_name, "tab.focus-pane-left"))
        primary = active_x - x, secondary = fabs (active_y - y);
      else if (g_str_equal (action_name, "tab.focus-pane-right"))
        primary = x - active_x, secondary = fabs (active_y - y);
      else if (g_str_equal (action_name, "tab.focus-pane-up"))
        primary = active_y - y, secondary = fabs (active_x - x);
      else
        primary = y - active_y, secondary = fabs (active_x - x);

      if (primary > 0 && primary + secondary * 2 < best_score)
        {
          best_score = primary + secondary * 2;
          best_pane = pane;
        }
    }

  if (best_pane != NULL)
    {
      ptyxis_tab_set_active_pane (self, best_pane);
      gtk_widget_grab_focus (GTK_WIDGET (ptyxis_pane_get_terminal (best_pane)));
    }
}

static void
ptyxis_tab_pane_focus_entered_cb (PtyxisTab  *self,
                                  PtyxisPane *pane)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_PANE (pane));

  ptyxis_tab_set_active_pane (self, pane);
}

static void
ptyxis_tab_connect_pane (PtyxisTab  *self,
                         PtyxisPane *pane)
{
  g_autoptr(PtyxisTabMonitor) monitor = NULL;
  PtyxisSettings *settings;
  PtyxisTerminal *terminal;

  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_PANE (pane));

  terminal = ptyxis_pane_get_terminal (pane);
  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);

  g_object_bind_property (settings, "audible-bell",
                          terminal, "audible-bell",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (settings, "cursor-shape",
                          terminal, "cursor-shape",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (settings, "cursor-blink-mode",
                          terminal, "cursor-blink-mode",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (settings, "enable-a11y",
                          terminal, "enable-a11y",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (settings, "font-desc",
                          terminal, "font-desc",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (settings, "text-blink-mode",
                          terminal, "text-blink-mode",
                          G_BINDING_SYNC_CREATE);

  if (ptyxis_pane_get_monitor (pane) == NULL)
    {
      monitor = ptyxis_tab_monitor_new (self, pane);
      ptyxis_pane_set_monitor (pane, monitor);
    }
  g_signal_connect_object (pane, "focus-entered",
                           G_CALLBACK (ptyxis_tab_pane_focus_entered_cb),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (terminal, "commit",
                           G_CALLBACK (ptyxis_tab_commit_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (terminal, "notify::palette",
                           G_CALLBACK (ptyxis_tab_notify_palette_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (terminal, "notify::window-title",
                           G_CALLBACK (ptyxis_tab_notify_window_title_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (terminal, "current-file-uri-changed",
                           G_CALLBACK (ptyxis_tab_notify_window_subtitle_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (terminal, "current-directory-uri-changed",
                           G_CALLBACK (ptyxis_tab_notify_window_subtitle_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (terminal, "decrease-font-size",
                           G_CALLBACK (ptyxis_tab_decrease_font_size_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (terminal, "increase-font-size",
                           G_CALLBACK (ptyxis_tab_increase_font_size_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (terminal, "bell",
                           G_CALLBACK (ptyxis_tab_bell_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (terminal, "termprop-changed::vte.container.name",
                           G_CALLBACK (ptyxis_tab_invalidate_icon), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (terminal, "termprop-changed::vte.container.runtime",
                           G_CALLBACK (ptyxis_tab_invalidate_icon), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (terminal, "termprop-changed::vte.progress.hint",
                           G_CALLBACK (ptyxis_tab_invalidate_progress), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (terminal, "termprop-changed::vte.progress.value",
                           G_CALLBACK (ptyxis_tab_invalidate_progress), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (terminal, "match-clicked",
                           G_CALLBACK (ptyxis_tab_match_clicked_cb), self, G_CONNECT_SWAPPED);

  g_signal_connect_object (ptyxis_pane_get_profile_signals (pane),
                           "bind",
                           G_CALLBACK (ptyxis_tab_profile_signals_bind_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_group_connect_object (ptyxis_pane_get_profile_signals (pane),
                                 "notify::limit-scrollback",
                                 G_CALLBACK (ptyxis_tab_update_scrollback_lines),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (ptyxis_pane_get_profile_signals (pane),
                                 "notify::scrollback-lines",
                                 G_CALLBACK (ptyxis_tab_update_scrollback_lines),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (ptyxis_pane_get_profile_signals (pane),
                                 "notify::cell-height-scale",
                                 G_CALLBACK (ptyxis_tab_update_cell_height_scale),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (ptyxis_pane_get_profile_signals (pane),
                                 "notify::cell-width-scale",
                                 G_CALLBACK (ptyxis_tab_update_cell_width_scale),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (ptyxis_pane_get_profile_signals (pane),
                                 "custom-links-changed",
                                 G_CALLBACK (ptyxis_tab_update_custom_links),
                                 self,
                                 G_CONNECT_SWAPPED);
}

G_DEFINE_FINAL_TYPE (PtyxisTab, ptyxis_tab, GTK_TYPE_WIDGET)

#ifdef __linux__
static XdpPortal *portal;
#endif

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

typedef struct
{
  PtyxisTab *tab;
  PtyxisPane *pane;
} PtyxisTabPaneCall;

static PtyxisTabPaneCall *
ptyxis_tab_pane_call_new (PtyxisTab  *tab,
                          PtyxisPane *pane)
{
  PtyxisTabPaneCall *call = g_new0 (PtyxisTabPaneCall, 1);

  call->tab = g_object_ref (tab);
  call->pane = g_object_ref (pane);
  return call;
}

static void
ptyxis_tab_pane_call_free (PtyxisTabPaneCall *call)
{
  g_clear_object (&call->tab);
  g_clear_object (&call->pane);
  g_free (call);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PtyxisTabPaneCall, ptyxis_tab_pane_call_free)
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

  vte_terminal_set_scrollback_lines (VTE_TERMINAL (ptyxis_pane_get_terminal (self->active_pane)), scrollback_lines);
}

static void
ptyxis_tab_update_cell_height_scale (PtyxisTab *self)
{
  double cell_height_scale = 1.0;

  g_assert (PTYXIS_IS_TAB (self));

  if (ptyxis_profile_get_cell_height_scale (ptyxis_tab_get_profile (self)))
    cell_height_scale = ptyxis_profile_get_cell_height_scale (ptyxis_tab_get_profile (self));

  vte_terminal_set_cell_height_scale (VTE_TERMINAL (ptyxis_pane_get_terminal (self->active_pane)), cell_height_scale);
}

static void
ptyxis_tab_update_cell_width_scale (PtyxisTab *self)
{
  double cell_width_scale = 1.0;

  g_assert (PTYXIS_IS_TAB (self));

  if (ptyxis_profile_get_cell_width_scale (ptyxis_tab_get_profile (self)))
    cell_width_scale = ptyxis_profile_get_cell_width_scale (ptyxis_tab_get_profile (self));

  vte_terminal_set_cell_width_scale (VTE_TERMINAL (ptyxis_pane_get_terminal (self->active_pane)), cell_width_scale);
}

static void
ptyxis_tab_update_custom_links (PtyxisTab *self)
{
  g_autoptr(GListModel) custom_links_list = NULL;

  g_assert (PTYXIS_IS_TAB (self));

  custom_links_list = ptyxis_profile_list_custom_links(ptyxis_tab_get_profile (self));
  ptyxis_terminal_update_custom_links_list(ptyxis_pane_get_terminal (self->active_pane), custom_links_list);
}

static void
ptyxis_tab_update_inhibit_pane (PtyxisTab  *self,
                                PtyxisPane *pane)
{
  PtyxisSettings *settings;
  gboolean inhibit = FALSE;
  GtkWidget *window;

  g_assert (PTYXIS_IS_TAB (self));

  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);

  /* Clear if the user has disabled logout inhibition */
  if (!ptyxis_settings_get_inhibit_logout (settings))
    {
      if (ptyxis_pane_get_inhibit_cookie (pane))
        {
          gtk_application_uninhibit (GTK_APPLICATION (PTYXIS_APPLICATION_DEFAULT),
                                     ptyxis_pane_get_inhibit_cookie (pane));
          ptyxis_pane_set_inhibit_cookie (pane, 0);
        }

      return;
    }

  /* Only inhibit if there's a foreground process running and it's not a shell */
  if (ptyxis_pane_get_has_foreground_process (pane) &&
      ptyxis_pane_get_program_name (pane) != NULL &&
      !ptyxis_is_shell (ptyxis_pane_get_program_name (pane)))
    inhibit = TRUE;

  /* Check if we need to change the inhibit state */
  if ((inhibit && ptyxis_pane_get_inhibit_cookie (pane) != 0) ||
      (!inhibit && ptyxis_pane_get_inhibit_cookie (pane) == 0))
    return;

  /* Get the window to use for the inhibit call */
  window = gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW);

  if (inhibit)
    {
      /* Only inhibit if we have a valid window reference */
      if (window != NULL)
        {
          ptyxis_pane_set_inhibit_cookie (
            pane,
            gtk_application_inhibit (GTK_APPLICATION (PTYXIS_APPLICATION_DEFAULT),
                                     GTK_WINDOW (window),
                                     GTK_APPLICATION_INHIBIT_LOGOUT,
                                     _("A foreground process is running")));
        }
    }
  else
    {
      gtk_application_uninhibit (GTK_APPLICATION (PTYXIS_APPLICATION_DEFAULT),
                                 ptyxis_pane_get_inhibit_cookie (pane));
      ptyxis_pane_set_inhibit_cookie (pane, 0);
    }
}

static void
ptyxis_tab_update_inhibit (PtyxisTab *self)
{
  g_assert (PTYXIS_IS_TAB (self));
  ptyxis_tab_update_inhibit_pane (self, self->active_pane);
}

static void
ptyxis_tab_wait_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  PtyxisApplication *app = (PtyxisApplication *)object;
  g_autoptr(PtyxisTabPaneCall) call = user_data;
  g_autoptr(GError) error = NULL;
  PtyxisTab *self = call->tab;
  PtyxisPane *pane = call->pane;
  AdwBanner *banner = ADW_BANNER (ptyxis_pane_get_banner (pane));
  PtyxisExitAction exit_action;
  PtyxisWindow *window;
  AdwTabPage *page = NULL;
  GtkWidget *tab_view;
  gboolean is_front = FALSE;
  int exit_code;

  g_assert (PTYXIS_IS_APPLICATION (app));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (ptyxis_pane_get_state (pane) == PTYXIS_PANE_STATE_RUNNING);

  ptyxis_pane_set_process (pane, NULL);

  /* Update inhibit state when process exits */
  ptyxis_tab_update_inhibit_pane (self, pane);

  exit_code = ptyxis_application_wait_finish (app, result, &error);

  g_debug ("Process completed with exit-code 0x%x %s",
           exit_code,
           error ? error->message : "");

  if (error == NULL && WIFEXITED (exit_code) && WEXITSTATUS (exit_code) == 0)
    ptyxis_pane_set_state (pane, PTYXIS_PANE_STATE_EXITED);
  else
    ptyxis_pane_set_state (pane, PTYXIS_PANE_STATE_FAILED);

  if (ptyxis_pane_get_forced_exit (pane))
    return;

  if ((window = PTYXIS_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), PTYXIS_TYPE_WINDOW))))
    is_front = self == ptyxis_window_get_active_tab (window);

  if (WIFSIGNALED (exit_code))
    {
      g_autofree char *title = NULL;

      title = g_strdup_printf (_("Process Exited from Signal %d"), WTERMSIG (exit_code));

      adw_banner_set_title (banner, title);
      adw_banner_set_button_label (banner, _("_Restart"));
      gtk_actionable_set_action_name (GTK_ACTIONABLE (banner), "tab.respawn");
      gtk_widget_set_visible (GTK_WIDGET (banner), TRUE);
      return;
    }

  exit_action = ptyxis_profile_get_exit_action (ptyxis_pane_get_profile (pane));
  tab_view = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_TAB_VIEW);

  /* If this was started with something like ptyxis_window_new_for_command()
   * then we just want to exit the application (so allow tab to close).
   */
  if (ptyxis_pane_get_command (pane) != NULL)
    exit_action = PTYXIS_EXIT_ACTION_CLOSE;

  if (ADW_IS_TAB_VIEW (tab_view))
    page = adw_tab_view_get_page (ADW_TAB_VIEW (tab_view), GTK_WIDGET (self));

  /* Always prepare the banner even if we don't show it because we may
   * display it again if the tab is removed from the parking lot and
   * restored into the window.
   */
  adw_banner_set_title (banner, _("Process Exited"));
  adw_banner_set_button_label (banner, _("_Restart"));
  gtk_actionable_set_action_name (GTK_ACTIONABLE (banner), "tab.respawn");

  /* If we took less than .5 a second to spawn and no key has been
   * pressed in the terminal, then treat this as a failed spawn. Don't
   * allow ourselves to auto-close in that case as it's likely an error
   * the user would want to see.
   */
  if ((ptyxis_pane_get_command (pane) == NULL || ptyxis_pane_get_state (pane) == PTYXIS_PANE_STATE_FAILED) &&
      (g_get_monotonic_time () - ptyxis_pane_get_respawn_time (pane)) < (G_USEC_PER_SEC/2) &&
      !ptyxis_tab_monitor_get_has_pressed_key (ptyxis_pane_get_monitor (pane)))
    exit_action = PTYXIS_EXIT_ACTION_NONE;

  switch (exit_action)
    {
    case PTYXIS_EXIT_ACTION_RESTART:
      ptyxis_tab_respawn_pane (self, pane);
      break;

    case PTYXIS_EXIT_ACTION_CLOSE:
      if (ptyxis_split_node_count_leaves (self->split_root) > 1 &&
          ptyxis_split_node_find_pane (self->split_root, G_OBJECT (pane)) != NULL)
        {
          ptyxis_tab_remove_pane (self, pane);
        }
      else if (ADW_IS_TAB_VIEW (tab_view) && ADW_IS_TAB_PAGE (page))
        {
          if (adw_tab_page_get_pinned (page))
            adw_tab_view_set_page_pinned (ADW_TAB_VIEW (tab_view), page, FALSE);
          adw_tab_view_close_page (ADW_TAB_VIEW (tab_view), page);
        }
      break;

    case PTYXIS_EXIT_ACTION_NONE:
      gtk_widget_set_visible (GTK_WIDGET (banner), TRUE);
      if (is_front)
        gtk_widget_child_focus (GTK_WIDGET (banner), GTK_DIR_TAB_FORWARD);
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
  g_autoptr(PtyxisTabPaneCall) call = user_data;
  g_autoptr(GError) error = NULL;
  PtyxisTab *self = call->tab;
  PtyxisPane *pane = call->pane;
  PtyxisTerminal *terminal = ptyxis_pane_get_terminal (pane);
  AdwBanner *banner = ADW_BANNER (ptyxis_pane_get_banner (pane));

  g_assert (PTYXIS_IS_TAB (self));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (ptyxis_pane_get_state (pane) == PTYXIS_PANE_STATE_SPAWNING);

  if (!(process = ptyxis_application_spawn_finish (app, result, &error)))
    {
      const char *profile_uuid = ptyxis_profile_get_uuid (ptyxis_pane_get_profile (pane));

      ptyxis_pane_set_state (pane, PTYXIS_PANE_STATE_FAILED);

      vte_terminal_feed (VTE_TERMINAL (terminal), error->message, -1);
      vte_terminal_feed (VTE_TERMINAL (terminal), "\r\n", -1);

      adw_banner_set_title (banner, _("Failed to launch terminal"));
      adw_banner_set_button_label (banner, _("Edit Profile"));
      gtk_actionable_set_action_target (GTK_ACTIONABLE (banner), "s", profile_uuid);
      gtk_actionable_set_action_name (GTK_ACTIONABLE (banner), "app.edit-profile");
      gtk_widget_set_visible (GTK_WIDGET (banner), TRUE);

      return;
    }

  ptyxis_pane_set_state (pane, PTYXIS_PANE_STATE_RUNNING);
  ptyxis_pane_set_respawn_time (pane, g_get_monotonic_time ());

  ptyxis_pane_set_process (pane, process);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ICON]);

  ptyxis_application_wait_async (app,
                                 process,
                                 NULL,
                                 ptyxis_tab_wait_cb,
                                 ptyxis_tab_pane_call_new (self, pane));
}

static void
ptyxis_tab_respawn_pane (PtyxisTab  *self,
                         PtyxisPane *pane)
{
  g_autofree char *default_container = NULL;
  g_autoptr(PtyxisIpcContainer) container = NULL;
  g_autoptr(PtyxisIpcContainer) container_at_creation = NULL;
  g_autoptr(VtePty) new_pty = NULL;
  PtyxisApplication *app;
  const char *profile_uuid;
  const char *cwd_uri;
  PtyxisTerminal *terminal = ptyxis_pane_get_terminal (pane);
  AdwBanner *banner = ADW_BANNER (ptyxis_pane_get_banner (pane));
  VtePty *pty;

  g_assert (PTYXIS_IS_TAB (self));
  g_assert (ptyxis_pane_get_state (pane) == PTYXIS_PANE_STATE_INITIAL ||
            ptyxis_pane_get_state (pane) == PTYXIS_PANE_STATE_EXITED ||
            ptyxis_pane_get_state (pane) == PTYXIS_PANE_STATE_FAILED);

  gtk_widget_set_visible (GTK_WIDGET (banner), FALSE);

  app = PTYXIS_APPLICATION_DEFAULT;
  profile_uuid = ptyxis_profile_get_uuid (ptyxis_pane_get_profile (pane));
  default_container = ptyxis_profile_dup_default_container (ptyxis_pane_get_profile (pane));

  container_at_creation = ptyxis_pane_dup_container (pane);
  if (container_at_creation != NULL)
    container = g_object_ref (container_at_creation);
  else
    container = ptyxis_application_lookup_container (app, default_container);

  if (container == NULL)
    {
      g_autofree char *title = NULL;

      ptyxis_pane_set_state (pane, PTYXIS_PANE_STATE_FAILED);

      title = g_strdup_printf (_("Cannot locate container “%s”"), default_container);
      adw_banner_set_title (banner, title);
      adw_banner_set_button_label (banner, _("Edit Profile"));
      gtk_actionable_set_action_target (GTK_ACTIONABLE (banner), "s", profile_uuid);
      gtk_actionable_set_action_name (GTK_ACTIONABLE (banner), "app.edit-profile");
      gtk_widget_set_visible (GTK_WIDGET (banner), TRUE);

      return;
    }

  ptyxis_pane_set_state (pane, PTYXIS_PANE_STATE_SPAWNING);

  pty = vte_terminal_get_pty (VTE_TERMINAL (terminal));

  if (pty == NULL)
    {
      g_autoptr(GError) error = NULL;

      new_pty = ptyxis_application_create_pty (PTYXIS_APPLICATION_DEFAULT, &error);

      if (new_pty == NULL)
        {
          ptyxis_pane_set_state (pane, PTYXIS_PANE_STATE_FAILED);

          adw_banner_set_title (banner, _("Failed to create pseudo terminal device"));
          adw_banner_set_button_label (banner, NULL);
          gtk_actionable_set_action_name (GTK_ACTIONABLE (banner), NULL);
          gtk_widget_set_visible (GTK_WIDGET (banner), TRUE);

          return;
        }

      vte_terminal_set_pty (VTE_TERMINAL (terminal), new_pty);

      pty = new_pty;
    }

  cwd_uri = ptyxis_pane_get_previous_working_directory_uri (pane);
  if (ptyxis_pane_get_initial_working_directory_uri (pane))
    cwd_uri = ptyxis_pane_get_initial_working_directory_uri (pane);

  ptyxis_application_spawn_async (PTYXIS_APPLICATION_DEFAULT,
                                  container,
                                  ptyxis_pane_get_profile (pane),
                                  cwd_uri,
                                  pty,
                                  ptyxis_pane_get_command (pane),
                                  NULL,
                                  ptyxis_tab_spawn_cb,
                                  ptyxis_tab_pane_call_new (self, pane));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

static void
ptyxis_tab_respawn (PtyxisTab *self)
{
  g_assert (PTYXIS_IS_TAB (self));
  ptyxis_tab_respawn_pane (self, self->active_pane);
}

static void
ptyxis_tab_respawn_action (GtkWidget  *widget,
                           const char *action_name,
                           GVariant   *params)
{
  PtyxisTab *self = (PtyxisTab *)widget;

  g_assert (PTYXIS_IS_TAB (self));

  if (ptyxis_pane_get_state (self->active_pane) == PTYXIS_PANE_STATE_FAILED ||
      ptyxis_pane_get_state (self->active_pane) == PTYXIS_PANE_STATE_EXITED)
    ptyxis_tab_respawn (self);
}

static void
ptyxis_tab_split_position_changed_cb (GtkPaned        *paned,
                                      GParamSpec      *pspec,
                                      PtyxisSplitNode *node)
{
  GtkOrientation orientation;
  int extent;
  int position;

  g_assert (GTK_IS_PANED (paned));
  g_assert (node != NULL);

  orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (paned));
  extent = orientation == GTK_ORIENTATION_HORIZONTAL
         ? gtk_widget_get_width (GTK_WIDGET (paned))
         : gtk_widget_get_height (GTK_WIDGET (paned));
  position = gtk_paned_get_position (paned);

  if (extent > 1)
    ptyxis_split_node_set_ratio (node, (double)position / extent);
}

static gboolean
ptyxis_tab_apply_split_ratio_cb (GtkWidget     *widget,
                                 GdkFrameClock *frame_clock,
                                 gpointer       user_data)
{
  PtyxisSplitNode *node = user_data;
  GtkOrientation orientation;
  int extent;

  orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (widget));
  extent = orientation == GTK_ORIENTATION_HORIZONTAL
         ? gtk_widget_get_width (widget)
         : gtk_widget_get_height (widget);

  if (extent <= 1)
    return G_SOURCE_CONTINUE;

  gtk_paned_set_position (GTK_PANED (widget),
                          round (extent * ptyxis_split_node_get_ratio (node)));
  g_signal_connect (widget,
                    "notify::position",
                    G_CALLBACK (ptyxis_tab_split_position_changed_cb),
                    node);
  return G_SOURCE_REMOVE;
}

static PtyxisPane *
ptyxis_tab_split_pane (PtyxisTab            *self,
                       PtyxisPane           *source,
                       PtyxisSplitDirection  direction,
                       double                ratio)
{
  g_autoptr(PtyxisPane) new_pane = NULL;
  PtyxisSplitNode *leaf;
  GtkWidget *old_parent;
  GtkWidget *paned;
  gboolean was_start = FALSE;

  leaf = ptyxis_split_node_find_pane (self->split_root, G_OBJECT (source));
  g_return_val_if_fail (leaf != NULL, NULL);

  new_pane = ptyxis_pane_new_for_split (source);
  g_object_ref_sink (new_pane);
  ptyxis_tab_connect_pane (self, new_pane);

  old_parent = gtk_widget_get_parent (GTK_WIDGET (source));
  g_object_ref (source);
  if (GTK_IS_PANED (old_parent))
    {
      was_start = gtk_paned_get_start_child (GTK_PANED (old_parent)) == GTK_WIDGET (source);
      if (was_start)
        gtk_paned_set_start_child (GTK_PANED (old_parent), NULL);
      else
        gtk_paned_set_end_child (GTK_PANED (old_parent), NULL);
    }
  else
    gtk_widget_unparent (GTK_WIDGET (source));

  paned = gtk_paned_new (direction == PTYXIS_SPLIT_HORIZONTAL
                         ? GTK_ORIENTATION_HORIZONTAL
                         : GTK_ORIENTATION_VERTICAL);
  gtk_paned_set_start_child (GTK_PANED (paned), GTK_WIDGET (source));
  gtk_paned_set_end_child (GTK_PANED (paned), GTK_WIDGET (new_pane));

  if (GTK_IS_PANED (old_parent))
    {
      if (was_start)
        gtk_paned_set_start_child (GTK_PANED (old_parent), paned);
      else
        gtk_paned_set_end_child (GTK_PANED (old_parent), paned);
    }
  else
    gtk_widget_set_parent (paned, GTK_WIDGET (self));
  g_object_unref (source);

  ptyxis_split_node_split (leaf, direction, ratio, G_OBJECT (new_pane));
  gtk_widget_add_tick_callback (paned, ptyxis_tab_apply_split_ratio_cb, leaf, NULL);
  ptyxis_tab_update_split_sizing (self);
  ptyxis_tab_update_pane_accessibility (self);

  return g_steal_pointer (&new_pane);
}

static void
ptyxis_tab_split_action (GtkWidget  *widget,
                         const char *action_name,
                         GVariant   *params)
{
  PtyxisTab *self = PTYXIS_TAB (widget);
  g_autoptr(PtyxisPane) new_pane = NULL;
  PtyxisSplitDirection direction;

  if (PTYXIS_IS_WINDOW (gtk_widget_get_root (widget)) &&
      ptyxis_window_get_single_terminal_mode (PTYXIS_WINDOW (gtk_widget_get_root (widget))))
    return;

  if (g_str_equal (action_name, "tab.split-auto"))
    direction = gtk_widget_get_width (GTK_WIDGET (self->active_pane)) >=
                gtk_widget_get_height (GTK_WIDGET (self->active_pane))
              ? PTYXIS_SPLIT_HORIZONTAL
              : PTYXIS_SPLIT_VERTICAL;
  else
    direction = g_str_equal (action_name, "tab.split-horizontal")
              ? PTYXIS_SPLIT_HORIZONTAL
              : PTYXIS_SPLIT_VERTICAL;
  new_pane = ptyxis_tab_split_pane (self, self->active_pane, direction, .5);
  g_return_if_fail (new_pane != NULL);
  ptyxis_tab_set_active_pane (self, new_pane);
  ptyxis_tab_update_scrollback_lines (self);
  ptyxis_tab_update_cell_height_scale (self);
  ptyxis_tab_update_cell_width_scale (self);
  ptyxis_tab_update_custom_links (self);
  ptyxis_tab_apply_zoom (self);
  ptyxis_tab_respawn_pane (self, new_pane);
  gtk_widget_grab_focus (GTK_WIDGET (ptyxis_pane_get_terminal (new_pane)));
}

static void
ptyxis_tab_remove_pane (PtyxisTab  *self,
                        PtyxisPane *pane)
{
  PtyxisSplitNode *leaf;
  PtyxisSplitNode *next;
  GtkPaned *paned;
  GtkWidget *grandparent;
  GtkWidget *sibling;
  gboolean was_start = FALSE;

  leaf = ptyxis_split_node_find_pane (self->split_root, G_OBJECT (pane));
  g_return_if_fail (leaf != NULL && ptyxis_split_node_get_parent (leaf) != NULL);
  next = ptyxis_split_node_get_next_leaf (self->split_root, leaf, FALSE);
  if (next == NULL)
    next = ptyxis_split_node_get_previous_leaf (self->split_root, leaf, FALSE);

  paned = GTK_PANED (gtk_widget_get_parent (GTK_WIDGET (pane)));
  grandparent = gtk_widget_get_parent (GTK_WIDGET (paned));
  sibling = gtk_paned_get_start_child (paned) == GTK_WIDGET (pane)
          ? gtk_paned_get_end_child (paned)
          : gtk_paned_get_start_child (paned);

  /* Move focus outside the GtkPaned before detaching either child. The pane
   * which will survive is still a descendant of this GtkPaned until the
   * collapse is complete, so focusing it here would not clear GtkPaned's
   * last-focus tracking.
   */
  ptyxis_tab_set_active_pane (self, PTYXIS_PANE (ptyxis_split_node_get_pane (next)));
  gtk_widget_set_focusable (GTK_WIDGET (self), TRUE);
  gtk_widget_grab_focus (GTK_WIDGET (self));

  g_object_ref (sibling);
  gtk_paned_set_start_child (paned, NULL);
  gtk_paned_set_end_child (paned, NULL);

  if (GTK_IS_PANED (grandparent))
    {
      was_start = gtk_paned_get_start_child (GTK_PANED (grandparent)) == GTK_WIDGET (paned);
      if (was_start)
        gtk_paned_set_start_child (GTK_PANED (grandparent), NULL);
      else
        gtk_paned_set_end_child (GTK_PANED (grandparent), NULL);
      if (was_start)
        gtk_paned_set_start_child (GTK_PANED (grandparent), sibling);
      else
        gtk_paned_set_end_child (GTK_PANED (grandparent), sibling);
    }
  else
    {
      gtk_widget_unparent (GTK_WIDGET (paned));
      gtk_widget_set_parent (sibling, GTK_WIDGET (self));
    }
  g_object_unref (sibling);

  gtk_widget_grab_focus (GTK_WIDGET (ptyxis_pane_get_terminal (self->active_pane)));
  gtk_widget_set_focusable (GTK_WIDGET (self), FALSE);

  ptyxis_pane_force_quit (pane);
  ptyxis_split_node_remove (leaf);
  ptyxis_tab_update_split_sizing (self);
  ptyxis_tab_update_pane_accessibility (self);
}

static void
ptyxis_tab_close_pane_dialog_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  g_autoptr(PtyxisTabPaneCall) call = user_data;
  g_autoptr(GError) error = NULL;

  if (_ptyxis_close_dialog_run_finish (result, &error))
    ptyxis_tab_remove_pane (call->tab, call->pane);
}

static void
ptyxis_tab_close_pane_action (GtkWidget  *widget,
                              const char *action_name,
                              GVariant   *params)
{
  PtyxisTab *self = PTYXIS_TAB (widget);
  PtyxisSettings *settings;

  if (ptyxis_split_node_count_leaves (self->split_root) == 1)
    {
      GtkWidget *view = gtk_widget_get_ancestor (widget, ADW_TYPE_TAB_VIEW);
      AdwTabPage *page = adw_tab_view_get_page (ADW_TAB_VIEW (view), widget);
      adw_tab_view_close_page (ADW_TAB_VIEW (view), page);
      return;
    }

  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);
  if (ptyxis_tab_active_pane_is_running (self, NULL) && ptyxis_settings_get_prompt_on_close (settings))
    {
      GtkWindow *window = GTK_WINDOW (gtk_widget_get_root (widget));

      _ptyxis_close_dialog_run_for_pane_async (window, self, self->active_pane, NULL,
                                               ptyxis_tab_close_pane_dialog_cb,
                                               ptyxis_tab_pane_call_new (self, self->active_pane));
      return;
    }

  ptyxis_tab_remove_pane (self, self->active_pane);
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

  for (guint i = 0; i < ptyxis_split_node_count_leaves (self->split_root); i++)
    {
      PtyxisSplitNode *leaf = ptyxis_split_node_get_nth_leaf (self->split_root, i);
      PtyxisPane *pane = PTYXIS_PANE (ptyxis_split_node_get_pane (leaf));

      if (ptyxis_pane_get_state (pane) == PTYXIS_PANE_STATE_INITIAL)
        ptyxis_tab_respawn_pane (self, pane);
    }
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
                                           ptyxis_pane_get_uuid (self->active_pane));
    }
}

static void
ptyxis_tab_notify_window_title_cb (PtyxisTab      *self,
                                   GParamSpec     *pspec,
                                   PtyxisTerminal *terminal)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_TERMINAL (terminal));

  if (terminal == self->terminal)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

static void
ptyxis_tab_notify_window_subtitle_cb (PtyxisTab      *self,
                                      PtyxisTerminal *terminal)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_TERMINAL (terminal));

  if (terminal == self->terminal)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBTITLE]);
}

static void
ptyxis_tab_increase_font_size_cb (PtyxisTab      *self,
                                  PtyxisTerminal *terminal)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_TERMINAL (terminal));

  if (terminal == self->terminal)
    ptyxis_tab_zoom_in (self);
}

static void
ptyxis_tab_decrease_font_size_cb (PtyxisTab      *self,
                                  PtyxisTerminal *terminal)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_TERMINAL (terminal));

  if (terminal == self->terminal)
    ptyxis_tab_zoom_out (self);
}

static void
ptyxis_tab_bell_cb (PtyxisTab      *self,
                    PtyxisTerminal *terminal)
{
  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_TERMINAL (terminal));

  if (terminal == self->terminal && ptyxis_tab_is_active (self))
    g_signal_emit (self, signals[BELL], 0);
  else
    ptyxis_tab_set_needs_attention (self, TRUE);
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

  kind = ptyxis_pane_get_process_leader_kind (self->active_pane);

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
            if (!(container = ptyxis_pane_dup_container (self->active_pane)))
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
          GskRenderer *renderer;

          gtk_snapshot_scale (sub_snapshot, scale_factor, scale_factor);
          gtk_snapshot_append_color (sub_snapshot,
                                     &bg,
                                     &GRAPHENE_RECT_INIT (0, 0, width, height));
          GTK_WIDGET_CLASS (ptyxis_tab_parent_class)->snapshot (widget, sub_snapshot);

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
ptyxis_tab_release_inhibits (PtyxisTab *self)
{
  guint n_panes;

  g_assert (PTYXIS_IS_TAB (self));

  if (self->split_root == NULL)
    return;

  n_panes = ptyxis_split_node_count_leaves (self->split_root);
  for (guint i = 0; i < n_panes; i++)
    {
      PtyxisSplitNode *leaf = ptyxis_split_node_get_nth_leaf (self->split_root, i);
      PtyxisPane *pane = PTYXIS_PANE (ptyxis_split_node_get_pane (leaf));
      guint cookie = ptyxis_pane_get_inhibit_cookie (pane);

      if (cookie != 0)
        {
          gtk_application_uninhibit (GTK_APPLICATION (PTYXIS_APPLICATION_DEFAULT), cookie);
          ptyxis_pane_set_inhibit_cookie (pane, 0);
        }
    }
}

static void
ptyxis_tab_unroot (GtkWidget *widget)
{
  PtyxisTab *self = PTYXIS_TAB (widget);

  /* Clear inhibit cookie when widget is unrooted since the window
   * reference may no longer be valid.
   */
  ptyxis_tab_release_inhibits (self);

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

  /* Release application state for every pane before disposing the tree. */
  ptyxis_tab_release_inhibits (self);

  gtk_widget_dispose_template (GTK_WIDGET (self), PTYXIS_TYPE_TAB);

  self->active_pane = NULL;
  g_clear_pointer (&self->split_root, ptyxis_split_node_unref);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  g_clear_object (&self->cached_texture);
  g_clear_pointer (&self->uuid, g_free);
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
      g_value_set_string (value, ptyxis_pane_get_command_line (self->active_pane));
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
      g_value_set_enum (value, ptyxis_pane_get_process_leader_kind (self->active_pane));
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
      g_value_set_boolean (value, ptyxis_pane_get_read_only (self->active_pane));
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
      ptyxis_pane_set_profile (self->active_pane, g_value_get_object (value));
      break;

    case PROP_READ_ONLY:
      ptyxis_pane_set_read_only (self->active_pane, g_value_get_boolean (value));
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

  gtk_widget_class_bind_template_child (widget_class, PtyxisTab, pane);

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
  gtk_widget_class_install_action (widget_class, "tab.focus-pane-left", NULL,
                                   ptyxis_tab_focus_direction_action);
  gtk_widget_class_install_action (widget_class, "tab.focus-pane-right", NULL,
                                   ptyxis_tab_focus_direction_action);
  gtk_widget_class_install_action (widget_class, "tab.focus-pane-up", NULL,
                                   ptyxis_tab_focus_direction_action);
  gtk_widget_class_install_action (widget_class, "tab.focus-pane-down", NULL,
                                   ptyxis_tab_focus_direction_action);
  gtk_widget_class_install_action (widget_class, "tab.split-horizontal", NULL,
                                   ptyxis_tab_split_action);
  gtk_widget_class_install_action (widget_class, "tab.split-vertical", NULL,
                                   ptyxis_tab_split_action);
  gtk_widget_class_install_action (widget_class, "tab.split-auto", NULL,
                                   ptyxis_tab_split_action);
  gtk_widget_class_install_action (widget_class, "tab.close-pane", NULL,
                                   ptyxis_tab_close_pane_action);

  g_type_ensure (PTYXIS_TYPE_TERMINAL);
  g_type_ensure (PTYXIS_TYPE_PANE);
}

static void
ptyxis_tab_init (PtyxisTab *self)
{
  GtkEventController *controller;


  gtk_widget_init_template (GTK_WIDGET (self));

  self->banner = ADW_BANNER (ptyxis_pane_get_banner (self->pane));
  self->terminal = ptyxis_pane_get_terminal (self->pane);
  self->scrolled_window = ptyxis_pane_get_scrolled_window (self->pane);
  self->split_root = ptyxis_split_node_new_leaf (G_OBJECT (self->pane));
  self->active_pane = self->pane;
  self->uuid = g_strdup (ptyxis_pane_get_uuid (self->pane));
  ptyxis_tab_connect_pane (self, self->pane);
  ptyxis_tab_update_pane_accessibility (self);

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
      PtyxisSettings *settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);

      g_object_freeze_notify (G_OBJECT (self));
      self->active_pane = pane;
      self->banner = ADW_BANNER (ptyxis_pane_get_banner (pane));
      self->terminal = ptyxis_pane_get_terminal (pane);
      self->scrolled_window = ptyxis_pane_get_scrolled_window (pane);
      ptyxis_tab_notify_set_terminal (&self->notify, self->terminal);
      ptyxis_tab_update_scrollbar_policy (self);
      ptyxis_tab_update_padding_cb (self, NULL, settings);
      ptyxis_tab_update_word_char_exceptions (self, NULL, settings);
      ptyxis_tab_update_scrollback_lines (self);
      ptyxis_tab_update_cell_height_scale (self);
      ptyxis_tab_update_cell_width_scale (self);
      ptyxis_tab_update_custom_links (self);
      ptyxis_tab_apply_zoom (self);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_PANE]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMMAND_LINE]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ICON]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IGNORE_OSC_TITLE]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INDICATOR_ICON]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROCESS_LEADER_KIND]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROFILE]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS_FRACTION]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_READ_ONLY]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBTITLE]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE_PREFIX]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ZOOM]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ZOOM_LABEL]);
      g_object_thaw_notify (G_OBJECT (self));
    }
}

PtyxisSplitNode *
ptyxis_tab_get_split_root (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);
  return self->split_root;
}

guint
ptyxis_tab_get_n_panes (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), 0);
  return ptyxis_split_node_count_leaves (self->split_root);
}

static void
ptyxis_tab_get_node_grid_size (PtyxisSplitNode *node,
                               guint           *columns,
                               guint           *rows)
{
  if (ptyxis_split_node_is_leaf (node))
    {
      PtyxisPane *pane = PTYXIS_PANE (ptyxis_split_node_get_pane (node));
      VteTerminal *terminal = VTE_TERMINAL (ptyxis_pane_get_terminal (pane));

      *columns = vte_terminal_get_column_count (terminal);
      *rows = vte_terminal_get_row_count (terminal);
    }
  else
    {
      guint first_columns;
      guint first_rows;
      guint second_columns;
      guint second_rows;

      ptyxis_tab_get_node_grid_size (ptyxis_split_node_get_first (node),
                                     &first_columns, &first_rows);
      ptyxis_tab_get_node_grid_size (ptyxis_split_node_get_second (node),
                                     &second_columns, &second_rows);

      if (ptyxis_split_node_get_direction (node) == PTYXIS_SPLIT_HORIZONTAL)
        {
          *columns = first_columns + second_columns;
          *rows = MAX (first_rows, second_rows);
        }
      else
        {
          *columns = MAX (first_columns, second_columns);
          *rows = first_rows + second_rows;
        }
    }
}

void
ptyxis_tab_get_grid_size (PtyxisTab *self,
                          guint     *columns,
                          guint     *rows)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));
  g_return_if_fail (columns != NULL);
  g_return_if_fail (rows != NULL);

  ptyxis_tab_get_node_grid_size (self->split_root, columns, rows);
}

static GVariant *
ptyxis_tab_serialize_node (PtyxisSplitNode *node)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);

  if (ptyxis_split_node_is_leaf (node))
    {
      PtyxisPane *pane = PTYXIS_PANE (ptyxis_split_node_get_pane (node));
      PtyxisTerminal *terminal = ptyxis_pane_get_terminal (pane);
      g_autoptr(PtyxisIpcContainer) container = ptyxis_pane_dup_container (pane);
      g_autofree char *cwd = ptyxis_terminal_dup_current_directory_uri (terminal);
      const char *title;

      if (cwd == NULL)
        cwd = g_strdup (ptyxis_pane_get_previous_working_directory_uri (pane));

      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      title = vte_terminal_get_window_title (VTE_TERMINAL (terminal));
      G_GNUC_END_IGNORE_DEPRECATIONS
      if (ptyxis_str_empty0 (title))
        title = ptyxis_pane_get_initial_title (pane);

      g_variant_builder_add (&builder, "{sv}", "type", g_variant_new_string ("pane"));
      g_variant_builder_add (&builder, "{sv}", "uuid",
                             g_variant_new_string (ptyxis_pane_get_uuid (pane)));
      g_variant_builder_add (&builder, "{sv}", "profile",
                             g_variant_new_string (ptyxis_profile_get_uuid (ptyxis_pane_get_profile (pane))));
      g_variant_builder_add (&builder, "{sv}", "zoom",
                             g_variant_new_uint32 (ptyxis_pane_get_zoom (pane)));

      if (container != NULL)
        g_variant_builder_add (&builder, "{sv}", "container",
                               g_variant_new_string (ptyxis_ipc_container_get_id (container)));
      if (!ptyxis_str_empty0 (cwd))
        g_variant_builder_add (&builder, "{sv}", "cwd", g_variant_new_string (cwd));
      if (!ptyxis_str_empty0 (title))
        g_variant_builder_add (&builder, "{sv}", "window-title", g_variant_new_string (title));
    }
  else
    {
      const char *type = ptyxis_split_node_get_direction (node) == PTYXIS_SPLIT_HORIZONTAL
                       ? "horizontal"
                       : "vertical";

      g_variant_builder_add (&builder, "{sv}", "type", g_variant_new_string (type));
      g_variant_builder_add (&builder, "{sv}", "ratio",
                             g_variant_new_double (ptyxis_split_node_get_ratio (node)));
      g_variant_builder_add (&builder, "{sv}", "first",
                             ptyxis_tab_serialize_node (ptyxis_split_node_get_first (node)));
      g_variant_builder_add (&builder, "{sv}", "second",
                             ptyxis_tab_serialize_node (ptyxis_split_node_get_second (node)));
    }

  return g_variant_ref_sink (g_variant_builder_end (&builder));
}

GVariant *
ptyxis_tab_serialize_layout (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  return ptyxis_tab_serialize_node (self->split_root);
}

typedef struct
{
  const char *active_uuid;
  PtyxisPane *active_pane;
} PtyxisTabRestoreState;

static gboolean
ptyxis_tab_restore_node (PtyxisTab             *self,
                         PtyxisPane            *pane,
                         GVariant              *layout,
                         PtyxisTabRestoreState *state)
{
  const char *type;

  if (!g_variant_lookup (layout, "type", "&s", &type))
    return FALSE;

  if (g_str_equal (type, "pane"))
    {
      g_autoptr(PtyxisIpcContainer) container = NULL;
      g_autoptr(PtyxisProfile) profile = NULL;
      const char *container_id = NULL;
      const char *cwd = NULL;
      const char *profile_uuid = NULL;
      const char *title = NULL;
      const char *uuid = NULL;
      guint32 zoom = PTYXIS_ZOOM_LEVEL_DEFAULT;

      g_variant_lookup (layout, "uuid", "&s", &uuid);
      g_variant_lookup (layout, "profile", "&s", &profile_uuid);
      g_variant_lookup (layout, "container", "&s", &container_id);
      g_variant_lookup (layout, "cwd", "&s", &cwd);
      g_variant_lookup (layout, "window-title", "&s", &title);
      g_variant_lookup (layout, "zoom", "u", &zoom);

      if (!ptyxis_str_empty0 (profile_uuid))
        profile = ptyxis_application_dup_profile (PTYXIS_APPLICATION_DEFAULT, profile_uuid);
      if (profile != NULL)
        {
          ptyxis_pane_set_profile (pane, profile);
          g_signal_group_set_target (ptyxis_pane_get_profile_signals (pane), profile);
        }

      if (!ptyxis_str_empty0 (container_id))
        container = ptyxis_application_lookup_container (PTYXIS_APPLICATION_DEFAULT, container_id);
      if (container != NULL)
        ptyxis_pane_set_container (pane, container);
      if (cwd != NULL)
        ptyxis_pane_set_previous_working_directory_uri (pane, cwd);
      if (title != NULL)
        ptyxis_pane_set_initial_title (pane, title);
      if (zoom > 0 && zoom < PTYXIS_ZOOM_LEVEL_LAST)
        ptyxis_pane_set_zoom (pane, zoom);

      ptyxis_tab_set_active_pane (self, pane);
      ptyxis_tab_update_scrollback_lines (self);
      ptyxis_tab_update_cell_height_scale (self);
      ptyxis_tab_update_cell_width_scale (self);
      ptyxis_tab_update_custom_links (self);
      ptyxis_tab_apply_zoom (self);

      if (state->active_uuid != NULL && g_strcmp0 (uuid, state->active_uuid) == 0)
        state->active_pane = pane;

      return TRUE;
    }
  else
    {
      g_autoptr(GVariant) first = NULL;
      g_autoptr(GVariant) second = NULL;
      g_autoptr(PtyxisPane) second_pane = NULL;
      PtyxisSplitDirection direction;
      double ratio = .5;

      if (g_str_equal (type, "horizontal"))
        direction = PTYXIS_SPLIT_HORIZONTAL;
      else if (g_str_equal (type, "vertical"))
        direction = PTYXIS_SPLIT_VERTICAL;
      else
        return FALSE;

      first = g_variant_lookup_value (layout, "first", G_VARIANT_TYPE_VARDICT);
      second = g_variant_lookup_value (layout, "second", G_VARIANT_TYPE_VARDICT);
      g_variant_lookup (layout, "ratio", "d", &ratio);
      if (first == NULL || second == NULL)
        return FALSE;

      second_pane = ptyxis_tab_split_pane (self, pane, direction, ratio);
      if (second_pane == NULL)
        return FALSE;

      return ptyxis_tab_restore_node (self, pane, first, state) &&
             ptyxis_tab_restore_node (self, second_pane, second, state);
    }
}

gboolean
ptyxis_tab_restore_layout (PtyxisTab  *self,
                           GVariant   *layout,
                           const char *active_pane_uuid)
{
  PtyxisTabRestoreState state = { active_pane_uuid, NULL };

  g_return_val_if_fail (PTYXIS_IS_TAB (self), FALSE);
  g_return_val_if_fail (layout != NULL, FALSE);
  g_return_val_if_fail (g_variant_is_of_type (layout, G_VARIANT_TYPE_VARDICT), FALSE);
  g_return_val_if_fail (ptyxis_tab_get_n_panes (self) == 1, FALSE);

  if (!ptyxis_tab_restore_node (self, self->pane, layout, &state))
    return FALSE;

  if (state.active_pane != NULL)
    ptyxis_tab_set_active_pane (self, state.active_pane);

  return TRUE;
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

  return ptyxis_pane_get_title_prefix (self->active_pane) ?: "";
}

void
ptyxis_tab_set_title_prefix (PtyxisTab  *self,
                             const char *title_prefix)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  if (ptyxis_str_empty0 (title_prefix))
    title_prefix = NULL;

  if (g_strcmp0 (ptyxis_pane_get_title_prefix (self->active_pane), title_prefix) != 0)
    {
      ptyxis_pane_set_title_prefix (self->active_pane, title_prefix);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE_PREFIX]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
    }
}

char *
ptyxis_tab_dup_title (PtyxisTab *self)
{
  GString *gstr;
  PtyxisPane *pane;
  PtyxisTerminal *terminal;

  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  pane = self->active_pane;
  terminal = ptyxis_pane_get_terminal (pane);
  gstr = g_string_new (ptyxis_pane_get_title_prefix (pane));

  if (!ptyxis_pane_get_ignore_osc_title (pane))
    {
      const char *window_title;

      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        window_title = vte_terminal_get_window_title (VTE_TERMINAL (terminal));
      G_GNUC_END_IGNORE_DEPRECATIONS

      if (window_title && window_title[0])
        g_string_append (gstr, window_title);
      else if (ptyxis_pane_get_command (pane) != NULL &&
               ptyxis_pane_get_command (pane)[0] != NULL)
        g_string_append (gstr, ptyxis_pane_get_command (pane)[0]);
      else if (ptyxis_pane_get_initial_title (pane) != NULL)
        g_string_append (gstr, ptyxis_pane_get_initial_title (pane));
    }

  if (gstr->len == 0)
    g_string_append (gstr, _("Terminal"));

  if (ptyxis_pane_get_state (pane) == PTYXIS_PANE_STATE_EXITED)
    g_string_append_printf (gstr, " (%s)", _("Exited"));
  else if (ptyxis_pane_get_state (pane) == PTYXIS_PANE_STATE_FAILED)
    g_string_append_printf (gstr, " (%s)", _("Failed"));
  else if (ptyxis_pane_get_has_foreground_process (pane) &&
           !ptyxis_str_empty0 (ptyxis_pane_get_command_line (pane)) &&
           !ptyxis_str_empty0 (ptyxis_pane_get_program_name (pane)) &&
           !ptyxis_is_shell (ptyxis_pane_get_program_name (pane)))
    g_string_append_printf (gstr, " — %s", ptyxis_pane_get_command_line (pane));

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

  ptyxis_pane_set_initial_working_directory_uri (self->active_pane, initial_working_directory_uri);
}

char *
ptyxis_tab_dup_previous_working_directory_uri (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  return g_strdup (ptyxis_pane_get_previous_working_directory_uri (self->active_pane));
}


void
ptyxis_tab_set_previous_working_directory_uri (PtyxisTab  *self,
                                               const char *previous_working_directory_uri)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  ptyxis_pane_set_previous_working_directory_uri (self->active_pane, previous_working_directory_uri);
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
ptyxis_tab_poll_pane_agent (PtyxisTab  *self,
                            PtyxisPane *pane)
{
  Wait wait;

  g_return_val_if_fail (PTYXIS_IS_TAB (self), FALSE);

  wait.context = g_main_context_get_thread_default ();
  wait.completed = FALSE;
  wait.success = FALSE;

  ptyxis_tab_poll_pane_agent_async (self,
                                    pane,
                                    NULL,
                                    ptyxis_tab_poll_agent_sync_cb,
                                    &wait);

  while (!wait.completed)
    g_main_context_iteration (wait.context, TRUE);

  return wait.success;
}

static gboolean
ptyxis_tab_active_pane_is_running (PtyxisTab  *self,
                                   char      **cmdline)
{
  PtyxisPane *pane;

  g_return_val_if_fail (PTYXIS_IS_TAB (self), FALSE);

  pane = self->active_pane;
  ptyxis_tab_poll_pane_agent (self, pane);

  if (cmdline != NULL)
    *cmdline = g_strdup (ptyxis_pane_get_command_line (pane));

  return ptyxis_pane_get_has_foreground_process (pane) &&
         !ptyxis_str_empty0 (ptyxis_pane_get_program_name (pane)) &&
         !ptyxis_is_shell (ptyxis_pane_get_program_name (pane));
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

  if (cmdline != NULL)
    *cmdline = NULL;

  for (guint i = 0; i < ptyxis_split_node_count_leaves (self->split_root); i++)
    {
      PtyxisSplitNode *leaf = ptyxis_split_node_get_nth_leaf (self->split_root, i);
      PtyxisPane *pane = PTYXIS_PANE (ptyxis_split_node_get_pane (leaf));

      ptyxis_tab_poll_pane_agent (self, pane);

      if (ptyxis_pane_get_has_foreground_process (pane) &&
          !ptyxis_str_empty0 (ptyxis_pane_get_program_name (pane)) &&
          !ptyxis_is_shell (ptyxis_pane_get_program_name (pane)))
        {
          if (cmdline != NULL)
            *cmdline = g_strdup (ptyxis_pane_get_command_line (pane));
          return TRUE;
        }
    }

  return FALSE;
}

GPtrArray *
ptyxis_tab_list_running_panes (PtyxisTab *self)
{
  GPtrArray *panes;

  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  panes = g_ptr_array_new_with_free_func (g_object_unref);

  for (guint i = 0; i < ptyxis_split_node_count_leaves (self->split_root); i++)
    {
      PtyxisSplitNode *leaf = ptyxis_split_node_get_nth_leaf (self->split_root, i);
      PtyxisPane *pane = PTYXIS_PANE (ptyxis_split_node_get_pane (leaf));

      ptyxis_tab_poll_pane_agent (self, pane);

      if (ptyxis_pane_get_has_foreground_process (pane) &&
          !ptyxis_str_empty0 (ptyxis_pane_get_program_name (pane)) &&
          !ptyxis_is_shell (ptyxis_pane_get_program_name (pane)))
        g_ptr_array_add (panes, g_object_ref (pane));
    }

  return panes;
}

void
ptyxis_tab_force_quit (PtyxisTab *self)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  g_debug ("Forcing tab to quit");

  for (guint i = 0; i < ptyxis_split_node_count_leaves (self->split_root); i++)
    {
      PtyxisSplitNode *leaf = ptyxis_split_node_get_nth_leaf (self->split_root, i);
      ptyxis_pane_force_quit (PTYXIS_PANE (ptyxis_split_node_get_pane (leaf)));
    }
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

void
ptyxis_tab_set_search_target_visible (PtyxisTab *self,
                                      gboolean   visible)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  for (guint i = 0; i < ptyxis_split_node_count_leaves (self->split_root); i++)
    {
      PtyxisSplitNode *leaf = ptyxis_split_node_get_nth_leaf (self->split_root, i);
      PtyxisPane *pane = PTYXIS_PANE (ptyxis_split_node_get_pane (leaf));

      if (visible && pane == self->active_pane)
        gtk_widget_add_css_class (GTK_WIDGET (pane), "search-target");
      else
        gtk_widget_remove_css_class (GTK_WIDGET (pane), "search-target");
    }
}

const char *
ptyxis_tab_get_uuid (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  return self->uuid;
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
    container = ptyxis_pane_dup_container (self->active_pane);

  return g_steal_pointer (&container);
}

void
ptyxis_tab_set_container (PtyxisTab          *self,
                          PtyxisIpcContainer *container)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));
  g_return_if_fail (!container || PTYXIS_IPC_IS_CONTAINER (container));

  ptyxis_pane_set_container (self->active_pane, container);
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
  PtyxisPane *pane;
  PtyxisTab *self;
  GPid the_pid;

  g_assert (PTYXIS_IPC_IS_PROCESS (process));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  pane = g_task_get_task_data (task);

  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_PANE (pane));

  ptyxis_ipc_process_call_has_foreground_process_finish (process,
                                                         &has_foreground_process,
                                                         &the_pid,
                                                         &the_cmdline,
                                                         &the_leader_kind,
                                                         NULL,
                                                         result,
                                                         NULL);

  if (ptyxis_pane_get_foreground_pid (pane) != the_pid)
    {
      changed = TRUE;
      ptyxis_pane_set_foreground_pid (pane, the_pid);
    }

  if (ptyxis_pane_get_has_foreground_process (pane) != has_foreground_process)
    {
      changed = TRUE;
      inhibit_changed = TRUE;
      ptyxis_pane_set_has_foreground_process (pane, has_foreground_process);
    }

  if (g_strcmp0 (the_leader_kind, "superuser") == 0)
    leader_kind = PTYXIS_PROCESS_LEADER_KIND_SUPERUSER;
  else if (g_strcmp0 (the_leader_kind, "container") == 0)
    leader_kind = PTYXIS_PROCESS_LEADER_KIND_CONTAINER;
  else if (g_strcmp0 (the_leader_kind, "remote") == 0)
    leader_kind = PTYXIS_PROCESS_LEADER_KIND_REMOTE;
  else
    leader_kind = PTYXIS_PROCESS_LEADER_KIND_UNKNOWN;

  if (ptyxis_pane_get_process_leader_kind (pane) != leader_kind)
    {
      changed = TRUE;
      ptyxis_pane_set_process_leader_kind (pane, leader_kind);

      if (!ptyxis_tab_is_active (self))
        ptyxis_tab_set_needs_attention (self, TRUE);

      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROCESS_LEADER_KIND]);
    }

  if (g_strcmp0 (ptyxis_pane_get_command_line (pane), the_cmdline) != 0)
    {
      g_autofree char *program_name = NULL;
      const char *space;

      changed = TRUE;
      ptyxis_pane_set_command_line (pane, the_cmdline);

      if (the_cmdline != NULL && (space = strchr (the_cmdline, ' ')))
        program_name = g_strndup (the_cmdline, space - the_cmdline);

      if (g_strcmp0 (ptyxis_pane_get_program_name (pane), program_name) != 0)
        {
          ptyxis_pane_set_program_name (pane, program_name);
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
ptyxis_tab_poll_pane_agent_async (PtyxisTab           *self,
                                  PtyxisPane          *pane,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  g_autoptr(GUnixFDList) fd_list = NULL;
  g_autoptr(GTask) task = NULL;
  PtyxisIpcProcess *process;
  PtyxisTerminal *terminal;
  VtePty *pty;
  int handle;
  int pty_fd;

  g_assert (PTYXIS_IS_TAB (self));
  g_assert (PTYXIS_IS_PANE (pane));
  g_assert (ptyxis_split_node_find_pane (self->split_root, G_OBJECT (pane)) != NULL);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, ptyxis_tab_poll_agent_async);
  terminal = ptyxis_pane_get_terminal (pane);
  process = ptyxis_pane_get_process (pane);
  g_task_set_task_data (task, g_object_ref (pane), g_object_unref);

  if (process == NULL)
    {
      ptyxis_pane_set_has_foreground_process (pane, FALSE);
      ptyxis_pane_set_foreground_pid (pane, -1);

      if (ptyxis_pane_get_command_line (pane) != NULL)
        {
          ptyxis_pane_set_command_line (pane, NULL);
          g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMMAND_LINE]);
        }

      if (ptyxis_pane_get_process_leader_kind (pane) != PTYXIS_PROCESS_LEADER_KIND_UNKNOWN)
        {
          ptyxis_pane_set_process_leader_kind (pane, PTYXIS_PROCESS_LEADER_KIND_UNKNOWN);
          g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROCESS_LEADER_KIND]);
        }

      g_task_return_boolean (task, FALSE);

      return;
    }

  pty = vte_terminal_get_pty (VTE_TERMINAL (terminal));
  pty_fd = vte_pty_get_fd (pty);
  fd_list = g_unix_fd_list_new ();
  handle = g_unix_fd_list_append (fd_list, pty_fd, NULL);

  ptyxis_ipc_process_call_has_foreground_process (process,
                                                  g_variant_new_handle (handle),
                                                  fd_list,
                                                  cancellable,
                                                  ptyxis_tab_poll_agent_cb,
                                                  g_steal_pointer (&task));


}

void
ptyxis_tab_poll_agent_async (PtyxisTab           *self,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  ptyxis_tab_poll_pane_agent_async (self, self->active_pane, cancellable, callback, user_data);
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

  ptyxis_tab_poll_pane_agent (self, self->active_pane);

  if (pid != NULL)
    *pid = ptyxis_pane_get_foreground_pid (self->active_pane);

  if (cmdline != NULL)
    *cmdline = g_strdup (ptyxis_pane_get_command_line (self->active_pane));

  return ptyxis_pane_get_has_foreground_process (self->active_pane);
}

void
ptyxis_tab_set_command (PtyxisTab          *self,
                        const char * const *command)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  if (command != NULL && command[0] == NULL)
    command = NULL;

  ptyxis_pane_set_command (self->active_pane, command);
}

const char *
ptyxis_tab_get_initial_title (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  return ptyxis_pane_get_initial_title (self->active_pane);
}

void
ptyxis_tab_set_initial_title (PtyxisTab  *self,
                              const char *initial_title)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  ptyxis_pane_set_initial_title (self->active_pane, initial_title);
}

const char *
ptyxis_tab_get_command_line (PtyxisTab *self)
{
  g_return_val_if_fail (PTYXIS_IS_TAB (self), NULL);

  return ptyxis_pane_get_command_line (self->active_pane);
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

  return ptyxis_pane_get_ignore_osc_title (self->active_pane);
}

void
ptyxis_tab_set_ignore_osc_title (PtyxisTab *self,
                                 gboolean   ignore_osc_title)
{
  g_return_if_fail (PTYXIS_IS_TAB (self));

  ignore_osc_title = !!ignore_osc_title;

  if (ignore_osc_title != ptyxis_pane_get_ignore_osc_title (self->active_pane))
    {
      ptyxis_pane_set_ignore_osc_title (self->active_pane, ignore_osc_title);
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
