/* ptyxis-pane.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <gtk/gtk.h>
#include "ptyxis-agent-ipc.h"
#include "ptyxis-profile.h"
#include "ptyxis-terminal.h"

G_BEGIN_DECLS

typedef struct _PtyxisTabMonitor PtyxisTabMonitor;

typedef enum _PtyxisProcessLeader
{
  PTYXIS_PROCESS_LEADER_KIND_UNKNOWN,
  PTYXIS_PROCESS_LEADER_KIND_SUPERUSER,
  PTYXIS_PROCESS_LEADER_KIND_REMOTE,
  PTYXIS_PROCESS_LEADER_KIND_CONTAINER,
} PtyxisProcessLeaderKind;

typedef enum _PtyxisPaneState
{
  PTYXIS_PANE_STATE_INITIAL,
  PTYXIS_PANE_STATE_SPAWNING,
  PTYXIS_PANE_STATE_RUNNING,
  PTYXIS_PANE_STATE_EXITED,
  PTYXIS_PANE_STATE_FAILED,
} PtyxisPaneState;

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
PtyxisTabMonitor *ptyxis_pane_get_monitor (PtyxisPane      *self);
void              ptyxis_pane_set_monitor (PtyxisPane      *self,
                                            PtyxisTabMonitor *monitor);
const char       *ptyxis_pane_get_uuid     (PtyxisPane      *self);
const char *const *ptyxis_pane_get_command (PtyxisPane      *self);
void              ptyxis_pane_set_command (PtyxisPane      *self,
                                            const char *const *command);
const char *ptyxis_pane_get_initial_working_directory_uri (PtyxisPane *self);
void        ptyxis_pane_set_initial_working_directory_uri (PtyxisPane *self,
                                                            const char *uri);
const char *ptyxis_pane_get_previous_working_directory_uri (PtyxisPane *self);
void        ptyxis_pane_set_previous_working_directory_uri (PtyxisPane *self,
                                                             const char *uri);
PtyxisIpcContainer *ptyxis_pane_dup_container (PtyxisPane         *self);
void                ptyxis_pane_set_container (PtyxisPane         *self,
                                               PtyxisIpcContainer *container);
const char *ptyxis_pane_get_initial_title (PtyxisPane *self);
void        ptyxis_pane_set_initial_title (PtyxisPane *self,
                                           const char *title);
const char *ptyxis_pane_get_title_prefix  (PtyxisPane *self);
void        ptyxis_pane_set_title_prefix  (PtyxisPane *self,
                                           const char *prefix);
GPid        ptyxis_pane_get_foreground_pid (PtyxisPane *self);
void        ptyxis_pane_set_foreground_pid (PtyxisPane *self,
                                            GPid         pid);
gboolean    ptyxis_pane_get_has_foreground_process (PtyxisPane *self);
void        ptyxis_pane_set_has_foreground_process (PtyxisPane *self,
                                                     gboolean    has_foreground_process);
const char *ptyxis_pane_get_command_line (PtyxisPane *self);
void        ptyxis_pane_set_command_line (PtyxisPane *self,
                                          const char *command_line);
const char *ptyxis_pane_get_program_name (PtyxisPane *self);
void        ptyxis_pane_set_program_name (PtyxisPane *self,
                                          const char *program_name);
PtyxisProcessLeaderKind ptyxis_pane_get_process_leader_kind (PtyxisPane *self);
void                    ptyxis_pane_set_process_leader_kind (PtyxisPane *self,
                                                             PtyxisProcessLeaderKind kind);
PtyxisPaneState ptyxis_pane_get_state        (PtyxisPane *self);
void            ptyxis_pane_set_state        (PtyxisPane *self,
                                               PtyxisPaneState state);
gint64          ptyxis_pane_get_respawn_time (PtyxisPane *self);
void            ptyxis_pane_set_respawn_time (PtyxisPane *self,
                                               gint64      respawn_time);
gboolean        ptyxis_pane_get_forced_exit  (PtyxisPane *self);
void            ptyxis_pane_set_forced_exit  (PtyxisPane *self,
                                               gboolean    forced_exit);
gboolean        ptyxis_pane_get_read_only    (PtyxisPane *self);
void            ptyxis_pane_set_read_only    (PtyxisPane *self,
                                               gboolean    read_only);
gboolean        ptyxis_pane_get_ignore_osc_title (PtyxisPane *self);
void            ptyxis_pane_set_ignore_osc_title (PtyxisPane *self,
                                                   gboolean    ignore_osc_title);
void            ptyxis_pane_set_terminal (PtyxisPane     *self,
                                           PtyxisTerminal *terminal);

G_END_DECLS
