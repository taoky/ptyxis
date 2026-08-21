/*
 * ptyxis-close-dialog.h
 *
 * Copyright 2021-2023 Christian Hergert <chergert@redhat.com>
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

G_BEGIN_DECLS

typedef struct _PtyxisPane PtyxisPane;
typedef struct _PtyxisTab PtyxisTab;

void     _ptyxis_close_dialog_run_async  (GtkWindow            *parent,
                                          GPtrArray            *tabs,
                                          GCancellable         *cancellable,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data);
void     _ptyxis_close_dialog_run_for_pane_async
                                         (GtkWindow            *parent,
                                          PtyxisTab            *tab,
                                          PtyxisPane           *pane,
                                          GCancellable         *cancellable,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data);
gboolean _ptyxis_close_dialog_run_finish (GAsyncResult         *result,
                                          GError              **error);

G_END_DECLS
