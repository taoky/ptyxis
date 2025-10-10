/*
 * ptyxis-settings.c
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

#include <gio/gio.h>

#include "glib.h"
#include "ptyxis-application.h"
#include "ptyxis-enums.h"
#include "ptyxis-settings.h"
#include "ptyxis-util.h"

struct _PtyxisSettings
{
  GObject    parent_instance;
  GSettings *settings;
};

enum {
  PROP_0,
  PROP_AUDIBLE_BELL,
  PROP_CURSOR_BLINK_MODE,
  PROP_CURSOR_SHAPE,
  PROP_DEFAULT_PROFILE_UUID,
  PROP_DISABLE_PADDING,
  PROP_ENABLE_A11Y,
  PROP_ENABLE_ZOOM_SCROLL_CTRL,
  PROP_IGNORE_OSC_TITLE,
  PROP_FONT_DESC,
  PROP_FONT_NAME,
  PROP_INTERFACE_STYLE,
  PROP_NEW_TAB_POSITION,
  PROP_PROFILE_UUIDS,
  PROP_RESTORE_SESSION,
  PROP_RESTORE_WINDOW_SIZE,
  PROP_DEFAULT_COLUMNS,
  PROP_DEFAULT_ROWS,
  PROP_SCROLLBAR_POLICY,
  PROP_SELECT_TO_COPY,
  PROP_TAB_MIDDLE_CLICK,
  PROP_TEXT_BLINK_MODE,
  PROP_TOAST_ON_COPY_CLIPBOARD,
  PROP_USE_SYSTEM_FONT,
  PROP_VISUAL_BELL,
  PROP_VISUAL_PROCESS_LEADER,
  PROP_WORD_CHAR_EXCEPTIONS,
  PROP_INHIBIT_LOGOUT,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (PtyxisSettings, ptyxis_settings, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
ptyxis_settings_changed_cb (PtyxisSettings *self,
                            const char     *key,
                            GSettings      *settings)
{
  g_assert (PTYXIS_IS_SETTINGS (self));
  g_assert (key != NULL);
  g_assert (G_IS_SETTINGS (settings));

  if (g_str_equal (key, PTYXIS_SETTING_KEY_DEFAULT_PROFILE_UUID))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEFAULT_PROFILE_UUID]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_DISABLE_PADDING))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DISABLE_PADDING]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_PROFILE_UUIDS))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROFILE_UUIDS]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_NEW_TAB_POSITION))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NEW_TAB_POSITION]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_AUDIBLE_BELL))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUDIBLE_BELL]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_VISUAL_BELL))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISUAL_BELL]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_VISUAL_PROCESS_LEADER))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISUAL_PROCESS_LEADER]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_CURSOR_SHAPE))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURSOR_SHAPE]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_CURSOR_BLINK_MODE))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURSOR_BLINK_MODE]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_SCROLLBAR_POLICY))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCROLLBAR_POLICY]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_TAB_MIDDLE_CLICK))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TAB_MIDDLE_CLICK]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_TEXT_BLINK_MODE))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT_BLINK_MODE]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_INTERFACE_STYLE))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTERFACE_STYLE]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_RESTORE_SESSION))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESTORE_SESSION]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_RESTORE_WINDOW_SIZE))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESTORE_WINDOW_SIZE]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_DEFAULT_COLUMNS))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEFAULT_COLUMNS]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_DEFAULT_ROWS))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEFAULT_ROWS]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_TOAST_ON_COPY_CLIPBOARD))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TOAST_ON_COPY_CLIPBOARD]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_ENABLE_A11Y))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLE_A11Y]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_ENABLE_ZOOM_SCROLL_CTRL))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLE_ZOOM_SCROLL_CTRL]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_IGNORE_OSC_TITLE))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IGNORE_OSC_TITLE]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_INHIBIT_LOGOUT))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INHIBIT_LOGOUT]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_SELECT_TO_COPY))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELECT_TO_COPY]);
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_FONT_NAME))
    {
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_NAME]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_DESC]);
    }
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_USE_SYSTEM_FONT))
    {
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_USE_SYSTEM_FONT]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_DESC]);
    }
  else if (g_str_equal (key, PTYXIS_SETTING_KEY_WORD_CHAR_EXCEPTIONS))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WORD_CHAR_EXCEPTIONS]);
}

static void
ptyxis_settings_dispose (GObject *object)
{
  PtyxisSettings *self = (PtyxisSettings *)object;

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (ptyxis_settings_parent_class)->dispose (object);
}

static void
ptyxis_settings_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  PtyxisSettings *self = PTYXIS_SETTINGS (object);

  switch (prop_id)
    {
    case PROP_AUDIBLE_BELL:
      g_value_set_boolean (value, ptyxis_settings_get_audible_bell (self));
      break;

    case PROP_CURSOR_BLINK_MODE:
      g_value_set_enum (value, ptyxis_settings_get_cursor_blink_mode (self));
      break;

    case PROP_CURSOR_SHAPE:
      g_value_set_enum (value, ptyxis_settings_get_cursor_shape (self));
      break;

    case PROP_DEFAULT_PROFILE_UUID:
      g_value_take_string (value, ptyxis_settings_dup_default_profile_uuid (self));
      break;

    case PROP_DISABLE_PADDING:
      g_value_set_boolean (value, ptyxis_settings_get_disable_padding (self));
      break;

    case PROP_ENABLE_A11Y:
      g_value_set_boolean (value, ptyxis_settings_get_enable_a11y (self));
      break;

    case PROP_IGNORE_OSC_TITLE:
      g_value_set_boolean (value, ptyxis_settings_get_ignore_osc_title (self));
      break;

    case PROP_INHIBIT_LOGOUT:
      g_value_set_boolean (value, ptyxis_settings_get_inhibit_logout (self));
      break;
    
    case PROP_SELECT_TO_COPY:
      g_value_set_boolean (value, ptyxis_settings_get_select_to_copy (self));
      break;

    case PROP_FONT_DESC:
      g_value_take_boxed (value, ptyxis_settings_dup_font_desc (self));
      break;

    case PROP_INTERFACE_STYLE:
      g_value_set_enum (value, ptyxis_settings_get_interface_style (self));
      break;

    case PROP_FONT_NAME:
      g_value_take_string (value, ptyxis_settings_dup_font_name (self));
      break;

    case PROP_NEW_TAB_POSITION:
      g_value_set_enum (value, ptyxis_settings_get_new_tab_position (self));
      break;

    case PROP_PROFILE_UUIDS:
      g_value_take_boxed (value, ptyxis_settings_dup_profile_uuids (self));
      break;

    case PROP_RESTORE_SESSION:
      g_value_set_boolean (value, ptyxis_settings_get_restore_session (self));
      break;

    case PROP_RESTORE_WINDOW_SIZE:
      g_value_set_boolean (value, ptyxis_settings_get_restore_window_size (self));
      break;

    case PROP_DEFAULT_COLUMNS:
      g_value_set_uint (value, ptyxis_settings_get_default_columns (self));
      break;

    case PROP_DEFAULT_ROWS:
      g_value_set_uint (value, ptyxis_settings_get_default_rows (self));
      break;

    case PROP_SCROLLBAR_POLICY:
      g_value_set_enum (value, ptyxis_settings_get_scrollbar_policy (self));
      break;

    case PROP_TAB_MIDDLE_CLICK:
      g_value_set_enum (value, ptyxis_settings_get_tab_middle_click (self));
      break;

    case PROP_TEXT_BLINK_MODE:
      g_value_set_enum (value, ptyxis_settings_get_text_blink_mode (self));
      break;

    case PROP_TOAST_ON_COPY_CLIPBOARD:
      g_value_set_boolean (value, ptyxis_settings_get_toast_on_copy_clipboard (self));
      break;

    case PROP_USE_SYSTEM_FONT:
      g_value_set_boolean (value, ptyxis_settings_get_use_system_font (self));
      break;

    case PROP_VISUAL_BELL:
      g_value_set_boolean (value, ptyxis_settings_get_visual_bell (self));
      break;

    case PROP_VISUAL_PROCESS_LEADER:
      g_value_set_boolean (value, ptyxis_settings_get_visual_process_leader (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ptyxis_settings_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  PtyxisSettings *self = PTYXIS_SETTINGS (object);

  switch (prop_id)
    {
    case PROP_AUDIBLE_BELL:
      ptyxis_settings_set_audible_bell (self, g_value_get_boolean (value));
      break;

    case PROP_CURSOR_BLINK_MODE:
      ptyxis_settings_set_cursor_blink_mode (self, g_value_get_enum (value));
      break;

    case PROP_CURSOR_SHAPE:
      ptyxis_settings_set_cursor_shape (self, g_value_get_enum (value));
      break;

    case PROP_FONT_DESC:
      ptyxis_settings_set_font_desc (self, g_value_get_boxed (value));
      break;

    case PROP_ENABLE_A11Y:
      ptyxis_settings_set_enable_a11y (self, g_value_get_boolean (value));
      break;

    case PROP_ENABLE_ZOOM_SCROLL_CTRL:
      ptyxis_settings_set_enable_zoom_scroll_ctrl (self, g_value_get_boolean (value));
      break;

    case PROP_IGNORE_OSC_TITLE:
      ptyxis_settings_set_ignore_osc_title (self, g_value_get_boolean (value));
      break;

    case PROP_INHIBIT_LOGOUT:
      ptyxis_settings_set_inhibit_logout (self, g_value_get_boolean (value));
      break;
    
    case PROP_SELECT_TO_COPY:
      ptyxis_settings_set_select_to_copy (self, g_value_get_boolean (value));
      break;

    case PROP_FONT_NAME:
      ptyxis_settings_set_font_name (self, g_value_get_string (value));
      break;

    case PROP_INTERFACE_STYLE:
      ptyxis_settings_set_interface_style (self, g_value_get_enum (value));
      break;

    case PROP_NEW_TAB_POSITION:
      ptyxis_settings_set_new_tab_position (self, g_value_get_enum (value));
      break;

    case PROP_DEFAULT_PROFILE_UUID:
      ptyxis_settings_set_default_profile_uuid (self, g_value_get_string (value));
      break;

    case PROP_DISABLE_PADDING:
      ptyxis_settings_set_disable_padding (self, g_value_get_boolean (value));
      break;

    case PROP_RESTORE_SESSION:
      ptyxis_settings_set_restore_session (self, g_value_get_boolean (value));
      break;

    case PROP_RESTORE_WINDOW_SIZE:
      ptyxis_settings_set_restore_window_size (self, g_value_get_boolean (value));
      break;

    case PROP_DEFAULT_COLUMNS:
      ptyxis_settings_set_default_columns (self, g_value_get_uint (value));
      break;

    case PROP_DEFAULT_ROWS:
      ptyxis_settings_set_default_rows (self, g_value_get_uint (value));
      break;

    case PROP_SCROLLBAR_POLICY:
      ptyxis_settings_set_scrollbar_policy (self, g_value_get_enum (value));
      break;

    case PROP_TAB_MIDDLE_CLICK:
      ptyxis_settings_set_tab_middle_click (self, g_value_get_enum (value));
      break;

    case PROP_TEXT_BLINK_MODE:
      ptyxis_settings_set_text_blink_mode (self, g_value_get_enum (value));
      break;

    case PROP_TOAST_ON_COPY_CLIPBOARD:
      ptyxis_settings_set_toast_on_copy_clipboard (self, g_value_get_boolean (value));
      break;

    case PROP_USE_SYSTEM_FONT:
      ptyxis_settings_set_use_system_font (self, g_value_get_boolean (value));
      break;

    case PROP_VISUAL_BELL:
      ptyxis_settings_set_visual_bell (self, g_value_get_boolean (value));
      break;

    case PROP_VISUAL_PROCESS_LEADER:
      ptyxis_settings_set_visual_process_leader (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ptyxis_settings_class_init (PtyxisSettingsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = ptyxis_settings_dispose;
  object_class->get_property = ptyxis_settings_get_property;
  object_class->set_property = ptyxis_settings_set_property;

  properties[PROP_AUDIBLE_BELL] =
    g_param_spec_boolean (PTYXIS_SETTING_KEY_AUDIBLE_BELL, NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_CURSOR_BLINK_MODE] =
    g_param_spec_enum (PTYXIS_SETTING_KEY_CURSOR_BLINK_MODE, NULL, NULL,
                       VTE_TYPE_CURSOR_BLINK_MODE,
                       VTE_CURSOR_BLINK_SYSTEM,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_CURSOR_SHAPE] =
    g_param_spec_enum (PTYXIS_SETTING_KEY_CURSOR_SHAPE, NULL, NULL,
                       VTE_TYPE_CURSOR_SHAPE,
                       VTE_CURSOR_SHAPE_BLOCK,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_ENABLE_A11Y] =
    g_param_spec_boolean (PTYXIS_SETTING_KEY_ENABLE_A11Y, NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_ENABLE_ZOOM_SCROLL_CTRL] =
    g_param_spec_boolean (PTYXIS_SETTING_KEY_ENABLE_ZOOM_SCROLL_CTRL, NULL, NULL,
                          FALSE,
                          (G_PARAM_READABLE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_IGNORE_OSC_TITLE] =
    g_param_spec_boolean (PTYXIS_SETTING_KEY_IGNORE_OSC_TITLE, NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_INHIBIT_LOGOUT] =
    g_param_spec_boolean (PTYXIS_SETTING_KEY_INHIBIT_LOGOUT, NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));
  
  properties[PROP_SELECT_TO_COPY] =
    g_param_spec_boolean (PTYXIS_SETTING_KEY_SELECT_TO_COPY, NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_FONT_DESC] =
    g_param_spec_boxed ("font-desc", NULL, NULL,
                        PANGO_TYPE_FONT_DESCRIPTION,
                        (G_PARAM_READWRITE |
                         G_PARAM_EXPLICIT_NOTIFY |
                         G_PARAM_STATIC_STRINGS));

  properties[PROP_FONT_NAME] =
    g_param_spec_string (PTYXIS_SETTING_KEY_FONT_NAME, NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_INTERFACE_STYLE] =
    g_param_spec_enum (PTYXIS_SETTING_KEY_INTERFACE_STYLE, NULL, NULL,
                       ADW_TYPE_COLOR_SCHEME,
                       ADW_COLOR_SCHEME_DEFAULT,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_NEW_TAB_POSITION] =
    g_param_spec_enum (PTYXIS_SETTING_KEY_NEW_TAB_POSITION, NULL, NULL,
                       PTYXIS_TYPE_NEW_TAB_POSITION,
                       PTYXIS_NEW_TAB_POSITION_LAST,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_DEFAULT_PROFILE_UUID] =
    g_param_spec_string (PTYXIS_SETTING_KEY_DEFAULT_PROFILE_UUID, NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_DISABLE_PADDING] =
    g_param_spec_boolean (PTYXIS_SETTING_KEY_DISABLE_PADDING, NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_PROFILE_UUIDS] =
    g_param_spec_boxed (PTYXIS_SETTING_KEY_PROFILE_UUIDS, NULL, NULL,
                        G_TYPE_STRV,
                        (G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS));

  properties[PROP_RESTORE_SESSION] =
    g_param_spec_boolean (PTYXIS_SETTING_KEY_RESTORE_SESSION, NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_RESTORE_WINDOW_SIZE] =
    g_param_spec_boolean (PTYXIS_SETTING_KEY_RESTORE_WINDOW_SIZE, NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_DEFAULT_COLUMNS] =
    g_param_spec_uint (PTYXIS_SETTING_KEY_DEFAULT_COLUMNS, NULL, NULL,
                       1, 65535, 80,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_DEFAULT_ROWS] =
    g_param_spec_uint (PTYXIS_SETTING_KEY_DEFAULT_ROWS, NULL, NULL,
                       1, 65535, 24,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_SCROLLBAR_POLICY] =
    g_param_spec_enum (PTYXIS_SETTING_KEY_SCROLLBAR_POLICY, NULL, NULL,
                       PTYXIS_TYPE_SCROLLBAR_POLICY,
                       PTYXIS_SCROLLBAR_POLICY_SYSTEM,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_TAB_MIDDLE_CLICK] =
    g_param_spec_enum (PTYXIS_SETTING_KEY_TAB_MIDDLE_CLICK, NULL, NULL,
                       PTYXIS_TYPE_TAB_MIDDLE_CLICK_BEHAVIOR,
                       PTYXIS_TAB_MIDDLE_CLICK_CLOSE,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_TEXT_BLINK_MODE] =
    g_param_spec_enum (PTYXIS_SETTING_KEY_TEXT_BLINK_MODE, NULL, NULL,
                       VTE_TYPE_TEXT_BLINK_MODE,
                       VTE_TEXT_BLINK_ALWAYS,
                       (G_PARAM_READWRITE |
                        G_PARAM_EXPLICIT_NOTIFY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_TOAST_ON_COPY_CLIPBOARD] =
    g_param_spec_boolean (PTYXIS_SETTING_KEY_TOAST_ON_COPY_CLIPBOARD, NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_USE_SYSTEM_FONT] =
    g_param_spec_boolean (PTYXIS_SETTING_KEY_USE_SYSTEM_FONT, NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_VISUAL_BELL] =
    g_param_spec_boolean (PTYXIS_SETTING_KEY_VISUAL_BELL, NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_VISUAL_PROCESS_LEADER] =
    g_param_spec_boolean (PTYXIS_SETTING_KEY_VISUAL_PROCESS_LEADER, NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_WORD_CHAR_EXCEPTIONS] =
    g_param_spec_string (PTYXIS_SETTING_KEY_WORD_CHAR_EXCEPTIONS, NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
ptyxis_settings_init (PtyxisSettings *self)
{
  self->settings = g_settings_new (APP_SCHEMA_ID);

  g_signal_connect_object (self->settings,
                           "changed",
                           G_CALLBACK (ptyxis_settings_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

PtyxisSettings *
ptyxis_settings_new (void)
{
  return g_object_new (PTYXIS_TYPE_SETTINGS, NULL);
}

void
ptyxis_settings_set_default_profile_uuid (PtyxisSettings *self,
                                          const char     *default_profile_uuid)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));
  g_return_if_fail (default_profile_uuid != NULL);

  g_settings_set_string (self->settings,
                         PTYXIS_SETTING_KEY_DEFAULT_PROFILE_UUID,
                         default_profile_uuid);
}

char *
ptyxis_settings_dup_default_profile_uuid (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), NULL);

  return g_settings_get_string (self->settings,
                                PTYXIS_SETTING_KEY_DEFAULT_PROFILE_UUID);
}

char **
ptyxis_settings_dup_profile_uuids (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), NULL);

  return g_settings_get_strv (self->settings,
                              PTYXIS_SETTING_KEY_PROFILE_UUIDS);
}

void
ptyxis_settings_add_profile_uuid (PtyxisSettings *self,
                                  const char     *uuid)
{
  g_auto(GStrv) profiles = NULL;
  gsize len;

  g_return_if_fail (PTYXIS_IS_SETTINGS (self));
  g_return_if_fail (uuid != NULL);

  profiles = g_settings_get_strv (self->settings,
                                  PTYXIS_SETTING_KEY_PROFILE_UUIDS);

  if (g_strv_contains ((const char * const *)profiles, uuid))
    return;

  len = g_strv_length (profiles);
  profiles = g_realloc_n (profiles, len + 2, sizeof (char *));
  profiles[len] = g_strdup (uuid);
  profiles[len+1] = NULL;

  g_settings_set_strv (self->settings,
                       PTYXIS_SETTING_KEY_PROFILE_UUIDS,
                       (const char * const *)profiles);
}

void
ptyxis_settings_remove_profile_uuid (PtyxisSettings *self,
                                     const char     *uuid)
{
  g_auto(GStrv) profiles = NULL;
  g_autoptr(GStrvBuilder) builder = NULL;
  g_autofree char *default_profile_uuid = NULL;

  g_return_if_fail (PTYXIS_IS_SETTINGS (self));
  g_return_if_fail (uuid != NULL);

  default_profile_uuid = g_settings_get_string (self->settings,
                                                PTYXIS_SETTING_KEY_DEFAULT_PROFILE_UUID);
  profiles = g_settings_get_strv (self->settings,
                                  PTYXIS_SETTING_KEY_PROFILE_UUIDS);

  builder = g_strv_builder_new ();
  for (guint i = 0; profiles[i]; i++)
    {
      if (!g_str_equal (profiles[i], uuid))
        g_strv_builder_add (builder, profiles[i]);
    }

  g_clear_pointer (&profiles, g_strfreev);
  profiles = g_strv_builder_end (builder);

  /* Make sure we have at least one profile */
  if (profiles[0] == NULL)
    {
      profiles = g_realloc_n (profiles, 2, sizeof (char *));
      profiles[0] = g_dbus_generate_guid ();
      profiles[1] = NULL;
    }

  g_settings_set_strv (self->settings,
                       PTYXIS_SETTING_KEY_PROFILE_UUIDS,
                       (const char * const *)profiles);

  if (g_str_equal (uuid, default_profile_uuid))
    g_settings_set_string (self->settings,
                           PTYXIS_SETTING_KEY_DEFAULT_PROFILE_UUID,
                           profiles[0]);
}

PtyxisNewTabPosition
ptyxis_settings_get_new_tab_position (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), 0);

  return g_settings_get_enum (self->settings, PTYXIS_SETTING_KEY_NEW_TAB_POSITION);
}

void
ptyxis_settings_set_new_tab_position (PtyxisSettings       *self,
                                      PtyxisNewTabPosition  new_tab_position)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_enum (self->settings,
                       PTYXIS_SETTING_KEY_NEW_TAB_POSITION,
                       new_tab_position);
}

GSettings *
ptyxis_settings_get_settings (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), NULL);

  return self->settings;
}

gboolean
ptyxis_settings_get_enable_a11y (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, PTYXIS_SETTING_KEY_ENABLE_A11Y);
}

void
ptyxis_settings_set_enable_a11y (PtyxisSettings *self,
                                 gboolean        enable_a11y)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_boolean (self->settings,
                          PTYXIS_SETTING_KEY_ENABLE_A11Y,
                          enable_a11y);
}

gboolean
ptyxis_settings_get_enable_zoom_scroll_ctrl (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings,
                                 PTYXIS_SETTING_KEY_ENABLE_ZOOM_SCROLL_CTRL);
}

void
ptyxis_settings_set_enable_zoom_scroll_ctrl (PtyxisSettings *self,
                                             gboolean        enable_zoom_scroll_ctrl)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_boolean (self->settings,
                          PTYXIS_SETTING_KEY_ENABLE_ZOOM_SCROLL_CTRL,
                          enable_zoom_scroll_ctrl);
}

gboolean
ptyxis_settings_get_audible_bell (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, PTYXIS_SETTING_KEY_AUDIBLE_BELL);
}

void
ptyxis_settings_set_audible_bell (PtyxisSettings *self,
                                  gboolean        audible_bell)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_boolean (self->settings,
                          PTYXIS_SETTING_KEY_AUDIBLE_BELL,
                          audible_bell);
}

gboolean
ptyxis_settings_get_visual_bell (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, PTYXIS_SETTING_KEY_VISUAL_BELL);
}

void
ptyxis_settings_set_visual_bell (PtyxisSettings *self,
                                 gboolean        visual_bell)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_boolean (self->settings,
                          PTYXIS_SETTING_KEY_VISUAL_BELL,
                          visual_bell);
}

gboolean
ptyxis_settings_get_visual_process_leader (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, PTYXIS_SETTING_KEY_VISUAL_PROCESS_LEADER);
}

void
ptyxis_settings_set_visual_process_leader (PtyxisSettings *self,
                                           gboolean        visual_process_leader)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_boolean (self->settings,
                          PTYXIS_SETTING_KEY_VISUAL_PROCESS_LEADER,
                          visual_process_leader);
}

VteCursorBlinkMode
ptyxis_settings_get_cursor_blink_mode (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), 0);

  return g_settings_get_enum (self->settings, PTYXIS_SETTING_KEY_CURSOR_BLINK_MODE);
}

void
ptyxis_settings_set_cursor_blink_mode (PtyxisSettings     *self,
                                       VteCursorBlinkMode  cursor_blink_mode)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_enum (self->settings,
                       PTYXIS_SETTING_KEY_CURSOR_BLINK_MODE,
                       cursor_blink_mode);
}

VteCursorShape
ptyxis_settings_get_cursor_shape (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), 0);

  return g_settings_get_enum (self->settings, PTYXIS_SETTING_KEY_CURSOR_SHAPE);
}

void
ptyxis_settings_set_cursor_shape (PtyxisSettings *self,
                                  VteCursorShape  cursor_shape)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_enum (self->settings,
                       PTYXIS_SETTING_KEY_CURSOR_SHAPE,
                       cursor_shape);
}

char *
ptyxis_settings_dup_font_name (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), NULL);

  return g_settings_get_string (self->settings, PTYXIS_SETTING_KEY_FONT_NAME);
}

void
ptyxis_settings_set_font_name (PtyxisSettings *self,
                               const char     *font_name)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  if (font_name == NULL)
    font_name = "";

  g_settings_set_string (self->settings, PTYXIS_SETTING_KEY_FONT_NAME, font_name);
}

gboolean
ptyxis_settings_get_use_system_font (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, PTYXIS_SETTING_KEY_USE_SYSTEM_FONT);
}

void
ptyxis_settings_set_use_system_font (PtyxisSettings *self,
                                     gboolean        use_system_font)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_boolean (self->settings, PTYXIS_SETTING_KEY_USE_SYSTEM_FONT, use_system_font);
}

PangoFontDescription *
ptyxis_settings_dup_font_desc (PtyxisSettings *self)
{
  PtyxisApplication *app;
  g_autofree char *font_name = NULL;
  const char *system_font_name;

  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), NULL);

  app = PTYXIS_APPLICATION_DEFAULT;
  system_font_name = ptyxis_application_get_system_font_name (app);

  if (ptyxis_settings_get_use_system_font (self) ||
      !(font_name = ptyxis_settings_dup_font_name (self)) ||
      ptyxis_str_empty0 (font_name))
    return pango_font_description_from_string (system_font_name);

  return pango_font_description_from_string (font_name);
}

void
ptyxis_settings_set_font_desc (PtyxisSettings             *self,
                               const PangoFontDescription *font_desc)
{
  g_autofree char *font_name = NULL;

  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  if (font_desc != NULL)
    font_name = pango_font_description_to_string (font_desc);

  ptyxis_settings_set_font_name (self, font_name);
}

PtyxisScrollbarPolicy
ptyxis_settings_get_scrollbar_policy (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), 0);

  return g_settings_get_enum (self->settings, PTYXIS_SETTING_KEY_SCROLLBAR_POLICY);
}

void
ptyxis_settings_set_scrollbar_policy (PtyxisSettings        *self,
                                      PtyxisScrollbarPolicy  scrollbar_policy)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_enum (self->settings,
                       PTYXIS_SETTING_KEY_SCROLLBAR_POLICY,
                       scrollbar_policy);
}

PtyxisTabMiddleClickBehavior
ptyxis_settings_get_tab_middle_click (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), 0);

  return g_settings_get_enum (self->settings, PTYXIS_SETTING_KEY_TAB_MIDDLE_CLICK);
}

void
ptyxis_settings_set_tab_middle_click (PtyxisSettings               *self,
                                      PtyxisTabMiddleClickBehavior  tab_middle_click)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_enum (self->settings,
                       PTYXIS_SETTING_KEY_TAB_MIDDLE_CLICK,
                       tab_middle_click);
}

VteTextBlinkMode
ptyxis_settings_get_text_blink_mode (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), 0);

  return g_settings_get_enum (self->settings, PTYXIS_SETTING_KEY_TEXT_BLINK_MODE);
}

void
ptyxis_settings_set_text_blink_mode (PtyxisSettings   *self,
                                     VteTextBlinkMode  text_blink_mode)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_enum (self->settings,
                       PTYXIS_SETTING_KEY_TEXT_BLINK_MODE,
                       text_blink_mode);
}

gboolean
ptyxis_settings_get_restore_session (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, PTYXIS_SETTING_KEY_RESTORE_SESSION);
}

void
ptyxis_settings_set_restore_session (PtyxisSettings *self,
                                     gboolean        restore_session)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_boolean (self->settings,
                          PTYXIS_SETTING_KEY_RESTORE_SESSION,
                          restore_session);
}

gboolean
ptyxis_settings_get_restore_window_size (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, PTYXIS_SETTING_KEY_RESTORE_WINDOW_SIZE);
}

void
ptyxis_settings_set_restore_window_size (PtyxisSettings *self,
                                         gboolean        restore_window_size)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_boolean (self->settings,
                          PTYXIS_SETTING_KEY_RESTORE_WINDOW_SIZE,
                          restore_window_size);
}

void
ptyxis_settings_get_default_size (PtyxisSettings *self,
                                  guint          *columns,
                                  guint          *rows)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  if (columns != NULL)
    *columns = ptyxis_settings_get_default_columns (self);

  if (rows != NULL)
    *rows = ptyxis_settings_get_default_rows (self);
}

guint
ptyxis_settings_get_default_columns (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), 0);

  return g_settings_get_uint (self->settings, PTYXIS_SETTING_KEY_DEFAULT_COLUMNS);
}

guint
ptyxis_settings_get_default_rows (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), 0);

  return g_settings_get_uint (self->settings, PTYXIS_SETTING_KEY_DEFAULT_ROWS);
}

void
ptyxis_settings_set_default_columns (PtyxisSettings *self,
                                     guint          columns)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_uint (self->settings,
                      PTYXIS_SETTING_KEY_DEFAULT_COLUMNS,
                      columns);
}

void
ptyxis_settings_set_default_rows (PtyxisSettings *self,
                                  guint          rows)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_uint (self->settings,
                      PTYXIS_SETTING_KEY_DEFAULT_ROWS,
                      rows);
}

void
ptyxis_settings_get_window_size (PtyxisSettings *self,
                                 guint          *columns,
                                 guint          *rows)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));
  g_return_if_fail (columns != NULL);
  g_return_if_fail (rows != NULL);

  g_settings_get (self->settings, "window-size", "(uu)", columns, rows);
}

void
ptyxis_settings_set_window_size (PtyxisSettings *self,
                                 guint           columns,
                                 guint           rows)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set (self->settings, "window-size", "(uu)", columns, rows);
}

AdwColorScheme
ptyxis_settings_get_interface_style (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), 0);

  return g_settings_get_enum (self->settings, PTYXIS_SETTING_KEY_INTERFACE_STYLE);
}

void
ptyxis_settings_set_interface_style (PtyxisSettings *self,
                                     AdwColorScheme  color_scheme)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));
  g_return_if_fail (color_scheme == ADW_COLOR_SCHEME_DEFAULT ||
                    color_scheme == ADW_COLOR_SCHEME_FORCE_LIGHT ||
                    color_scheme == ADW_COLOR_SCHEME_FORCE_DARK);

  g_settings_set_enum (self->settings, PTYXIS_SETTING_KEY_INTERFACE_STYLE, color_scheme);
}

gboolean
ptyxis_settings_get_toast_on_copy_clipboard (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings, PTYXIS_SETTING_KEY_TOAST_ON_COPY_CLIPBOARD);
}

void
ptyxis_settings_set_toast_on_copy_clipboard (PtyxisSettings *self,
                                             gboolean        toast_on_copy_clipboard)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_boolean (self->settings,
                          PTYXIS_SETTING_KEY_TOAST_ON_COPY_CLIPBOARD,
                          toast_on_copy_clipboard);
}

void
ptyxis_settings_set_disable_padding (PtyxisSettings *self,
                                     gboolean        disable_padding)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_boolean (self->settings,
                          PTYXIS_SETTING_KEY_DISABLE_PADDING,
                          !!disable_padding);
}

gboolean
ptyxis_settings_get_disable_padding (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings,
                                 PTYXIS_SETTING_KEY_DISABLE_PADDING);
}

char *
ptyxis_settings_dup_word_char_exceptions (PtyxisSettings *self)
{
  char *word_char_exceptions;

  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), NULL);

  g_settings_get (self->settings, PTYXIS_SETTING_KEY_WORD_CHAR_EXCEPTIONS, "ms", &word_char_exceptions);
  return word_char_exceptions;
}

void
ptyxis_settings_set_prompt_on_close (PtyxisSettings *self,
                                     gboolean        prompt_on_close)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_boolean (self->settings,
                          PTYXIS_SETTING_KEY_PROMPT_ON_CLOSE,
                          !!prompt_on_close);
}

gboolean
ptyxis_settings_get_prompt_on_close (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings,
                                 PTYXIS_SETTING_KEY_PROMPT_ON_CLOSE);
}

void
ptyxis_settings_set_ignore_osc_title (PtyxisSettings *self,
                                      gboolean        ignore_osc_title)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_boolean (self->settings,
                          PTYXIS_SETTING_KEY_IGNORE_OSC_TITLE,
                          !!ignore_osc_title);
}

gboolean
ptyxis_settings_get_ignore_osc_title (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings,
                                 PTYXIS_SETTING_KEY_IGNORE_OSC_TITLE);
}

void
ptyxis_settings_set_inhibit_logout (PtyxisSettings *self,
                                    gboolean        inhibit_logout)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_boolean (self->settings,
                          PTYXIS_SETTING_KEY_INHIBIT_LOGOUT,
                          !!inhibit_logout);
}

gboolean
ptyxis_settings_get_inhibit_logout (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings,
                                 PTYXIS_SETTING_KEY_INHIBIT_LOGOUT);
}

void
ptyxis_settings_set_select_to_copy (PtyxisSettings *self,
                                    gboolean        select_to_copy)
{
  g_return_if_fail (PTYXIS_IS_SETTINGS (self));

  g_settings_set_boolean (self->settings,
                          PTYXIS_SETTING_KEY_SELECT_TO_COPY,
                          !!select_to_copy);
}

gboolean
ptyxis_settings_get_select_to_copy (PtyxisSettings *self)
{
  g_return_val_if_fail (PTYXIS_IS_SETTINGS (self), FALSE);

  return g_settings_get_boolean (self->settings,
                                 PTYXIS_SETTING_KEY_SELECT_TO_COPY);
}
