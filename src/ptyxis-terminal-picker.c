/* ptyxis-terminal-picker.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <glib/gi18n.h>

#include "ptyxis-application.h"
#include "ptyxis-terminal-picker.h"
#include "ptyxis-terminal-picker-match.h"
#include "ptyxis-util.h"

typedef struct _PtyxisTerminalPickerItem
{
  GObject parent_instance;
  char *tab_uuid;
  char *pane_uuid;
  char *title;
  char *path;
  char *context;
  gint64 mru;
  guint fallback;
  int score;
  guint current : 1;
} PtyxisTerminalPickerItem;

typedef GObjectClass PtyxisTerminalPickerItemClass;

GType ptyxis_terminal_picker_item_get_type (void);
G_DEFINE_TYPE (PtyxisTerminalPickerItem, ptyxis_terminal_picker_item, G_TYPE_OBJECT)

struct _PtyxisTerminalPicker
{
  AdwDialog parent_instance;
  PtyxisWindow *source_window;
  GtkSearchEntry *search;
  GtkListView *list;
  GtkSingleSelection *selection;
  GtkStack *stack;
  GListStore *snapshot;
  GListStore *visible;
  char *pending_tab_uuid;
  char *pending_pane_uuid;
};

G_DEFINE_FINAL_TYPE (PtyxisTerminalPicker, ptyxis_terminal_picker, ADW_TYPE_DIALOG)

static void
ptyxis_terminal_picker_item_finalize (GObject *object)
{
  PtyxisTerminalPickerItem *self = (PtyxisTerminalPickerItem *)object;

  g_clear_pointer (&self->tab_uuid, g_free);
  g_clear_pointer (&self->pane_uuid, g_free);
  g_clear_pointer (&self->title, g_free);
  g_clear_pointer (&self->path, g_free);
  g_clear_pointer (&self->context, g_free);
  G_OBJECT_CLASS (ptyxis_terminal_picker_item_parent_class)->finalize (object);
}

static void
ptyxis_terminal_picker_item_class_init (PtyxisTerminalPickerItemClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = ptyxis_terminal_picker_item_finalize;
}

static void
ptyxis_terminal_picker_item_init (PtyxisTerminalPickerItem *self)
{
}

static int
compare_items (gconstpointer a,
               gconstpointer b,
               gpointer      user_data)
{
  const PtyxisTerminalPickerItem *ia = *(PtyxisTerminalPickerItem * const *)a;
  const PtyxisTerminalPickerItem *ib = *(PtyxisTerminalPickerItem * const *)b;
  gboolean empty = GPOINTER_TO_INT (user_data);

  if (empty && ia->current != ib->current)
    return ib->current - ia->current;
  if (ia->score != ib->score)
    return ib->score - ia->score;
  if (ia->mru != ib->mru)
    return ia->mru < ib->mru ? 1 : -1;
  return ia->fallback < ib->fallback ? -1 : ia->fallback > ib->fallback;
}

static void ptyxis_terminal_picker_refresh (PtyxisTerminalPicker *self);

static void
ptyxis_terminal_picker_rebuild (PtyxisTerminalPicker *self)
{
  g_autoptr(GPtrArray) matches = g_ptr_array_new_with_free_func (g_object_unref);
  const char *query = gtk_editable_get_text (GTK_EDITABLE (self->search));
  g_autofree char *stripped = g_strdup (query);
  gboolean empty = stripped == NULL || g_strstrip (stripped)[0] == 0;

  if (empty)
    query = "";

  g_list_store_remove_all (self->visible);
  for (guint i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (self->snapshot)); i++)
    {
      PtyxisTerminalPickerItem *item = g_list_model_get_item (G_LIST_MODEL (self->snapshot), i);

      item->score = ptyxis_terminal_picker_match (query, item->title, item->path);
      if (item->score >= 0)
        g_ptr_array_add (matches, item);
      else
        g_object_unref (item);
    }

  g_ptr_array_sort_with_data (matches, compare_items, GINT_TO_POINTER (empty));
  for (guint i = 0; i < matches->len; i++)
    g_list_store_append (self->visible, g_ptr_array_index (matches, i));

  gtk_stack_set_visible_child_name (self->stack, matches->len ? "results" : "empty");
  if (matches->len > 0)
    gtk_single_selection_set_selected (self->selection, empty && matches->len > 1 ? 1 : 0);
}

static void
search_changed_cb (PtyxisTerminalPicker *self)
{
  ptyxis_terminal_picker_rebuild (self);
}

static void
factory_setup_cb (GtkSignalListItemFactory *factory,
                  GtkListItem              *list_item)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *title = gtk_label_new (NULL);
  GtkWidget *subtitle = gtk_label_new (NULL);

  gtk_widget_set_margin_top (box, 8);
  gtk_widget_set_margin_bottom (box, 8);
  gtk_widget_set_margin_start (box, 12);
  gtk_widget_set_margin_end (box, 12);
  gtk_label_set_xalign (GTK_LABEL (title), 0);
  gtk_label_set_ellipsize (GTK_LABEL (title), PANGO_ELLIPSIZE_END);
  gtk_widget_add_css_class (title, "heading");
  gtk_label_set_xalign (GTK_LABEL (subtitle), 0);
  gtk_label_set_ellipsize (GTK_LABEL (subtitle), PANGO_ELLIPSIZE_MIDDLE);
  gtk_widget_add_css_class (subtitle, "dim-label");
  gtk_box_append (GTK_BOX (box), title);
  gtk_box_append (GTK_BOX (box), subtitle);
  gtk_list_item_set_child (list_item, box);
}

static void
factory_bind_cb (GtkSignalListItemFactory *factory,
                 GtkListItem              *list_item)
{
  PtyxisTerminalPickerItem *item = gtk_list_item_get_item (list_item);
  GtkWidget *box = gtk_list_item_get_child (list_item);
  GtkWidget *title = gtk_widget_get_first_child (box);
  GtkWidget *subtitle = gtk_widget_get_next_sibling (title);
  g_autofree char *detail = NULL;

  gtk_label_set_text (GTK_LABEL (title), item->title);
  detail = item->path[0] ? g_strdup_printf ("%s  ·  %s", item->path, item->context) : g_strdup (item->context);
  gtk_label_set_text (GTK_LABEL (subtitle), detail);
}

static void
activate_position (PtyxisTerminalPicker *self,
                   guint                 position)
{
  PtyxisTerminalPickerItem *item;

  item = g_list_model_get_item (G_LIST_MODEL (self->visible), position);
  if (item == NULL)
    return;

  g_set_str (&self->pending_tab_uuid, item->tab_uuid);
  g_set_str (&self->pending_pane_uuid, item->pane_uuid);
  adw_dialog_close (ADW_DIALOG (self));
  g_object_unref (item);
}

static void
list_activate_cb (PtyxisTerminalPicker *self,
                  guint                 position)
{
  activate_position (self, position);
}

static void
search_activate_cb (PtyxisTerminalPicker *self)
{
  guint selected = gtk_single_selection_get_selected (self->selection);

  if (selected != GTK_INVALID_LIST_POSITION)
    activate_position (self, selected);
}

static gboolean
key_pressed_cb (PtyxisTerminalPicker *self,
                guint                 keyval,
                guint                 keycode,
                GdkModifierType       state)
{
  guint n_items = g_list_model_get_n_items (G_LIST_MODEL (self->visible));
  guint selected = gtk_single_selection_get_selected (self->selection);

  if (keyval == GDK_KEY_Escape)
    {
      adw_dialog_close (ADW_DIALOG (self));
      return TRUE;
    }

  if (n_items > 0 && (keyval == GDK_KEY_Down || keyval == GDK_KEY_Up))
    {
      if (selected == GTK_INVALID_LIST_POSITION)
        selected = 0;
      else if (keyval == GDK_KEY_Down)
        selected = (selected + 1) % n_items;
      else
        selected = (selected + n_items - 1) % n_items;
      gtk_single_selection_set_selected (self->selection, selected);
      gtk_list_view_scroll_to (self->list, selected, GTK_LIST_SCROLL_FOCUS, NULL);
      return TRUE;
    }
  return FALSE;
}

static void
ptyxis_terminal_picker_refresh (PtyxisTerminalPicker *self)
{
  const GList *windows = gtk_application_get_windows (GTK_APPLICATION (PTYXIS_APPLICATION_DEFAULT));
  guint window_number = 0;
  guint fallback = 0;

  g_list_store_remove_all (self->snapshot);
  for (const GList *iter = windows; iter; iter = iter->next)
    {
      PtyxisWindow *window;
      g_autoptr(GListModel) pages = NULL;
      const char *window_title;

      if (!PTYXIS_IS_WINDOW (iter->data))
        continue;
      window = iter->data;
      window_number++;
      window_title = gtk_window_get_title (GTK_WINDOW (window));
      pages = ptyxis_window_list_pages (window);

      for (guint ti = 0; ti < g_list_model_get_n_items (pages); ti++)
        {
          g_autoptr(AdwTabPage) page = g_list_model_get_item (pages, ti);
          PtyxisTab *tab = PTYXIS_TAB (adw_tab_page_get_child (page));
          guint n_panes = ptyxis_tab_get_n_panes (tab);

          for (guint pi = 0; pi < n_panes; pi++)
            {
              PtyxisPane *pane = ptyxis_tab_get_pane (tab, pi);
              PtyxisTerminalPickerItem *item = g_object_new (ptyxis_terminal_picker_item_get_type (), NULL);
              gboolean current_window = window == self->source_window;
              gboolean current = current_window &&
                                 tab == ptyxis_window_get_active_tab (window) &&
                                 pane == ptyxis_tab_get_active_pane (tab);
              g_autofree char *where = current_window
                ? g_strdup (_("Current Window"))
                : g_strdup_printf (_("Window %u — %s"), window_number, window_title ?: ptyxis_app_name ());
              g_autofree char *pane_part = n_panes > 1
                ? g_strdup_printf (_("Pane %u/%u"), pi + 1, n_panes)
                : NULL;
              g_autofree char *tab_part = g_strdup_printf (_("Tab %u"), ti + 1);

              item->tab_uuid = g_strdup (ptyxis_tab_get_uuid (tab));
              item->pane_uuid = g_strdup (ptyxis_pane_get_uuid (pane));
              item->title = ptyxis_tab_dup_pane_title (tab, pane);
              item->path = ptyxis_tab_dup_pane_directory (tab, pane);
              item->mru = ptyxis_pane_get_last_focus_time (pane);
              item->fallback = fallback++;
              item->current = current;
              item->context = pane_part
                ? g_strdup_printf ("%s · %s · %s%s", where, tab_part, pane_part,
                                   current ? _(" · Current") : "")
                : g_strdup_printf ("%s · %s%s", where, tab_part,
                                   current ? _(" · Current") : "");
              g_list_store_append (self->snapshot, item);
              g_object_unref (item);
            }
        }
    }
  ptyxis_terminal_picker_rebuild (self);
}

static void
ptyxis_terminal_picker_dispose (GObject *object)
{
  PtyxisTerminalPicker *self = (PtyxisTerminalPicker *)object;

  self->source_window = NULL;
  g_clear_pointer (&self->pending_tab_uuid, g_free);
  g_clear_pointer (&self->pending_pane_uuid, g_free);
  g_clear_object (&self->snapshot);

  /* gtk_single_selection_new() takes ownership of visible, and
   * gtk_list_view_new() in turn takes ownership of selection.  Both fields
   * are borrowed pointers.  The AdwDialog parent must dispose the widget
   * subtree (and therefore the list view) before those objects disappear.
   */
  self->selection = NULL;
  self->visible = NULL;
  G_OBJECT_CLASS (ptyxis_terminal_picker_parent_class)->dispose (object);
}

static gboolean
focus_pending_cb (gpointer data)
{
  PtyxisTerminalPicker *self = data;
  g_autofree char *tab_uuid = g_steal_pointer (&self->pending_tab_uuid);
  g_autofree char *pane_uuid = g_steal_pointer (&self->pending_pane_uuid);

  if (!ptyxis_application_focus_pane_by_uuid (PTYXIS_APPLICATION_DEFAULT,
                                              tab_uuid,
                                              pane_uuid) &&
      self->source_window != NULL)
    {
      ptyxis_terminal_picker_refresh (self);
      adw_dialog_present (ADW_DIALOG (self), GTK_WIDGET (self->source_window));
      adw_dialog_set_focus (ADW_DIALOG (self), GTK_WIDGET (self->search));
    }

  return G_SOURCE_REMOVE;
}

static void
ptyxis_terminal_picker_closed_cb (PtyxisTerminalPicker *self)
{
  if (self->pending_tab_uuid != NULL)
    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                     focus_pending_cb,
                     g_object_ref (self),
                     g_object_unref);
}

static void
ptyxis_terminal_picker_class_init (PtyxisTerminalPickerClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = ptyxis_terminal_picker_dispose;
}

static void
ptyxis_terminal_picker_init (PtyxisTerminalPicker *self)
{
  GtkWidget *toolbar = adw_toolbar_view_new ();
  GtkWidget *header = adw_header_bar_new ();
  GtkWidget *close = gtk_button_new_from_icon_name ("window-close-symbolic");
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *scroller = gtk_scrolled_window_new ();
  GtkWidget *empty = adw_status_page_new ();
  GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
  GtkEventController *keys = gtk_event_controller_key_new ();

  gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);

  self->snapshot = g_list_store_new (ptyxis_terminal_picker_item_get_type ());
  self->visible = g_list_store_new (ptyxis_terminal_picker_item_get_type ());
  self->selection = gtk_single_selection_new (G_LIST_MODEL (self->visible));
  self->search = GTK_SEARCH_ENTRY (gtk_search_entry_new ());
  self->list = GTK_LIST_VIEW (gtk_list_view_new (GTK_SELECTION_MODEL (self->selection), factory));
  self->stack = GTK_STACK (gtk_stack_new ());

  adw_header_bar_set_title_widget (ADW_HEADER_BAR (header), gtk_label_new (_("Go to Terminal")));
  adw_header_bar_set_show_start_title_buttons (ADW_HEADER_BAR (header), FALSE);
  adw_header_bar_set_show_end_title_buttons (ADW_HEADER_BAR (header), FALSE);
  gtk_widget_set_tooltip_text (close, _("Close"));
  gtk_widget_add_css_class (close, "flat");
  adw_header_bar_pack_end (ADW_HEADER_BAR (header), close);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);
  gtk_search_entry_set_placeholder_text (self->search, _("Search terminals"));
  gtk_widget_set_margin_top (GTK_WIDGET (self->search), 12);
  gtk_widget_set_margin_bottom (GTK_WIDGET (self->search), 12);
  gtk_widget_set_margin_start (GTK_WIDGET (self->search), 12);
  gtk_widget_set_margin_end (GTK_WIDGET (self->search), 12);
  gtk_widget_set_size_request (scroller, 560, 360);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroller), GTK_WIDGET (self->list));
  adw_status_page_set_title (ADW_STATUS_PAGE (empty), _("No Matching Terminals"));
  gtk_stack_add_named (self->stack, scroller, "results");
  gtk_stack_add_named (self->stack, empty, "empty");
  gtk_box_append (GTK_BOX (box), GTK_WIDGET (self->search));
  gtk_box_append (GTK_BOX (box), GTK_WIDGET (self->stack));
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), box);
  adw_dialog_set_child (ADW_DIALOG (self), toolbar);
  adw_dialog_set_content_width (ADW_DIALOG (self), 600);
  adw_dialog_set_content_height (ADW_DIALOG (self), 460);
  adw_dialog_set_presentation_mode (ADW_DIALOG (self), ADW_DIALOG_FLOATING);
  adw_dialog_set_focus (ADW_DIALOG (self), GTK_WIDGET (self->search));

  g_signal_connect (factory, "setup", G_CALLBACK (factory_setup_cb), NULL);
  g_signal_connect (factory, "bind", G_CALLBACK (factory_bind_cb), NULL);
  g_signal_connect_swapped (self->search, "search-changed", G_CALLBACK (search_changed_cb), self);
  g_signal_connect_swapped (self->search, "activate", G_CALLBACK (search_activate_cb), self);
  g_signal_connect_swapped (self->list, "activate", G_CALLBACK (list_activate_cb), self);
  g_signal_connect_swapped (self, "closed", G_CALLBACK (ptyxis_terminal_picker_closed_cb), self);
  g_signal_connect_swapped (keys, "key-pressed", G_CALLBACK (key_pressed_cb), self);
  g_signal_connect_swapped (close, "clicked", G_CALLBACK (adw_dialog_close), self);
  gtk_widget_add_controller (GTK_WIDGET (self), keys);
}

PtyxisTerminalPicker *
ptyxis_terminal_picker_new (PtyxisWindow *window)
{
  PtyxisTerminalPicker *self;

  g_return_val_if_fail (PTYXIS_IS_WINDOW (window), NULL);
  self = g_object_new (PTYXIS_TYPE_TERMINAL_PICKER, NULL);
  self->source_window = window;
  ptyxis_terminal_picker_refresh (self);
  return self;
}
