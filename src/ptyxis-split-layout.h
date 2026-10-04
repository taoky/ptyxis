/* ptyxis-split-layout.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "ptyxis-split-node.h"

G_BEGIN_DECLS

void ptyxis_split_layout_bind   (GtkPaned        *paned,
                                 PtyxisSplitNode *node);
void ptyxis_split_layout_rebind (GtkPaned        *paned,
                                 PtyxisSplitNode *node);

G_END_DECLS
