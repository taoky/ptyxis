/* test-split-node.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "config.h"
#include "src/ptyxis-split-node.h"

static void
test_split_and_traverse (void)
{
  g_autoptr(GObject) a = g_object_new (G_TYPE_OBJECT, NULL);
  g_autoptr(GObject) b = g_object_new (G_TYPE_OBJECT, NULL);
  g_autoptr(GObject) c = g_object_new (G_TYPE_OBJECT, NULL);
  g_autoptr(GObject) missing = g_object_new (G_TYPE_OBJECT, NULL);
  g_autoptr(PtyxisSplitNode) root = ptyxis_split_node_new_leaf (a);
  PtyxisSplitNode *first;

  ptyxis_split_node_split (root, PTYXIS_SPLIT_HORIZONTAL, .6, b);
  first = ptyxis_split_node_get_first (root);
  ptyxis_split_node_split (first, PTYXIS_SPLIT_VERTICAL, .4, c);

  g_assert_cmpuint (ptyxis_split_node_count_leaves (root), ==, 3);
  g_assert_true (ptyxis_split_node_get_pane (ptyxis_split_node_get_nth_leaf (root, 0)) == a);
  g_assert_true (ptyxis_split_node_get_pane (ptyxis_split_node_get_nth_leaf (root, 1)) == c);
  g_assert_true (ptyxis_split_node_get_pane (ptyxis_split_node_get_nth_leaf (root, 2)) == b);
  g_assert_null (ptyxis_split_node_get_nth_leaf (root, 3));
  g_assert_true (ptyxis_split_node_find_pane (root, c) ==
                 ptyxis_split_node_get_nth_leaf (root, 1));
  g_assert_null (ptyxis_split_node_find_pane (root, missing));
  g_assert_true (ptyxis_split_node_get_parent (first) == root);
  g_assert_true (ptyxis_split_node_get_next_leaf (
                   root, ptyxis_split_node_get_nth_leaf (root, 0), FALSE) ==
                 ptyxis_split_node_get_nth_leaf (root, 1));
  g_assert_null (ptyxis_split_node_get_next_leaf (
                   root, ptyxis_split_node_get_nth_leaf (root, 2), FALSE));
  g_assert_true (ptyxis_split_node_get_next_leaf (
                   root, ptyxis_split_node_get_nth_leaf (root, 2), TRUE) ==
                 ptyxis_split_node_get_nth_leaf (root, 0));
  g_assert_true (ptyxis_split_node_get_previous_leaf (
                   root, ptyxis_split_node_get_nth_leaf (root, 0), TRUE) ==
                 ptyxis_split_node_get_nth_leaf (root, 2));
}

static void
test_remove_and_collapse (void)
{
  g_autoptr(GObject) a = g_object_new (G_TYPE_OBJECT, NULL);
  g_autoptr(GObject) b = g_object_new (G_TYPE_OBJECT, NULL);
  g_autoptr(GObject) c = g_object_new (G_TYPE_OBJECT, NULL);
  g_autoptr(PtyxisSplitNode) root = ptyxis_split_node_new_leaf (a);
  PtyxisSplitNode *first;
  PtyxisSplitNode *c_leaf;
  PtyxisSplitNode *b_leaf;

  b_leaf = ptyxis_split_node_split (root, PTYXIS_SPLIT_HORIZONTAL, .5, b);
  first = ptyxis_split_node_get_first (root);
  c_leaf = ptyxis_split_node_split (first, PTYXIS_SPLIT_VERTICAL, .5, c);

  g_assert_true (ptyxis_split_node_remove (c_leaf));
  g_assert_cmpuint (ptyxis_split_node_count_leaves (root), ==, 2);
  g_assert_true (ptyxis_split_node_get_pane (ptyxis_split_node_get_first (root)) == a);

  g_assert_true (ptyxis_split_node_remove (b_leaf));
  g_assert_true (ptyxis_split_node_is_leaf (root));
  g_assert_true (ptyxis_split_node_get_pane (root) == a);
  g_assert_false (ptyxis_split_node_remove (root));
}

static void
test_ratio_bounds (void)
{
  g_autoptr(GObject) a = g_object_new (G_TYPE_OBJECT, NULL);
  g_autoptr(GObject) b = g_object_new (G_TYPE_OBJECT, NULL);
  g_autoptr(PtyxisSplitNode) root = ptyxis_split_node_new_leaf (a);

  ptyxis_split_node_split (root, PTYXIS_SPLIT_VERTICAL, 2., b);
  g_assert_cmpfloat (ptyxis_split_node_get_ratio (root), ==, .95);
  ptyxis_split_node_set_ratio (root, -1.);
  g_assert_cmpfloat (ptyxis_split_node_get_ratio (root), ==, .05);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/split-node/split-and-traverse", test_split_and_traverse);
  g_test_add_func ("/split-node/remove-and-collapse", test_remove_and_collapse);
  g_test_add_func ("/split-node/ratio-bounds", test_ratio_bounds);
  return g_test_run ();
}
