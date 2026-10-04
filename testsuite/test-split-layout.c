/* test-split-layout.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <math.h>

#include <gtk/gtk.h>

#include "src/ptyxis-split-layout.h"

static gboolean have_display;

static gboolean
wait_for_applied_ratio (GtkPaned *paned,
                        double    ratio)
{
  for (int i = 0; i < 500; i++)
    {
      int extent = gtk_widget_get_width (GTK_WIDGET (paned));

      if (extent > 1 && gtk_paned_get_position (paned) == (int) round (extent * ratio))
        return TRUE;

      g_main_context_iteration (NULL, FALSE);
      g_usleep (1000);
    }

  return FALSE;
}

static GtkWidget *
make_bound_window (GtkPaned        **out_paned,
                   PtyxisSplitNode  *node)
{
  GtkWidget *window = gtk_window_new ();
  GtkWidget *paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);

  gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);
  gtk_paned_set_start_child (GTK_PANED (paned), gtk_label_new ("Left"));
  gtk_paned_set_end_child (GTK_PANED (paned), gtk_label_new ("Right"));
  gtk_window_set_child (GTK_WINDOW (window), paned);
  ptyxis_split_layout_bind (GTK_PANED (paned), node);
  gtk_window_present (GTK_WINDOW (window));

  *out_paned = GTK_PANED (paned);
  return window;
}

/* The paned must be bound before it is laid out (as ptyxis_tab_split_pane()
 * does when restoring a saved split): GtkPaned computes an initial position
 * from the children's natural sizes during the first layout, which must not
 * clobber the ratio the paned was bound to restore.
 */
static void
test_bind_before_first_layout_restores_ratio (void)
{
  g_autoptr(GObject) a = g_object_new (G_TYPE_OBJECT, NULL);
  g_autoptr(GObject) b = g_object_new (G_TYPE_OBJECT, NULL);
  g_autoptr(PtyxisSplitNode) root = ptyxis_split_node_new_leaf (a);
  GtkWidget *window;
  GtkPaned *paned;

  if (!have_display)
    {
      g_test_skip ("Requires a display");
      return;
    }

  ptyxis_split_node_split (root, PTYXIS_SPLIT_HORIZONTAL, .25, b);
  window = make_bound_window (&paned, root);

  /* The initial ratio is applied once the paned has been allocated. */
  g_assert_true (wait_for_applied_ratio (paned, .25));
  g_assert_cmpfloat_with_epsilon (ptyxis_split_node_get_ratio (root), .25, 1e-6);

  gtk_window_destroy (GTK_WINDOW (window));
}

/* Regression test for a heap-use-after-free: closing a pane of a nested
 * split collapses the sibling node into the parent node and frees it,
 * while the GtkPaned managing that sub-split survives. Its position
 * callback must be re-bound to the parent node, otherwise later position
 * changes (e.g. when moving the window to another monitor resizes the
 * paned) wrote the ratio through the freed node.
 */
static void
test_collapse_rebinds_surviving_paned (void)
{
  g_autoptr(GObject) a = g_object_new (G_TYPE_OBJECT, NULL);
  g_autoptr(GObject) b = g_object_new (G_TYPE_OBJECT, NULL);
  g_autoptr(GObject) c = g_object_new (G_TYPE_OBJECT, NULL);
  g_autoptr(PtyxisSplitNode) root = ptyxis_split_node_new_leaf (a);
  GtkWidget *window;
  GtkPaned *paned;
  PtyxisSplitNode *inner;
  PtyxisSplitNode *b_leaf;
  int extent;

  if (!have_display)
    {
      g_test_skip ("Requires a display");
      return;
    }

  /* Node tree: root splits [inner[a|c]] and [b]. */
  b_leaf = ptyxis_split_node_split (root, PTYXIS_SPLIT_HORIZONTAL, .5, b);
  inner = ptyxis_split_node_get_first (root);
  ptyxis_split_node_split (inner, PTYXIS_SPLIT_VERTICAL, .5, c);

  /* The GtkPaned managing the inner split, bound as ptyxis_tab_split_pane()
   * does, laid out inside a window so its callbacks are active.
   */
  window = make_bound_window (&paned, inner);
  g_assert_true (wait_for_applied_ratio (paned, .5));

  /* Position changes update the node the paned is bound to. */
  gtk_paned_set_position (paned, 200);
  extent = gtk_widget_get_width (GTK_WIDGET (paned));
  g_assert_cmpfloat_with_epsilon (ptyxis_split_node_get_ratio (inner), 200.0 / extent, 1e-6);

  /* Closing pane B collapses inner into root, as ptyxis_tab_remove_pane() does. */
  ptyxis_split_layout_rebind (paned, root);
  g_assert_true (ptyxis_split_node_remove (b_leaf));

  /* The freed node must no longer receive updates; the promoted node must. */
  gtk_paned_set_position (paned, 293);
  extent = gtk_widget_get_width (GTK_WIDGET (paned));
  g_assert_cmpfloat_with_epsilon (ptyxis_split_node_get_ratio (root), 293.0 / extent, 1e-6);

  gtk_window_destroy (GTK_WINDOW (window));
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  /* gtk_init_check() must only be called once: it flips GTK's internal
   * "initialized" flag even when opening the display fails, so a second
   * call would wrongly report success and crash on widget creation.
   */
  have_display = gtk_init_check ();

  g_test_add_func ("/split-layout/bind-before-first-layout-restores-ratio",
                   test_bind_before_first_layout_restores_ratio);
  g_test_add_func ("/split-layout/collapse-rebinds-surviving-paned",
                   test_collapse_rebinds_surviving_paned);
  return g_test_run ();
}
