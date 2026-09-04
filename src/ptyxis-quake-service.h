/* ptyxis-quake-service.h
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PTYXIS_QUAKE_AUTOSTART_KEY "quake-autostart"
#define PTYXIS_QUAKE_PROMPTED_KEY  "quake-autostart-prompted"
#define PTYXIS_QUAKE_SHORTCUT_DESCRIPTION_KEY "quake-shortcut-description"

gboolean ptyxis_quake_service_is_available         (void);
void     ptyxis_quake_service_check_supported_async(GCancellable         *cancellable,
                                                     GAsyncReadyCallback   callback,
                                                     gpointer              user_data);
gboolean ptyxis_quake_service_check_supported_finish(GAsyncResult        *result,
                                                      GError             **error);
void     ptyxis_quake_service_start                (void);
void     ptyxis_quake_service_set_autostart_async  (GtkWindow            *parent,
                                                     gboolean              enabled,
                                                     GCancellable         *cancellable,
                                                     GAsyncReadyCallback   callback,
                                                     gpointer              user_data);
gboolean ptyxis_quake_service_set_autostart_finish (GAsyncResult         *result,
                                                     GError              **error);
void     ptyxis_quake_service_configure_async       (GCancellable         *cancellable,
                                                     GAsyncReadyCallback   callback,
                                                     gpointer              user_data);
gboolean ptyxis_quake_service_configure_finish      (GAsyncResult         *result,
                                                     GError              **error);
void     ptyxis_quake_service_stop_async            (GCancellable         *cancellable,
                                                     GAsyncReadyCallback   callback,
                                                     gpointer              user_data);
gboolean ptyxis_quake_service_stop_finish           (GAsyncResult         *result,
                                                     GError              **error);

/* Exposed for focused tests; callers should normally use the async API. */
gboolean _ptyxis_quake_service_set_native_autostart (const char           *template_path,
                                                      const char           *config_dir,
                                                      gboolean              enabled,
                                                      GError              **error);

G_END_DECLS
