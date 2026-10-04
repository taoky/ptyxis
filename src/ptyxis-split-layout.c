/* ptyxis-split-layout.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Glue between the GtkPaned widgets building up a split layout and the
 * PtyxisSplitNode tree that models it.
 *
 * A GtkPaned can outlive the split node it was created for: closing a pane
 * collapses the sibling node into their parent and frees both the removed
 * leaf and the sibling. Storing the node pointer as callback data would
 * leave the surviving GtkPaned writing through a dangling pointer. The
 * association is therefore kept on the widget itself and looked up at
 * emission time, so it can be moved to the collapsed node before the old
 * one is freed.
 */

#include "config.h"

#include <math.h>

#include "ptyxis-split-layout.h"

static GQuark split_node_quark;

static GQuark
split_node_key (void)
{
  if (G_UNLIKELY (split_node_quark == 0))
    split_node_quark = g_quark_from_static_string ("ptyxis-split-node");
  return split_node_quark;
}

static PtyxisSplitNode *
ptyxis_split_layout_get_node (GtkPaned *paned)
{
  return g_object_get_qdata (G_OBJECT (paned), split_node_key ());
}

static void
ptyxis_split_layout_position_changed (GtkPaned   *paned,
                                      GParamSpec *pspec)
{
  PtyxisSplitNode *node = ptyxis_split_layout_get_node (paned);
  GtkOrientation orientation;
  int extent;
  int position;

  if (node == NULL)
    return;

  orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (paned));
  extent = orientation == GTK_ORIENTATION_HORIZONTAL
         ? gtk_widget_get_width (GTK_WIDGET (paned))
         : gtk_widget_get_height (GTK_WIDGET (paned));
  position = gtk_paned_get_position (paned);

  if (extent > 1)
    ptyxis_split_node_set_ratio (node, (double)position / extent);
}

static gboolean
ptyxis_split_layout_apply_ratio (GtkWidget     *widget,
                                 GdkFrameClock *frame_clock,
                                 gpointer       user_data)
{
  PtyxisSplitNode *node = ptyxis_split_layout_get_node (GTK_PANED (widget));
  GtkOrientation orientation;
  int extent;

  if (node == NULL)
    return G_SOURCE_REMOVE;

  orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (widget));
  extent = orientation == GTK_ORIENTATION_HORIZONTAL
         ? gtk_widget_get_width (widget)
         : gtk_widget_get_height (widget);

  if (extent <= 1)
    return G_SOURCE_CONTINUE;

  gtk_paned_set_position (GTK_PANED (widget),
                          round (extent * ptyxis_split_node_get_ratio (node)));

  /* Only start tracking position changes once the initial ratio has been
   * applied. GtkPaned computes an initial position from the children's
   * natural sizes during the first layout, and writing that back would
   * clobber the ratio this paned was bound to restore.
   */
  g_signal_connect (widget,
                    "notify::position",
                    G_CALLBACK (ptyxis_split_layout_position_changed),
                    NULL);
  return G_SOURCE_REMOVE;
}

/**
 * ptyxis_split_layout_bind:
 * @paned: the paned managing a split
 * @node: the internal node modeling @paned's split
 *
 * Associates @paned with @node and schedules applying @node's ratio once
 * @paned has been allocated. Position tracking starts when that has
 * happened; until then the callbacks look the node up on the widget, so
 * the association can be moved at any time with
 * ptyxis_split_layout_rebind().
 */
void
ptyxis_split_layout_bind (GtkPaned        *paned,
                          PtyxisSplitNode *node)
{
  g_return_if_fail (GTK_IS_PANED (paned));
  g_return_if_fail (node != NULL);

  g_object_set_qdata (G_OBJECT (paned), split_node_key (), node);
  gtk_widget_add_tick_callback (GTK_WIDGET (paned),
                                ptyxis_split_layout_apply_ratio,
                                NULL, NULL);
}

/**
 * ptyxis_split_layout_rebind:
 * @paned: the paned managing a split
 * @node: the node which absorbed @paned's previous node
 *
 * Moves the association of @paned to @node. This must be done before the
 * previous node is freed when collapsing splits, so that pending position
 * callbacks never observe a stale node.
 */
void
ptyxis_split_layout_rebind (GtkPaned        *paned,
                            PtyxisSplitNode *node)
{
  g_return_if_fail (GTK_IS_PANED (paned));
  g_return_if_fail (node != NULL);

  g_object_set_qdata (G_OBJECT (paned), split_node_key (), node);
}
