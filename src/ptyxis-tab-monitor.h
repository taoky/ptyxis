/*
 * ptyxis-tab-monitor.h
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include <gio/gio.h>

#include "ptyxis-tab.h"

G_BEGIN_DECLS

#define PTYXIS_TYPE_TAB_MONITOR (ptyxis_tab_monitor_get_type())

G_DECLARE_FINAL_TYPE (PtyxisTabMonitor, ptyxis_tab_monitor, PTYXIS, TAB_MONITOR, GObject)

PtyxisTabMonitor *ptyxis_tab_monitor_new                 (PtyxisTab        *tab,
                                                          PtyxisPane       *pane);
gboolean          ptyxis_tab_monitor_get_has_pressed_key (PtyxisTabMonitor *self);
void              ptyxis_tab_monitor_set_has_pressed_key (PtyxisTabMonitor *self,
                                                          gboolean          has_pressed_key);

G_END_DECLS
