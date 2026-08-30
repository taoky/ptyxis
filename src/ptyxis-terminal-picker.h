/* ptyxis-terminal-picker.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <adwaita.h>

#include "ptyxis-window.h"

G_BEGIN_DECLS

#define PTYXIS_TYPE_TERMINAL_PICKER (ptyxis_terminal_picker_get_type())
G_DECLARE_FINAL_TYPE (PtyxisTerminalPicker, ptyxis_terminal_picker, PTYXIS, TERMINAL_PICKER, AdwDialog)

PtyxisTerminalPicker *ptyxis_terminal_picker_new (PtyxisWindow *window);

G_END_DECLS
