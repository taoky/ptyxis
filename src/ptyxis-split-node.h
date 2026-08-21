/* ptyxis-split-node.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
  PTYXIS_SPLIT_HORIZONTAL,
  PTYXIS_SPLIT_VERTICAL,
} PtyxisSplitDirection;

typedef struct _PtyxisSplitNode PtyxisSplitNode;

PtyxisSplitNode *ptyxis_split_node_new_leaf      (GObject              *pane);
PtyxisSplitNode *ptyxis_split_node_ref           (PtyxisSplitNode      *self);
void             ptyxis_split_node_unref         (PtyxisSplitNode      *self);
gboolean         ptyxis_split_node_is_leaf       (PtyxisSplitNode      *self);
GObject         *ptyxis_split_node_get_pane      (PtyxisSplitNode      *self);
PtyxisSplitNode *ptyxis_split_node_get_parent    (PtyxisSplitNode      *self);
PtyxisSplitNode *ptyxis_split_node_get_first     (PtyxisSplitNode      *self);
PtyxisSplitNode *ptyxis_split_node_get_second    (PtyxisSplitNode      *self);
PtyxisSplitDirection ptyxis_split_node_get_direction (PtyxisSplitNode *self);
double           ptyxis_split_node_get_ratio     (PtyxisSplitNode      *self);
void             ptyxis_split_node_set_ratio     (PtyxisSplitNode      *self,
                                                   double                ratio);
PtyxisSplitNode *ptyxis_split_node_split         (PtyxisSplitNode      *leaf,
                                                   PtyxisSplitDirection direction,
                                                   double                ratio,
                                                   GObject              *new_pane);
gboolean         ptyxis_split_node_remove        (PtyxisSplitNode      *leaf);
guint            ptyxis_split_node_count_leaves  (PtyxisSplitNode      *self);
PtyxisSplitNode *ptyxis_split_node_get_nth_leaf  (PtyxisSplitNode      *self,
                                                   guint                 position);
PtyxisSplitNode *ptyxis_split_node_find_pane     (PtyxisSplitNode      *self,
                                                   GObject              *pane);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PtyxisSplitNode, ptyxis_split_node_unref)

G_END_DECLS
