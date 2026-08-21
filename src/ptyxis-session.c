/*
 * ptyxis-session.c
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

#include "ptyxis-application.h"
#include "ptyxis-session.h"
#include "ptyxis-settings.h"
#include "ptyxis-util.h"
#include "ptyxis-window.h"

GVariant *
ptyxis_session_save (PtyxisApplication *app)
{
  PtyxisSettings *settings;
  GVariantBuilder builder;
  gboolean restore_session;

  g_return_val_if_fail (PTYXIS_IS_APPLICATION (app), NULL);

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add_parsed (&builder, "{'version', <%u>}", 2);
  g_variant_builder_open (&builder, G_VARIANT_TYPE ("{sv}"));
  g_variant_builder_add (&builder, "s", "windows");
  g_variant_builder_open (&builder, G_VARIANT_TYPE ("v"));
  g_variant_builder_open (&builder, G_VARIANT_TYPE ("aa{sv}"));

  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);
  restore_session = ptyxis_settings_get_restore_session (settings);

  for (const GList *list = gtk_application_get_windows (GTK_APPLICATION (app));
       list != NULL;
       list = list->next)
    {
      if (PTYXIS_IS_WINDOW (list->data))
        {
          PtyxisWindow *window = PTYXIS_WINDOW (list->data);
          g_autoptr(GListModel) pages = ptyxis_window_list_pages (window);
          guint n_items = g_list_model_get_n_items (pages);

          g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));

          if (gtk_window_is_maximized (GTK_WINDOW (window)))
            g_variant_builder_add_parsed (&builder, "{'maximized', <%b>}", TRUE);

          if (gtk_widget_get_width (GTK_WIDGET (window)) > 0 &&
              gtk_widget_get_height (GTK_WIDGET (window)) > 0)
            g_variant_builder_add (&builder, "{sv}", "size",
                                   g_variant_new ("(ii)",
                                                  gtk_widget_get_width (GTK_WIDGET (window)),
                                                  gtk_widget_get_height (GTK_WIDGET (window))));

          g_variant_builder_open (&builder, G_VARIANT_TYPE ("{sv}"));
          g_variant_builder_add (&builder, "s", "tabs");
          g_variant_builder_open (&builder, G_VARIANT_TYPE ("v"));
          g_variant_builder_open (&builder, G_VARIANT_TYPE ("aa{sv}"));

          for (guint i = 0; i < n_items; i++)
            {
              g_autoptr(AdwTabPage) page = g_list_model_get_item (pages, i);
              gboolean pinned;

              g_assert (ADW_IS_TAB_PAGE (page));

              pinned = adw_tab_page_get_pinned (page);

              if (restore_session || pinned)
                {
                  PtyxisTab *tab = PTYXIS_TAB (adw_tab_page_get_child (page));
                  g_autoptr(PtyxisIpcContainer) container = NULL;
                  g_autoptr(GVariant) layout = NULL;
                  g_autofree char *default_container = NULL;
                  g_autofree char *cwd = NULL;
                  PtyxisTerminal *terminal;
                  PtyxisProfile *profile;
                  const char *container_id = NULL;
                  const char *window_title;
                  const char *uuid;
                  gboolean is_active;
                  PtyxisZoomLevel zoom;
                  guint rows;
                  guint columns;

                  g_assert (PTYXIS_IS_TAB (tab));

                  profile = ptyxis_tab_get_profile (tab);
                  uuid = ptyxis_profile_get_uuid (profile);
                  default_container = ptyxis_profile_dup_default_container (profile);
                  container = ptyxis_tab_dup_container (tab);
                  is_active = ptyxis_window_get_active_tab (window) == tab;

                  terminal = ptyxis_tab_get_terminal (tab);
                  layout = ptyxis_tab_serialize_layout (tab);
                  ptyxis_tab_get_grid_size (tab, &columns, &rows);
                  cwd = ptyxis_terminal_dup_current_directory_uri (terminal);
                  zoom = ptyxis_tab_get_zoom (tab);

                  /* If we had an initial directory and we've never got an update,
                   * it could be that the tab has never spawned to process the
                   * directory state updates.
                   */
                  if (cwd == NULL)
                    cwd = ptyxis_tab_dup_previous_working_directory_uri (tab);

                  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
                  if (!(window_title = vte_terminal_get_window_title (VTE_TERMINAL (terminal))))
                    window_title = ptyxis_tab_get_initial_title (tab);
                  G_GNUC_END_IGNORE_DEPRECATIONS

                  if (container != NULL)
                    container_id = ptyxis_ipc_container_get_id (container);

                  g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));
                  g_variant_builder_add_parsed (&builder, "{'profile', <%s>}", uuid);
                  g_variant_builder_add_parsed (&builder, "{'pinned', <%b>}", pinned);
                  g_variant_builder_add_parsed (&builder, "{'size', <(%u,%u)>}", columns, rows);
                  if (zoom != PTYXIS_ZOOM_LEVEL_DEFAULT)
                    g_variant_builder_add_parsed (&builder, "{'zoom', <%u>}", zoom);
                  g_variant_builder_add_parsed (&builder, "{'active', <%b>}", is_active);
                  g_variant_builder_add (&builder, "{sv}", "active-pane",
                                         g_variant_new_string (ptyxis_pane_get_uuid (ptyxis_tab_get_active_pane (tab))));
                  g_variant_builder_add (&builder, "{sv}", "layout",
                                         layout);
                  if (!ptyxis_str_empty0 (window_title))
                    g_variant_builder_add_parsed (&builder, "{'window-title', <%s>}", window_title);
                  if (!ptyxis_str_empty0 (cwd))
                    g_variant_builder_add_parsed (&builder, "{'cwd', <%s>}", cwd);
                  if (container_id != NULL &&
                      g_strcmp0 (default_container, container_id) != 0)
                    g_variant_builder_add_parsed (&builder, "{'container', <%s>}", container_id);
                  g_variant_builder_close (&builder);
                }
            }

          g_variant_builder_close (&builder);
          g_variant_builder_close (&builder);
          g_variant_builder_close (&builder);
          g_variant_builder_close (&builder);
        }
    }

  g_variant_builder_close (&builder);
  g_variant_builder_close (&builder);
  g_variant_builder_close (&builder);

  return g_variant_take_ref (g_variant_builder_end (&builder));
}

gboolean
ptyxis_session_restore (PtyxisApplication *app,
                        GVariant          *state)
{
  g_autoptr(GVariant) windows = NULL;
  PtyxisSettings *settings;
  GVariantIter iter;
  GVariant *window;
  gboolean restore_session;
  gboolean restore_window_size;
  gboolean added_window = FALSE;
  guint32 version;

  g_return_val_if_fail (PTYXIS_IS_APPLICATION (app), FALSE);
  g_return_val_if_fail (state != NULL, FALSE);
  g_return_val_if_fail (g_variant_is_of_type (state, G_VARIANT_TYPE ("a{sv}")), FALSE);

  if (!g_variant_lookup (state, "version", "u", &version))
    return FALSE;

  if (!(windows = g_variant_lookup_value (state, "windows", G_VARIANT_TYPE ("aa{sv}"))))
    return FALSE;

  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);
  restore_session = ptyxis_settings_get_restore_session (settings);
  restore_window_size = ptyxis_settings_get_restore_window_size (settings);

  g_variant_iter_init (&iter, windows);
  while (g_variant_iter_loop (&iter, "@a{sv}", &window))
    {
      g_autoptr(GVariant) tabs = NULL;
      g_autoptr(GVariant) tab = NULL;
      PtyxisWindow *the_window = NULL;
      PtyxisTab *active_tab = NULL;
      GVariantIter tab_iter;
      gboolean maximized;
      gboolean has_window_size;
      int window_height = 0;
      int window_width = 0;

      if (!(tabs = g_variant_lookup_value (window, "tabs", G_VARIANT_TYPE ("aa{sv}"))) ||
          g_variant_n_children (tabs) == 0)
        continue;

      if (!g_variant_lookup (window, "maximized", "b", &maximized))
        maximized = FALSE;

      has_window_size = version >= 2 &&
                        g_variant_lookup (window, "size", "(ii)",
                                          &window_width, &window_height) &&
                        window_width > 0 && window_height > 0;

      g_variant_iter_init (&tab_iter, tabs);
      while (g_variant_iter_loop (&tab_iter, "@a{sv}", &tab))
        {
          g_autoptr(PtyxisIpcContainer) the_container = NULL;
          g_autoptr(PtyxisProfile) the_profile = NULL;
          PtyxisTerminal *terminal;
          const char *profile;
          const char *container;
          const char *cwd;
          const char *window_title;
          const char *active_pane_uuid;
          PtyxisTab *the_tab;
          guint32 zoom;
          gboolean is_active;
          gboolean pinned;
          guint32 columns;
          guint32 rows;
          g_autoptr(GVariant) layout = NULL;

          if (!g_variant_lookup (tab, "profile", "&s", &profile))
            profile = NULL;

          if (!g_variant_lookup (tab, "container", "&s", &container))
            container = NULL;

          if (!g_variant_lookup (tab, "pinned", "b", &pinned))
            pinned = FALSE;

          if (!pinned && !restore_session)
            continue;

          if (!restore_window_size ||
              !g_variant_lookup (tab, "size", "(uu)", &columns, &rows))
            ptyxis_settings_get_default_size (settings, &columns, &rows);

          if (!g_variant_lookup (tab, "cwd", "&s", &cwd))
            cwd = NULL;

          if (!g_variant_lookup (tab, "window-title", "&s", &window_title))
            window_title = NULL;

          if (!g_variant_lookup (tab, "active-pane", "&s", &active_pane_uuid))
            active_pane_uuid = NULL;

          if (version >= 2)
            layout = g_variant_lookup_value (tab, "layout", G_VARIANT_TYPE_VARDICT);

          if (!g_variant_lookup (tab, "active", "b", &is_active))
            is_active = FALSE;

          if (!g_variant_lookup (tab, "zoom", "u", &zoom) || zoom >= PTYXIS_ZOOM_LEVEL_LAST)
            zoom = PTYXIS_ZOOM_LEVEL_DEFAULT;

          if (!ptyxis_str_empty0 (container))
            the_container = ptyxis_application_lookup_container (app, container);

          if (!ptyxis_str_empty0 (profile))
            the_profile = ptyxis_application_dup_profile (app, profile);

          if (the_profile == NULL)
            the_profile = ptyxis_application_dup_default_profile (app);

          if (the_window == NULL)
            the_window = ptyxis_window_new_empty ();

          the_tab = ptyxis_tab_new (the_profile);

          if (the_container != NULL)
            ptyxis_tab_set_container (the_tab, the_container);

          if (cwd != NULL)
            ptyxis_tab_set_previous_working_directory_uri (the_tab, cwd);

          if (window_title != NULL)
            ptyxis_tab_set_initial_title (the_tab, window_title);

          terminal = ptyxis_tab_get_terminal (the_tab);

          if (!maximized)
            vte_terminal_set_size (VTE_TERMINAL (terminal), columns, rows);

          if (zoom != PTYXIS_ZOOM_LEVEL_DEFAULT)
            ptyxis_tab_set_zoom (the_tab, zoom);

          if (layout != NULL)
            ptyxis_tab_restore_layout (the_tab, layout, active_pane_uuid);

          ptyxis_window_add_tab_at_end (the_window, the_tab);
          ptyxis_window_set_tab_pinned (the_window, the_tab, pinned);

          if (is_active)
            active_tab = the_tab;
        }

      if (the_window != NULL)
        {
          if (!restore_session)
            {
              g_autoptr(PtyxisProfile) the_profile = NULL;
              PtyxisTab *the_tab;

              /* Also add a default profile tab which will be our focused
               * tab for the new window since we're not restoring the
               * full tab session for the user.
               */
              the_profile = ptyxis_application_dup_default_profile (app);
              the_tab = ptyxis_tab_new (the_profile);
              ptyxis_window_add_tab_at_end (the_window, the_tab);

              if (!active_tab)
                active_tab = the_tab;
            }

          if (!maximized && restore_window_size && has_window_size)
            gtk_window_set_default_size (GTK_WINDOW (the_window),
                                         window_width, window_height);

          if (maximized)
            gtk_window_maximize (GTK_WINDOW (the_window));

          gtk_window_present (GTK_WINDOW (the_window));

          if (active_tab)
            {
              ptyxis_window_set_active_tab (the_window, active_tab);
              ptyxis_tab_grab_focus (active_tab);
            }
        }

      added_window |= the_window != NULL;
    }

  return added_window;
}
