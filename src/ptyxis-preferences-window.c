/*
 * ptyxis-preferences-window.c
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

#include <math.h>

#include <glib/gi18n.h>

#include "gtk/gtk.h"
#include "ptyxis-add-button-list-item.h"
#include "ptyxis-add-button-list-model.h"
#include "ptyxis-application.h"
#include "ptyxis-custom-link-editor.h"
#include "ptyxis-custom-link-row.h"
#include "ptyxis-palette-preview.h"
#include "ptyxis-preferences-list-item.h"
#include "ptyxis-preferences-window.h"
#include "ptyxis-profile-editor.h"
#include "ptyxis-profile-row.h"
#include "ptyxis-shortcut-row.h"
#include "ptyxis-util.h"


/* This will not transition to AdwDialog until there is a way for
 * toplevel windows _with_ transient-for set to maintain window
 * group ordering.
 */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS

struct _PtyxisPreferencesWindow
{
  AdwPreferencesWindow  parent_instance;

  char                 *default_palette_id;
  GtkCustomFilter      *filter;
  GtkFilterListModel   *filter_palettes;
  guint                 filter_show_more : 1;

  AdwSwitchRow         *audible_bell;
  AdwComboRow          *backspace_binding;
  AdwSpinRow           *cell_height_scale;
  AdwSpinRow           *cell_width_scale;
  AdwComboRow          *cjk_ambiguous_width;
  GListModel           *cjk_ambiguous_widths;
  AdwComboRow          *cursor_blink_mode;
  GListModel           *cursor_blink_modes;
  AdwComboRow          *cursor_shape;
  GListModel           *cursor_shapes;
  AdwComboRow          *delete_binding;
  AdwSwitchRow         *enable_a11y;
  GListModel           *erase_bindings;
  AdwComboRow          *exit_action;
  GListModel           *exit_actions;
  GtkLabel             *font_name;
  AdwActionRow         *font_name_row;
  AdwSwitchRow         *limit_scrollback;
  AdwSwitchRow         *login_shell;
  GtkAdjustment        *opacity_adjustment;
  AdwPreferencesGroup  *opacity_group;
  GtkLabel             *opacity_label;
  GtkFlowBox           *palette_previews;
  AdwComboRow          *preserve_directory;
  GListModel           *preserve_directories;
  GtkListBox           *profiles_list_box;
  AdwSwitchRow         *restore_session;
  GtkSwitch            *restore_window_size;
  AdwSpinRow           *default_rows;
  AdwSpinRow           *default_columns;
  AdwSpinRow           *scrollback_lines;
  AdwSwitchRow         *scroll_on_output;
  AdwSwitchRow         *scroll_on_keystroke;
  AdwComboRow          *scrollbar_policy;
  GListModel           *scrollbar_policies;
  PtyxisShortcutRow    *shortcut_close_other_tabs;
  PtyxisShortcutRow    *shortcut_close_tab;
  PtyxisShortcutRow    *shortcut_close_window;
  PtyxisShortcutRow    *shortcut_copy_clipboard;
  PtyxisShortcutRow    *shortcut_detach_tab;
  PtyxisShortcutRow    *shortcut_focus_tab_10;
  PtyxisShortcutRow    *shortcut_focus_tab_1;
  PtyxisShortcutRow    *shortcut_focus_tab_2;
  PtyxisShortcutRow    *shortcut_focus_tab_3;
  PtyxisShortcutRow    *shortcut_focus_tab_4;
  PtyxisShortcutRow    *shortcut_focus_tab_5;
  PtyxisShortcutRow    *shortcut_focus_tab_6;
  PtyxisShortcutRow    *shortcut_focus_tab_7;
  PtyxisShortcutRow    *shortcut_focus_tab_8;
  PtyxisShortcutRow    *shortcut_focus_tab_9;
  PtyxisShortcutRow    *shortcut_focus_tab_last;
  PtyxisShortcutRow    *shortcut_focus_pane_down;
  PtyxisShortcutRow    *shortcut_focus_pane_left;
  PtyxisShortcutRow    *shortcut_focus_pane_right;
  PtyxisShortcutRow    *shortcut_focus_pane_up;
  PtyxisShortcutRow    *shortcut_move_next_tab;
  PtyxisShortcutRow    *shortcut_move_previous_tab;
  PtyxisShortcutRow    *shortcut_move_tab_left;
  PtyxisShortcutRow    *shortcut_move_tab_right;
  PtyxisShortcutRow    *shortcut_new_tab;
  PtyxisShortcutRow    *shortcut_new_window;
  PtyxisShortcutRow    *shortcut_paste_clipboard;
  PtyxisShortcutRow    *shortcut_popup_menu;
  PtyxisShortcutRow    *shortcut_set_title;
  PtyxisShortcutRow    *shortcut_preferences;
  PtyxisShortcutRow    *shortcut_show_keyboard_shortcuts;
  PtyxisShortcutRow    *shortcut_primary_menu;
  PtyxisShortcutRow    *shortcut_tab_menu;
  PtyxisShortcutRow    *shortcut_reset;
  PtyxisShortcutRow    *shortcut_reset_and_clear;
  PtyxisShortcutRow    *shortcut_search;
  PtyxisShortcutRow    *shortcut_select_all;
  PtyxisShortcutRow    *shortcut_select_none;
  PtyxisShortcutRow    *shortcut_split_auto;
  PtyxisShortcutRow    *shortcut_split_horizontal;
  PtyxisShortcutRow    *shortcut_split_vertical;
  PtyxisShortcutRow    *shortcut_tab_overview;
  PtyxisShortcutRow    *shortcut_toggle_fullscreen;
  PtyxisShortcutRow    *shortcut_undo_close_tab;
  PtyxisShortcutRow    *shortcut_zoom_in;
  PtyxisShortcutRow    *shortcut_zoom_one;
  PtyxisShortcutRow    *shortcut_zoom_out;
  AdwButtonContent     *show_more_palettes;
  AdwComboRow          *tab_position;
  GListModel           *tab_positions;
  AdwComboRow          *text_blink_mode;
  GListModel           *text_blink_modes;
  AdwSwitchRow         *use_system_font;
  AdwSwitchRow         *visual_bell;
  GtkListBox           *custom_links_list_box;
  AdwSwitchRow         *select_to_copy;
};

G_DEFINE_FINAL_TYPE (PtyxisPreferencesWindow, ptyxis_preferences_window, ADW_TYPE_PREFERENCES_WINDOW)

enum {
  PROP_0,
  PROP_DEFAULT_PALETTE_ID,
  N_PROPS
};

static PtyxisPreferencesWindow *instance;
static GParamSpec *properties[N_PROPS];

static gboolean
ptyxis_preferences_window_spin_row_show_decimal_cb (PtyxisPreferencesWindow *self,
                                                    AdwSpinRow              *spin)
{
  g_autofree char *text = NULL;
  double value;

  g_assert (PTYXIS_IS_PREFERENCES_WINDOW (self));
  g_assert (ADW_IS_SPIN_ROW (spin));

  value = adw_spin_row_get_value (spin);
  text = g_strdup_printf ("%.1f", value);
  gtk_editable_set_text (GTK_EDITABLE (spin), text);

  return TRUE;
}

static void
invalidate_filter (PtyxisPreferencesWindow *self)
{
  g_autoptr(PtyxisProfile) default_profile = NULL;
  g_autofree char *default_palette_id = NULL;

  g_assert (PTYXIS_IS_PREFERENCES_WINDOW (self));

  default_profile = ptyxis_application_dup_default_profile (PTYXIS_APPLICATION_DEFAULT);
  default_palette_id = ptyxis_profile_dup_palette_id (default_profile);
  g_set_str (&self->default_palette_id, default_palette_id);

  gtk_filter_changed (GTK_FILTER (self->filter), GTK_FILTER_CHANGE_DIFFERENT);
}

static gboolean
do_filter_palettes (gpointer item,
                    gpointer user_data)
{
  PtyxisPreferencesWindow *self = user_data;
  PtyxisPalette *palette = item;
  AdwStyleManager *style_manager;
  gboolean dark;

  if (ptyxis_palette_is_primary (palette))
    return TRUE;

  if (g_strcmp0 (self->default_palette_id, ptyxis_palette_get_id (palette)) == 0)
    return TRUE;

  if (!self->filter_show_more)
    return FALSE;

  style_manager = adw_style_manager_get_default ();
  dark = adw_style_manager_get_dark (style_manager);

  if (dark && !ptyxis_palette_has_dark (palette))
    return FALSE;

  if (!dark && !ptyxis_palette_has_light (palette))
    return FALSE;

  return TRUE;
}

static gboolean
monospace_filter (gpointer item,
                  gpointer user_data)
{
  PangoFontFamily *family = NULL;

  if (PANGO_IS_FONT_FAMILY (item))
    family = PANGO_FONT_FAMILY (item);
  else if (PANGO_IS_FONT_FACE (item))
    family = pango_font_face_get_family (PANGO_FONT_FACE (item));

  return family ? pango_font_family_is_monospace (family) : FALSE;
}

static void
ptyxis_preferences_window_select_custom_font_cb (GObject      *object,
                                                 GAsyncResult *result,
                                                 gpointer      user_data)
{
  GtkFontDialog *dialog = GTK_FONT_DIALOG (object);
  g_autoptr(PangoFontDescription) font_desc = NULL;
  g_autoptr(PtyxisSettings) settings = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (GTK_IS_FONT_DIALOG (dialog));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (PTYXIS_IS_SETTINGS (settings));

  if ((font_desc = gtk_font_dialog_choose_font_finish (dialog, result, &error)))
    {
      g_autofree char *font_name = pango_font_description_to_string (font_desc);

      if (!ptyxis_str_empty0 (font_name))
        ptyxis_settings_set_font_name (settings, font_name);
    }
}

static void
ptyxis_preferences_window_select_custom_font (GtkWidget  *widget,
                                              const char *action_name,
                                              GVariant   *param)
{
  g_autoptr(PangoFontDescription) font_desc = NULL;
  g_autoptr(GtkCustomFilter) filter = NULL;
  g_autoptr(GtkFontDialog) dialog = NULL;
  g_autofree char *font_name = NULL;
  PtyxisApplication *app;
  PtyxisSettings *settings;

  g_assert (PTYXIS_IS_PREFERENCES_WINDOW (widget));

  app = PTYXIS_APPLICATION_DEFAULT;
  settings = ptyxis_application_get_settings (app);

  font_name = ptyxis_settings_dup_font_name (settings);
  if (ptyxis_str_empty0 (font_name))
    g_set_str (&font_name, ptyxis_application_get_system_font_name (app));

  font_desc = pango_font_description_from_string (font_name);

  filter = gtk_custom_filter_new (monospace_filter, NULL, NULL);
  dialog = (GtkFontDialog *)g_object_new (GTK_TYPE_FONT_DIALOG,
                                          "title", _("Select Font"),
                                          "filter", filter,
                                          NULL);

  gtk_font_dialog_choose_font (dialog,
                               GTK_WINDOW (gtk_widget_get_root (widget)),
                               font_desc,
                               NULL,
                               ptyxis_preferences_window_select_custom_font_cb,
                               g_object_ref (settings));
}

static void
ptyxis_preferences_window_add_profile (GtkWidget  *widget,
                                       const char *action_name,
                                       GVariant   *param)
{
  g_autoptr(PtyxisProfile) profile = NULL;

  g_assert (PTYXIS_IS_PREFERENCES_WINDOW (widget));

  profile = ptyxis_profile_new (NULL);

  ptyxis_application_add_profile (PTYXIS_APPLICATION_DEFAULT, profile);
}

static void
ptyxis_preferences_window_add_toast (GtkWidget  *widget,
                                     const char *action_name,
                                     GVariant   *param)
{
  PtyxisPreferencesWindow *self = PTYXIS_PREFERENCES_WINDOW (widget);
  AdwToast *toast;
  const char *title;
  guint timeout;

  if (!g_variant_lookup (param, "title", "&s", &title))
    title = "";

  if (!g_variant_lookup (param, "timeout", "u", &timeout))
    timeout = 0;

  toast = g_object_new (ADW_TYPE_TOAST,
                        "title", title,
                        "timeout", timeout,
                        NULL);

  adw_preferences_window_add_toast (ADW_PREFERENCES_WINDOW (self), toast);
}

static void
ptyxis_preferences_window_profile_row_activated_cb (PtyxisPreferencesWindow *self,
                                                    PtyxisProfileRow        *row)
{
  PtyxisProfile *profile;

  g_assert (PTYXIS_IS_PREFERENCES_WINDOW (self));
  g_assert (PTYXIS_IS_PROFILE_ROW (row));

  profile = ptyxis_profile_row_get_profile (row);

  ptyxis_preferences_window_edit_profile (self, profile);
}

static gboolean
ptyxis_preferences_window_show_all_cb (PtyxisPreferencesWindow *self,
                                       GtkButton               *button)
{
  g_assert (PTYXIS_IS_PREFERENCES_WINDOW (self));
  g_assert (GTK_IS_BUTTON (button));

  self->filter_show_more = !self->filter_show_more;

  if (self->filter_show_more)
    {
      adw_button_content_set_label (self->show_more_palettes, _("Show Fewer Palettes"));
      adw_button_content_set_icon_name (self->show_more_palettes, "up-small-symbolic");
    }
  else
    {
      adw_button_content_set_label (self->show_more_palettes, _("Show All Palettes"));
      adw_button_content_set_icon_name (self->show_more_palettes, "down-small-symbolic");
    }

  gtk_filter_changed (GTK_FILTER (self->filter), GTK_FILTER_CHANGE_DIFFERENT);

  return TRUE;
}

static GtkWidget *
ptyxis_preferences_window_create_profile_row_cb (gpointer item,
                                                 gpointer user_data)
{
  PtyxisAddButtonListItem *add_button_item = PTYXIS_ADD_BUTTON_LIST_ITEM (item);
  GObject *wrapped_item;

  wrapped_item = ptyxis_add_button_list_item_get_item (add_button_item);

  if (wrapped_item != NULL)
    {
      return ptyxis_profile_row_new (PTYXIS_PROFILE (wrapped_item));
    }
  else
    {
      return g_object_new (ADW_TYPE_BUTTON_ROW,
                           "title", _("Add Profile"),
                           "start-icon-name", "list-add-symbolic",
                           "action-name", "profile.add",
                           NULL);
    }
}

static gboolean
string_to_index (GValue   *value,
                 GVariant *variant,
                 gpointer  user_data)
{
  GListModel *model = G_LIST_MODEL (user_data);
  guint n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(PtyxisPreferencesListItem) item = PTYXIS_PREFERENCES_LIST_ITEM (g_list_model_get_item (model, i));
      GVariant *item_value = ptyxis_preferences_list_item_get_value (item);

      if (g_variant_equal (variant, item_value))
        {
          g_value_set_uint (value, i);
          return TRUE;
        }
    }

  return FALSE;
}

static GVariant *
index_to_string (const GValue       *value,
                 const GVariantType *type,
                 gpointer            user_data)
{
  guint index = g_value_get_uint (value);
  GListModel *model = G_LIST_MODEL (user_data);
  g_autoptr(PtyxisPreferencesListItem) item = PTYXIS_PREFERENCES_LIST_ITEM (g_list_model_get_item (model, index));

  if (item != NULL)
    return g_variant_ref (ptyxis_preferences_list_item_get_value (item));

  return NULL;
}

static gboolean
map_palette_to_selected (GBinding     *binding,
                         const GValue *from_value,
                         GValue       *to_value,
                         gpointer      user_data)
{
  PtyxisPalette *current;
  PtyxisPalette *palette;

  g_assert (G_IS_BINDING (binding));
  g_assert (from_value != NULL);
  g_assert (to_value != NULL);

  palette = g_value_get_object (from_value);
  current = ptyxis_palette_preview_get_palette (user_data);

  g_value_set_boolean (to_value,
                       0 == g_strcmp0 (ptyxis_palette_get_id (palette),
                                       ptyxis_palette_get_id (current)));
  return TRUE;
}

static gboolean
opacity_to_label (GBinding     *binding,
                  const GValue *from_value,
                  GValue       *to_value,
                  gpointer      user_data)
{
  double opacity;

  g_assert (G_IS_BINDING (binding));
  g_assert (G_VALUE_HOLDS_DOUBLE (from_value));
  g_assert (G_VALUE_HOLDS_STRING (to_value));

  opacity = g_value_get_double (from_value);
  g_value_take_string (to_value, g_strdup_printf ("%3.0lf%%", floor (100.*opacity)));

  return TRUE;
}

static GtkWidget *
ptyxis_preferences_window_create_custom_link_row_cb (gpointer item,
                                                     gpointer user_data)
{
  PtyxisAddButtonListItem *add_button_item = PTYXIS_ADD_BUTTON_LIST_ITEM (item);
  GObject *wrapped_item;

  g_assert (PTYXIS_IS_ADD_BUTTON_LIST_ITEM (add_button_item));

  if ((wrapped_item = ptyxis_add_button_list_item_get_item (add_button_item)))
    {
      g_autoptr(PtyxisProfile) profile = ptyxis_application_dup_default_profile (PTYXIS_APPLICATION_DEFAULT);

      return ptyxis_custom_link_row_new (profile, PTYXIS_CUSTOM_LINK (wrapped_item));
    }

  return g_object_new (ADW_TYPE_BUTTON_ROW,
                       "title", _("Add Link"),
                       "start-icon-name", "list-add-symbolic",
                       "action-name", "custom-link.add",
                       NULL);
}

static void
ptyxis_preferences_window_notify_default_profile_cb (PtyxisPreferencesWindow *self,
                                                     GParamSpec              *pspec,
                                                     PtyxisApplication       *app)
{
  g_autoptr(PtyxisProfile) profile = NULL;
  g_autoptr(GPropertyAction) palette_action = NULL;
  g_autoptr(GSettings) gsettings = NULL;
  g_autoptr(GSimpleActionGroup) group = NULL;
  g_autoptr(GListModel) custom_links_list = NULL;
  g_autoptr(PtyxisAddButtonListModel) custom_links_list_model = NULL;

  g_assert (PTYXIS_IS_PREFERENCES_WINDOW (self));
  g_assert (PTYXIS_IS_APPLICATION (app));

  profile = ptyxis_application_dup_default_profile (app);
  gsettings = ptyxis_profile_dup_settings (profile);

  g_signal_connect_object (profile,
                           "notify::palette-id",
                           G_CALLBACK (invalidate_filter),
                           self,
                           G_CONNECT_SWAPPED);

  /* If the user changed things in gsettings, show the toggle. This
   * also helps on installations where the distributor may have changed
   * the default value for the opacity gsetting.
   */
  gtk_widget_set_visible (GTK_WIDGET (self->opacity_group),
                          ptyxis_profile_get_opacity (profile) < 1.);

  invalidate_filter (self);

  for (GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self->palette_previews));
       child;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkWidget *button = gtk_flow_box_child_get_child (GTK_FLOW_BOX_CHILD (child));
      GtkWidget *preview = gtk_button_get_child (GTK_BUTTON (button));

      g_object_bind_property_full (profile, "palette", preview, "selected",
                                   G_BINDING_SYNC_CREATE,
                                   map_palette_to_selected, NULL,
                                   preview, NULL);
    }

  group = g_simple_action_group_new ();
  palette_action = g_property_action_new ("palette", profile, "palette-id");
  g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (palette_action));
  gtk_widget_insert_action_group (GTK_WIDGET (self),
                                  "default-profile",
                                  G_ACTION_GROUP (group));

  g_object_bind_property (profile, "opacity",
                          self->opacity_adjustment, "value",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property_full (profile, "opacity",
                               self->opacity_label, "label",
                               G_BINDING_SYNC_CREATE,
                               opacity_to_label, NULL, NULL, NULL);
  g_object_bind_property (profile, "limit-scrollback",
                          self->limit_scrollback, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (profile, "scroll-on-output",
                          self->scroll_on_output, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (profile, "scroll-on-keystroke",
                          self->scroll_on_keystroke, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (profile, "scrollback-lines",
                          self->scrollback_lines, "value",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (profile, "cell-height-scale",
                          self->cell_height_scale, "value",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (profile, "cell-width-scale",
                          self->cell_width_scale, "value",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (profile, "login-shell",
                          self->login_shell, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_settings_bind_with_mapping (gsettings,
                                PTYXIS_PROFILE_KEY_BACKSPACE_BINDING,
                                self->backspace_binding,
                                "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                string_to_index,
                                index_to_string,
                                g_object_ref (self->erase_bindings),
                                g_object_unref);

  g_settings_bind_with_mapping (gsettings,
                                PTYXIS_PROFILE_KEY_DELETE_BINDING,
                                self->delete_binding,
                                "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                string_to_index,
                                index_to_string,
                                g_object_ref (self->erase_bindings),
                                g_object_unref);

  g_settings_bind_with_mapping (gsettings,
                                PTYXIS_PROFILE_KEY_CJK_AMBIGUOUS_WIDTH,
                                self->cjk_ambiguous_width,
                                "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                string_to_index,
                                index_to_string,
                                g_object_ref (self->cjk_ambiguous_widths),
                                g_object_unref);

  g_settings_bind_with_mapping (gsettings,
                                PTYXIS_PROFILE_KEY_EXIT_ACTION,
                                self->exit_action,
                                "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                string_to_index,
                                index_to_string,
                                g_object_ref (self->exit_actions),
                                g_object_unref);

  g_settings_bind_with_mapping (gsettings,
                                PTYXIS_PROFILE_KEY_PRESERVE_DIRECTORY,
                                self->preserve_directory,
                                "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                string_to_index,
                                index_to_string,
                                g_object_ref (self->preserve_directories),
                                g_object_unref);

  custom_links_list = ptyxis_profile_list_custom_links (profile);
  custom_links_list_model = ptyxis_add_button_list_model_new (custom_links_list);

  gtk_list_box_bind_model (self->custom_links_list_box,
                           G_LIST_MODEL (custom_links_list_model),
                           ptyxis_preferences_window_create_custom_link_row_cb,
                           g_object_ref(self),
                           g_object_unref);
}

static gboolean
ptyxis_preferences_window_drop_palette_cb (PtyxisPreferencesWindow *self,
                                           const GValue            *value,
                                           double                   x,
                                           double                   y,
                                           GtkDropTarget           *drop_target)
{
  g_assert (PTYXIS_IS_PREFERENCES_WINDOW (self));
  g_assert (GTK_IS_DROP_TARGET (drop_target));

  if (G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST))
    {
      const GSList *files = g_value_get_boxed (value);

      for (const GSList *iter = files; iter; iter = iter->next)
        {
          GFile *file = iter->data;
          g_autofree char *name = g_file_get_basename (file);
          g_autoptr(GFile) dest = NULL;

          if (!g_str_has_suffix (name, ".palette"))
            return FALSE;

          dest = g_file_new_build_filename (g_get_user_data_dir (),
                                            APP_ID, "palettes", name,
                                            NULL);
          g_file_copy_async (file,
                             dest,
                             G_FILE_COPY_OVERWRITE,
                             G_PRIORITY_DEFAULT,
                             NULL, NULL, NULL, NULL, NULL);
        }

      return TRUE;
    }

  return FALSE;
}

static void
ptyxis_preferences_window_activate_palette_cb (GtkFlowBoxChild *child,
                                               PtyxisPalette   *palette)
{
  g_autoptr(GVariant) param = NULL;

  g_assert (GTK_IS_FLOW_BOX_CHILD (child));
  g_assert (PTYXIS_IS_PALETTE (palette));

  param = g_variant_take_ref (g_variant_new_string (ptyxis_palette_get_id (palette)));

  gtk_widget_activate_action_variant (GTK_WIDGET (child), "default-profile.palette", param);
}

static GtkWidget *
create_palette_preview (gpointer item,
                        gpointer user_data)
{
  AdwStyleManager *style_manager = user_data;
  g_autoptr(PtyxisProfile) default_profile = NULL;
  g_autoptr(GVariant) action_target = NULL;
  PtyxisPalette *palette = item;
  PtyxisSettings *settings;
  GtkButton *button;
  GtkWidget *preview;
  GtkWidget *child;

  g_assert (PTYXIS_IS_PALETTE (palette));
  g_assert (ADW_IS_STYLE_MANAGER (style_manager));

  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);
  action_target = g_variant_take_ref (g_variant_new_string (ptyxis_palette_get_id (palette)));
  preview = ptyxis_palette_preview_new (palette);
  g_object_bind_property (style_manager, "dark", preview, "dark", G_BINDING_SYNC_CREATE);
  g_object_bind_property (settings, "font-desc", preview, "font-desc", G_BINDING_SYNC_CREATE);
  button = g_object_new (GTK_TYPE_TOGGLE_BUTTON,
                         "css-classes", (const char * const[]) { "palette", NULL },
                         "action-name", "default-profile.palette",
                         "action-target", action_target,
                         "child", preview,
                         "focus-on-click", FALSE,
                         "can-focus", FALSE,
                         "overflow", GTK_OVERFLOW_HIDDEN,
                         NULL);
  child = g_object_new (GTK_TYPE_FLOW_BOX_CHILD,
                        "child", button,
                        NULL);
  g_signal_connect_object (child,
                           "activate",
                           G_CALLBACK (ptyxis_preferences_window_activate_palette_cb),
                           palette,
                           0);

  /* This is probably pretty slow and terrible to do here, but we need another
   * way to track default-palette of default-profile, both of which could change.
   */
  default_profile = ptyxis_application_dup_default_profile (PTYXIS_APPLICATION_DEFAULT);
  g_object_bind_property_full (default_profile, "palette", preview, "selected",
                               G_BINDING_SYNC_CREATE,
                               map_palette_to_selected, NULL,
                               preview, NULL);

  return child;
}

static void
ptyxis_preferences_window_add_custom_link (GtkWidget  *widget,
                                           const char *action_name,
                                           GVariant   *param)
{
  PtyxisPreferencesWindow *self = (PtyxisPreferencesWindow *)widget;
  g_autoptr(PtyxisProfile) default_profile = NULL;
  g_autoptr(PtyxisCustomLink) custom_link = NULL;
  PtyxisCustomLinkEditor *editor;

  g_assert (PTYXIS_IS_PREFERENCES_WINDOW (self));

  default_profile = ptyxis_application_dup_default_profile (PTYXIS_APPLICATION_DEFAULT);
  custom_link = ptyxis_custom_link_new ();
  ptyxis_profile_add_custom_link (default_profile, custom_link);

  editor = ptyxis_custom_link_editor_new (default_profile, custom_link);
  adw_preferences_window_push_subpage (ADW_PREFERENCES_WINDOW (self),
                                       ADW_NAVIGATION_PAGE (editor));
}

static gboolean
dispose_in_idle (gpointer data)
{
  g_object_run_dispose (data);
  return G_SOURCE_REMOVE;
}

static gboolean
ptyxis_preferences_window_close_request (GtkWindow *window)
{
  GtkWindowGroup *group;

  g_assert (PTYXIS_IS_PREFERENCES_WINDOW (window));

  if (instance == PTYXIS_PREFERENCES_WINDOW (window))
    {
      instance = NULL;

      /* We use a single window group for the preferences window
       * so that it can stack appropriately above other windows.
       * Clear it so that we release the group too.
       */
      if ((group = gtk_window_get_group (window)))
        gtk_window_group_remove_window (group, window);

      /* Dispose in idle to force cleanup. This wasn't seeming to
       * happen automatically on close-request.
       */
      g_idle_add_full (G_PRIORITY_LOW,
                       dispose_in_idle,
                       g_object_ref (window),
                       g_object_unref);
    }

  return GTK_WINDOW_CLASS (ptyxis_preferences_window_parent_class)->close_request (window);
}

static void
ptyxis_preferences_window_constructed (GObject *object)
{
  PtyxisPreferencesWindow *self = (PtyxisPreferencesWindow *)object;
  PtyxisApplication *app = PTYXIS_APPLICATION_DEFAULT;
  PtyxisSettings *settings = ptyxis_application_get_settings (app);
  PtyxisShortcuts *shortcuts = ptyxis_application_get_shortcuts (app);
  GSettings *gsettings = ptyxis_settings_get_settings (settings);
  AdwStyleManager *style_manager = adw_style_manager_get_default ();
  g_autoptr(GListModel) profiles = NULL;
  g_autoptr(PtyxisAddButtonListModel) profiles_list_model = NULL;

  G_OBJECT_CLASS (ptyxis_preferences_window_parent_class)->constructed (object);

  self->filter = gtk_custom_filter_new (do_filter_palettes, self, NULL);
  self->filter_palettes = gtk_filter_list_model_new (g_object_ref (ptyxis_palette_get_all ()),
                                                     g_object_ref (GTK_FILTER (self->filter)));
  g_signal_connect_object (style_manager,
                           "notify::dark",
                           G_CALLBACK (invalidate_filter),
                           self,
                           G_CONNECT_SWAPPED);

  gtk_flow_box_bind_model (self->palette_previews,
                           G_LIST_MODEL (self->filter_palettes),
                           create_palette_preview,
                           g_object_ref (style_manager),
                           g_object_unref);

  g_signal_connect_object (app,
                           "notify::default-profile",
                           G_CALLBACK (ptyxis_preferences_window_notify_default_profile_cb),
                           self,
                           G_CONNECT_SWAPPED);
  ptyxis_preferences_window_notify_default_profile_cb (self, NULL, app);

  g_settings_bind_with_mapping (gsettings,
                                PTYXIS_SETTING_KEY_NEW_TAB_POSITION,
                                self->tab_position,
                                "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                string_to_index,
                                index_to_string,
                                g_object_ref (self->tab_positions),
                                g_object_unref);

  g_settings_bind_with_mapping (gsettings,
                                PTYXIS_SETTING_KEY_CURSOR_SHAPE,
                                self->cursor_shape,
                                "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                string_to_index,
                                index_to_string,
                                g_object_ref (self->cursor_shapes),
                                g_object_unref);

  g_settings_bind_with_mapping (gsettings,
                                PTYXIS_SETTING_KEY_CURSOR_BLINK_MODE,
                                self->cursor_blink_mode,
                                "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                string_to_index,
                                index_to_string,
                                g_object_ref (self->cursor_blink_modes),
                                g_object_unref);

  g_settings_bind_with_mapping (gsettings,
                                PTYXIS_SETTING_KEY_SCROLLBAR_POLICY,
                                self->scrollbar_policy,
                                "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                string_to_index,
                                index_to_string,
                                g_object_ref (self->scrollbar_policies),
                                g_object_unref);

  g_settings_bind_with_mapping (gsettings,
                                PTYXIS_SETTING_KEY_TEXT_BLINK_MODE,
                                self->text_blink_mode,
                                "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                string_to_index,
                                index_to_string,
                                g_object_ref (self->text_blink_modes),
                                g_object_unref);

  profiles = ptyxis_application_list_profiles (app);
  profiles_list_model = ptyxis_add_button_list_model_new (profiles);

  gtk_list_box_bind_model (self->profiles_list_box,
                           G_LIST_MODEL (profiles_list_model),
                           ptyxis_preferences_window_create_profile_row_cb,
                           NULL, NULL);

  g_object_bind_property (settings, "audible-bell",
                          self->audible_bell, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (settings, "visual-bell",
                          self->visual_bell, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (settings, "restore-session",
                          self->restore_session, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (settings, "restore-window-size",
                          self->restore_window_size, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (settings, "use-system-font",
                          self->use_system_font, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  
  g_object_bind_property (settings, "select-to-copy",
                          self->select_to_copy, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (settings, "default-columns",
                          self->default_columns, "value",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (settings, "default-rows",
                          self->default_rows, "value",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (settings, "font-name",
                          self->font_name, "label",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (settings, "use-system-font",
                          self->font_name, "sensitive",
                          G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
  g_object_bind_property (settings, "use-system-font",
                          self->font_name_row, "activatable",
                          G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
  g_object_bind_property (settings, "enable-a11y",
                          self->enable_a11y, "active",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (shortcuts, "new-tab",
                          self->shortcut_new_tab, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "new-window",
                          self->shortcut_new_window, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "split-auto",
                          self->shortcut_split_auto, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "split-horizontal",
                          self->shortcut_split_horizontal, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "split-vertical",
                          self->shortcut_split_vertical, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "focus-pane-left",
                          self->shortcut_focus_pane_left, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "focus-pane-right",
                          self->shortcut_focus_pane_right, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "focus-pane-up",
                          self->shortcut_focus_pane_up, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "focus-pane-down",
                          self->shortcut_focus_pane_down, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "tab-overview",
                          self->shortcut_tab_overview, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (shortcuts, "focus-tab-1",
                          self->shortcut_focus_tab_1, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "focus-tab-2",
                          self->shortcut_focus_tab_2, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "focus-tab-3",
                          self->shortcut_focus_tab_3, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "focus-tab-4",
                          self->shortcut_focus_tab_4, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "focus-tab-5",
                          self->shortcut_focus_tab_5, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "focus-tab-6",
                          self->shortcut_focus_tab_6, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "focus-tab-7",
                          self->shortcut_focus_tab_7, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "focus-tab-8",
                          self->shortcut_focus_tab_8, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "focus-tab-9",
                          self->shortcut_focus_tab_9, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "focus-tab-10",
                          self->shortcut_focus_tab_10, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "focus-tab-last",
                          self->shortcut_focus_tab_last, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (shortcuts, "toggle-fullscreen",
                          self->shortcut_toggle_fullscreen, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "preferences",
                          self->shortcut_preferences, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "show-keyboard-shortcuts",
                          self->shortcut_show_keyboard_shortcuts, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "primary-menu",
                          self->shortcut_primary_menu, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "tab-menu",
                          self->shortcut_tab_menu, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (shortcuts, "copy-clipboard",
                          self->shortcut_copy_clipboard, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "paste-clipboard",
                          self->shortcut_paste_clipboard, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (shortcuts, "reset",
                          self->shortcut_reset, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "reset-and-clear",
                          self->shortcut_reset_and_clear, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (shortcuts, "search",
                          self->shortcut_search, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (shortcuts, "select-all",
                          self->shortcut_select_all, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "select-none",
                          self->shortcut_select_none, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (shortcuts, "popup-menu",
                          self->shortcut_popup_menu, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (shortcuts, "set-title",
                          self->shortcut_set_title, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (shortcuts, "zoom-in",
                          self->shortcut_zoom_in, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "zoom-one",
                          self->shortcut_zoom_one, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "zoom-out",
                          self->shortcut_zoom_out, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (shortcuts, "close-tab",
                          self->shortcut_close_tab, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "close-other-tabs",
                          self->shortcut_close_other_tabs, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "undo-close-tab",
                          self->shortcut_undo_close_tab, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "close-window",
                          self->shortcut_close_window, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (shortcuts, "move-next-tab",
                          self->shortcut_move_next_tab, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "move-previous-tab",
                          self->shortcut_move_previous_tab, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_object_bind_property (shortcuts, "move-tab-left",
                          self->shortcut_move_tab_left, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "move-tab-right",
                          self->shortcut_move_tab_right, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  g_object_bind_property (shortcuts, "detach-tab",
                          self->shortcut_detach_tab, "accelerator",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
}

static void
ptyxis_preferences_window_dispose (GObject *object)
{
  PtyxisPreferencesWindow *self = (PtyxisPreferencesWindow *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), PTYXIS_TYPE_PREFERENCES_WINDOW);

  g_clear_pointer (&self->default_palette_id, g_free);
  g_clear_object (&self->filter);
  g_clear_object (&self->filter_palettes);

  G_OBJECT_CLASS (ptyxis_preferences_window_parent_class)->dispose (object);
}

static void
ptyxis_preferences_window_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  PtyxisPreferencesWindow *self = PTYXIS_PREFERENCES_WINDOW (object);

  switch (prop_id)
    {
    case PROP_DEFAULT_PALETTE_ID:
      g_value_set_string (value, self->default_palette_id);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ptyxis_preferences_window_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  PtyxisPreferencesWindow *self = PTYXIS_PREFERENCES_WINDOW (object);

  switch (prop_id)
    {
    case PROP_DEFAULT_PALETTE_ID:
      if (g_set_str (&self->default_palette_id, g_value_get_string (value)))
        {
          gtk_filter_changed (GTK_FILTER (self->filter), GTK_FILTER_CHANGE_DIFFERENT);
          g_object_notify_by_pspec (G_OBJECT (self), pspec);
        }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ptyxis_preferences_window_class_init (PtyxisPreferencesWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkWindowClass *window_class = GTK_WINDOW_CLASS (klass);

  object_class->constructed = ptyxis_preferences_window_constructed;
  object_class->dispose = ptyxis_preferences_window_dispose;
  object_class->get_property = ptyxis_preferences_window_get_property;
  object_class->set_property = ptyxis_preferences_window_set_property;

  window_class->close_request = ptyxis_preferences_window_close_request;

  properties[PROP_DEFAULT_PALETTE_ID] =
    g_param_spec_string ("default-palette-id", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Ptyxis/ptyxis-preferences-window.ui");

  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, audible_bell);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, backspace_binding);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, cell_height_scale);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, cell_width_scale);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, cjk_ambiguous_width);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, cjk_ambiguous_widths);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, cursor_blink_mode);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, cursor_blink_modes);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, cursor_shape);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, cursor_shapes);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, custom_links_list_box);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, delete_binding);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, enable_a11y);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, erase_bindings);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, exit_action);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, exit_actions);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, font_name);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, font_name_row);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, limit_scrollback);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, login_shell);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, opacity_adjustment);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, opacity_group);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, opacity_label);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, palette_previews);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, preserve_directories);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, preserve_directory);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, profiles_list_box);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, restore_session);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, restore_window_size);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, default_rows);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, default_columns);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, scroll_on_keystroke);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, scroll_on_output);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, scrollback_lines);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, scrollbar_policies);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, scrollbar_policy);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_close_other_tabs);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_close_tab);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_close_window);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_copy_clipboard);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_detach_tab);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_focus_tab_1);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_focus_tab_10);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_focus_tab_2);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_focus_tab_3);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_focus_tab_4);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_focus_tab_5);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_focus_tab_6);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_focus_tab_7);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_focus_tab_8);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_focus_tab_9);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_focus_tab_last);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_focus_pane_down);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_focus_pane_left);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_focus_pane_right);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_focus_pane_up);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_move_next_tab);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_move_previous_tab);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_move_tab_left);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_move_tab_right);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_new_tab);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_new_window);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_paste_clipboard);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_popup_menu);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_set_title);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_preferences);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_show_keyboard_shortcuts);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_primary_menu);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_tab_menu);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_reset);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_reset_and_clear);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_search);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_select_all);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_select_none);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_split_auto);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_split_horizontal);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_split_vertical);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_tab_overview);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_toggle_fullscreen);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_undo_close_tab);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_zoom_in);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_zoom_one);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, shortcut_zoom_out);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, show_more_palettes);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, tab_position);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, tab_positions);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, text_blink_mode);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, text_blink_modes);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, use_system_font);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, visual_bell);
  gtk_widget_class_bind_template_child (widget_class, PtyxisPreferencesWindow, select_to_copy);

  gtk_widget_class_bind_template_callback (widget_class, ptyxis_preferences_window_profile_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_preferences_window_show_all_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_preferences_window_spin_row_show_decimal_cb);

  gtk_widget_class_install_action (widget_class,
                                   "custom-link.add",
                                   NULL,
                                   ptyxis_preferences_window_add_custom_link);
  gtk_widget_class_install_action (widget_class,
                                   "profile.add",
                                   NULL,
                                   ptyxis_preferences_window_add_profile);
  gtk_widget_class_install_action (widget_class,
                                   "settings.select-custom-font",
                                   NULL,
                                   ptyxis_preferences_window_select_custom_font);
  gtk_widget_class_install_action (widget_class,
                                   "toast.add",
                                   NULL,
                                   ptyxis_preferences_window_add_toast);

  g_type_ensure (PTYXIS_TYPE_PREFERENCES_LIST_ITEM);
  g_type_ensure (PTYXIS_TYPE_PROFILE_EDITOR);
  g_type_ensure (PTYXIS_TYPE_PROFILE_ROW);
  g_type_ensure (PTYXIS_TYPE_SHORTCUT_ROW);
}

static void
ptyxis_preferences_window_init (PtyxisPreferencesWindow *self)
{
  GtkDropTarget *drop_target;

  gtk_widget_init_template (GTK_WIDGET (self));

  drop_target = gtk_drop_target_new (GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
  g_signal_connect_object (drop_target,
                           "drop",
                           G_CALLBACK (ptyxis_preferences_window_drop_palette_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_widget_add_controller (GTK_WIDGET (self->palette_previews),
                             GTK_EVENT_CONTROLLER (drop_target));
}

GtkWindow *
ptyxis_preferences_window_new (GtkApplication* application)
{
  return g_object_new (PTYXIS_TYPE_PREFERENCES_WINDOW,
                       NULL);
}

void
ptyxis_preferences_window_edit_profile (PtyxisPreferencesWindow *self,
                                        PtyxisProfile           *profile)
{
  PtyxisProfileEditor *editor;

  g_return_if_fail (PTYXIS_IS_PREFERENCES_WINDOW (self));
  g_return_if_fail (PTYXIS_IS_PROFILE (profile));

  editor = ptyxis_profile_editor_new (profile);

  adw_preferences_window_pop_subpage (ADW_PREFERENCES_WINDOW (self));
  adw_preferences_window_push_subpage (ADW_PREFERENCES_WINDOW (self),
                                       ADW_NAVIGATION_PAGE (editor));
}

/**
 * ptyxis_preferences_window_get_default:
 *
 * Gets the default preferences window for the process.
 *
 * Returns: (transfer none): a #PtyxisPreferencesWindow
 */
PtyxisPreferencesWindow *
ptyxis_preferences_window_get_default (void)
{
  if (instance == NULL)
    {
      g_autoptr(GtkWindowGroup) sole_group = gtk_window_group_new ();

      instance = g_object_new (PTYXIS_TYPE_PREFERENCES_WINDOW,
                               "modal", FALSE,
                               NULL);
      gtk_window_group_add_window (sole_group, GTK_WINDOW (instance));
    }

  return instance;
}

void
ptyxis_preferences_window_edit_shortcuts (PtyxisPreferencesWindow *self)
{
  g_return_if_fail (PTYXIS_IS_PREFERENCES_WINDOW (self));

  adw_preferences_window_pop_subpage (ADW_PREFERENCES_WINDOW (self));
  adw_preferences_window_set_visible_page_name (ADW_PREFERENCES_WINDOW (self), "shortcuts");
}

G_GNUC_END_IGNORE_DEPRECATIONS
