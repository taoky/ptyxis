/* ptyxis-pane.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "config.h"
#include "ptyxis-pane.h"

struct _PtyxisPane
{
  GtkWidget parent_instance;
  PtyxisTerminal *terminal;
};

G_DEFINE_FINAL_TYPE (PtyxisPane, ptyxis_pane, GTK_TYPE_WIDGET)

static void
ptyxis_pane_dispose (GObject *object)
{
  PtyxisPane *self = PTYXIS_PANE (object);
  GtkWidget *child;

  self->terminal = NULL;
  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  G_OBJECT_CLASS (ptyxis_pane_parent_class)->dispose (object);
}

static void
ptyxis_pane_class_init (PtyxisPaneClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = ptyxis_pane_dispose;
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "ptyxis-pane");
}

static void
ptyxis_pane_init (PtyxisPane *self)
{
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

void
ptyxis_pane_set_terminal (PtyxisPane     *self,
                          PtyxisTerminal *terminal)
{
  g_return_if_fail (PTYXIS_IS_PANE (self));
  g_return_if_fail (PTYXIS_IS_TERMINAL (terminal));
  g_return_if_fail (self->terminal == NULL || self->terminal == terminal);
  self->terminal = terminal;
}
