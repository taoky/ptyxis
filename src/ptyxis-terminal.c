/*
 * ptyxis-terminal.c
 *
 * Copyright © 2001 Havoc Pennington
 * Copyright © 2007, 2008, 2010, 2011 Christian Persch
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
#include "glib.h"

#define PCRE2_CODE_UNIT_WIDTH 0
#include <pcre2.h>

#include <adwaita.h>

#include <glib/gi18n.h>

#include "ptyxis-application.h"
#include "ptyxis-custom-link.h"
#include "ptyxis-shortcuts.h"
#include "ptyxis-tab.h"
#include "ptyxis-terminal.h"
#include "ptyxis-util.h"
#include "ptyxis-window.h"

#include "terminal-regex.h"

#define SIZE_DISMISS_TIMEOUT_MSEC 1000
#define URL_MATCH_CURSOR_NAME "pointer"

#define DROP_REQUEST_PRIORITY               G_PRIORITY_DEFAULT
#define APPLICATION_VND_PORTAL_FILETRANSFER "application/vnd.portal.filetransfer"
#define APPLICATION_VND_PORTAL_FILES        "application/vnd.portal.files"
#define TEXT_X_MOZ_URL                      "text/x-moz-url"
#define TEXT_URI_LIST                       "text/uri-list"

#define FILE_ATTRIBUTE_HOST_PATH "xattr::document-portal.host-path"

struct _PtyxisTerminal
{
  VteTerminal        parent_instance;

  PtyxisShortcuts    *shortcuts;
  PtyxisPalette      *palette;
  char               *url;
  GHashTable         *custom_links;

  GtkPopover         *popover;
  GMenu              *terminal_menu;
  GtkWidget          *drop_highlight;
  GtkDropTargetAsync *drop_target;
  GtkRevealer        *size_revealer;
  GtkLabel           *size_label;

  GdkRGBA             background;

  guint               size_dismiss_source;
  guint               n_columns;
  guint               n_rows;

  guint               cell_height;
  guint               cell_width;
};

enum {
  PROP_0,
  PROP_CURRENT_CONTAINER_NAME,
  PROP_CURRENT_CONTAINER_RUNTIME,
  PROP_PALETTE,
  PROP_SHORTCUTS,
  N_PROPS
};

enum {
  MATCH_CLICKED,
  GRID_SIZE_CHANGED,
  SHELL_PRECMD,
  SHELL_PREEXEC,
  N_SIGNALS
};

G_DEFINE_FINAL_TYPE (PtyxisTerminal, ptyxis_terminal, VTE_TYPE_TERMINAL)

static GParamSpec *properties [N_PROPS];
static guint signals[N_SIGNALS];
static const char * const url_regexes_str[] = {
  REGEX_URL_AS_IS,
  REGEX_URL_HTTP,
  REGEX_URL_FILE,
  REGEX_EMAIL,
};
static VteRegex *url_regexes[G_N_ELEMENTS (url_regexes_str)];

static void
ptyxis_terminal_update_colors (PtyxisTerminal *self)
{
  const PtyxisPaletteFace *face;
  AdwStyleManager *style_manager;
  gboolean dark;

  g_assert (PTYXIS_IS_TERMINAL (self));

  style_manager = adw_style_manager_get_default ();
  dark = adw_style_manager_get_dark (style_manager);

  if (self->palette == NULL)
    self->palette = ptyxis_palette_lookup ("gnome");

  face = ptyxis_palette_get_face (self->palette, dark);

  vte_terminal_set_colors (VTE_TERMINAL (self),
                           &face->foreground,
                           &face->background,
                           &face->indexed[0],
                           G_N_ELEMENTS (face->indexed));

  self->background = face->background;

  if (face->cursor_bg.alpha > 0)
    {
      vte_terminal_set_color_cursor (VTE_TERMINAL (self), &face->cursor_bg);

      if (face->cursor_fg.alpha)
        vte_terminal_set_color_cursor_foreground (VTE_TERMINAL (self), &face->cursor_fg);
      else
        vte_terminal_set_color_cursor_foreground (VTE_TERMINAL (self), &face->background);
    }
  else
    {
      vte_terminal_set_color_cursor (VTE_TERMINAL (self), NULL);
      vte_terminal_set_color_cursor_foreground (VTE_TERMINAL (self), NULL);
    }
}

static void
ptyxis_terminal_toast (PtyxisTerminal *self,
                       int             timeout,
                       const char     *title)
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

static gboolean
ptyxis_terminal_is_active (PtyxisTerminal *self)
{
  PtyxisTerminal *active_terminal = NULL;
  PtyxisWindow *window;
  PtyxisTab *active_tab;

  g_assert (PTYXIS_IS_TERMINAL (self));

  if ((window = PTYXIS_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), PTYXIS_TYPE_WINDOW))) &&
      (active_tab = ptyxis_window_get_active_tab (window)))
    active_terminal = ptyxis_tab_get_terminal (active_tab);

  return active_terminal == self;
}

static void
ptyxis_terminal_update_clipboard_actions (PtyxisTerminal *self)
{
  GdkClipboard *clipboard;
  gboolean can_paste;
  gboolean has_selection;

  g_assert (PTYXIS_IS_TERMINAL (self));

  clipboard = gtk_widget_get_clipboard (GTK_WIDGET (self));
  can_paste = gdk_content_formats_contain_gtype (gdk_clipboard_get_formats (clipboard), G_TYPE_STRING);
  has_selection = vte_terminal_get_has_selection (VTE_TERMINAL (self));

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "clipboard.copy", has_selection);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "clipboard.copy-as-html", has_selection);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "clipboard.paste", can_paste);
}

static void
ptyxis_terminal_update_url_actions (PtyxisTerminal *self,
                                    double          x,
                                    double          y)
{
  g_autofree char *pattern = NULL;
  int tag = 0;

  g_assert (PTYXIS_IS_TERMINAL (self));

  if (!(pattern = vte_terminal_check_hyperlink_at (VTE_TERMINAL (self), x, y)))
    pattern = vte_terminal_check_match_at (VTE_TERMINAL (self), x, y, &tag);

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "clipboard.copy-link", pattern != NULL);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "terminal.open-link", pattern != NULL);

  g_set_str (&self->url, pattern);
}

static gboolean
ptyxis_terminal_match_clicked (PtyxisTerminal  *self,
                               double           x,
                               double           y,
                               int              button,
                               GdkModifierType  state,
                               const char      *match)
{
  gboolean ret = FALSE;

  g_assert (PTYXIS_IS_TERMINAL (self));
  g_assert (match != NULL);

  g_signal_emit (self, signals[MATCH_CLICKED], 0, x, y, button, state, match, &ret);

  return ret;
}

static void
ptyxis_terminal_setup_context_menu (VteTerminal           *terminal,
                                    const VteEventContext *context)
{
  PtyxisTerminal *self = (PtyxisTerminal *)terminal;
  double x, y;

  g_assert (PTYXIS_IS_TERMINAL (self));

  if (context == NULL)
    {
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "clipboard.copy-link", FALSE);
      gtk_widget_action_set_enabled (GTK_WIDGET (self), "terminal.open-link", FALSE);
      return;
    }

  ptyxis_terminal_update_clipboard_actions (self);

  vte_event_context_get_coordinates (context, &x, &y);

  ptyxis_terminal_update_url_actions (self, x, y);
  gtk_popover_set_pointing_to (self->popover, &(GdkRectangle) {x, y, 1, 1});

  if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
    gtk_widget_set_halign (GTK_WIDGET (self->popover), GTK_ALIGN_END);
  else
    gtk_widget_set_halign (GTK_WIDGET (self->popover), GTK_ALIGN_START);
}

static void
ptyxis_terminal_capture_click_pressed_cb (PtyxisTerminal  *self,
                                          int              n_press,
                                          double           x,
                                          double           y,
                                          GtkGestureClick *click)
{
  g_autofree char *hyperlink = NULL;
  g_autofree char *match = NULL;
  GdkModifierType state;
  gboolean handled = FALSE;
  GdkEvent *event;
  int button;
  int tag = 0;

  g_assert (PTYXIS_IS_TERMINAL (self));
  g_assert (GTK_IS_GESTURE_CLICK (click));

  event = gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (click));
  state = gdk_event_get_modifier_state (event) & gtk_accelerator_get_default_mod_mask ();
  button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (click));

  hyperlink = vte_terminal_check_hyperlink_at (VTE_TERMINAL (self), x, y);
  match = vte_terminal_check_match_at (VTE_TERMINAL (self), x, y, &tag);

  if (n_press == 1 &&
      !handled &&
      (button == 1 || button == 2) &&
      (state & GDK_CONTROL_MASK))
    {
      if (hyperlink != NULL)
        handled = ptyxis_terminal_match_clicked (self, x, y, button, state, hyperlink);
      else if (match != NULL)
        {
          PtyxisCustomLink *custom_link = g_hash_table_lookup (self->custom_links, GINT_TO_POINTER (tag));

          if (custom_link != NULL)
            {
              g_autofree gchar *subst = ptyxis_custom_link_substitute (custom_link, match);

              if (subst != NULL)
                handled = ptyxis_terminal_match_clicked (self, x, y, button, state, subst);
            }
          else
            handled = ptyxis_terminal_match_clicked (self, x, y, button, state, match);
        }
    }

  if (handled)
    gtk_gesture_set_state (GTK_GESTURE (click), GTK_EVENT_SEQUENCE_CLAIMED);
  else
    gtk_gesture_set_state (GTK_GESTURE (click), GTK_EVENT_SEQUENCE_DENIED);
}

static void
ptyxis_terminal_scroll_to_bottom (PtyxisTerminal *self)
{
  GtkAdjustment *adjustment;
  GtkWidget *scroller;
  double upper;
  double value;
  double page_size;

  g_assert (PTYXIS_IS_TERMINAL (self));

  scroller = gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_SCROLLED_WINDOW);
  adjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroller));
  upper = gtk_adjustment_get_upper (adjustment);
  value = gtk_adjustment_get_value (adjustment);
  page_size = gtk_adjustment_get_page_size (adjustment);

  if (upper - page_size > value)
    gtk_adjustment_set_value (adjustment, upper - page_size);
}

static gboolean
ptyxis_terminal_capture_key_pressed_cb (PtyxisTerminal     *self,
                                        guint               keyval,
                                        guint               keycode,
                                        GdkModifierType     state,
                                        GtkEventController *controller)
{
  GtkScrolledWindow *scroller;
  GtkAdjustment *adjustment;
  double upper;
  double value;
  double page_size;
  GdkEvent *event;

  g_assert (PTYXIS_IS_TERMINAL (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_KEY (controller));


  /* HACK:
   *
   * This hack works around the fact that GtkScrolledWindow will
   * attempt to continue a kinetic scroll even though VteTerminal will
   * adjust the GtkAdjustment:value to the bottom of the view when
   * scroll-on-keystroke is enabled.
   *
   * This is managed by clearing and resetting the kinetic scrolling
   * property as that will clear any pending kinetic scrolling attempt.
   */

  /* Make sure the property is even enabled first */
  if (!vte_terminal_get_scroll_on_keystroke (VTE_TERMINAL (self)))
    return FALSE;

  /* Check all of the input keyvals which are just modifiers and
   * leave those alone until an input key is pressed.
   */
  event = gtk_event_controller_get_current_event (controller);
  if (gdk_key_event_is_modifier (event))
    return FALSE;

  /* Find our scrolled window and see if kinetic strolling is even enabled */
  scroller = GTK_SCROLLED_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_SCROLLED_WINDOW));
  if (!gtk_scrolled_window_get_kinetic_scrolling (scroller))
    return FALSE;

  /* Tweaking the property is somewhat expensive, so make sure we're not
   * already at the bottom of the visible area before tweaking.
   */
  adjustment = gtk_scrolled_window_get_vadjustment (scroller);
  upper = gtk_adjustment_get_upper (adjustment);
  value = gtk_adjustment_get_value (adjustment);
  page_size = gtk_adjustment_get_page_size (adjustment);
  if (upper - page_size > value)
    {
      gtk_scrolled_window_set_kinetic_scrolling (scroller, FALSE);
      gtk_scrolled_window_set_kinetic_scrolling (scroller, TRUE);
    }

  return FALSE;
}

static void
select_all_action (GtkWidget  *widget,
                   const char *action_name,
                   GVariant   *param)
{
  g_assert (PTYXIS_IS_TERMINAL (widget));
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_BOOLEAN));

  if (g_variant_get_boolean (param))
    vte_terminal_select_all (VTE_TERMINAL (widget));
  else
    vte_terminal_unselect_all (VTE_TERMINAL (widget));
}

static void
copy_clipboard_action (GtkWidget  *widget,
                       const char *action_name,
                       GVariant   *param)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL (widget);
  GdkClipboard *clipboard = gtk_widget_get_clipboard (widget);
  g_autofree char *text = NULL;
  VteFormat format = VTE_FORMAT_TEXT;
  gboolean is_select_to_copy = g_strcmp0 (action_name, "clipboard.copy-by-select") == 0;

  if (strstr (action_name, "copy-as-html"))
    format = VTE_FORMAT_HTML;

  text = vte_terminal_get_text_selected (VTE_TERMINAL (widget), format);

  if (text && text[0] != 0)
    {
      PtyxisSettings *settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);

      gdk_clipboard_set_text (clipboard, text);

      if (ptyxis_settings_get_toast_on_copy_clipboard (settings) && !is_select_to_copy)
        ptyxis_terminal_toast (self, 1, _("Copied to clipboard"));
    }
}

static void
paste_clipboard_action (GtkWidget  *widget,
                        const char *action_name,
                        GVariant   *param)
{
  g_assert (PTYXIS_IS_TERMINAL (widget));

  ptyxis_terminal_paste (PTYXIS_TERMINAL (widget));
}

static void
ptyxis_terminal_handle_select_to_copy (PtyxisTerminal *self)
{
  PtyxisSettings *settings;

  g_assert (PTYXIS_IS_TERMINAL (self));

  settings = ptyxis_application_get_settings (PTYXIS_APPLICATION_DEFAULT);

  if (ptyxis_settings_get_select_to_copy (settings))
    {
      if (vte_terminal_get_has_selection (VTE_TERMINAL (self)))
        copy_clipboard_action (GTK_WIDGET (self), "clipboard.copy-by-select", NULL);
    }
}

static void
ptyxis_terminal_selection_changed (VteTerminal *terminal)
{
  ptyxis_terminal_update_clipboard_actions (PTYXIS_TERMINAL (terminal));
  ptyxis_terminal_handle_select_to_copy (PTYXIS_TERMINAL (terminal));
}

static void
copy_link_address_action (GtkWidget  *widget,
                          const char *action_name,
                          GVariant   *param)
{
  PtyxisTerminal *self = (PtyxisTerminal *)widget;

  g_assert (PTYXIS_IS_TERMINAL (self));

  if (self->url && self->url[0])
    {
      gdk_clipboard_set_text (gtk_widget_get_clipboard (GTK_WIDGET (self)), self->url);
      ptyxis_terminal_toast (self, 1, _("Copied to clipboard"));
    }
}

static void
open_link_action (GtkWidget  *widget,
                  const char *action_name,
                  GVariant   *param)
{
  PtyxisTerminal *self = (PtyxisTerminal *)widget;
  PtyxisTab *tab;

  g_assert (PTYXIS_IS_TERMINAL (self));

  if (ptyxis_str_empty0 (self->url))
    return;

  tab = PTYXIS_TAB (gtk_widget_get_ancestor (widget, PTYXIS_TYPE_TAB));

  ptyxis_tab_open_uri (tab, self->url);
}

typedef struct {
  PtyxisTerminal *terminal;
  GdkDrop *drop;
  GList *files;
  const char *mime_type;
} TextUriList;

static void
text_uri_list_free (TextUriList *uri_list)
{
  g_clear_object (&uri_list->terminal);
  g_clear_object (&uri_list->drop);
  g_clear_list (&uri_list->files, g_object_unref);
  uri_list->mime_type = NULL;
  g_free (uri_list);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (TextUriList, text_uri_list_free)

static gboolean
file_is_from_document_portal (GFile *file)
{
#ifdef G_OS_UNIX
  static char *docportal;

  if G_UNLIKELY (docportal == NULL)
    docportal = g_strdup_printf ("%s/doc/", g_get_user_runtime_dir ());

  if (g_file_is_native (file))
    {
      const char *path = g_file_peek_path (file);

      if (g_str_has_prefix (path, docportal))
        return TRUE;
    }
#endif

  return FALSE;
}

static char *
ptyxis_terminal_get_portal_host_path (GFile *file)
{
  g_autoptr(GFileInfo) info = NULL;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  if ((info = g_file_query_info (file,
                                 FILE_ATTRIBUTE_HOST_PATH,
                                 G_FILE_QUERY_INFO_NONE,
                                 NULL, NULL)))
    {
      const char *host_path;

      if ((host_path = g_file_info_get_attribute_string (info, FILE_ATTRIBUTE_HOST_PATH)))
        {
          g_autofree gchar *fs_path = NULL;
          gsize len = strlen (host_path);

          /* Early portal versions added a "\x00" suffix, trim it if present */
          if (len > 4 && g_strcmp0 (&host_path[len-4], "\\x00") == 0)
              fs_path = g_strndup (host_path, len - 4);
          else
              fs_path = g_strdup (host_path);

          return g_filename_display_name (fs_path);
        }
    }

  return NULL;
}

static void
ptyxis_terminal_drop_file_list (PtyxisTerminal *self,
                                const GList    *files)
{
  g_autoptr(GString) string = NULL;

  g_assert (PTYXIS_IS_TERMINAL (self));
  g_assert (files == NULL || G_IS_FILE (files->data));

  string = g_string_new (NULL);

  for (const GList *iter = files; iter; iter = iter->next)
    {
      g_autofree char *host_path = NULL;
      GFile *file = G_FILE (iter->data);

      if (file_is_from_document_portal (file) &&
          (host_path = ptyxis_terminal_get_portal_host_path (file)))
        {
          g_autofree char *quoted = g_shell_quote (host_path);

          g_string_append (string, quoted);
          g_string_append_c (string, ' ');
        }
      else if (g_file_is_native (file))
        {
          g_autofree char *quoted = g_shell_quote (g_file_peek_path (file));

          g_string_append (string, quoted);
          g_string_append_c (string, ' ');
        }
      else
        {
          g_autofree char *uri = g_file_get_uri (file);
          g_autofree char *quoted = g_shell_quote (uri);

          g_string_append (string, quoted);
          g_string_append_c (string, ' ');
        }
    }

  if (string->len > 0)
    vte_terminal_paste_text (VTE_TERMINAL (self), string->str);
}

static void
ptyxis_terminal_drop_uri_list_line_cb (GObject      *object,
                                       GAsyncResult *result,
                                       gpointer      user_data)
{
  GDataInputStream *line_reader = G_DATA_INPUT_STREAM (object);
  g_autoptr(TextUriList) state = (TextUriList*)user_data;
  g_autoptr(GError) error = NULL;
  g_autofree char *line = NULL;
  gsize len = 0;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (state != NULL);
  g_assert (PTYXIS_IS_TERMINAL (state->terminal));
  g_assert (GDK_IS_DROP (state->drop));

  line = g_data_input_stream_read_line_finish_utf8 (line_reader, result, &len, &error);

  if (error != NULL)
    {
      g_debug ("Failed to receive '%s': %s", state->mime_type, error->message);
      gdk_drop_finish (state->drop, 0);
      return;
    }

  if (line != NULL && line[0] != 0 && line[0] != '#')
    {
      GFile *file = g_file_new_for_uri (line);

      if (file != NULL)
        state->files = g_list_append (state->files, file);
    }

  if (line == NULL || g_strcmp0 (state->mime_type, TEXT_X_MOZ_URL) == 0)
    {
      ptyxis_terminal_drop_file_list (state->terminal, state->files);
      gdk_drop_finish (state->drop, GDK_ACTION_COPY);
      return;
    }

  g_data_input_stream_read_line_async (line_reader,
                                       DROP_REQUEST_PRIORITY,
                                       NULL,
                                       ptyxis_terminal_drop_uri_list_line_cb,
                                       g_steal_pointer (&state));
}

static void
ptyxis_terminal_drop_uri_list_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  GdkDrop *drop = (GdkDrop *)object;
  g_autoptr(PtyxisTerminal) self = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GInputStream) stream = NULL;
  g_autoptr(GDataInputStream) line_reader = NULL;
  g_autoptr(TextUriList) state = NULL;
  const char *mime_type = NULL;

  g_assert (GDK_IS_DROP (drop));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (PTYXIS_IS_TERMINAL (self));

  if (!(stream = gdk_drop_read_finish (drop, result, &mime_type, &error)))
    {
      g_debug ("Failed to receive text/uri-list offer: %s", error->message);
      gdk_drop_finish (drop, 0);
      return;
    }

  g_assert (g_strcmp0 (mime_type, TEXT_URI_LIST) == 0);
  g_assert (G_IS_INPUT_STREAM (stream));

  line_reader = g_data_input_stream_new (stream);
  g_data_input_stream_set_newline_type (line_reader,
                                        G_DATA_STREAM_NEWLINE_TYPE_CR_LF);

  state = g_new0 (TextUriList, 1);
  state->terminal = g_object_ref (self);
  state->drop = g_object_ref (drop);
  state->mime_type = g_intern_string (mime_type);

  g_data_input_stream_read_line_async (line_reader,
                                       DROP_REQUEST_PRIORITY,
                                       NULL,
                                       ptyxis_terminal_drop_uri_list_line_cb,
                                       g_steal_pointer (&state));
}

static void
ptyxis_terminal_drop_file_list_cb (GObject      *object,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  GdkDrop *drop = (GdkDrop *)object;
  g_autoptr(PtyxisTerminal) self = user_data;
  g_autoptr(GError) error = NULL;
  const GValue *value;
  const GList *file_list;

  g_assert (GDK_IS_DROP (drop));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (PTYXIS_IS_TERMINAL (self));

  if (!(value = gdk_drop_read_value_finish (drop, result, &error)))
    {
      g_debug ("Failed to receive file-list offer: %s", error->message);

      /* If the user dragged a directory from Nautilus or another
       * new-style application, a portal request would be made. But
       * GTK won't be able to open the directory so the request for
       * APPLICATION_VND_PORTAL_FILETRANSFER will fail. Fallback to
       * opening the request via TEXT_URI_LIST gracefully.
       */
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
          g_error_matches (error, G_IO_ERROR, G_DBUS_ERROR_ACCESS_DENIED))
        gdk_drop_read_async (drop,
                             (const char **)(const char * const[]){TEXT_URI_LIST, NULL},
                             DROP_REQUEST_PRIORITY,
                             NULL,
                             ptyxis_terminal_drop_uri_list_cb,
                             g_object_ref (self));
      else
        gdk_drop_finish (drop, 0);

      return;
    }

  g_assert (value != NULL);
  g_assert (G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST));

  file_list = (const GList *)g_value_get_boxed (value);
  ptyxis_terminal_drop_file_list (self, file_list);
  gdk_drop_finish (drop, GDK_ACTION_COPY);
}

static void
ptyxis_terminal_drop_string_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  GdkDrop *drop = (GdkDrop *)object;
  g_autoptr(PtyxisTerminal) self = user_data;
  g_autoptr(GError) error = NULL;
  const GValue *value;
  const char *string;

  g_assert (GDK_IS_DROP (drop));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (PTYXIS_IS_TERMINAL (self));

  if (!(value = gdk_drop_read_value_finish (drop, result, &error)))
    {
      gdk_drop_finish (drop, 0);
      return;
    }

  g_assert (value != NULL);
  g_assert (G_VALUE_HOLDS_STRING (value));

  string = g_value_get_string (value);

  if (string != NULL && string[0] != 0)
    vte_terminal_paste_text (VTE_TERMINAL (self), string);

  gdk_drop_finish (drop, GDK_ACTION_COPY);
}

static void
ptyxis_terminal_drop_moz_url_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  GdkDrop *drop = (GdkDrop *)object;
  g_autoptr(PtyxisTerminal) self = user_data;
  g_autoptr(GCharsetConverter) converter = NULL;
  g_autoptr(GDataInputStream) line_reader = NULL;
  g_autoptr(GInputStream) converter_stream = NULL;
  g_autoptr(GInputStream) stream = NULL;
  g_autoptr(GError) error = NULL;
  const char *mime_type = NULL;
  TextUriList *state;

  g_assert (GDK_IS_DROP (drop));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (PTYXIS_IS_TERMINAL (self));

  if (!(stream = gdk_drop_read_finish (drop, result, &mime_type, &error))) {
    gdk_drop_finish (drop, 0);
    return;
  }

  g_assert (G_IS_INPUT_STREAM (stream));

  if (!(converter = g_charset_converter_new ("UTF-8", "UCS-2", &error))) {
    g_debug ("Failed to create UTF-8 decoder: %s", error->message);
    gdk_drop_finish (drop, 0);
    return;
  }

  /* TEXT_X_MOZ_URL is in UCS-2 so convert it to UTF-8.
   *
   * The data is expected to be URL, a \n, then the title of the web page.
   *
   * However, some applications (e.g. dolphin) delimit with a \r\n (see
   * issue#293) so handle that generically with the line reader.
   */
  converter_stream = g_converter_input_stream_new (stream, G_CONVERTER (converter));
  line_reader = g_data_input_stream_new (converter_stream);
  g_data_input_stream_set_newline_type (line_reader,
                                        G_DATA_STREAM_NEWLINE_TYPE_ANY);

  state = g_new0 (TextUriList, 1);
  state->terminal = g_object_ref (self);
  state->drop = g_object_ref (drop);
  state->mime_type = g_intern_string (TEXT_X_MOZ_URL);

  g_data_input_stream_read_line_async (line_reader,
                                       DROP_REQUEST_PRIORITY,
                                       NULL,
                                       ptyxis_terminal_drop_uri_list_line_cb,
                                       g_steal_pointer (&state));
}

static gboolean
ptyxis_terminal_drop_target_drop (PtyxisTerminal     *self,
                                  GdkDrop            *drop,
                                  double              x,
                                  double              y,
                                  GtkDropTargetAsync *drop_target)
{
  GdkContentFormats *formats;

  g_assert (PTYXIS_IS_TERMINAL (self));
  g_assert (GDK_IS_DROP (drop));
  g_assert (GTK_IS_DROP_TARGET_ASYNC (drop_target));

  formats = gdk_drop_get_formats (drop);

  if (gdk_content_formats_contain_gtype (formats, GDK_TYPE_FILE_LIST) ||
      gdk_content_formats_contain_gtype (formats, G_TYPE_FILE) ||
      gdk_content_formats_contain_mime_type (formats, TEXT_URI_LIST) ||
      gdk_content_formats_contain_mime_type (formats, APPLICATION_VND_PORTAL_FILETRANSFER) ||
      gdk_content_formats_contain_mime_type (formats, APPLICATION_VND_PORTAL_FILES)) {
    gdk_drop_read_value_async (drop,
                               GDK_TYPE_FILE_LIST,
                               DROP_REQUEST_PRIORITY,
                               NULL,
                               ptyxis_terminal_drop_file_list_cb,
                               g_object_ref (self));
    return TRUE;
  } else if (gdk_content_formats_contain_mime_type (formats, TEXT_X_MOZ_URL)) {
    gdk_drop_read_async (drop,
                         (const char **)(const char * const []){TEXT_X_MOZ_URL, NULL},
                         DROP_REQUEST_PRIORITY,
                         NULL,
                         ptyxis_terminal_drop_moz_url_cb,
                         g_object_ref (self));
    return TRUE;
  } else if (gdk_content_formats_contain_gtype (formats, G_TYPE_STRING)) {
    gdk_drop_read_value_async (drop,
                               G_TYPE_STRING,
                               DROP_REQUEST_PRIORITY,
                               NULL,
                               ptyxis_terminal_drop_string_cb,
                               g_object_ref (self));
    return TRUE;
  }

  return FALSE;
}

static GdkDragAction
ptyxis_terminal_drop_target_drag_enter (PtyxisTerminal     *self,
                                        GdkDrop            *drop,
                                        double              x,
                                        double              y,
                                        GtkDropTargetAsync *drop_target)
{
  g_assert (PTYXIS_IS_TERMINAL (self));
  g_assert (GTK_IS_DROP_TARGET_ASYNC (drop_target));

  gtk_widget_set_visible (self->drop_highlight, TRUE);

  return GDK_ACTION_COPY;
}

static void
ptyxis_terminal_drop_target_drag_leave (PtyxisTerminal     *self,
                                        GdkDrop            *drop,
                                        GtkDropTargetAsync *drop_target)
{
  g_assert (PTYXIS_IS_TERMINAL (self));
  g_assert (GTK_IS_DROP_TARGET_ASYNC (drop_target));

  gtk_widget_set_visible (self->drop_highlight, FALSE);
}

static void
ptyxis_terminal_measure (GtkWidget      *widget,
                         GtkOrientation  orientation,
                         int             for_size,
                         int            *minimum,
                         int            *natural,
                         int            *minimum_baseline,
                         int            *natural_baseline)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL (widget);
  int min_revealer;
  int nat_revealer;

  GTK_WIDGET_CLASS (ptyxis_terminal_parent_class)->measure (widget,
                                                            orientation,
                                                            for_size,
                                                            minimum,
                                                            natural,
                                                            minimum_baseline,
                                                            natural_baseline);

  gtk_widget_measure (GTK_WIDGET (self->size_revealer),
                      orientation, for_size,
                      &min_revealer, &nat_revealer, NULL, NULL);

  *minimum = MAX (*minimum, min_revealer);
  *natural = MAX (*natural, nat_revealer);
}

static gboolean
dismiss_size_label_cb (gpointer user_data)
{
  PtyxisTerminal *self = user_data;

  gtk_revealer_set_reveal_child (self->size_revealer, FALSE);
  self->size_dismiss_source = 0;

  return G_SOURCE_REMOVE;
}

static void
ptyxis_terminal_size_allocate (GtkWidget *widget,
                               int        width,
                               int        height,
                               int        baseline)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL (widget);
  GtkRequisition min;
  GtkAllocation revealer_alloc, dnd_alloc;
  GtkBorder padding, margin;
  GtkRoot *root;
  gboolean emit_size_changed = FALSE;
  int column_count;
  int row_count;

  g_assert (PTYXIS_IS_TERMINAL (self));

  GTK_WIDGET_CLASS (ptyxis_terminal_parent_class)->size_allocate (widget, width, height, baseline);

  column_count = vte_terminal_get_column_count (VTE_TERMINAL (self));
  row_count = vte_terminal_get_row_count (VTE_TERMINAL (self));
  emit_size_changed = self->n_columns != column_count || self->n_rows != row_count;
  self->n_columns = column_count;
  self->n_rows = row_count;

  root = gtk_widget_get_root (widget);

  if (ptyxis_terminal_is_active (self) &&
      GTK_IS_WINDOW (root) &&
      !gtk_window_is_maximized (GTK_WINDOW (root)) &&
      !gtk_window_is_fullscreen (GTK_WINDOW (root)) &&
      emit_size_changed)
    {
      char format[32];

      g_snprintf (format, sizeof format, "%ld × %ld",
                  vte_terminal_get_column_count (VTE_TERMINAL (self)),
                  vte_terminal_get_row_count (VTE_TERMINAL (self)));
      gtk_label_set_label (self->size_label, format);
      gtk_accessible_announce (GTK_ACCESSIBLE (self->size_label),
                               format,
                               GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);

      gtk_revealer_set_reveal_child (self->size_revealer, TRUE);

      g_clear_handle_id (&self->size_dismiss_source, g_source_remove);
      self->size_dismiss_source = g_timeout_add (SIZE_DISMISS_TIMEOUT_MSEC,
                                                 dismiss_size_label_cb,
                                                 self);
    }
  else if (gtk_window_is_maximized (GTK_WINDOW (root)) ||
           gtk_window_is_fullscreen (GTK_WINDOW (root)))
    {
      g_clear_handle_id (&self->size_dismiss_source, g_source_remove);
      gtk_revealer_set_reveal_child (self->size_revealer, FALSE);
    }

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
  gtk_style_context_get_margin (gtk_widget_get_style_context (widget), &margin);
  gtk_style_context_get_padding (gtk_widget_get_style_context (widget), &padding);
  G_GNUC_END_IGNORE_DEPRECATIONS;

  gtk_widget_get_preferred_size (GTK_WIDGET (self->size_revealer), &min, NULL);
  revealer_alloc.x = width - min.width + padding.right;
  revealer_alloc.y = height + padding.bottom + margin.bottom - min.height;
  revealer_alloc.width = min.width;
  revealer_alloc.height = min.height;
  gtk_widget_size_allocate (GTK_WIDGET (self->size_revealer), &revealer_alloc, -1);

  gtk_widget_get_preferred_size (GTK_WIDGET (self->drop_highlight), &min, NULL);
  dnd_alloc.x = -padding.left + 1;
  dnd_alloc.y = 1;
  dnd_alloc.width = padding.left - 1 + width + padding.right - 1;
  dnd_alloc.height = height - 2;
  gtk_widget_size_allocate (GTK_WIDGET (self->drop_highlight), &dnd_alloc, -1);

  if (emit_size_changed)
    g_signal_emit (self, signals[GRID_SIZE_CHANGED], 0, column_count, row_count);
}

/*
 * ptyxis_terminal_rewrite_snapshot:
 *
 * This function will chain up to the parent VteTerminal to snapshot
 * the terminal. However, afterwards, it rewrites the snapshot to
 * both optimize a large window draw (by removing the color node
 * similar to what vte_terminal_set_clear_background() would do) as
 * well as removing the toplevel clip node.
 *
 * By doing so, we allow our PtyxisTerminal widget to have padding
 * in the normal case (so that it fits rounded corners well) but also
 * allow the content to reach the top and bottom when scrolling.
 */
static void
ptyxis_terminal_rewrite_snapshot (GtkWidget   *widget,
                                  GtkSnapshot *snapshot)
{
  g_autoptr(GtkSnapshot) alternate = NULL;
  g_autoptr(GskRenderNode) root = NULL;
  g_autoptr(GPtrArray) children = NULL;
  gboolean dropped_bg = FALSE;

  g_assert (GTK_IS_SNAPSHOT (snapshot));

  alternate = gtk_snapshot_new ();
  children = g_ptr_array_new ();

  GTK_WIDGET_CLASS (ptyxis_terminal_parent_class)->snapshot (widget, alternate);

  if (!(root = gtk_snapshot_free_to_node (g_steal_pointer (&alternate))))
    return;

  if (gsk_render_node_get_node_type (root) == GSK_CONTAINER_NODE)
    {
      guint n_children = gsk_container_node_get_n_children (root);

      for (guint i = 0; i < n_children; i++)
        {
          GskRenderNode *node = gsk_container_node_get_child (root, i);
          GskRenderNodeType node_type = gsk_render_node_get_node_type (node);

          /* Drop the color node because we get that for free from our
           * background recoloring. This avoids an extra large overdraw
           * as a bonus optimization while we fix clipping.
           */
          if (!dropped_bg && node_type == GSK_COLOR_NODE)
            {
              dropped_bg = TRUE;
              continue;
            }

          /* If we get a clip node here, it's because we're in some
           * sort of window size that has partial line offset in the
           * drag resize, or we're scrolled up a bit so the line doesn't
           * exactly match our actual sizing. In that case we'll replace
           * the clip with our own so that we get nice padding normally
           * but appropriate draws up to the border elsewise.
           */
          if (node_type == GSK_CLIP_NODE)
            node = gsk_clip_node_get_child (node);

          g_ptr_array_add (children, node);
        }
    }
  else if (gsk_render_node_get_node_type (root) == GSK_COLOR_NODE)
    {
      /* If we got a color node then we are probably blinking a cursor on
       * screen and this is the frame where there is no cursor visible.
       *
       * This is effectively the background and we don't care about that for
       * the same reasons as above. Just remove it and paint nothing.
       */
      return;
    }

  if (children->len > 0)
    {
      GskRenderNode *new_root;

      new_root = gsk_container_node_new ((GskRenderNode **)children->pdata, children->len);
      gsk_render_node_unref (root);
      root = new_root;
    }

  gtk_snapshot_append_node (snapshot, root);
}

static void
ptyxis_terminal_snapshot (GtkWidget   *widget,
                          GtkSnapshot *snapshot)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL (widget);
  GtkBorder padding;

  g_assert (PTYXIS_IS_TERMINAL (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  gtk_style_context_get_padding (gtk_widget_get_style_context (widget), &padding);
  G_GNUC_END_IGNORE_DEPRECATIONS

  gtk_snapshot_push_clip (snapshot,
                          &GRAPHENE_RECT_INIT (-2,
                                               -padding.top,
                                               gtk_widget_get_width (widget) + 4,
                                               padding.top + gtk_widget_get_height (widget) + padding.bottom));
  ptyxis_terminal_rewrite_snapshot (widget, snapshot);
  gtk_snapshot_pop (snapshot);

  gtk_widget_snapshot_child (widget, GTK_WIDGET (self->size_revealer), snapshot);
  gtk_widget_snapshot_child (widget, GTK_WIDGET (self->drop_highlight), snapshot);
}

static void
ptyxis_terminal_shortcuts_notify_cb (PtyxisTerminal  *self,
                                     GParamSpec      *pspec,
                                     PtyxisShortcuts *shortcuts)
{
  g_assert (PTYXIS_IS_TERMINAL (self));
  g_assert (PTYXIS_IS_SHORTCUTS (shortcuts));

  ptyxis_shortcuts_update_menu (shortcuts, self->terminal_menu);
}

static void
ptyxis_terminal_emit_shell_precmd (PtyxisTerminal *self)
{
  g_assert (PTYXIS_IS_TERMINAL (self));

  g_signal_emit (self, signals[SHELL_PRECMD], 0);
}

static void
ptyxis_terminal_emit_shell_preexec (PtyxisTerminal *self)
{
  g_assert (PTYXIS_IS_TERMINAL (self));

  g_signal_emit (self, signals[SHELL_PREEXEC], 0);
}

static void
notify_property_changed (PtyxisTerminal *self,
                         const char     *termprop,
                         GParamSpec     *property)
{
  g_object_notify_by_pspec (G_OBJECT (self), property);
}

static void
ptyxis_terminal_char_size_changed (VteTerminal *terminal,
                                   guint        width,
                                   guint        height)
{
  PtyxisTerminal *self = (PtyxisTerminal *)terminal;

  g_assert (PTYXIS_IS_TERMINAL (self));

  self->cell_width = width;
  self->cell_height = height;
}

static void
ptyxis_terminal_constructed (GObject *object)
{
  PtyxisTerminal *self = (PtyxisTerminal *)object;

  g_assert (PTYXIS_IS_TERMINAL (self));

  G_OBJECT_CLASS (ptyxis_terminal_parent_class)->constructed (object);

  adw_style_manager_get_dark (adw_style_manager_get_default());
  g_signal_connect_object (adw_style_manager_get_default (),
                           "notify::color-scheme",
                           G_CALLBACK (ptyxis_terminal_update_colors),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (adw_style_manager_get_default (),
                           "notify::dark",
                           G_CALLBACK (ptyxis_terminal_update_colors),
                           self,
                           G_CONNECT_SWAPPED);

  self->custom_links = g_hash_table_new_full (g_direct_hash,
                                              g_direct_equal,
                                              NULL,
                                              g_object_unref);
}

static void
ptyxis_terminal_dispose (GObject *object)
{
  PtyxisTerminal *self = (PtyxisTerminal *)object;

  g_debug ("Disposing %s @ %p", G_OBJECT_TYPE_NAME (self), object);

  gtk_widget_dispose_template (GTK_WIDGET (self), PTYXIS_TYPE_TERMINAL);

  g_clear_object (&self->palette);
  g_clear_object (&self->shortcuts);
  g_clear_handle_id (&self->size_dismiss_source, g_source_remove);
  g_clear_pointer (&self->custom_links, g_hash_table_unref);
  g_clear_pointer (&self->url, g_free);

  G_OBJECT_CLASS (ptyxis_terminal_parent_class)->dispose (object);
}

static void
ptyxis_terminal_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL (object);

  switch (prop_id)
    {
    case PROP_CURRENT_CONTAINER_NAME:
      g_value_set_string (value,
                          vte_terminal_get_termprop_string_by_id (VTE_TERMINAL (self),
                                                                  VTE_PROPERTY_ID_CONTAINER_NAME,
                                                                  NULL));
      break;

    case PROP_CURRENT_CONTAINER_RUNTIME:
      g_value_set_string (value,
                          vte_terminal_get_termprop_string_by_id (VTE_TERMINAL (self),
                                                                  VTE_PROPERTY_ID_CONTAINER_RUNTIME,
                                                                  NULL));
      break;

    case PROP_PALETTE:
      g_value_set_object (value, ptyxis_terminal_get_palette (self));
      break;

    case PROP_SHORTCUTS:
      g_value_set_object (value, self->shortcuts);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ptyxis_terminal_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL (object);

  switch (prop_id)
    {
    case PROP_PALETTE:
      ptyxis_terminal_set_palette (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ptyxis_terminal_class_init (PtyxisTerminalClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  VteTerminalClass *terminal_class = VTE_TERMINAL_CLASS (klass);

  object_class->constructed = ptyxis_terminal_constructed;
  object_class->dispose = ptyxis_terminal_dispose;
  object_class->get_property = ptyxis_terminal_get_property;
  object_class->set_property = ptyxis_terminal_set_property;

  widget_class->measure = ptyxis_terminal_measure;
  widget_class->size_allocate = ptyxis_terminal_size_allocate;
  widget_class->snapshot = ptyxis_terminal_snapshot;

  terminal_class->selection_changed = ptyxis_terminal_selection_changed;
  terminal_class->setup_context_menu = ptyxis_terminal_setup_context_menu;
  terminal_class->char_size_changed = ptyxis_terminal_char_size_changed;

  properties[PROP_CURRENT_CONTAINER_NAME] =
    g_param_spec_string ("current-container-name", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_CURRENT_CONTAINER_RUNTIME] =
    g_param_spec_string ("current-container-runtime", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_PALETTE] =
    g_param_spec_object ("palette", NULL, NULL,
                         PTYXIS_TYPE_PALETTE,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_SHORTCUTS] =
    g_param_spec_object ("shortcuts", NULL, NULL,
                         PTYXIS_TYPE_SHORTCUTS,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals[GRID_SIZE_CHANGED] =
    g_signal_new ("grid-size-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[MATCH_CLICKED] =
    g_signal_new ("match-clicked",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  g_signal_accumulator_true_handled, NULL,
                  NULL,
                  G_TYPE_BOOLEAN,
                  5,
                  G_TYPE_DOUBLE,
                  G_TYPE_DOUBLE,
                  G_TYPE_INT,
                  GDK_TYPE_MODIFIER_TYPE,
                  G_TYPE_STRING | G_SIGNAL_TYPE_STATIC_SCOPE);

  signals[SHELL_PRECMD] =
    g_signal_new ("shell-precmd",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 0);

  signals[SHELL_PREEXEC] =
    g_signal_new ("shell-preexec",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 0);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Ptyxis/ptyxis-terminal.ui");

  gtk_widget_class_bind_template_child (widget_class, PtyxisTerminal, drop_highlight);
  gtk_widget_class_bind_template_child (widget_class, PtyxisTerminal, drop_target);
  gtk_widget_class_bind_template_child (widget_class, PtyxisTerminal, popover);
  gtk_widget_class_bind_template_child (widget_class, PtyxisTerminal, size_label);
  gtk_widget_class_bind_template_child (widget_class, PtyxisTerminal, size_revealer);
  gtk_widget_class_bind_template_child (widget_class, PtyxisTerminal, terminal_menu);

  gtk_widget_class_bind_template_callback (widget_class, ptyxis_terminal_capture_click_pressed_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_terminal_capture_key_pressed_cb);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_terminal_drop_target_drag_enter);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_terminal_drop_target_drag_leave);
  gtk_widget_class_bind_template_callback (widget_class, ptyxis_terminal_drop_target_drop);

  gtk_widget_class_install_action (widget_class, "clipboard.copy", NULL, copy_clipboard_action);
  gtk_widget_class_install_action (widget_class, "clipboard.copy-as-html", NULL, copy_clipboard_action);
  gtk_widget_class_install_action (widget_class, "clipboard.copy-link", NULL, copy_link_address_action);
  gtk_widget_class_install_action (widget_class, "clipboard.paste", NULL, paste_clipboard_action);
  gtk_widget_class_install_action (widget_class, "terminal.open-link", NULL, open_link_action);
  gtk_widget_class_install_action (widget_class, "terminal.select-all", "b", select_all_action);

  for (guint i = 0; i < G_N_ELEMENTS (url_regexes); i++)
    {
      g_autoptr(GError) error = NULL;

      url_regexes[i] = vte_regex_new_for_match (url_regexes_str[i],
                                                strlen (url_regexes_str[i]),
                                                PCRE2_UTF | PCRE2_NO_UTF_CHECK | PCRE2_UCP | PCRE2_MULTILINE,
                                                &error);
      g_assert_no_error (error);
      g_assert_nonnull (url_regexes[i]);

      if (!vte_regex_jit (url_regexes[i], PCRE2_JIT_COMPLETE, &error) ||
          !vte_regex_jit (url_regexes[i], PCRE2_JIT_PARTIAL_SOFT, &error))
        g_warning ("Failed to JIT regex: %s: Regex was: %s",
                   error->message,
                   url_regexes_str[i]);
    }
}

static void
ptyxis_terminal_init (PtyxisTerminal *self)
{
  g_autoptr(GdkContentFormats) formats = NULL;
  GdkContentFormatsBuilder *builder;
  PtyxisApplication *app = PTYXIS_APPLICATION_DEFAULT;
  PtyxisShortcuts *shortcuts = ptyxis_application_get_shortcuts (app);

  g_set_object (&self->shortcuts, shortcuts);

  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (shortcuts,
                           "notify",
                           G_CALLBACK (ptyxis_terminal_shortcuts_notify_cb),
                           self,
                           G_CONNECT_SWAPPED);
  ptyxis_terminal_shortcuts_notify_cb (self, NULL, shortcuts);

  builder = gdk_content_formats_builder_new ();
  gdk_content_formats_builder_add_gtype (builder, G_TYPE_STRING);
  gdk_content_formats_builder_add_gtype (builder, GDK_TYPE_FILE_LIST);
  gdk_content_formats_builder_add_mime_type (builder, APPLICATION_VND_PORTAL_FILES);
  gdk_content_formats_builder_add_mime_type (builder, APPLICATION_VND_PORTAL_FILETRANSFER);
  gdk_content_formats_builder_add_mime_type (builder, TEXT_URI_LIST);
  gdk_content_formats_builder_add_mime_type (builder, TEXT_X_MOZ_URL);
  formats = gdk_content_formats_builder_free_to_formats (builder);

  gtk_drop_target_async_set_actions (self->drop_target,
                                     (GDK_ACTION_COPY |
                                      GDK_ACTION_MOVE));
  gtk_drop_target_async_set_formats (self->drop_target, formats);

  g_signal_connect (self,
                    "termprop-changed::" VTE_TERMPROP_SHELL_PRECMD,
                    G_CALLBACK (ptyxis_terminal_emit_shell_precmd),
                    NULL);
  g_signal_connect (self,
                    "termprop-changed::" VTE_TERMPROP_SHELL_PREEXEC,
                    G_CALLBACK (ptyxis_terminal_emit_shell_preexec),
                    NULL);
  g_signal_connect (self,
                    "termprop-changed::" VTE_TERMPROP_CONTAINER_NAME,
                    G_CALLBACK (notify_property_changed),
                    properties[PROP_CURRENT_CONTAINER_NAME]);
  g_signal_connect (self,
                    "termprop-changed::" VTE_TERMPROP_CONTAINER_RUNTIME,
                    G_CALLBACK (notify_property_changed),
                    properties[PROP_CURRENT_CONTAINER_RUNTIME]);

  g_signal_connect_object (gtk_widget_get_clipboard (GTK_WIDGET (self)),
                           "changed",
                           G_CALLBACK (ptyxis_terminal_update_clipboard_actions),
                           self,
                           G_CONNECT_SWAPPED);

  ptyxis_terminal_update_clipboard_actions (self);
}

PtyxisPalette *
ptyxis_terminal_get_palette (PtyxisTerminal *self)
{
  g_return_val_if_fail (PTYXIS_IS_TERMINAL (self), NULL);

  return self->palette;
}

void
ptyxis_terminal_set_palette (PtyxisTerminal *self,
                             PtyxisPalette  *palette)
{
  g_return_if_fail (PTYXIS_IS_TERMINAL (self));

  if (g_set_object (&self->palette, palette))
    {
      ptyxis_terminal_update_colors (self);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PALETTE]);
    }
}

const char *
ptyxis_terminal_get_current_container_name (PtyxisTerminal *self)
{
  gsize len;

  g_return_val_if_fail (PTYXIS_IS_TERMINAL (self), NULL);

  return vte_terminal_get_termprop_string (VTE_TERMINAL (self),
                                           VTE_TERMPROP_CONTAINER_NAME,
                                           &len);
}

const char *
ptyxis_terminal_get_current_container_runtime (PtyxisTerminal *self)
{
  gsize len;

  g_return_val_if_fail (PTYXIS_IS_TERMINAL (self), NULL);

  return vte_terminal_get_termprop_string (VTE_TERMINAL (self),
                                           VTE_TERMPROP_CONTAINER_RUNTIME,
                                           &len);
}

static char *
ptyxis_terminal_dup_termprop_uri_by_id (PtyxisTerminal *self,
                                        guint           property)
{
  g_autoptr(GUri) uri = NULL;

  g_assert (PTYXIS_IS_TERMINAL (self));

  if ((uri = vte_terminal_ref_termprop_uri_by_id (VTE_TERMINAL (self), property)))
    return g_uri_to_string (uri);

  return NULL;
}

char *
ptyxis_terminal_dup_current_directory_uri (PtyxisTerminal *self)
{
  g_return_val_if_fail (PTYXIS_IS_TERMINAL (self), NULL);

  return ptyxis_terminal_dup_termprop_uri_by_id (self, VTE_PROPERTY_ID_CURRENT_DIRECTORY_URI);
}

char *
ptyxis_terminal_dup_current_file_uri (PtyxisTerminal *self)
{
  g_autoptr(GUri) uri = NULL;

  g_return_val_if_fail (PTYXIS_IS_TERMINAL (self), NULL);

  return ptyxis_terminal_dup_termprop_uri_by_id (self, VTE_PROPERTY_ID_CURRENT_FILE_URI);
}

gboolean
ptyxis_terminal_can_paste (PtyxisTerminal *self)
{
  GdkClipboard *clipboard;

  g_return_val_if_fail (PTYXIS_IS_TERMINAL (self), FALSE);

  clipboard = gtk_widget_get_clipboard (GTK_WIDGET (self));

  return gdk_content_formats_contain_gtype (gdk_clipboard_get_formats (clipboard), G_TYPE_STRING);
}

void
ptyxis_terminal_paste (PtyxisTerminal *self)
{
  g_return_if_fail (PTYXIS_IS_TERMINAL (self));

  vte_terminal_paste_clipboard (VTE_TERMINAL (self));

  if (vte_terminal_get_scroll_on_keystroke (VTE_TERMINAL (self)))
    ptyxis_terminal_scroll_to_bottom (self);
}

void
ptyxis_terminal_reset_for_size (PtyxisTerminal *self)
{
  int width;
  int height;
  int cols;
  int rows;

  g_return_if_fail (PTYXIS_IS_TERMINAL (self));

  if (self->cell_width == 0 || self->cell_height == 0)
    return;

  width = gtk_widget_get_width (GTK_WIDGET (self));
  height = gtk_widget_get_height (GTK_WIDGET (self));

  cols = width / self->cell_width;
  rows = height / self->cell_height;

  vte_terminal_set_size (VTE_TERMINAL (self), cols, rows);
}

void
ptyxis_terminal_update_custom_links_list (PtyxisTerminal *self,
                                          GListModel     *custom_links)
{
  guint n_items;

  g_return_if_fail (PTYXIS_IS_TERMINAL (self));
  g_return_if_fail (G_IS_LIST_MODEL (custom_links));

  /* Clean-up previous regexes added to VTE */
  vte_terminal_match_remove_all (VTE_TERMINAL(self));
  g_hash_table_remove_all (self->custom_links);

  n_items = g_list_model_get_n_items (custom_links);

  /* First put custom links in order */
  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(PtyxisCustomLink) custom_link = g_list_model_get_item (custom_links, i);
      g_autoptr(VteRegex) regex = ptyxis_custom_link_compile (custom_link);
      int tag;

      if (regex == NULL)
        continue;

      tag = vte_terminal_match_add_regex (VTE_TERMINAL (self), regex, 0);
      vte_terminal_match_set_cursor_name (VTE_TERMINAL (self), tag, URL_MATCH_CURSOR_NAME);
      g_hash_table_insert (self->custom_links, GINT_TO_POINTER (tag), g_steal_pointer (&custom_link));
    }

  /* Then the predefined regexes */
  for (guint i = 0; i < G_N_ELEMENTS (url_regexes); i++)
    {
      int tag;

      tag = vte_terminal_match_add_regex (VTE_TERMINAL (self), url_regexes[i], 0);
      vte_terminal_match_set_cursor_name (VTE_TERMINAL (self), tag, URL_MATCH_CURSOR_NAME);
    }
}
