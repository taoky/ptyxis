/* ptyxis-application.h
 *
 * Copyright 2023 Christian Hergert
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

#include <adwaita.h>

#include "ptyxis-agent-ipc.h"
#include "ptyxis-profile.h"
#include "ptyxis-settings.h"
#include "ptyxis-shortcuts.h"

G_BEGIN_DECLS

#define PTYXIS_TYPE_APPLICATION    (ptyxis_application_get_type())
#define PTYXIS_APPLICATION_DEFAULT (PTYXIS_APPLICATION(g_application_get_default()))

G_DECLARE_FINAL_TYPE (PtyxisApplication, ptyxis_application, PTYXIS, APPLICATION, AdwApplication)

gint64              ptyxis_application_get_default_rlimit_nofile  (void);
PtyxisApplication  *ptyxis_application_new                        (const char           *application_id,
                                                                   GApplicationFlags     flags);
const char         *ptyxis_application_get_user_data_dir          (PtyxisApplication    *self);
const char         *ptyxis_application_get_os_name                (PtyxisApplication    *self);
PtyxisSettings     *ptyxis_application_get_settings               (PtyxisApplication    *self);
PtyxisShortcuts    *ptyxis_application_get_shortcuts              (PtyxisApplication    *self);
gboolean            ptyxis_application_focus_pane_by_uuid          (PtyxisApplication    *self,
                                                                    const char           *tab_uuid,
                                                                    const char           *pane_uuid);
const char         *ptyxis_application_get_system_font_name       (PtyxisApplication    *self);
gboolean            ptyxis_application_get_overlay_scrollbars     (PtyxisApplication    *self);
gboolean            ptyxis_application_control_is_pressed         (PtyxisApplication    *self);
void                ptyxis_application_add_profile                (PtyxisApplication    *self,
                                                                   PtyxisProfile        *profile);
void                ptyxis_application_remove_profile             (PtyxisApplication    *self,
                                                                   PtyxisProfile        *profile);
PtyxisProfile      *ptyxis_application_dup_default_profile        (PtyxisApplication    *self);
void                ptyxis_application_set_default_profile        (PtyxisApplication    *self,
                                                                   PtyxisProfile        *profile);
PtyxisProfile      *ptyxis_application_dup_profile                (PtyxisApplication    *self,
                                                                   const char           *profile_uuid);
GListModel         *ptyxis_application_list_profiles              (PtyxisApplication    *self);
GListModel         *ptyxis_application_list_containers            (PtyxisApplication    *self);
PtyxisIpcContainer *ptyxis_application_lookup_container           (PtyxisApplication    *self,
                                                                   const char           *container_id);
void                ptyxis_application_report_error               (PtyxisApplication    *self,
                                                                   GType                 subsystem,
                                                                   const GError         *error);
VtePty             *ptyxis_application_create_pty                 (PtyxisApplication    *self,
                                                                   GError              **error);
void                ptyxis_application_spawn_async                (PtyxisApplication    *self,
                                                                   PtyxisIpcContainer   *container,
                                                                   PtyxisProfile        *profile,
                                                                   const char           *last_working_directory_uri,
                                                                   VtePty               *pty,
                                                                   const char * const   *argv,
                                                                   GCancellable         *cancellable,
                                                                   GAsyncReadyCallback   callback,
                                                                   gpointer              user_data);
PtyxisIpcProcess   *ptyxis_application_spawn_finish               (PtyxisApplication    *self,
                                                                   GAsyncResult         *result,
                                                                   GError              **error);
void                ptyxis_application_wait_async                 (PtyxisApplication    *self,
                                                                   PtyxisIpcProcess     *process,
                                                                   GCancellable         *cancellable,
                                                                   GAsyncReadyCallback   callback,
                                                                   gpointer              user_data);
int                 ptyxis_application_wait_finish                (PtyxisApplication    *self,
                                                                   GAsyncResult         *result,
                                                                   GError              **error);
PtyxisIpcContainer *ptyxis_application_discover_current_container (PtyxisApplication    *self,
                                                                   VtePty               *pty);
PtyxisIpcContainer *ptyxis_application_find_container_by_name     (PtyxisApplication    *self,
                                                                   const char           *runtime,
                                                                   const char           *name);
void                ptyxis_application_save_session               (PtyxisApplication    *self);

G_END_DECLS
