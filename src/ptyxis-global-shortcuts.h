/* ptyxis-global-shortcuts.h
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define PTYXIS_TYPE_GLOBAL_SHORTCUTS (ptyxis_global_shortcuts_get_type())

G_DECLARE_FINAL_TYPE (PtyxisGlobalShortcuts, ptyxis_global_shortcuts, PTYXIS, GLOBAL_SHORTCUTS, GObject)

PtyxisGlobalShortcuts *ptyxis_global_shortcuts_new          (const char            *application_id);
void                   ptyxis_global_shortcuts_register     (PtyxisGlobalShortcuts *self);
void                   ptyxis_global_shortcuts_start        (PtyxisGlobalShortcuts *self);
void                   ptyxis_global_shortcuts_ensure_bound (PtyxisGlobalShortcuts *self);
void                   ptyxis_global_shortcuts_configure    (PtyxisGlobalShortcuts *self,
                                                             const char            *parent_window);

G_END_DECLS
