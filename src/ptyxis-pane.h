/* ptyxis-pane.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <gtk/gtk.h>
#include "ptyxis-agent-ipc.h"
#include "ptyxis-profile.h"
#include "ptyxis-terminal.h"

G_BEGIN_DECLS

typedef enum _PtyxisZoomLevel
{
  PTYXIS_ZOOM_LEVEL_MINUS_14 = 1,
  PTYXIS_ZOOM_LEVEL_MINUS_13,
  PTYXIS_ZOOM_LEVEL_MINUS_12,
  PTYXIS_ZOOM_LEVEL_MINUS_11,
  PTYXIS_ZOOM_LEVEL_MINUS_10,
  PTYXIS_ZOOM_LEVEL_MINUS_9,
  PTYXIS_ZOOM_LEVEL_MINUS_8,
  PTYXIS_ZOOM_LEVEL_MINUS_7,
  PTYXIS_ZOOM_LEVEL_MINUS_6,
  PTYXIS_ZOOM_LEVEL_MINUS_5,
  PTYXIS_ZOOM_LEVEL_MINUS_4,
  PTYXIS_ZOOM_LEVEL_MINUS_3,
  PTYXIS_ZOOM_LEVEL_MINUS_2,
  PTYXIS_ZOOM_LEVEL_MINUS_1,
  PTYXIS_ZOOM_LEVEL_DEFAULT,
  PTYXIS_ZOOM_LEVEL_PLUS_1,
  PTYXIS_ZOOM_LEVEL_PLUS_2,
  PTYXIS_ZOOM_LEVEL_PLUS_3,
  PTYXIS_ZOOM_LEVEL_PLUS_4,
  PTYXIS_ZOOM_LEVEL_PLUS_5,
  PTYXIS_ZOOM_LEVEL_PLUS_6,
  PTYXIS_ZOOM_LEVEL_PLUS_7,
  PTYXIS_ZOOM_LEVEL_PLUS_8,
  PTYXIS_ZOOM_LEVEL_PLUS_9,
  PTYXIS_ZOOM_LEVEL_PLUS_10,
  PTYXIS_ZOOM_LEVEL_PLUS_11,
  PTYXIS_ZOOM_LEVEL_PLUS_12,
  PTYXIS_ZOOM_LEVEL_PLUS_13,
  PTYXIS_ZOOM_LEVEL_PLUS_14,
} PtyxisZoomLevel;

#define PTYXIS_ZOOM_LEVEL_LAST (PTYXIS_ZOOM_LEVEL_PLUS_14 + 1)

#define PTYXIS_TYPE_PANE (ptyxis_pane_get_type())
G_DECLARE_FINAL_TYPE (PtyxisPane, ptyxis_pane, PTYXIS, PANE, GtkWidget)

PtyxisPane     *ptyxis_pane_new          (void);
PtyxisTerminal *ptyxis_pane_get_terminal (PtyxisPane     *self);
PtyxisProfile  *ptyxis_pane_get_profile  (PtyxisPane     *self);
void            ptyxis_pane_set_profile  (PtyxisPane     *self,
                                           PtyxisProfile  *profile);
PtyxisZoomLevel ptyxis_pane_get_zoom     (PtyxisPane     *self);
void            ptyxis_pane_set_zoom     (PtyxisPane     *self,
                                           PtyxisZoomLevel zoom);
PtyxisIpcProcess *ptyxis_pane_get_process (PtyxisPane     *self);
void              ptyxis_pane_set_process (PtyxisPane     *self,
                                            PtyxisIpcProcess *process);
void            ptyxis_pane_set_terminal (PtyxisPane     *self,
                                           PtyxisTerminal *terminal);

G_END_DECLS
