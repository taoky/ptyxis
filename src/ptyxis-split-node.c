/* ptyxis-split-node.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "config.h"
#include "ptyxis-split-node.h"

struct _PtyxisSplitNode
{
  gatomicrefcount ref_count;
  PtyxisSplitNode *parent;
  PtyxisSplitNode *first;
  PtyxisSplitNode *second;
  GObject *pane;
  PtyxisSplitDirection direction;
  double ratio;
};

PtyxisSplitNode *
ptyxis_split_node_new_leaf (GObject *pane)
{
  PtyxisSplitNode *self;

  g_return_val_if_fail (G_IS_OBJECT (pane), NULL);
  self = g_new0 (PtyxisSplitNode, 1);
  g_atomic_ref_count_init (&self->ref_count);
  self->pane = g_object_ref (pane);
  self->ratio = .5;
  return self;
}

PtyxisSplitNode *
ptyxis_split_node_ref (PtyxisSplitNode *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_atomic_ref_count_inc (&self->ref_count);
  return self;
}

void
ptyxis_split_node_unref (PtyxisSplitNode *self)
{
  if (self != NULL && g_atomic_ref_count_dec (&self->ref_count))
    {
      g_clear_object (&self->pane);
      ptyxis_split_node_unref (self->first);
      ptyxis_split_node_unref (self->second);
      g_free (self);
    }
}

gboolean ptyxis_split_node_is_leaf (PtyxisSplitNode *self) { return self && self->pane != NULL; }
GObject *ptyxis_split_node_get_pane (PtyxisSplitNode *self) { return self ? self->pane : NULL; }
PtyxisSplitNode *ptyxis_split_node_get_parent (PtyxisSplitNode *self) { return self ? self->parent : NULL; }
PtyxisSplitNode *ptyxis_split_node_get_first (PtyxisSplitNode *self) { return self ? self->first : NULL; }
PtyxisSplitNode *ptyxis_split_node_get_second (PtyxisSplitNode *self) { return self ? self->second : NULL; }
PtyxisSplitDirection ptyxis_split_node_get_direction (PtyxisSplitNode *self) { return self ? self->direction : 0; }
double ptyxis_split_node_get_ratio (PtyxisSplitNode *self) { return self ? self->ratio : .5; }

void
ptyxis_split_node_set_ratio (PtyxisSplitNode *self,
                             double           ratio)
{
  g_return_if_fail (self != NULL);
  self->ratio = CLAMP (ratio, .05, .95);
}

PtyxisSplitNode *
ptyxis_split_node_split (PtyxisSplitNode      *leaf,
                         PtyxisSplitDirection  direction,
                         double                ratio,
                         GObject              *new_pane)
{
  PtyxisSplitNode *first;
  PtyxisSplitNode *second;

  g_return_val_if_fail (ptyxis_split_node_is_leaf (leaf), NULL);
  g_return_val_if_fail (G_IS_OBJECT (new_pane), NULL);

  first = ptyxis_split_node_new_leaf (leaf->pane);
  second = ptyxis_split_node_new_leaf (new_pane);
  first->parent = leaf;
  second->parent = leaf;
  g_clear_object (&leaf->pane);
  leaf->first = first;
  leaf->second = second;
  leaf->direction = direction;
  ptyxis_split_node_set_ratio (leaf, ratio);
  return second;
}

gboolean
ptyxis_split_node_remove (PtyxisSplitNode *leaf)
{
  PtyxisSplitNode *parent;
  PtyxisSplitNode *sibling;

  g_return_val_if_fail (ptyxis_split_node_is_leaf (leaf), FALSE);
  if ((parent = leaf->parent) == NULL)
    return FALSE;

  sibling = parent->first == leaf ? parent->second : parent->first;
  parent->pane = g_steal_pointer (&sibling->pane);
  parent->direction = sibling->direction;
  parent->ratio = sibling->ratio;
  parent->first = g_steal_pointer (&sibling->first);
  parent->second = g_steal_pointer (&sibling->second);
  if (parent->first) parent->first->parent = parent;
  if (parent->second) parent->second->parent = parent;
  leaf->parent = NULL;
  sibling->parent = NULL;
  ptyxis_split_node_unref (leaf);
  ptyxis_split_node_unref (sibling);
  return TRUE;
}

guint
ptyxis_split_node_count_leaves (PtyxisSplitNode *self)
{
  g_return_val_if_fail (self != NULL, 0);
  if (ptyxis_split_node_is_leaf (self))
    return 1;
  return ptyxis_split_node_count_leaves (self->first) +
         ptyxis_split_node_count_leaves (self->second);
}

PtyxisSplitNode *
ptyxis_split_node_get_nth_leaf (PtyxisSplitNode *self,
                                guint            position)
{
  guint first_count;

  g_return_val_if_fail (self != NULL, NULL);

  if (ptyxis_split_node_is_leaf (self))
    return position == 0 ? self : NULL;

  first_count = ptyxis_split_node_count_leaves (self->first);
  if (position < first_count)
    return ptyxis_split_node_get_nth_leaf (self->first, position);

  return ptyxis_split_node_get_nth_leaf (self->second, position - first_count);
}
