/* ptyxis-pane.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "config.h"
#include "ptyxis-pane.h"

struct _PtyxisPane
{
  GtkWidget parent_instance;
  PtyxisProfile *profile;
  PtyxisTerminal *terminal;
};

enum {
  PROP_0,
  PROP_PROFILE,
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
  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  G_OBJECT_CLASS (ptyxis_pane_parent_class)->dispose (object);
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
  object_class->get_property = ptyxis_pane_get_property;
  object_class->set_property = ptyxis_pane_set_property;

  properties[PROP_PROFILE] =
    g_param_spec_object ("profile", NULL, NULL,
                         PTYXIS_TYPE_PROFILE,
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
