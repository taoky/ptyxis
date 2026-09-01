/* ptyxis-quake-service.c
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <libportal/portal.h>
#include <libportal-gtk4/portal-gtk4.h>

#include "ptyxis-quake-service.h"

#define QUAKE_DAEMON_PATH BINDIR "/ptyxis-quake-daemon"
#define QUAKE_AUTOSTART_TEMPLATE PKGDATADIR "/" APP_ID ".QuakeDaemon.desktop"
#define QUAKE_CONFIGURE_ACTION "configure-shortcut"

typedef struct
{
  GTask     *task;
  XdpPortal *portal;
} PortalRequest;

static void
portal_request_free (PortalRequest *request)
{
  g_clear_object (&request->task);
  g_clear_object (&request->portal);
  g_free (request);
}

void
ptyxis_quake_service_start (void)
{
  g_autoptr(GSubprocess) subprocess = NULL;
  g_autoptr(GError) error = NULL;

  subprocess = g_subprocess_new (G_SUBPROCESS_FLAGS_NONE,
                                 &error,
                                 QUAKE_DAEMON_PATH,
                                 NULL);
  if (subprocess == NULL)
    g_warning ("Failed to start the Quake shortcut service: %s", error->message);
}

gboolean
_ptyxis_quake_service_set_native_autostart (const char  *template_path,
                                             const char  *config_dir,
                                             gboolean     enabled,
                                             GError     **error)
{
  g_autofree char *autostart_dir = NULL;
  g_autofree char *autostart_path = NULL;
  g_autofree char *filename = NULL;
  g_autofree char *contents = NULL;
  gsize length = 0;

  g_return_val_if_fail (template_path != NULL, FALSE);
  g_return_val_if_fail (config_dir != NULL, FALSE);

  filename = g_strconcat (APP_ID, ".QuakeDaemon.desktop", NULL);
  autostart_dir = g_build_filename (config_dir, "autostart", NULL);
  autostart_path = g_build_filename (autostart_dir, filename, NULL);

  if (!enabled)
    {
      if (g_remove (autostart_path) == 0 || errno == ENOENT)
        return TRUE;

      g_set_error (error,
                   G_FILE_ERROR,
                   g_file_error_from_errno (errno),
                   "Failed to remove %s: %s",
                   autostart_path,
                   g_strerror (errno));
      return FALSE;
    }

  if (!g_file_get_contents (template_path, &contents, &length, error))
    return FALSE;

  if (g_mkdir_with_parents (autostart_dir, 0700) != 0)
    {
      int errsv = errno;

      g_set_error (error,
                   G_FILE_ERROR,
                   g_file_error_from_errno (errsv),
                   "Failed to create %s: %s",
                   autostart_dir,
                   g_strerror (errsv));
      return FALSE;
    }

  return g_file_set_contents_full (autostart_path,
                                   contents,
                                   length,
                                   G_FILE_SET_CONTENTS_CONSISTENT,
                                   0600,
                                   error);
}

static gboolean
set_native_autostart_cb (gpointer user_data)
{
  GTask *task = user_data;
  gboolean enabled = GPOINTER_TO_INT (g_task_get_task_data (task));
  g_autoptr(GError) error = NULL;

  if (_ptyxis_quake_service_set_native_autostart (QUAKE_AUTOSTART_TEMPLATE,
                                                   g_get_user_config_dir (),
                                                   enabled,
                                                   &error))
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_error (task, g_steal_pointer (&error));

  return G_SOURCE_REMOVE;
}

static void
portal_background_cb (GObject      *object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  PortalRequest *request = user_data;
  g_autoptr(GError) error = NULL;

  if (xdp_portal_request_background_finish (XDP_PORTAL (object), result, &error))
    g_task_return_boolean (request->task, TRUE);
  else if (error != NULL)
    g_task_return_error (request->task, g_steal_pointer (&error));
  else
    g_task_return_new_error (request->task,
                             G_IO_ERROR,
                             G_IO_ERROR_PERMISSION_DENIED,
                             "Background or autostart access was not granted");

  portal_request_free (request);
}

void
ptyxis_quake_service_set_autostart_async (GtkWindow           *parent,
                                          gboolean             enabled,
                                          GCancellable        *cancellable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (parent == NULL || GTK_IS_WINDOW (parent));

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, ptyxis_quake_service_set_autostart_async);

  if (!xdp_portal_running_under_flatpak ())
    {
      g_task_set_task_data (task, GINT_TO_POINTER (enabled), NULL);
      g_idle_add_full (G_PRIORITY_DEFAULT,
                       set_native_autostart_cb,
                       g_steal_pointer (&task),
                       g_object_unref);
      return;
    }

  {
    PortalRequest *request = g_new0 (PortalRequest, 1);
    g_autoptr(XdpParent) portal_parent = NULL;
    g_autofree char *reason = NULL;
    GPtrArray *commandline = g_ptr_array_new ();
    XdpBackgroundFlags flags = XDP_BACKGROUND_FLAG_NONE;

    request->task = g_steal_pointer (&task);
    request->portal = xdp_portal_new ();
    if (parent != NULL)
      portal_parent = xdp_parent_new_gtk (parent);

    g_ptr_array_add (commandline, (gpointer)"ptyxis-quake-daemon");
    if (enabled)
      flags |= XDP_BACKGROUND_FLAG_AUTOSTART;
    reason = g_strdup ("Keep the Ptyxis Quake shortcut available");

    xdp_portal_request_background (request->portal,
                                   portal_parent,
                                   reason,
                                   commandline,
                                   flags,
                                   cancellable,
                                   portal_background_cb,
                                   request);
  }
}

gboolean
ptyxis_quake_service_set_autostart_finish (GAsyncResult  *result,
                                            GError       **error)
{
  g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) ==
                        ptyxis_quake_service_set_autostart_async, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
configure_call_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GError) error = NULL;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (object), result, &error);
  if (reply != NULL)
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_error (task, g_steal_pointer (&error));
}

static void
configure_version_cb (GObject      *object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  GDBusConnection *connection = G_DBUS_CONNECTION (object);
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GVariant) value = NULL;
  g_autoptr(GVariant) inner = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *daemon_id = NULL;
  g_autofree char *object_path = NULL;
  guint version;

  reply = g_dbus_connection_call_finish (connection, result, &error);
  if (reply == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  g_variant_get (reply, "(@v)", &value);
  inner = g_variant_get_variant (value);
  version = g_variant_get_uint32 (inner);
  if (version < 2)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_SUPPORTED,
                               "Changing global shortcuts requires Global Shortcuts portal version 2");
      return;
    }

  daemon_id = g_strconcat (APP_ID, ".QuakeDaemon", NULL);
  object_path = g_strdup_printf ("/%s/QuakeDaemon", APP_ID);
  g_strdelimit (object_path, ".", '/');

  g_dbus_connection_call (connection,
                          daemon_id,
                          object_path,
                          "org.freedesktop.Application",
                          "ActivateAction",
                          g_variant_new ("(s@av@a{sv})",
                                         QUAKE_CONFIGURE_ACTION,
                                         g_variant_new_array (G_VARIANT_TYPE_VARIANT, NULL, 0),
                                         g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0)),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          g_task_get_cancellable (task),
                          configure_call_cb,
                          g_steal_pointer (&task));
}

static void
configure_bus_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GError) error = NULL;

  connection = g_bus_get_finish (result, &error);
  if (connection == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  g_dbus_connection_call (connection,
                          "org.freedesktop.portal.Desktop",
                          "/org/freedesktop/portal/desktop",
                          "org.freedesktop.DBus.Properties",
                          "Get",
                          g_variant_new ("(ss)",
                                         "org.freedesktop.portal.GlobalShortcuts",
                                         "version"),
                          G_VARIANT_TYPE ("(v)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          g_task_get_cancellable (task),
                          configure_version_cb,
                          g_steal_pointer (&task));
}

void
ptyxis_quake_service_configure_async (GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
  GTask *task = g_task_new (NULL, cancellable, callback, user_data);

  g_task_set_source_tag (task, ptyxis_quake_service_configure_async);
  g_bus_get (G_BUS_TYPE_SESSION,
             cancellable,
             configure_bus_cb,
             task);
}

gboolean
ptyxis_quake_service_configure_finish (GAsyncResult  *result,
                                       GError       **error)
{
  g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) ==
                        ptyxis_quake_service_configure_async, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
