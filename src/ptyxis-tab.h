/*
 * ptyxis-tab.h
 *
 * Copyright 2023-2024 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "ptyxis-agent-ipc.h"
#include "ptyxis-pane.h"
#include "ptyxis-profile.h"
#include "ptyxis-terminal.h"

G_BEGIN_DECLS

typedef enum _PtyxisTabProgress
{
  PTYXIS_TAB_PROGRESS_INDETERMINATE,
  PTYXIS_TAB_PROGRESS_ACTIVE,
  PTYXIS_TAB_PROGRESS_ERROR,
} PtyxisTabProgress;

#define PTYXIS_TYPE_TAB          (ptyxis_tab_get_type())
#define PTYXIS_TYPE_TAB_PROGRESS (ptyxis_tab_progress_get_type())

G_DECLARE_FINAL_TYPE (PtyxisTab, ptyxis_tab, PTYXIS, TAB, GtkWidget)

PtyxisTab          *ptyxis_tab_new                                (PtyxisProfile        *profile);
PtyxisPane         *ptyxis_tab_get_active_pane                    (PtyxisTab            *self);
PtyxisTerminal     *ptyxis_tab_get_terminal                       (PtyxisTab            *self);
PtyxisProfile      *ptyxis_tab_get_profile                        (PtyxisTab            *self);
void                ptyxis_tab_apply_profile                      (PtyxisTab            *self,
                                                                   PtyxisProfile        *new_profile);
PtyxisIpcProcess   *ptyxis_tab_get_process                        (PtyxisTab            *self);
const char         *ptyxis_tab_get_uuid                           (PtyxisTab            *self);
const char         *ptyxis_tab_get_command_line                   (PtyxisTab            *self);
void                ptyxis_tab_set_command                        (PtyxisTab            *self,
                                                                   const char * const   *command);
GIcon              *ptyxis_tab_dup_indicator_icon                 (PtyxisTab            *self);
char               *ptyxis_tab_dup_subtitle                       (PtyxisTab            *self);
char               *ptyxis_tab_dup_title                          (PtyxisTab            *self);
gboolean            ptyxis_tab_get_ignore_osc_title               (PtyxisTab            *self);
void                ptyxis_tab_set_ignore_osc_title               (PtyxisTab            *self,
                                                                   gboolean              ignore_osc_title);
const char         *ptyxis_tab_get_title_prefix                   (PtyxisTab            *self);
void                ptyxis_tab_set_title_prefix                   (PtyxisTab            *self,
                                                                   const char           *title_prefix);
char               *ptyxis_tab_dup_current_directory_uri          (PtyxisTab            *self);
char               *ptyxis_tab_dup_previous_working_directory_uri (PtyxisTab            *self);
void                ptyxis_tab_set_previous_working_directory_uri (PtyxisTab            *self,
                                                                   const char           *previous_working_directory_uri);
PtyxisTabProgress   ptyxis_tab_get_progress                       (PtyxisTab            *self);
double              ptyxis_tab_get_progress_fraction              (PtyxisTab            *self);
PtyxisZoomLevel     ptyxis_tab_get_zoom                           (PtyxisTab            *self);
void                ptyxis_tab_set_zoom                           (PtyxisTab            *self,
                                                                   PtyxisZoomLevel       zoom);
void                ptyxis_tab_zoom_in                            (PtyxisTab            *self);
void                ptyxis_tab_zoom_out                           (PtyxisTab            *self);
char               *ptyxis_tab_dup_zoom_label                     (PtyxisTab            *self);
void                ptyxis_tab_raise                              (PtyxisTab            *self);
gboolean            ptyxis_tab_is_running                         (PtyxisTab            *self,
                                                                   char                **cmdline);
void                ptyxis_tab_force_quit                         (PtyxisTab            *self);
void                ptyxis_tab_show_banner                        (PtyxisTab            *self);
void                ptyxis_tab_set_needs_attention                (PtyxisTab            *self,
                                                                   gboolean              needs_attention);
PtyxisIpcContainer *ptyxis_tab_dup_container                      (PtyxisTab            *self);
void                ptyxis_tab_set_container                      (PtyxisTab            *self,
                                                                   PtyxisIpcContainer   *container);
gboolean            ptyxis_tab_has_foreground_process             (PtyxisTab            *self,
                                                                   GPid                 *pid,
                                                                   char                **cmdline);
const char         *ptyxis_tab_get_initial_title                  (PtyxisTab            *self);
void                ptyxis_tab_set_initial_title                  (PtyxisTab            *self,
                                                                   const char           *initial_title);
void                ptyxis_tab_set_initial_working_directory_uri  (PtyxisTab            *self,
                                                                   const char           *initial_working_directory_uri);
void                ptyxis_tab_poll_agent_async                   (PtyxisTab            *self,
                                                                   GCancellable         *cancellable,
                                                                   GAsyncReadyCallback   callback,
                                                                   gpointer              user_data);
gboolean            ptyxis_tab_poll_agent_finish                  (PtyxisTab            *self,
                                                                   GAsyncResult         *result,
                                                                   GError              **error);
void                ptyxis_tab_open_uri                           (PtyxisTab            *self,
                                                                   const char           *uri);
char               *ptyxis_tab_query_working_directory_from_agent (PtyxisTab            *self);
void                ptyxis_tab_grab_focus                         (PtyxisTab            *self);

G_END_DECLS
