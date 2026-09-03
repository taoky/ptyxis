/* ptyxis-window.c
 *
 * Copyright 2023 Christian Hergert
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

#include "ptyxis-application.h"
#include "ptyxis-close-dialog.h"
#include "ptyxis-find-bar.h"
#include "ptyxis-fullscreen-box.h"
#include "ptyxis-gated-list-model.h"
#include "ptyxis-menu-row.h"
#include "ptyxis-parking-lot.h"
#include "ptyxis-preferences-window.h"
#include "ptyxis-settings.h"
#include "ptyxis-shrinker.h"
#include "ptyxis-tab-monitor.h"
#include "ptyxis-tab-private.h"
#include "ptyxis-terminal-picker.h"
#include "ptyxis-theme-selector.h"
#include "ptyxis-title-dialog.h"
#include "ptyxis-profile-dialog.h"
#include "ptyxis-util.h"
#include "ptyxis-window.h"
#include "ptyxis-window-dressing.h"

#ifdef GDK_WINDOWING_X11
# include <gdk/x11/gdkx.h>
#endif

struct _PtyxisWindow
{
  AdwApplicationWindow   parent_instance;

  PtyxisShortcuts       *shortcuts;
  PtyxisParkingLot      *parking_lot;

  GtkButton             *new_terminal_button;
  GtkMenuButton         *new_terminal_menu_button;
  GtkSeparator          *new_terminal_separator;
  PtyxisFindBar         *find_bar;
  GtkRevealer           *find_bar_revealer;
  AdwHeaderBar          *header_bar;
  GMenu                 *primary_menu;
  GtkMenuButton         *primary_menu_button;
  AdwTabBar             *tab_bar;
  GMenu                 *tab_menu;
  AdwTabOverview        *tab_overview;
  AdwTabView            *tab_view;
  GtkWidget             *zoom_label;
  GtkWidget             *tab_overview_button;
  GtkWidget             *new_tab_box;
  PtyxisFullscreenBox   *fullscreen_box;
  GtkStack              *menu_search_stack;
  GtkSearchEntry        *menu_search;
  GtkListView           *menu_list_view;
  GtkFilterListModel    *maybe_containers;
  GtkCustomFilter       *maybe_containers_filter;
  GtkFilterListModel    *not_containers;
  GtkCustomFilter       *not_containers_filter;

  GBindingGroup         *profile_bindings;
  GBindingGroup         *active_tab_bindings;
  GSignalGroup          *active_tab_signals;
  GSignalGroup          *selected_page_signals;
  PtyxisWindowDressing  *dressing;
  GtkBox                *visual_bell;
  GPropertyAction       *interface_style_action;

  guint                  visual_bell_source;
  guint                  focus_active_tab_source;

  guint                  tab_overview_animating : 1;
  guint                  disposed : 1;
  guint                  single_terminal_mode : 1;
  guint                  quake_mode : 1;
  guint                  is_maximized : 1;
  guint                  is_fullscreen : 1;
  guint                  in_close_request : 1;
  guint                  reset_nonvisible_from_size_allocate : 1;
};

G_DEFINE_FINAL_TYPE (PtyxisWindow, ptyxis_window, ADW_TYPE_APPLICATION_WINDOW)

enum {
  PROP_0,
  PROP_ACTIVE_TAB,
  PROP_QUAKE_MODE,
  PROP_SHORTCUTS,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
ptyxis_window_save_size (PtyxisWindow *self)
{
  PtyxisTab *active_tab;

  g_assert (PTYXIS_IS_WINDOW (self));

  if (self->quake_mode)
    return;

  if ((active_tab = ptyxis_window_get_active_tab (self)))
    {
      PtyxisSettings *settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);
      guint columns;
      guint rows;

      ptyxis_tab_get_grid_size (active_tab, &columns, &rows);

      ptyxis_settings_set_window_size (settings, columns, rows);
    }
}

static void
ptyxis_window_maybe_reset_default_size (PtyxisWindow *self)
{
  PtyxisTab *active_tab;

  g_assert (PTYXIS_IS_WINDOW (self));

  active_tab = ptyxis_window_get_active_tab (self);
  if (active_tab == NULL || ptyxis_tab_get_n_panes (active_tab) == 1)
    gtk_window_set_default_size (GTK_WINDOW (self), -1, -1);
}

static void
ptyxis_window_close_page_dialog_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  g_autoptr(PtyxisTab) tab = user_data;
  g_autoptr(GError) error = NULL;
  PtyxisWindow *self;
  AdwTabPage *page;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (PTYXIS_IS_TAB (tab));

  self = PTYXIS_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (tab), PTYXIS_TYPE_WINDOW));
  page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (tab));

  if (!_ptyxis_close_dialog_run_finish (result, &error))
    {
      adw_tab_view_close_page_finish (self->tab_view, page, FALSE);
      return;
    }

  ptyxis_parking_lot_push (self->parking_lot, tab);

  /* Ignore snapshot because libadwaita will try to snapshot this when
   * calling adw_tab_view_close_page_finish(). This just skips past it
   * until we maybe get re-added to a view later.
   */
  _ptyxis_tab_ignore_snapshot (tab);

  adw_tab_view_close_page_finish (self->tab_view, page, TRUE);

  /* Resize if we are going from 2->1 tabs */
  if (adw_tab_view_get_n_pages (self->tab_view) == 1)
    ptyxis_window_maybe_reset_default_size (self);
}

static gboolean
ptyxis_window_close_page_cb (PtyxisWindow *self,
                             AdwTabPage   *tab_page,
                             AdwTabView   *tab_view)
{
  g_autoptr(GPtrArray) tabs = NULL;
  PtyxisSettings *settings;
  PtyxisTab *tab;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (ADW_IS_TAB_PAGE (tab_page));
  g_assert (ADW_IS_TAB_VIEW (tab_view));

  ptyxis_window_save_size (self);

  tab = PTYXIS_TAB (adw_tab_page_get_child (tab_page));
  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);

  /* If we are disposed, allow closing immediately */
  if (self->disposed)
    return GDK_EVENT_PROPAGATE;

  if (!ptyxis_tab_is_running (tab, NULL) ||
      !ptyxis_settings_get_prompt_on_close (settings))
    {
      ptyxis_parking_lot_push (self->parking_lot, tab);
      return GDK_EVENT_PROPAGATE;
    }

  tabs = g_ptr_array_new_with_free_func (g_object_unref);
  g_ptr_array_add (tabs, g_object_ref (tab));

  _ptyxis_close_dialog_run_async (GTK_WINDOW (self),
                                  tabs,
                                  NULL,
                                  ptyxis_window_close_page_dialog_cb,
                                  g_object_ref (tab));

  return GDK_EVENT_STOP;
}

static PtyxisTab *
ptyxis_window_get_tab_at_tab_bar_point (PtyxisWindow     *self,
                                        graphene_point_t *point)
{
  static GType tab_type = G_TYPE_INVALID;
  g_autoptr(AdwTabPage) page = NULL;
  GtkWidget *pick;
  GtkWidget *child;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (point != NULL);

  if (tab_type == G_TYPE_INVALID)
    {
      tab_type = g_type_from_name ("AdwTab");
      g_assert (tab_type != G_TYPE_INVALID);
    }

  if (!(pick = gtk_widget_pick (GTK_WIDGET (self->tab_bar), point->x, point->y, GTK_PICK_DEFAULT)))
    return NULL;

  if (!G_TYPE_CHECK_INSTANCE_TYPE (pick, tab_type))
    {
      if (!(pick = gtk_widget_get_ancestor (pick, tab_type)))
        return NULL;
    }

  g_object_get (pick, "page", &page, NULL);
  g_assert (!page || ADW_IS_TAB_PAGE (page));

  if (page == NULL ||
      !(child = adw_tab_page_get_child (page)) ||
      !PTYXIS_IS_TAB (child))
    return NULL;

  return PTYXIS_TAB (child);
}

static void
ptyxis_window_tab_bar_click_pressed_cb (PtyxisWindow    *self,
                                        guint            n_press,
                                        double           x,
                                        double           y,
                                        GtkGestureClick *click)
{
  PtyxisTabMiddleClickBehavior behavior;
  PtyxisSettings *settings;
  PtyxisTab *tab;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (GTK_IS_GESTURE_CLICK (click));

  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);
  behavior = ptyxis_settings_get_tab_middle_click (settings);

  if (behavior == PTYXIS_TAB_MIDDLE_CLICK_CLOSE)
    return;

  if (!(tab = ptyxis_window_get_tab_at_tab_bar_point (self, &GRAPHENE_POINT_INIT (x, y))))
    return;

  ptyxis_window_set_active_tab (self, tab);

  if (behavior == PTYXIS_TAB_MIDDLE_CLICK_PASTE)
    {
      PtyxisTerminal *terminal = ptyxis_tab_get_terminal (tab);

      if (ptyxis_terminal_can_paste (terminal))
        ptyxis_terminal_paste (terminal);
    }

  gtk_gesture_set_state (GTK_GESTURE (click), GTK_EVENT_SEQUENCE_CLAIMED);
}

static gboolean
ptyxis_window_focus_active_tab_cb (gpointer data)
{
  PtyxisWindow *self = data;
  PtyxisTab *active_tab;

  g_assert (PTYXIS_IS_WINDOW (self));

  self->focus_active_tab_source = 0;
  self->tab_overview_animating = FALSE;

  if ((active_tab = ptyxis_window_get_active_tab (self)))
    {
      ptyxis_tab_grab_focus (active_tab);
      gtk_widget_queue_resize (GTK_WIDGET (active_tab));
    }

  return G_SOURCE_REMOVE;
}

static void
ptyxis_window_update_overview_page_title (PtyxisWindow *self,
                                          PtyxisTab    *tab)
{
  g_autofree char *title = NULL;
  AdwTabPage *page;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (PTYXIS_IS_TAB (tab));

  page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (tab));
  if (page == NULL)
    return;

  if (adw_tab_overview_get_open (self->tab_overview))
    title = ptyxis_tab_dup_overview_title (tab);
  else
    title = ptyxis_tab_dup_title (tab);

  adw_tab_page_set_title (page, title);
}

static void
ptyxis_window_update_overview_titles (PtyxisWindow *self)
{
  guint n_pages;

  g_assert (PTYXIS_IS_WINDOW (self));

  n_pages = adw_tab_view_get_n_pages (self->tab_view);
  for (guint i = 0; i < n_pages; i++)
    {
      AdwTabPage *page = adw_tab_view_get_nth_page (self->tab_view, i);
      PtyxisTab *tab = PTYXIS_TAB (adw_tab_page_get_child (page));

      ptyxis_window_update_overview_page_title (self, tab);
    }
}

static void
ptyxis_window_tab_search_text_changed_cb (PtyxisWindow *self,
                                          GParamSpec   *pspec,
                                          PtyxisTab    *tab)
{
  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (PTYXIS_IS_TAB (tab));

  if (adw_tab_overview_get_open (self->tab_overview))
    ptyxis_window_update_overview_page_title (self, tab);
}

static void
ptyxis_window_tab_overview_notify_open_cb (PtyxisWindow   *self,
                                           GParamSpec     *pspec,
                                           AdwTabOverview *tab_overview)
{
  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (ADW_IS_TAB_OVERVIEW (tab_overview));

  ptyxis_window_update_overview_titles (self);

  /* For some reason when we get here the selected page is not
   * getting focused. So work around libadwaita by deferring the
   * focus to an idle so that we can ensure we're working with
   * the appropriate focus tab.
   *
   * See https://gitlab.gnome.org/GNOME/libadwaita/-/issues/670
   */

  g_clear_handle_id (&self->focus_active_tab_source, g_source_remove);

  if (!adw_tab_overview_get_open (tab_overview))
    {
      PtyxisTab *active_tab;
      GtkSettings *settings = gtk_settings_get_default ();
      gboolean gtk_enable_animations = TRUE;
      guint delay_msec = 425; /* Sync with libadwaita */

      g_object_get (settings,
                    "gtk-enable-animations", &gtk_enable_animations,
                    NULL);

      if (!gtk_enable_animations)
        delay_msec = 10;

      self->focus_active_tab_source = g_timeout_add_full (G_PRIORITY_LOW,
                                                          delay_msec,
                                                          ptyxis_window_focus_active_tab_cb,
                                                          self, NULL);

      if ((active_tab = ptyxis_window_get_active_tab (self)))
        ptyxis_tab_grab_focus (active_tab);
    }

  self->tab_overview_animating = TRUE;
}

static void
ptyxis_window_setup_menu_cb (PtyxisWindow *self,
                             AdwTabPage   *page,
                             AdwTabView   *view)
{
  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (!page || ADW_IS_TAB_PAGE (page));
  g_assert (ADW_IS_TAB_VIEW (view));

  if (page != NULL)
    adw_tab_view_set_selected_page (view, page);
}

static PtyxisWindow *
ptyxis_window_new_for_detach (PtyxisWindow *self)
{
  PtyxisWindow *other;
  int height;
  int width;

  g_assert (PTYXIS_IS_WINDOW (self));

  other = g_object_new (PTYXIS_TYPE_WINDOW,
                        "application", PTYXIS_APPLICATION_DEFAULT,
                        NULL);

  width = gtk_widget_get_width (GTK_WIDGET (self));
  height = gtk_widget_get_height (GTK_WIDGET (self));

  if (width > 0 && height > 0)
    gtk_window_set_default_size (GTK_WINDOW (other), width, height);

  return other;
}

static AdwTabView *
ptyxis_window_create_window_cb (PtyxisWindow *self,
                                AdwTabView   *tab_view)
{
  PtyxisWindow *other;

  g_assert (PTYXIS_IS_WINDOW (self));

  other = ptyxis_window_new_for_detach (self);
  gtk_window_present (GTK_WINDOW (other));

  return other->tab_view;
}

static void
update_visible_and_maybe_close (PtyxisWindow *self)
{
  gboolean visible;
  gboolean was_visible;
  guint n_pages;

  g_assert (PTYXIS_IS_WINDOW (self));

  if (self->disposed || self->tab_view == NULL)
    return;

  n_pages = adw_tab_view_get_n_pages (self->tab_view);

  if (n_pages == 0 && !adw_tab_view_get_is_transferring_page (self->tab_view))
    {
      if (!self->in_close_request && !self->quake_mode)
        ptyxis_application_save_session (PTYXIS_APPLICATION_DEFAULT);
      gtk_window_destroy (GTK_WINDOW (self));
      return;
    }

  was_visible = gtk_widget_get_visible (GTK_WIDGET (self->tab_bar));
  visible = !self->single_terminal_mode && n_pages > 1;

  if (visible != was_visible)
    {
      gtk_widget_set_visible (GTK_WIDGET (self->tab_bar), visible);
      ptyxis_window_maybe_reset_default_size (self);

      if (visible)
        gtk_widget_add_css_class (GTK_WIDGET (self), "has-tab-bar");
      else
        gtk_widget_remove_css_class (GTK_WIDGET (self), "has-tab-bar");
    }
}

static void
ptyxis_window_page_attached_cb (PtyxisWindow *self,
                                AdwTabPage   *page,
                                int           position,
                                AdwTabView   *tab_view)
{
  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (ADW_IS_TAB_PAGE (page));
  g_assert (ADW_IS_TAB_VIEW (tab_view));

  update_visible_and_maybe_close (self);
}

static void
ptyxis_window_page_detached_cb (PtyxisWindow *self,
                                AdwTabPage   *page,
                                int           position,
                                AdwTabView   *tab_view)
{
  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (ADW_IS_TAB_PAGE (page));
  g_assert (ADW_IS_TAB_VIEW (tab_view));

  update_visible_and_maybe_close (self);
}

static gboolean
bind_title_cb (GBinding     *binding,
               const GValue *from_value,
               GValue       *to_value,
               gpointer      user_data)
{
  const char *str = g_value_get_string (from_value);

  if (ptyxis_str_empty0 (str))
    g_value_set_static_string (to_value, ptyxis_app_name ());
  else
    g_value_set_string (to_value, str);

  return TRUE;
}

static void
ptyxis_window_notify_selected_page_cb (PtyxisWindow *self,
                                       GParamSpec   *pspec,
                                       AdwTabView   *tab_view)
{
  g_autoptr(GPropertyAction) read_only = NULL;
  PtyxisTerminal *terminal = NULL;
  AdwTabPage *page = NULL;
  PtyxisTab *tab = NULL;
  gboolean has_page = FALSE;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (ADW_IS_TAB_VIEW (tab_view));

  if (self->tab_view != NULL)
    page = adw_tab_view_get_selected_page (self->tab_view);

  g_signal_group_set_target (self->selected_page_signals, page);

  if (page != NULL)
    {
      tab = PTYXIS_TAB (adw_tab_page_get_child (page));

      has_page = TRUE;

      terminal = ptyxis_tab_get_terminal (tab);

      g_signal_group_set_target (self->active_tab_signals, tab);

      read_only = g_property_action_new ("tab.read-only", tab, "read-only");

      adw_tab_page_set_needs_attention (page, FALSE);

      ptyxis_tab_grab_focus (tab);
    }

  if (terminal == NULL)
    {
      gtk_revealer_set_reveal_child (self->find_bar_revealer, FALSE);
      gtk_window_set_title (GTK_WINDOW (self), ptyxis_app_name ());
    }

  ptyxis_find_bar_set_terminal (self->find_bar, terminal);

  if (!has_page)
    {
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.zoom-in", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.zoom-out", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.zoom-one", FALSE);
    }

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.search", has_page);

  /* Update "Change Profile" action state based on number of profiles */
  {
    g_autoptr(GListModel) profiles = NULL;
    guint n_profiles;

    profiles = ptyxis_application_list_profiles (PTYXIS_APPLICATION_DEFAULT);
    n_profiles = g_list_model_get_n_items (profiles);
    gtk_widget_action_set_enabled (GTK_WIDGET (self),
                                   "win.set-profile",
                                   has_page && n_profiles > 1);
  }

  g_action_map_remove_action (G_ACTION_MAP (self), "tab.read-only");
  if (read_only != NULL)
    g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (read_only));

  g_binding_group_set_source (self->active_tab_bindings, tab);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_TAB]);

  ptyxis_fullscreen_box_reveal (self->fullscreen_box);
}

static void
ptyxis_window_apply_current_settings (PtyxisWindow *self,
                                      PtyxisTab    *tab)
{
  PtyxisApplication *app;
  PtyxisTab *active_tab;
  PtyxisProfile *profile;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (PTYXIS_IS_TAB (tab));

  app = PTYXIS_APPLICATION_DEFAULT;
  profile = ptyxis_tab_get_profile (tab);

  if ((active_tab = ptyxis_window_get_active_tab (self)))
    {
      PtyxisTerminal *terminal = ptyxis_tab_get_terminal (active_tab);
      g_autofree char *current_directory_uri = ptyxis_tab_dup_current_directory_uri (active_tab);
      const char *current_container_name = ptyxis_terminal_get_current_container_name (terminal);
      const char *current_container_runtime = ptyxis_terminal_get_current_container_runtime (terminal);
      PtyxisZoomLevel zoom = ptyxis_tab_get_zoom (active_tab);
      g_autoptr(PtyxisIpcContainer) current_container = NULL;
      guint columns;
      guint rows;

      if (ptyxis_profile_get_preserve_container (profile) != PTYXIS_PRESERVE_CONTAINER_NEVER)
        {
          if ((current_container = ptyxis_application_find_container_by_name (app,
                                                                              current_container_runtime,
                                                                              current_container_name)) ||
              (current_container = ptyxis_tab_dup_container (active_tab)))
            ptyxis_tab_set_container (tab, current_container);
        }

      if (current_directory_uri != NULL)
        ptyxis_tab_set_previous_working_directory_uri (tab, current_directory_uri);

      ptyxis_tab_set_zoom (tab, zoom);
      ptyxis_tab_get_grid_size (active_tab, &columns, &rows);
      vte_terminal_set_size (VTE_TERMINAL (ptyxis_tab_get_terminal (tab)), columns, rows);
    }
}

static PtyxisProfile *
ptyxis_window_dup_profile_for_param (PtyxisWindow *self,
                                     const char   *profile_uuid)
{
  g_autoptr(PtyxisProfile) profile = NULL;
  PtyxisApplication *app;
  PtyxisProfile *active_profile;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (profile_uuid != NULL);

  app = PTYXIS_APPLICATION_DEFAULT;
  active_profile = ptyxis_window_get_active_profile (self);

  if (profile_uuid[0] == 0 && active_profile != NULL)
    profile = g_object_ref (active_profile);
  else if (profile_uuid[0] == 0 || g_str_equal (profile_uuid, "default"))
    profile = ptyxis_application_dup_default_profile (app);
  else
    profile = ptyxis_application_dup_profile (app, profile_uuid);

  return g_steal_pointer (&profile);
}

static AdwTabPage *
ptyxis_window_tab_overview_create_tab_cb (PtyxisWindow   *self,
                                          AdwTabOverview *tab_overview)
{
  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (ADW_IS_TAB_OVERVIEW (tab_overview));

  gtk_widget_activate_action (GTK_WIDGET (tab_overview), "win.new-tab", "(ss)", "", "");

  return adw_tab_view_get_selected_page (self->tab_view);
}

static void
ptyxis_window_new_tab_action (GtkWidget  *widget,
                              const char *action_name,
                              GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  g_autoptr(PtyxisProfile) profile = NULL;
  const char *profile_uuid = "";
  const char *container_id = "";
  PtyxisTab *tab;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (param != NULL);
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE ("(ss)")));

  g_variant_get (param, "(&s&s)", &profile_uuid, &container_id);
  profile = ptyxis_window_dup_profile_for_param (self, profile_uuid);

  tab = ptyxis_tab_new (profile);
  ptyxis_window_apply_current_settings (self, tab);

  if (!ptyxis_str_empty0 (container_id))
    {
      g_autoptr(PtyxisIpcContainer) container = NULL;

      if ((container = ptyxis_application_lookup_container (PTYXIS_APPLICATION_DEFAULT, container_id)))
        ptyxis_tab_set_container (tab, container);
    }

  ptyxis_window_add_tab (self, tab);
  ptyxis_window_set_active_tab (self, tab);
  ptyxis_tab_grab_focus (tab);
}

static void
ptyxis_window_new_window_action (GtkWidget  *widget,
                                 const char *action_name,
                                 GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  g_autoptr(PtyxisProfile) profile = NULL;
  PtyxisWindow *window;
  PtyxisSettings *settings;
  const char *profile_uuid = "";
  const char *container_id = "";
  PtyxisTab *tab;
  GdkToplevel *toplevel;
  GdkToplevelState state;
  guint columns, rows;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (param != NULL);
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE ("(ss)")));

  g_variant_get (param, "(&s&s)", &profile_uuid, &container_id);
  profile = ptyxis_window_dup_profile_for_param (self, profile_uuid);

  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);

  tab = ptyxis_tab_new (profile);
  ptyxis_window_apply_current_settings (self, tab);

  if (!ptyxis_str_empty0 (container_id))
    {
      g_autoptr(PtyxisIpcContainer) container = NULL;

      if ((container = ptyxis_application_lookup_container (PTYXIS_APPLICATION_DEFAULT, container_id)))
        ptyxis_tab_set_container (tab, container);
    }

  toplevel = GDK_TOPLEVEL (gtk_native_get_surface (GTK_NATIVE (self)));
  state = gdk_toplevel_get_state (toplevel);

  /* If the current window is maximized, don't maximize this window as
   * it's most likely they're just doing a temporary thing or would like
   * to move the window elsewhere.
   */
  if (!!(state & (GDK_TOPLEVEL_STATE_MAXIMIZED | GDK_TOPLEVEL_STATE_FULLSCREEN | GDK_TOPLEVEL_STATE_TILED)) ||
      !ptyxis_settings_get_restore_window_size (settings))
    {
      PtyxisTerminal *terminal = ptyxis_tab_get_terminal (tab);
      ptyxis_settings_get_default_size (settings, &columns, &rows);
      vte_terminal_set_size (VTE_TERMINAL (terminal), columns, rows);
    }

  window = g_object_new (PTYXIS_TYPE_WINDOW,
                         "application", PTYXIS_APPLICATION_DEFAULT,
                         NULL);
  ptyxis_window_add_tab (window, tab);

  gtk_window_present (GTK_WINDOW (window));

  ptyxis_tab_grab_focus (tab);
}

static void
ptyxis_window_new_terminal_action (GtkWidget  *widget,
                                   const char *action_name,
                                   GVariant   *param)
{
  g_assert (PTYXIS_IS_WINDOW (widget));
  g_assert (param != NULL);
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE ("(ss)")));

  if (ptyxis_application_control_is_pressed (PTYXIS_APPLICATION_DEFAULT))
    ptyxis_window_new_window_action (widget, NULL, param);
  else
    ptyxis_window_new_tab_action (widget, NULL, param);
}

static void
ptyxis_window_tab_overview_action (GtkWidget  *widget,
                                   const char *action_name,
                                   GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;

  g_assert (PTYXIS_IS_WINDOW (self));

  adw_tab_overview_set_open (self->tab_overview,
                             !adw_tab_overview_get_open (self->tab_overview));
}

static void
ptyxis_window_zoom_in_action (GtkWidget  *widget,
                              const char *action_name,
                              GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  PtyxisTab *active_tab;
  gboolean maybe_resize;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (param != NULL);
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_BOOLEAN));

  maybe_resize = g_variant_get_boolean (param);

  if ((active_tab = ptyxis_window_get_active_tab (self)))
    {
      ptyxis_tab_zoom_in (active_tab);

      if (maybe_resize)
        gtk_window_set_default_size (GTK_WINDOW (self), -1, -1);
    }
}

static void
ptyxis_window_zoom_out_action (GtkWidget  *widget,
                               const char *action_name,
                               GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  PtyxisTab *active_tab;
  gboolean maybe_resize;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (param != NULL);
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_BOOLEAN));

  maybe_resize = g_variant_get_boolean (param);

  if ((active_tab = ptyxis_window_get_active_tab (self)))
    {
      ptyxis_tab_zoom_out (active_tab);

      if (maybe_resize)
        gtk_window_set_default_size (GTK_WINDOW (self), -1, -1);
    }
}

static void
ptyxis_window_zoom_one_action (GtkWidget  *widget,
                               const char *action_name,
                               GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  PtyxisTab *active_tab;
  gboolean maybe_resize;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (param != NULL);
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_BOOLEAN));

  maybe_resize = g_variant_get_boolean (param);

  if ((active_tab = ptyxis_window_get_active_tab (self)))
    {
      ptyxis_tab_set_zoom (active_tab, PTYXIS_ZOOM_LEVEL_DEFAULT);

      if (maybe_resize)
        gtk_window_set_default_size (GTK_WINDOW (self), -1, -1);
    }
}

static void
ptyxis_window_active_tab_bell_cb (PtyxisWindow *self,
                                  PtyxisTab    *tab)
{
  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (PTYXIS_IS_TAB (tab));

  ptyxis_window_visual_bell (self);
}

static void
ptyxis_window_active_tab_commit_cb (PtyxisWindow *self,
                                    const char   *str,
                                    PtyxisTab    *tab)
{
  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (PTYXIS_IS_TAB (tab));

  if (self->is_fullscreen)
    ptyxis_fullscreen_box_unreveal (self->fullscreen_box);
}

static void
ptyxis_window_close_action (GtkWidget  *widget,
                            const char *action_name,
                            GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  PtyxisTab *tab;
  AdwTabPage *tab_page;

  g_assert (PTYXIS_IS_WINDOW (self));

  if (!(tab = ptyxis_window_get_active_tab (self)))
    return;

  tab_page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (tab));
  adw_tab_view_close_page (self->tab_view, tab_page);
}

static void
ptyxis_window_close_others_action (GtkWidget  *widget,
                                   const char *action_name,
                                   GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  PtyxisTab *tab;
  AdwTabPage *tab_page;

  g_assert (PTYXIS_IS_WINDOW (self));

  if (!(tab = ptyxis_window_get_active_tab (self)))
    return;

  tab_page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (tab));
  adw_tab_view_close_other_pages (self->tab_view, tab_page);
}

static void
ptyxis_window_detach_action (GtkWidget  *widget,
                             const char *action_name,
                             GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  PtyxisWindow *new_window;
  PtyxisTab *tab;
  AdwTabPage *tab_page;

  g_assert (PTYXIS_IS_WINDOW (self));

  if (!(tab = ptyxis_window_get_active_tab (self)))
    return;

  tab_page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (tab));

  new_window = ptyxis_window_new_for_detach (self);
  adw_tab_view_transfer_page (self->tab_view, tab_page, new_window->tab_view, 0);

  gtk_window_present (GTK_WINDOW (new_window));
}

static void
ptyxis_window_tab_pin_action (GtkWidget  *widget,
                              const char *action_name,
                              GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  AdwTabPage *tab_page;
  PtyxisTab *tab;

  g_assert (PTYXIS_IS_WINDOW (self));

  if (!(tab = ptyxis_window_get_active_tab (self)) ||
      !(tab_page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (tab))))
    return;

  adw_tab_view_set_page_pinned (self->tab_view, tab_page, TRUE);
}

static void
ptyxis_window_tab_unpin_action (GtkWidget  *widget,
                                const char *action_name,
                                GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  AdwTabPage *tab_page;
  PtyxisTab *tab;

  g_assert (PTYXIS_IS_WINDOW (self));

  if (!(tab = ptyxis_window_get_active_tab (self)) ||
      !(tab_page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (tab))))
    return;

  adw_tab_view_set_page_pinned (self->tab_view, tab_page, FALSE);
}

void
ptyxis_window_set_tab_pinned (PtyxisWindow *self,
                              PtyxisTab    *tab,
                              gboolean      pinned)
{
  AdwTabPage *tab_page;

  g_return_if_fail (PTYXIS_IS_WINDOW (self));
  g_return_if_fail (PTYXIS_IS_TAB (tab));

  if ((tab_page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (tab))))
    adw_tab_view_set_page_pinned (self->tab_view, tab_page, pinned);
}

static void
ptyxis_window_page_previous_action (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  PtyxisWindow *self = PTYXIS_WINDOW (widget);
  guint n_pages = adw_tab_view_get_n_pages (self->tab_view);

  if (n_pages == 0)
    return;

  if (!adw_tab_view_select_previous_page (self->tab_view))
    adw_tab_view_set_selected_page (self->tab_view,
                                    adw_tab_view_get_nth_page (self->tab_view, n_pages-1));
}

static void
ptyxis_window_page_next_action (GtkWidget  *widget,
                                const char *action_name,
                                GVariant   *param)
{
  PtyxisWindow *self = PTYXIS_WINDOW (widget);
  guint n_pages = adw_tab_view_get_n_pages (self->tab_view);

  if (n_pages == 0)
    return;

  if (!adw_tab_view_select_next_page (self->tab_view))
    adw_tab_view_set_selected_page (self->tab_view,
                                    adw_tab_view_get_nth_page (self->tab_view, 0));
}

static void
ptyxis_window_tab_focus_action (GtkWidget  *widget,
                                const char *action_name,
                                GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  guint nth = 0;
  guint n_pages;
  int position;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (param != NULL);
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_INT32));

  position = g_variant_get_int32 (param);
  n_pages = adw_tab_view_get_n_pages (self->tab_view);

  if (n_pages == 0)
    return;

  if (position <= 0)
    nth = n_pages - 1;
  else if (position > 0 && (guint)position <= n_pages)
    nth = (guint)position - 1;
  else
    nth = n_pages - 1;

  if (nth < n_pages)
    {
      AdwTabPage *page = adw_tab_view_get_nth_page (self->tab_view, nth);
      adw_tab_view_set_selected_page (self->tab_view, page);
    }
}

static void
ptyxis_window_tab_reset_action (GtkWidget  *widget,
                                const char *action_name,
                                GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  PtyxisTerminal *terminal;
  PtyxisTab *tab;
  gboolean clear;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (param != NULL);
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_BOOLEAN));

  clear = g_variant_get_boolean (param);

  if (!(tab = ptyxis_window_get_active_tab (self)))
    return;

  terminal = ptyxis_tab_get_terminal (tab);
  vte_terminal_reset (VTE_TERMINAL (terminal), TRUE, clear);
}

static void
ptyxis_window_move_left_action (GtkWidget  *widget,
                                const char *action_name,
                                GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  PtyxisTab *tab;
  AdwTabPage *tab_page;

  g_assert (PTYXIS_IS_WINDOW (self));

  if (!(tab = ptyxis_window_get_active_tab (self)))
    return;

  tab_page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (tab));
  adw_tab_view_reorder_backward (self->tab_view, tab_page);
  ptyxis_tab_raise (tab);
  ptyxis_tab_grab_focus (tab);
}

static void
ptyxis_window_move_right_action (GtkWidget  *widget,
                                 const char *action_name,
                                 GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  PtyxisTab *tab;
  AdwTabPage *tab_page;

  g_assert (PTYXIS_IS_WINDOW (self));

  if (!(tab = ptyxis_window_get_active_tab (self)))
    return;

  tab_page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (tab));
  adw_tab_view_reorder_forward (self->tab_view, tab_page);
  ptyxis_tab_raise (tab);
  ptyxis_tab_grab_focus (tab);
}

static void
ptyxis_window_fullscreen_action (GtkWidget  *widget,
                                 const char *action_name,
                                 GVariant   *param)
{
  gtk_window_fullscreen (GTK_WINDOW (widget));
}

static void
ptyxis_window_unfullscreen_action (GtkWidget  *widget,
                                   const char *action_name,
                                   GVariant   *param)
{
  gtk_window_unfullscreen (GTK_WINDOW (widget));
}

static void
ptyxis_window_hide_quake_action (GtkWidget  *widget,
                                 const char *action_name,
                                 GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;

  g_assert (PTYXIS_IS_WINDOW (self));

  if (self->quake_mode)
    gtk_widget_set_visible (widget, FALSE);
}

static void
ptyxis_window_toggle_fullscreen (GtkWidget  *widget,
                                 const char *action_name,
                                 GVariant   *param)
{
  if (gtk_window_is_fullscreen (GTK_WINDOW (widget)))
    gtk_window_unfullscreen (GTK_WINDOW (widget));
  else
    gtk_window_fullscreen (GTK_WINDOW (widget));
}

static void
ptyxis_window_notify_is_active_cb (PtyxisWindow *self,
                                   GParamSpec   *pspec)
{
  g_assert (PTYXIS_IS_WINDOW (self));

  if (gtk_window_is_active (GTK_WINDOW (self)))
    {
#ifdef GDK_WINDOWING_X11
      {
        GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (self));

        if (GDK_IS_X11_SURFACE (surface))
          gdk_x11_surface_set_urgency_hint (surface, FALSE);
      }
#endif
    }
}

static void
ptyxis_window_toplevel_state_changed_cb (PtyxisWindow *self,
                                         GParamSpec   *pspec,
                                         GdkToplevel  *toplevel)
{
  GdkToplevelState state;
  gboolean is_fullscreen;
  gboolean is_maximized;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (GDK_IS_TOPLEVEL (toplevel));

  state = gdk_toplevel_get_state (toplevel);

  is_fullscreen = !!(state & GDK_TOPLEVEL_STATE_FULLSCREEN);
  is_maximized = !!(state & GDK_TOPLEVEL_STATE_MAXIMIZED);

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.fullscreen", !is_fullscreen);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.unfullscreen", is_fullscreen);

  ptyxis_fullscreen_box_set_fullscreen (self->fullscreen_box, is_fullscreen);

  /* Clear cached grid size for non-visible tabs when leaving fullscreen or
   * maximized state. Otherwise we'll jump back to large grid size which is
   * not expected from a user-standpoint.
   */
  if ((!is_maximized && self->is_maximized) ||
      (!is_fullscreen && self->is_fullscreen))
    self->reset_nonvisible_from_size_allocate = TRUE;

  /* If transitioning to fullscreen, animate in the fullscreen controls which
   * ensures that they will disappear after timeout.
   *
   * See: https://gitlab.gnome.org/chergert/ptyxis/-/issues/376
   */
  if (!self->is_fullscreen && is_fullscreen)
    ptyxis_fullscreen_box_reveal (self->fullscreen_box);

  /* Ensure our CSS can apply a different background for fullscreen as the compositor
   * is required (in Wayland) to not allow a window to see things behind it when
   * in fullscreen mode.
   */
  if (is_fullscreen)
    gtk_widget_add_css_class (GTK_WIDGET (self), "fullscreen");
  else
    gtk_widget_remove_css_class (GTK_WIDGET (self), "fullscreen");

  self->is_fullscreen = is_fullscreen;
  self->is_maximized = is_maximized;
}

static void
ptyxis_window_realize (GtkWidget *widget)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  GdkToplevel *toplevel;

  g_assert (PTYXIS_IS_WINDOW (self));

  GTK_WIDGET_CLASS (ptyxis_window_parent_class)->realize (widget);

  toplevel = GDK_TOPLEVEL (gtk_native_get_surface (GTK_NATIVE (self)));

  g_signal_connect_object (toplevel,
                           "notify::state",
                           G_CALLBACK (ptyxis_window_toplevel_state_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

static void
ptyxis_window_shortcuts_notify_cb (PtyxisWindow    *self,
                                   GParamSpec      *pspec,
                                   PtyxisShortcuts *shortcuts)
{
  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (PTYXIS_IS_SHORTCUTS (shortcuts));

  ptyxis_shortcuts_update_menu (shortcuts, self->primary_menu);
  ptyxis_shortcuts_update_menu (shortcuts, self->tab_menu);
}

static void
ptyxis_window_set_title_action (GtkWidget  *widget,
                                const char *action_name,
                                GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  AdwDialog *dialog;
  PtyxisTab *active_tab;

  g_assert (PTYXIS_IS_WINDOW (self));

  if (!(active_tab = ptyxis_window_get_active_tab (self)))
    return;

  dialog = g_object_new (PTYXIS_TYPE_TITLE_DIALOG,
                         "tab", active_tab,
                         "title", _("Set Title"),
                         NULL);

  adw_dialog_set_presentation_mode (dialog, ADW_DIALOG_FLOATING);
  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (self));
}

static void
ptyxis_window_set_profile_action (GtkWidget  *widget,
                                  const char *action_name,
                                  GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  AdwDialog *dialog;
  PtyxisTab *active_tab;

  g_assert (PTYXIS_IS_WINDOW (self));

  if (!(active_tab = ptyxis_window_get_active_tab (self)))
    return;

  dialog = g_object_new (PTYXIS_TYPE_PROFILE_DIALOG,
                         "tab", active_tab,
                         NULL);

  adw_dialog_set_presentation_mode (dialog, ADW_DIALOG_FLOATING);
  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (self));
}

static void
ptyxis_window_search_action (GtkWidget  *widget,
                             const char *action_name,
                             GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;

  g_assert (PTYXIS_IS_WINDOW (self));

  gtk_revealer_set_reveal_child (self->find_bar_revealer, TRUE);
  gtk_widget_grab_focus (GTK_WIDGET (self->find_bar));
}

static void
ptyxis_window_undo_close_tab_action (GtkWidget  *widget,
                                     const char *action_name,
                                     GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  g_autoptr(PtyxisTab) tab = NULL;

  g_assert (PTYXIS_IS_WINDOW (self));

  if ((tab = ptyxis_parking_lot_pop (self->parking_lot)))
    {
      if (!ptyxis_tab_is_running (tab, NULL))
        ptyxis_tab_show_banner (tab);
      ptyxis_window_add_tab (self, tab);
      ptyxis_window_set_active_tab (self, tab);
      ptyxis_tab_grab_focus (tab);
    }
}

static void
ptyxis_window_preferences_action (GtkWidget  *widget,
                                  const char *action_name,
                                  GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  PtyxisApplication *app;
  const GList *windows;
  GtkWindow *window;

  g_assert (PTYXIS_IS_WINDOW (self));

  app = PTYXIS_APPLICATION_DEFAULT;
  windows = gtk_application_get_windows (GTK_APPLICATION (app));

  for (const GList *iter = windows; iter != NULL; iter = iter->next)
    {
      if (PTYXIS_IS_PREFERENCES_WINDOW (iter->data))
        {
          gtk_window_present (GTK_WINDOW (iter->data));
          return;
        }
    }

  window = ptyxis_preferences_window_new (GTK_APPLICATION (app));
  gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (window));
  gtk_window_set_transient_for (window, GTK_WINDOW (self));
  gtk_window_set_modal (window, FALSE);
  gtk_window_present (window);
}

static void
ptyxis_window_show_keyboard_shortcuts_action (GtkWidget  *widget,
                                              const char *action_name,
                                              GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;
  PtyxisPreferencesWindow *window;

  g_assert (PTYXIS_IS_WINDOW (self));

  window = ptyxis_preferences_window_get_default ();
  ptyxis_preferences_window_edit_shortcuts (window);
  gtk_window_present (GTK_WINDOW (window));
}

static void
ptyxis_window_go_to_terminal_action (GtkWidget  *widget,
                                     const char *action_name,
                                     GVariant   *param)
{
  PtyxisTerminalPicker *picker;

  g_assert (PTYXIS_IS_WINDOW (widget));

  picker = ptyxis_terminal_picker_new (PTYXIS_WINDOW (widget));
  adw_dialog_present (ADW_DIALOG (picker), widget);
}

static void
ptyxis_window_primary_menu_action (GtkWidget  *widget,
                                   const char *action_name,
                                   GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;

  g_assert (PTYXIS_IS_WINDOW (self));

  gtk_menu_button_popup (self->primary_menu_button);
}

static void
ptyxis_window_tab_menu_action (GtkWidget  *widget,
                               const char *action_name,
                               GVariant   *param)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;

  g_assert (PTYXIS_IS_WINDOW (self));

  gtk_menu_button_popup (self->new_terminal_menu_button);
}

static void
ptyxis_window_close_request_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  g_autoptr(PtyxisWindow) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (PTYXIS_IS_WINDOW (self));

  if (_ptyxis_close_dialog_run_finish (result, &error))
    gtk_window_destroy (GTK_WINDOW (self));

  self->in_close_request = FALSE;
}

static gboolean
is_last_window (PtyxisWindow *self)
{
  const GList *windows = gtk_application_get_windows (GTK_APPLICATION (PTYXIS_APPLICATION_DEFAULT));

  for (const GList *iter = windows;
       iter;
       iter = iter->next)
    {
      if (PTYXIS_IS_WINDOW (iter->data) &&
          !ptyxis_window_get_quake_mode (iter->data) &&
          iter->data != (gpointer)self)
        return FALSE;
    }

  return TRUE;
}

static gboolean
ptyxis_window_close_request (GtkWindow *window)
{
  PtyxisWindow *self = (PtyxisWindow *)window;
  g_autoptr(GPtrArray) tabs = NULL;
  PtyxisSettings *settings;
  guint n_pages;

  g_assert (PTYXIS_IS_WINDOW (self));

  ptyxis_window_save_size (self);

  if (!self->single_terminal_mode && !self->quake_mode && is_last_window (self))
    ptyxis_application_save_session (PTYXIS_APPLICATION_DEFAULT);

  /* Short-circuit if user dismissed guard rails */
  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);
  if (!ptyxis_settings_get_prompt_on_close (settings))
    return GDK_EVENT_PROPAGATE;

  tabs = g_ptr_array_new_with_free_func (g_object_unref);
  n_pages = adw_tab_view_get_n_pages (self->tab_view);

  self->in_close_request = TRUE;

  for (guint i = n_pages; i > 0; i--)
    {
      AdwTabPage *page = adw_tab_view_get_nth_page (self->tab_view, i - 1);
      PtyxisTab *tab = PTYXIS_TAB (adw_tab_page_get_child (page));

      if (ptyxis_tab_is_running (tab, NULL))
        g_ptr_array_add (tabs, g_object_ref (tab));
      else
        adw_tab_view_close_page (self->tab_view, page);
    }

  if (tabs->len > 0)
    _ptyxis_close_dialog_run_async (GTK_WINDOW (self),
                                    tabs,
                                    NULL,
                                    ptyxis_window_close_request_cb,
                                    g_object_ref (self));
  else
    self->in_close_request = FALSE;

  return tabs->len > 0;
}

static void
ptyxis_window_notify_process_leader_kind_cb (PtyxisWindow *self,
                                             GParamSpec   *pspec,
                                             PtyxisTab    *tab)
{
  PtyxisProcessLeaderKind kind;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (PTYXIS_IS_TAB (tab));

  g_object_get (tab,
                "process-leader-kind", &kind,
                NULL);

  gtk_widget_remove_css_class (GTK_WIDGET (self), "container");
  gtk_widget_remove_css_class (GTK_WIDGET (self), "remote");
  gtk_widget_remove_css_class (GTK_WIDGET (self), "superuser");

  if (kind == PTYXIS_PROCESS_LEADER_KIND_SUPERUSER)
    gtk_widget_add_css_class (GTK_WIDGET (self), "superuser");
  else if (kind == PTYXIS_PROCESS_LEADER_KIND_REMOTE)
    gtk_widget_add_css_class (GTK_WIDGET (self), "remote");
  else if (kind == PTYXIS_PROCESS_LEADER_KIND_CONTAINER)
    gtk_widget_add_css_class (GTK_WIDGET (self), "container");
}

static void
ptyxis_window_notify_zoom_cb (PtyxisWindow *self,
                              GParamSpec   *pspec,
                              PtyxisTab    *tab)
{
  PtyxisZoomLevel zoom;
  gboolean can_zoom_in;
  gboolean can_zoom_out;
  gboolean can_zoom_one;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (PTYXIS_IS_TAB (tab));

  zoom = ptyxis_tab_get_zoom (tab);

  can_zoom_in = zoom != PTYXIS_ZOOM_LEVEL_PLUS_14;
  can_zoom_one = zoom != PTYXIS_ZOOM_LEVEL_DEFAULT;
  can_zoom_out = zoom != PTYXIS_ZOOM_LEVEL_MINUS_14;

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.zoom-in", can_zoom_in);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.zoom-one", can_zoom_one);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.zoom-out", can_zoom_out);
}

static void
ptyxis_window_update_search_target (PtyxisWindow *self)
{
  PtyxisTab *active_tab;
  gboolean visible;
  guint n_pages;

  g_assert (PTYXIS_IS_WINDOW (self));

  active_tab = ptyxis_window_get_active_tab (self);
  visible = gtk_revealer_get_reveal_child (self->find_bar_revealer);
  n_pages = adw_tab_view_get_n_pages (self->tab_view);

  for (guint i = 0; i < n_pages; i++)
    {
      AdwTabPage *page = adw_tab_view_get_nth_page (self->tab_view, i);
      PtyxisTab *tab = PTYXIS_TAB (adw_tab_page_get_child (page));

      ptyxis_tab_set_search_target_visible (tab, visible && tab == active_tab);
    }
}

static void
ptyxis_window_find_bar_revealer_notify_cb (PtyxisWindow *self,
                                           GParamSpec   *pspec,
                                           GtkRevealer  *revealer)
{
  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (GTK_IS_REVEALER (revealer));

  ptyxis_window_update_search_target (self);
}

static void
ptyxis_window_notify_active_pane_cb (PtyxisWindow *self,
                                     GParamSpec   *pspec,
                                     PtyxisTab    *tab)
{
  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (PTYXIS_IS_TAB (tab));

  if (tab == ptyxis_window_get_active_tab (self))
    {
      ptyxis_find_bar_set_terminal (self->find_bar, ptyxis_tab_get_terminal (tab));
      ptyxis_window_update_search_target (self);
    }
}

static void
ptyxis_window_active_tab_bind_cb (PtyxisWindow *self,
                                  PtyxisTab    *tab,
                                  GSignalGroup *signals)
{
  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (PTYXIS_IS_TAB (tab));
  g_assert (G_IS_SIGNAL_GROUP (signals));

  ptyxis_window_notify_process_leader_kind_cb (self, NULL, tab);
  ptyxis_window_notify_zoom_cb (self, NULL, tab);
  ptyxis_window_notify_active_pane_cb (self, NULL, tab);
}

static void
ptyxis_window_add_zoom_controls (PtyxisWindow *self)
{
  GtkPopover *popover;
  GtkWidget *zoom_box;
  GtkWidget *zoom_out;
  GtkWidget *zoom_in;

  g_assert (PTYXIS_IS_WINDOW (self));

  popover = gtk_menu_button_get_popover (self->primary_menu_button);

  /* Add zoom controls */
  zoom_box = g_object_new (GTK_TYPE_BOX,
                           "spacing", 12,
                           "margin-start", 18,
                           "margin-end", 18,
                           NULL);
  zoom_in = g_object_new (GTK_TYPE_BUTTON,
                          "action-name", "win.zoom-in",
                          "action-target", g_variant_new_boolean (FALSE),
                          "tooltip-text", _("Zoom In"),
                          "child", g_object_new (GTK_TYPE_IMAGE,
                                                 "icon-name", "zoom-in-symbolic",
                                                 "pixel-size", 16,
                                                 NULL),
                          NULL);
  gtk_widget_add_css_class (zoom_in, "circular");
  gtk_widget_add_css_class (zoom_in, "flat");
  gtk_widget_set_tooltip_text (zoom_in, _("Zoom In"));
  gtk_accessible_update_property (GTK_ACCESSIBLE (zoom_in),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL,
                                  _("Zoom in"), -1);
  zoom_out = g_object_new (GTK_TYPE_BUTTON,
                           "action-name", "win.zoom-out",
                           "action-target", g_variant_new_boolean (FALSE),
                           "tooltip-text", _("Zoom Out"),
                           "child", g_object_new (GTK_TYPE_IMAGE,
                                                  "icon-name", "zoom-out-symbolic",
                                                  "pixel-size", 16,
                                                  NULL),
                           NULL);
  gtk_widget_add_css_class (zoom_out, "circular");
  gtk_widget_add_css_class (zoom_out, "flat");
  gtk_widget_set_tooltip_text (zoom_out, _("Zoom Out"));
  gtk_accessible_update_property (GTK_ACCESSIBLE (zoom_out),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL,
                                  _("Zoom out"), -1);
  self->zoom_label = g_object_new (GTK_TYPE_BUTTON,
                                   "css-classes", (const char * const[]) {"flat", "pill", NULL},
                                   "action-name", "win.zoom-one",
                                   "action-target", g_variant_new_boolean (FALSE),
                                   "hexpand", TRUE,
                                   "tooltip-text", _("Reset Zoom"),
                                   "label", "100%",
                                   NULL);
  g_binding_group_bind (self->active_tab_bindings, "zoom-label", self->zoom_label, "label", 0);
  gtk_box_append (GTK_BOX (zoom_box), zoom_out);
  gtk_box_append (GTK_BOX (zoom_box), self->zoom_label);
  gtk_box_append (GTK_BOX (zoom_box), zoom_in);
  gtk_popover_menu_add_child (GTK_POPOVER_MENU (popover), zoom_box, "zoom");
}

static void
ptyxis_window_add_theme_controls (PtyxisWindow *self)
{
  g_autoptr(GPropertyAction) interface_style = NULL;
  PtyxisSettings *settings;
  GtkPopover *popover;
  GtkWidget *selector;

  g_assert (PTYXIS_IS_WINDOW (self));

  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);
  self->interface_style_action = g_property_action_new ("interface-style", settings, "interface-style");
  g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (self->interface_style_action));

  popover = gtk_menu_button_get_popover (self->primary_menu_button);
  selector = g_object_new (PTYXIS_TYPE_THEME_SELECTOR,
                           "action-name", "win.interface-style",
                           NULL);
  gtk_popover_menu_add_child (GTK_POPOVER_MENU (popover), selector, "interface-style");
}

static void
ptyxis_window_update_menu_visibility (PtyxisWindow *self)
{
  g_autoptr(GListModel) containers = NULL;
  g_autoptr(GListModel) profiles = NULL;
  gboolean visible;
  guint n_profiles;

  g_assert (PTYXIS_IS_WINDOW (self));

  containers = ptyxis_application_list_containers (PTYXIS_APPLICATION_DEFAULT);
  profiles = ptyxis_application_list_profiles (PTYXIS_APPLICATION_DEFAULT);
  n_profiles = g_list_model_get_n_items (profiles);
  visible = g_list_model_get_n_items (containers) > 1 ||
            n_profiles > 1;

  gtk_widget_action_set_enabled (GTK_WIDGET (self),
                                 "my-computer",
                                 g_list_model_get_n_items (containers) > 1);

  /* Disable "Change Profile" action when there's only one profile */
  gtk_widget_action_set_enabled (GTK_WIDGET (self),
                                 "win.set-profile",
                                 n_profiles > 1);

  gtk_widget_set_visible (GTK_WIDGET (self->new_terminal_separator), visible);
  gtk_widget_set_visible (GTK_WIDGET (self->new_terminal_menu_button), visible);
}

static void
notify_decoration_layout_cb (PtyxisWindow *self,
                             GParamSpec   *pspec,
                             GtkSettings  *gtk_settings)
{
  g_autofree char *layout = NULL;
  gboolean inverted = FALSE;
  const char *colon;
  const char *close_;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (GTK_IS_SETTINGS (gtk_settings));

  g_object_get (gtk_settings,
                "gtk-decoration-layout", &layout,
                NULL);

  if ((colon = strchr (layout, ':')) && (close_ = strstr (layout, "close")))
    inverted = close_ < colon;

  if (self->tab_bar)
    adw_tab_bar_set_inverted (self->tab_bar, inverted);
}

static void
ptyxis_window_constructed (GObject *object)
{
  PtyxisWindow *self = (PtyxisWindow *)object;
  g_autoptr(GListModel) profiles = NULL;
  g_autoptr(GListModel) containers = NULL;
  g_autoptr(GMenu) menu = NULL;
  GtkSettings *gtk_settings;

  G_OBJECT_CLASS (ptyxis_window_parent_class)->constructed (object);

  self->dressing = ptyxis_window_dressing_new (self);
  g_binding_group_bind (self->profile_bindings, "palette",
                        self->dressing, "palette",
                        G_BINDING_SYNC_CREATE);
  g_binding_group_bind (self->profile_bindings, "opacity",
                        self->dressing, "opacity",
                        G_BINDING_SYNC_CREATE);

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.unfullscreen", FALSE);

  ptyxis_window_add_theme_controls (self);
  ptyxis_window_add_zoom_controls (self);

  containers = ptyxis_application_list_containers (PTYXIS_APPLICATION_DEFAULT);
  g_signal_connect_object (containers,
                           "items-changed",
                           G_CALLBACK (ptyxis_window_update_menu_visibility),
                           self,
                           G_CONNECT_SWAPPED);
  profiles = ptyxis_application_list_profiles (PTYXIS_APPLICATION_DEFAULT);
  g_signal_connect_object (profiles,
                           "items-changed",
                           G_CALLBACK (ptyxis_window_update_menu_visibility),
                           self,
                           G_CONNECT_SWAPPED);
  ptyxis_window_update_menu_visibility (self);

  gtk_settings = gtk_settings_get_default ();
  g_signal_connect_object (gtk_settings,
                           "notify::gtk-decoration-layout",
                           G_CALLBACK (notify_decoration_layout_cb),
                           self,
                           G_CONNECT_SWAPPED);
  notify_decoration_layout_cb (self, NULL, gtk_settings);

  g_signal_connect (self,
                    "notify::is-active",
                    G_CALLBACK (ptyxis_window_notify_is_active_cb),
                    NULL);
}

static void
ptyxis_window_selected_page_notify_pinned_cb (PtyxisWindow *self,
                                              GParamSpec   *pspec,
                                              AdwTabPage   *page)
{
  gboolean pinned;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (ADW_IS_TAB_PAGE (page));

  pinned = adw_tab_page_get_pinned (page);

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "tab.pin", !pinned);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "tab.unpin", pinned);
}

static void
ptyxis_window_selected_page_bind_cb (PtyxisWindow *self,
                                     AdwTabPage   *page,
                                     GSignalGroup *group)
{
  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (ADW_IS_TAB_PAGE (page));
  g_assert (G_IS_SIGNAL_GROUP (group));

  ptyxis_window_selected_page_notify_pinned_cb (self, NULL, page);
}

static void
ptyxis_window_my_computer_action (GtkWidget  *widget,
                                  const char *action_name,
                                  GVariant   *param)
{
  g_assert (PTYXIS_IS_WINDOW (widget));

  gtk_widget_activate_action (widget, "win.new-terminal", "(ss)", "", "session");
}

static gboolean
ptyxis_window_check_singular (gpointer instance,
                              guint    n_items)
{
  return n_items <= 1;
}

static char *
ptyxis_window_get_header_title (gpointer instance,
                                gpointer item)
{
  if (PTYXIS_IPC_IS_CONTAINER (item))
    {
      const char *str = ptyxis_ipc_container_get_display_name (item);

      if (str && str[0])
        return g_strdup (_("Containers"));

      return NULL;
    }

  if (PTYXIS_IS_PROFILE (item))
    return g_strdup (_("Profiles"));

  return NULL;
}

static char *
ptyxis_window_get_container_title (gpointer first,
                                   gpointer second)
{
  PtyxisIpcContainer *container;
  const char *title;

  g_assert (PTYXIS_IPC_IS_CONTAINER (first) || PTYXIS_IPC_IS_CONTAINER (second));

  if (PTYXIS_IPC_IS_CONTAINER (first))
    container = first;
  else if (PTYXIS_IPC_IS_CONTAINER (second))
    container = second;
  else
    g_assert_not_reached ();

  title = ptyxis_ipc_container_get_display_name (container);

  if (title && title[0])
    return g_strdup (title);

  return g_strdup (_("My Computer"));
}

static void
ptyxis_window_menu_stop_search (PtyxisWindow   *self,
                                GtkSearchEntry *entry)
{
  PtyxisTab *tab;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (GTK_IS_SEARCH_ENTRY (entry));

  gtk_editable_set_text (GTK_EDITABLE (entry), "");
  gtk_menu_button_popdown (self->new_terminal_menu_button);

  if ((tab = ptyxis_window_get_active_tab (self)))
    ptyxis_tab_grab_focus (tab);
}

static void
ptyxis_window_menu_activate_cb (PtyxisWindow *self,
                                guint         position,
                                GtkListView  *list_view)
{
  GtkSelectionModel *model;
  g_autoptr(GObject) item = NULL;
  PtyxisTab *tab;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (GTK_IS_LIST_VIEW (list_view));

  model = gtk_list_view_get_model (list_view);
  item = g_list_model_get_item (G_LIST_MODEL (model), position);

  gtk_menu_button_popdown (self->new_terminal_menu_button);
  gtk_editable_set_text (GTK_EDITABLE (self->menu_search), "");

  /* First return focus before we make changes so that if we
   * come back to this window we get proper focus which is not
   * on the button itself.
   */
  if ((tab = ptyxis_window_get_active_tab (self)))
    ptyxis_tab_grab_focus (tab);

  if (PTYXIS_IS_PROFILE (item))
    {
      const char *uuid = ptyxis_profile_get_uuid (PTYXIS_PROFILE (item));

      gtk_widget_activate_action (GTK_WIDGET (self), "win.new-terminal", "(ss)", uuid, "");
    }
  else if (PTYXIS_IPC_IS_CONTAINER (item))
    {
      const char *id = ptyxis_ipc_container_get_id (PTYXIS_IPC_CONTAINER (item));

      gtk_widget_activate_action (GTK_WIDGET (self), "win.new-terminal", "(ss)", "", id);
    }
  else
    {
      g_warning ("Unknown object type %s", G_OBJECT_TYPE_NAME (item));
    }
}

static void
ptyxis_window_menu_search_activate_cb (PtyxisWindow *self,
                                       GtkSearchEntry *entry)
{
  GtkSelectionModel *model;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (GTK_IS_SEARCH_ENTRY (entry));

  model = gtk_list_view_get_model (self->menu_list_view);

  if (g_list_model_get_n_items (G_LIST_MODEL (model)) > 0)
    ptyxis_window_menu_activate_cb (self, 0, self->menu_list_view);
}


static void
ptyxis_window_notify_menu_n_items_cb (PtyxisWindow *self,
                                      GParamSpec   *pspec,
                                      GListModel   *model)
{
  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (G_IS_LIST_MODEL (model));

  if (g_list_model_get_n_items (model) == 0)
    gtk_stack_set_visible_child_name (self->menu_search_stack, "empty");
  else
    gtk_stack_set_visible_child_name (self->menu_search_stack, "results");
}

static gboolean
nonempty_display_name (gpointer item,
                       gpointer user_data)
{
  const char *str = ptyxis_ipc_container_get_display_name (item);

  return str && str[0];
}

static gboolean
empty_display_name (gpointer item,
                    gpointer user_data)
{
  const char *str = ptyxis_ipc_container_get_display_name (item);

  return !str || !str[0];
}

static void
ptyxis_window_bind_section_title (PtyxisWindow             *self,
                                  GtkListHeader            *header,
                                  GtkSignalListItemFactory *factory)
{
  const char *str;
  GtkWidget *child;
  GObject *item;
  gboolean visible = TRUE;

  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (GTK_IS_LIST_HEADER (header));
  g_assert (GTK_IS_SIGNAL_LIST_ITEM_FACTORY (factory));

  if (!(child = gtk_list_header_get_child (header)))
    {
      child = gtk_label_new (NULL);
      gtk_label_set_xalign (GTK_LABEL (child), .5);
      gtk_widget_add_css_class (child, "dimmed");
      gtk_widget_add_css_class (child, "title");
      gtk_list_header_set_child (header, child);
    }

  item = gtk_list_header_get_item (header);

  if (PTYXIS_IS_PROFILE (item))
    gtk_label_set_label (GTK_LABEL (child), _("Profiles"));
  else if (PTYXIS_IPC_IS_CONTAINER (item) &&
           (str = ptyxis_ipc_container_get_display_name (PTYXIS_IPC_CONTAINER (item))) &&
           (str && str[0]))
    gtk_label_set_label (GTK_LABEL (child), _("Containers"));
  else
    visible = FALSE;

  gtk_widget_set_visible (child, visible);
}

static void
ptyxis_window_size_allocate (GtkWidget *widget,
                             int        width,
                             int        height,
                             int        baseline)
{
  PtyxisWindow *self = (PtyxisWindow *)widget;

  g_assert (PTYXIS_IS_WINDOW (self));

  GTK_WIDGET_CLASS (ptyxis_window_parent_class)->size_allocate (widget, width, height, baseline);

  if (self->reset_nonvisible_from_size_allocate)
    {
      g_autoptr(GListModel) pages = ptyxis_window_list_pages (self);
      guint n_pages = g_list_model_get_n_items (pages);

      for (guint i = 0; i < n_pages; i++)
        {
          g_autoptr(AdwTabPage) page = g_list_model_get_item (pages, i);
          PtyxisTab *tab = PTYXIS_TAB (adw_tab_page_get_child (page));
          PtyxisTerminal *terminal = ptyxis_tab_get_terminal (tab);

          if (page == adw_tab_view_get_selected_page (self->tab_view))
            continue;

          ptyxis_terminal_reset_for_size (terminal);
        }

      self->reset_nonvisible_from_size_allocate = FALSE;
    }
}

static void
ptyxis_window_dispose (GObject *object)
{
  PtyxisWindow *self = (PtyxisWindow *)object;

  self->disposed = TRUE;

  g_action_map_remove_action (G_ACTION_MAP (self), "interface-style");

  gtk_widget_dispose_template (GTK_WIDGET (self), PTYXIS_TYPE_WINDOW);

  g_signal_group_set_target (self->active_tab_signals, NULL);
  g_binding_group_set_source (self->active_tab_bindings, NULL);
  g_binding_group_set_source (self->profile_bindings, NULL);
  g_signal_group_set_target (self->selected_page_signals, NULL);
  g_clear_handle_id (&self->focus_active_tab_source, g_source_remove);
  g_clear_object (&self->parking_lot);
  g_clear_object (&self->interface_style_action);

  G_OBJECT_CLASS (ptyxis_window_parent_class)->dispose (object);
}

static void
ptyxis_window_finalize (GObject *object)
{
  PtyxisWindow *self = (PtyxisWindow *)object;

  g_clear_object (&self->active_tab_bindings);
  g_clear_object (&self->active_tab_signals);
  g_clear_object (&self->profile_bindings);
  g_clear_object (&self->selected_page_signals);

  G_OBJECT_CLASS (ptyxis_window_parent_class)->finalize (object);
}

static void
ptyxis_window_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  PtyxisWindow *self = PTYXIS_WINDOW (object);

  switch (prop_id)
    {
    case PROP_ACTIVE_TAB:
      g_value_set_object (value, ptyxis_window_get_active_tab (self));
      break;

    case PROP_QUAKE_MODE:
      g_value_set_boolean (value, ptyxis_window_get_quake_mode (self));
      break;

    case PROP_SHORTCUTS:
      g_value_set_object (value, self->shortcuts);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ptyxis_window_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  PtyxisWindow *self = PTYXIS_WINDOW (object);

  switch (prop_id)
    {
    case PROP_ACTIVE_TAB:
      ptyxis_window_set_active_tab (self, g_value_get_object (value));
      break;

    case PROP_QUAKE_MODE:
      ptyxis_window_set_quake_mode (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ptyxis_window_class_init (PtyxisWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkWindowClass *window_class = GTK_WINDOW_CLASS (klass);

  object_class->constructed = ptyxis_window_constructed;
  object_class->dispose = ptyxis_window_dispose;
  object_class->finalize = ptyxis_window_finalize;
  object_class->get_property = ptyxis_window_get_property;
  object_class->set_property = ptyxis_window_set_property;

  widget_class->realize = ptyxis_window_realize;
  widget_class->size_allocate = ptyxis_window_size_allocate;

  window_class->close_request = ptyxis_window_close_request;

  properties[PROP_ACTIVE_TAB] =
    g_param_spec_object ("active-tab", NULL, NULL,
                         PTYXIS_TYPE_TAB,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_SHORTCUTS] =
    g_param_spec_object ("shortcuts", NULL, NULL,
                         PTYXIS_TYPE_SHORTCUTS,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_QUAKE_MODE] =
    g_param_spec_boolean ("quake-mode", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Ptyxis/ptyxis-window.ui");

  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, find_bar);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, find_bar_revealer);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, fullscreen_box);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, header_bar);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, maybe_containers);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, maybe_containers_filter);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, menu_list_view);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, menu_search);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, menu_search_stack);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, new_tab_box);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, new_terminal_button);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, new_terminal_menu_button);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, new_terminal_separator);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, not_containers);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, not_containers_filter);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, primary_menu);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, primary_menu_button);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, tab_bar);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, tab_menu);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, tab_overview);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, tab_overview_button);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, tab_view);
  gtk_widget_class_bind_template_child (widget_class, PtyxisWindow, visual_bell);

  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_bind_section_title);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_check_singular);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_get_container_title);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_get_header_title);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_menu_activate_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_menu_search_activate_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_menu_stop_search);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_notify_menu_n_items_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_page_attached_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_page_detached_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_notify_selected_page_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_create_window_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_close_page_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_setup_menu_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_tab_overview_notify_open_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_window_tab_overview_create_tab_cb);

  gtk_widget_class_install_action (widget_class, "win.primary-menu", NULL, ptyxis_window_primary_menu_action);
  gtk_widget_class_install_action (widget_class, "win.tab-menu", NULL, ptyxis_window_tab_menu_action);
  gtk_widget_class_install_action (widget_class, "win.new-tab", "(ss)", ptyxis_window_new_tab_action);
  gtk_widget_class_install_action (widget_class, "win.new-window", "(ss)", ptyxis_window_new_window_action);
  gtk_widget_class_install_action (widget_class, "win.new-terminal", "(ss)", ptyxis_window_new_terminal_action);
  gtk_widget_class_install_action (widget_class, "win.fullscreen", NULL, ptyxis_window_fullscreen_action);
  gtk_widget_class_install_action (widget_class, "win.unfullscreen", NULL, ptyxis_window_unfullscreen_action);
  gtk_widget_class_install_action (widget_class, "win.hide-quake", NULL, ptyxis_window_hide_quake_action);
  gtk_widget_class_install_action (widget_class, "win.toggle-fullscreen", NULL, ptyxis_window_toggle_fullscreen);
  gtk_widget_class_install_action (widget_class, "win.tab-overview", NULL, ptyxis_window_tab_overview_action);
  gtk_widget_class_install_action (widget_class, "win.go-to-terminal", NULL, ptyxis_window_go_to_terminal_action);
  gtk_widget_class_install_action (widget_class, "win.zoom-in", "b", ptyxis_window_zoom_in_action);
  gtk_widget_class_install_action (widget_class, "win.zoom-out", "b", ptyxis_window_zoom_out_action);
  gtk_widget_class_install_action (widget_class, "win.zoom-one", "b", ptyxis_window_zoom_one_action);
  gtk_widget_class_install_action (widget_class, "page.move-left", NULL, ptyxis_window_move_left_action);
  gtk_widget_class_install_action (widget_class, "page.move-right", NULL, ptyxis_window_move_right_action);
  gtk_widget_class_install_action (widget_class, "page.close", NULL, ptyxis_window_close_action);
  gtk_widget_class_install_action (widget_class, "page.close-others", NULL, ptyxis_window_close_others_action);
  gtk_widget_class_install_action (widget_class, "page.detach", NULL, ptyxis_window_detach_action);
  gtk_widget_class_install_action (widget_class, "tab.pin", NULL, ptyxis_window_tab_pin_action);
  gtk_widget_class_install_action (widget_class, "tab.unpin", NULL, ptyxis_window_tab_unpin_action);
  gtk_widget_class_install_action (widget_class, "tab.reset", "b", ptyxis_window_tab_reset_action);
  gtk_widget_class_install_action (widget_class, "tab.focus", "i", ptyxis_window_tab_focus_action);
  gtk_widget_class_install_action (widget_class, "page.next", NULL, ptyxis_window_page_next_action);
  gtk_widget_class_install_action (widget_class, "page.previous", NULL, ptyxis_window_page_previous_action);
  gtk_widget_class_install_action (widget_class, "win.set-title", NULL, ptyxis_window_set_title_action);
  gtk_widget_class_install_action (widget_class, "win.set-profile", NULL, ptyxis_window_set_profile_action);
  gtk_widget_class_install_action (widget_class, "win.search", NULL, ptyxis_window_search_action);
  gtk_widget_class_install_action (widget_class, "win.undo-close-tab", NULL, ptyxis_window_undo_close_tab_action);
  gtk_widget_class_install_action (widget_class, "my-computer", NULL, ptyxis_window_my_computer_action);
  gtk_widget_class_install_action (widget_class, "win.preferences", NULL, ptyxis_window_preferences_action);
  gtk_widget_class_install_action (widget_class, "win.show-keyboard-shortcuts", NULL, ptyxis_window_show_keyboard_shortcuts_action);

  g_type_ensure (PTYXIS_TYPE_FIND_BAR);
  g_type_ensure (PTYXIS_TYPE_FULLSCREEN_BOX);
  g_type_ensure (PTYXIS_TYPE_GATED_LIST_MODEL);
  g_type_ensure (PTYXIS_TYPE_MENU_ROW);
  g_type_ensure (PTYXIS_TYPE_SHRINKER);
}

static void
ptyxis_window_init (PtyxisWindow *self)
{
  g_autoptr(GIcon) default_icon = NULL;
  GtkGestureClick *click;

  self->active_tab_bindings = g_binding_group_new ();
  self->profile_bindings = g_binding_group_new ();

  self->selected_page_signals = g_signal_group_new (ADW_TYPE_TAB_PAGE);
  g_signal_connect_object (self->selected_page_signals,
                           "bind",
                           G_CALLBACK (ptyxis_window_selected_page_bind_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_group_connect_object (self->selected_page_signals,
                                 "notify::pinned",
                                 G_CALLBACK (ptyxis_window_selected_page_notify_pinned_cb),
                                 self,
                                 G_CONNECT_SWAPPED);

  self->active_tab_signals = g_signal_group_new (PTYXIS_TYPE_TAB);
  g_signal_connect_object (self->active_tab_signals,
                           "bind",
                           G_CALLBACK (ptyxis_window_active_tab_bind_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_group_connect_object (self->active_tab_signals,
                                 "bell",
                                 G_CALLBACK (ptyxis_window_active_tab_bell_cb),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (self->active_tab_signals,
                                 "commit",
                                 G_CALLBACK (ptyxis_window_active_tab_commit_cb),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (self->active_tab_signals,
                                 "notify::active-pane",
                                 G_CALLBACK (ptyxis_window_notify_active_pane_cb),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (self->active_tab_signals,
                                 "notify::process-leader-kind",
                                 G_CALLBACK (ptyxis_window_notify_process_leader_kind_cb),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (self->active_tab_signals,
                                 "notify::zoom",
                                 G_CALLBACK (ptyxis_window_notify_zoom_cb),
                                 self,
                                 G_CONNECT_SWAPPED);

  self->parking_lot = ptyxis_parking_lot_new ();

  self->shortcuts = g_object_ref (ptyxis_application_get_shortcuts (PTYXIS_APPLICATION_DEFAULT));

  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (self->find_bar_revealer,
                           "notify::reveal-child",
                           G_CALLBACK (ptyxis_window_find_bar_revealer_notify_cb),
                           self,
                           G_CONNECT_SWAPPED);

  default_icon = g_themed_icon_new ("utilities-terminal-symbolic");
  adw_tab_view_set_default_icon (self->tab_view, default_icon);

#if DEVELOPMENT_BUILD
  gtk_widget_add_css_class (GTK_WIDGET (self), "devel");
#endif

  g_signal_connect_object (self->shortcuts,
                           "notify",
                           G_CALLBACK (ptyxis_window_shortcuts_notify_cb),
                           self,
                           G_CONNECT_SWAPPED);
  ptyxis_window_shortcuts_notify_cb (self, NULL, self->shortcuts);

  adw_tab_view_set_shortcuts (self->tab_view, 0);

  g_binding_group_bind (self->active_tab_bindings, "profile",
                        self->profile_bindings, "source",
                        G_BINDING_SYNC_CREATE);
  g_binding_group_bind_full (self->active_tab_bindings, "title",
                             self, "title",
                             G_BINDING_SYNC_CREATE,
                             bind_title_cb, NULL, NULL, NULL);

  click = GTK_GESTURE_CLICK (gtk_gesture_click_new ());
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), 2);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (click),
                                              GTK_PHASE_CAPTURE);
  g_signal_connect_object (click,
                           "pressed",
                           G_CALLBACK (ptyxis_window_tab_bar_click_pressed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_widget_add_controller (GTK_WIDGET (self->tab_bar),
                             GTK_EVENT_CONTROLLER (g_steal_pointer (&click)));

  gtk_custom_filter_set_filter_func (self->not_containers_filter,
                                     empty_display_name, NULL, NULL);
  gtk_custom_filter_set_filter_func (self->maybe_containers_filter,
                                     nonempty_display_name, NULL, NULL);
}

PtyxisTab *
ptyxis_window_add_tab_for_command (PtyxisWindow       *self,
                                   PtyxisProfile      *profile,
                                   const char * const *argv,
                                   const char         *cwd_uri)
{
  g_autoptr(PtyxisProfile) default_profile = NULL;
  PtyxisTab *tab;

  g_return_val_if_fail (PTYXIS_IS_WINDOW (self), NULL);
  g_return_val_if_fail (!profile || PTYXIS_IS_PROFILE (profile), NULL);
  g_return_val_if_fail (argv != NULL && argv[0] != NULL, NULL);

  if (profile == NULL)
    {
      default_profile = ptyxis_application_dup_default_profile (PTYXIS_APPLICATION_DEFAULT);
      profile = default_profile;
    }

  tab = ptyxis_tab_new (profile);
  ptyxis_tab_set_command (tab, argv);

  if (!ptyxis_str_empty0 (cwd_uri))
    ptyxis_tab_set_previous_working_directory_uri (tab, cwd_uri);

  ptyxis_window_append_tab (self, tab);

  return tab;
}

static PtyxisWindow *
ptyxis_window_new_for_profile_and_command (PtyxisProfile      *profile,
                                           const char * const *argv,
                                           const char         *cwd_uri)
{
  g_autoptr(PtyxisProfile) default_profile = NULL;
  PtyxisSettings *settings;
  PtyxisTerminal *terminal;
  PtyxisWindow *self;
  PtyxisTab *tab;
  guint columns = 0;
  guint rows = 0;

  g_return_val_if_fail (!profile || PTYXIS_IS_PROFILE (profile), NULL);

  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);

  ptyxis_settings_get_default_size (settings, &columns, &rows);

  if (profile == NULL)
    {
      default_profile = ptyxis_application_dup_default_profile (PTYXIS_APPLICATION_DEFAULT);
      profile = default_profile;
    }

  g_assert (PTYXIS_IS_PROFILE (profile));

  self = g_object_new (PTYXIS_TYPE_WINDOW,
                       "application", PTYXIS_APPLICATION_DEFAULT,
                       NULL);

  tab = ptyxis_tab_new (profile);
  terminal = ptyxis_tab_get_terminal (tab);

  if (ptyxis_settings_get_restore_window_size (settings))
    ptyxis_settings_get_window_size (settings, &columns, &rows);

  if (!columns || !rows)
    ptyxis_settings_get_default_size (settings, &columns, &rows);

  vte_terminal_set_size (VTE_TERMINAL (terminal), columns, rows);

  if (argv != NULL && argv[0] != NULL)
    {
      ptyxis_tab_set_command (tab, argv);
      gtk_window_set_title (GTK_WINDOW (self), argv[0]);
    }

  if (!ptyxis_str_empty0 (cwd_uri))
    ptyxis_tab_set_previous_working_directory_uri (tab, cwd_uri);

  ptyxis_window_append_tab (self, tab);

  gtk_window_set_default_size (GTK_WINDOW (self), -1, -1);

  return self;
}

static void
ptyxis_window_setup_page (PtyxisWindow *self,
                          PtyxisTab    *tab,
                          AdwTabPage   *page)
{
  g_assert (PTYXIS_IS_WINDOW (self));
  g_assert (PTYXIS_IS_TAB (tab));
  g_assert (ADW_IS_TAB_PAGE (page));

  g_object_bind_property (tab, "title", page, "title", G_BINDING_SYNC_CREATE);
  g_object_bind_property (tab, "search-text", page, "keyword", G_BINDING_SYNC_CREATE);
  g_object_bind_property (tab, "icon", page, "icon", G_BINDING_SYNC_CREATE);
  g_object_bind_property (tab, "indicator-icon", page, "indicator-icon", G_BINDING_SYNC_CREATE);
  g_signal_handlers_disconnect_by_func (tab,
                                        G_CALLBACK (ptyxis_window_tab_search_text_changed_cb),
                                        self);
  g_signal_connect_object (tab, "notify::search-text",
                           G_CALLBACK (ptyxis_window_tab_search_text_changed_cb),
                           self, G_CONNECT_SWAPPED);
}

void
ptyxis_window_append_tab (PtyxisWindow *self,
                          PtyxisTab    *tab)
{
  AdwTabPage *page;

  g_return_if_fail (PTYXIS_IS_WINDOW (self));
  g_return_if_fail (PTYXIS_IS_TAB (tab));

  page = adw_tab_view_append (self->tab_view, GTK_WIDGET (tab));

  ptyxis_window_setup_page (self, tab, page);

  ptyxis_tab_grab_focus (tab);
}

PtyxisWindow *
ptyxis_window_new (void)
{
  return ptyxis_window_new_for_profile (NULL);
}

PtyxisWindow *
ptyxis_window_new_empty (void)
{
  return g_object_new (PTYXIS_TYPE_WINDOW,
                       "application", PTYXIS_APPLICATION_DEFAULT,
                       NULL);
}

PtyxisWindow *
ptyxis_window_new_for_command (PtyxisProfile      *profile,
                               const char * const *argv,
                               const char         *cwd_uri)
{
  PtyxisWindow *self;

  g_return_val_if_fail (!profile || PTYXIS_IS_PROFILE (profile), NULL);
  g_return_val_if_fail (argv != NULL, NULL);
  g_return_val_if_fail (argv[0] != NULL, NULL);

  self = ptyxis_window_new_for_profile_and_command (profile, argv, cwd_uri);

  if (self != NULL)
    {
      GApplication *app = G_APPLICATION (PTYXIS_APPLICATION_DEFAULT);
      GApplicationFlags flags = g_application_get_flags (app);

      self->single_terminal_mode = !!(flags & G_APPLICATION_NON_UNIQUE);

      gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.new-tab", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.new-window", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.new-terminal", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.tab-overview", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.move-left", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.move-right", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.close-others", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.detach", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "tab.pin", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "tab.unpin", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.next", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.previous", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.undo-close-tab", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "my-computer", FALSE);

      gtk_widget_set_visible (GTK_WIDGET (self->tab_bar), FALSE);
      gtk_widget_set_visible (self->new_tab_box, FALSE);
      gtk_widget_set_visible (self->tab_overview_button, FALSE);
    }

  return self;
}

PtyxisWindow *
ptyxis_window_new_for_profile (PtyxisProfile *profile)
{
  return ptyxis_window_new_for_profile_and_command (profile, NULL, NULL);
}

void
ptyxis_window_add_tab (PtyxisWindow *self,
                       PtyxisTab    *tab)
{
  PtyxisNewTabPosition new_tab_position;
  PtyxisApplication *app;
  PtyxisSettings *settings;
  AdwTabPage *page;
  int position = 0;

  g_return_if_fail (PTYXIS_IS_WINDOW (self));
  g_return_if_fail (PTYXIS_IS_TAB (tab));

  app = PTYXIS_APPLICATION_DEFAULT;
  settings = ptyxis_application_get_settings (app);
  new_tab_position = ptyxis_settings_get_new_tab_position (settings);

  if ((page = adw_tab_view_get_selected_page (self->tab_view)))
    {
      position = adw_tab_view_get_page_position (self->tab_view, page);

      switch (new_tab_position)
        {
        case PTYXIS_NEW_TAB_POSITION_NEXT:
          position++;
          break;

        case PTYXIS_NEW_TAB_POSITION_LAST:
          position = adw_tab_view_get_n_pages (self->tab_view);
          break;

        default:
          g_assert_not_reached ();
        }
    }

  page = adw_tab_view_insert (self->tab_view, GTK_WIDGET (tab), position);

  ptyxis_window_setup_page (self, tab, page);

  /* Resize if we are going from 1->2 tabs */
  if (adw_tab_view_get_n_pages (self->tab_view) == 2)
    gtk_window_set_default_size (GTK_WINDOW (self), -1, -1);
}

void
ptyxis_window_add_tab_at_end (PtyxisWindow *self,
                              PtyxisTab    *tab)
{
  AdwTabPage *page;
  int position;

  g_return_if_fail (PTYXIS_IS_WINDOW (self));
  g_return_if_fail (PTYXIS_IS_TAB (tab));

  position = adw_tab_view_get_n_pages (self->tab_view);
  page = adw_tab_view_insert (self->tab_view, GTK_WIDGET (tab), position);

  ptyxis_window_setup_page (self, tab, page);

  /* Resize if we are going from 1->2 tabs */
  if (adw_tab_view_get_n_pages (self->tab_view) == 2)
    gtk_window_set_default_size (GTK_WINDOW (self), -1, -1);
}

PtyxisTab *
ptyxis_window_get_active_tab (PtyxisWindow *self)
{
  AdwTabPage *page;

  g_return_val_if_fail (PTYXIS_IS_WINDOW (self), NULL);

  if (self->tab_view == NULL)
    return NULL;

  if (!(page = adw_tab_view_get_selected_page (self->tab_view)))
    return NULL;

  return PTYXIS_TAB (adw_tab_page_get_child (page));
}

void
ptyxis_window_set_active_tab (PtyxisWindow *self,
                              PtyxisTab    *tab)
{
  AdwTabPage *page;

  g_return_if_fail (PTYXIS_IS_WINDOW (self));
  g_return_if_fail (!tab || PTYXIS_IS_TAB (tab));

  if (self->tab_view == NULL)
    return;

  if (tab == NULL)
    return;

  if (!(page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (tab))))
    return;

  adw_tab_view_set_selected_page (self->tab_view, page);
}

static gboolean
ptyxis_window_remove_visual_bell (gpointer data)
{
  PtyxisWindow *self = data;

  g_assert (PTYXIS_IS_WINDOW (self));

  self->visual_bell_source = 0;

  gtk_widget_remove_css_class (GTK_WIDGET (self->visual_bell), "visual-bell");

  return G_SOURCE_REMOVE;
}

void
ptyxis_window_visual_bell (PtyxisWindow *self)
{
  PtyxisSettings *settings;

  g_return_if_fail (PTYXIS_IS_WINDOW (self));

  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);
  if (!ptyxis_settings_get_visual_bell (settings))
    return;

  gtk_widget_add_css_class (GTK_WIDGET (self->visual_bell), "visual-bell");

  g_clear_handle_id (&self->visual_bell_source, g_source_remove);

  self->visual_bell_source = g_timeout_add_full (G_PRIORITY_HIGH_IDLE,
                                                 /* Sync duration with style.css */
                                                 500,
                                                 ptyxis_window_remove_visual_bell,
                                                 g_object_ref (self),
                                                 g_object_unref);
}

/**
 * ptyxis_window_get_active_profile:
 * @self: a #PtyxisWindow
 *
 * Returns: (transfer none) (nullable): the profile of the active tab
 *   or %NULL if no tab is active.
 */
PtyxisProfile *
ptyxis_window_get_active_profile (PtyxisWindow *self)
{
  PtyxisTab *active_tab;

  g_return_val_if_fail (PTYXIS_IS_WINDOW (self), NULL);

  if ((active_tab = ptyxis_window_get_active_tab (self)))
    return ptyxis_tab_get_profile (active_tab);

  return NULL;
}

/**
 * ptyxis_window_list_pages:
 * @self: a #PtyxisWindow
 *
 * Gets the list of pages in the window.
 *
 * Returns: (transfer full): a #GListModel of #AdwTabPage
 */
GListModel *
ptyxis_window_list_pages (PtyxisWindow *self)
{
  g_return_val_if_fail (PTYXIS_IS_WINDOW (self), NULL);

  return G_LIST_MODEL (adw_tab_view_get_pages (self->tab_view));
}

gboolean
ptyxis_window_focus_tab_by_uuid (PtyxisWindow *self,
                                 const char   *uuid)
{
  g_autoptr(GtkSelectionModel) model = NULL;
  guint n_items;

  g_return_val_if_fail (PTYXIS_IS_WINDOW (self), FALSE);
  g_return_val_if_fail (uuid != NULL, FALSE);

  model = adw_tab_view_get_pages (self->tab_view);
  n_items = g_list_model_get_n_items (G_LIST_MODEL (model));

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(AdwTabPage) page = g_list_model_get_item (G_LIST_MODEL (model), i);
      PtyxisTab *tab = PTYXIS_TAB (adw_tab_page_get_child (page));

      g_debug ("Window has tab \"%s\"", ptyxis_tab_get_uuid (tab));

      if (0 == g_strcmp0 (uuid, ptyxis_tab_get_uuid (tab)))
        {
          ptyxis_window_set_active_tab (self, tab);
          gtk_window_present (GTK_WINDOW (self));
          ptyxis_tab_grab_focus (tab);
          return TRUE;
        }
    }

  return FALSE;
}

gboolean
ptyxis_window_focus_pane_by_uuid (PtyxisWindow *self,
                                  const char   *tab_uuid,
                                  const char   *pane_uuid)
{
  guint n_pages;

  g_return_val_if_fail (PTYXIS_IS_WINDOW (self), FALSE);
  g_return_val_if_fail (tab_uuid != NULL, FALSE);
  g_return_val_if_fail (pane_uuid != NULL, FALSE);

  n_pages = adw_tab_view_get_n_pages (self->tab_view);
  for (guint i = 0; i < n_pages; i++)
    {
      AdwTabPage *page = adw_tab_view_get_nth_page (self->tab_view, i);
      PtyxisTab *tab = PTYXIS_TAB (adw_tab_page_get_child (page));
      PtyxisPane *pane;

      if (g_strcmp0 (ptyxis_tab_get_uuid (tab), tab_uuid) != 0 ||
          !(pane = ptyxis_tab_find_pane_by_uuid (tab, pane_uuid)))
        continue;

      adw_tab_view_set_selected_page (self->tab_view, page);
      ptyxis_tab_set_active_pane (tab, pane);
      gtk_window_present (GTK_WINDOW (self));
      gtk_widget_grab_focus (GTK_WIDGET (ptyxis_pane_get_terminal (pane)));
      return TRUE;
    }

  return FALSE;
}

gboolean
ptyxis_window_is_animating (PtyxisWindow *self)
{
  g_return_val_if_fail (PTYXIS_IS_WINDOW (self), FALSE);

  return self->tab_overview_animating;
}

gboolean
ptyxis_window_get_single_terminal_mode (PtyxisWindow *self)
{
  g_return_val_if_fail (PTYXIS_IS_WINDOW (self), FALSE);
  return self->single_terminal_mode;
}

gboolean
ptyxis_window_get_quake_mode (PtyxisWindow *self)
{
  g_return_val_if_fail (PTYXIS_IS_WINDOW (self), FALSE);

  return self->quake_mode;
}

void
ptyxis_window_set_quake_mode (PtyxisWindow *self,
                              gboolean      quake_mode)
{
  g_return_if_fail (PTYXIS_IS_WINDOW (self));

  quake_mode = !!quake_mode;

  if (self->quake_mode == quake_mode)
    return;

  self->quake_mode = quake_mode;

  if (quake_mode)
    gtk_widget_add_css_class (GTK_WIDGET (self), "quake");
  else
    gtk_widget_remove_css_class (GTK_WIDGET (self), "quake");

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_QUAKE_MODE]);
}
