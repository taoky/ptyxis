/* ptyxis-pane.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <gtk/gtk.h>
#include "ptyxis-terminal.h"

G_BEGIN_DECLS

#define PTYXIS_TYPE_PANE (ptyxis_pane_get_type())
G_DECLARE_FINAL_TYPE (PtyxisPane, ptyxis_pane, PTYXIS, PANE, GtkWidget)

PtyxisPane     *ptyxis_pane_new          (void);
PtyxisTerminal *ptyxis_pane_get_terminal (PtyxisPane     *self);
void            ptyxis_pane_set_terminal (PtyxisPane     *self,
                                           PtyxisTerminal *terminal);

G_END_DECLS
