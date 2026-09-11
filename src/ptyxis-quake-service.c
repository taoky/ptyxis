/* ptyxis-quake-service.c
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <libportal/portal.h>
#include <libportal-gtk4/portal-gtk4.h>

#ifdef GDK_WINDOWING_X11
# include <gdk/x11/gdkx.h>
#endif

#include "ptyxis-quake-service.h"

#define QUAKE_DAEMON_PATH BINDIR "/ptyxis-quake-daemon"
#define QUAKE_AUTOSTART_TEMPLATE PKGDATADIR "/" APP_ID ".QuakeDaemon.desktop"
#define QUAKE_CONFIGURE_ACTION "configure-shortcut"
#define QUAKE_QUIT_ACTION "quit"
#define PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define PORTAL_GLOBAL_SHORTCUTS_INTERFACE "org.freedesktop.portal.GlobalShortcuts"

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

gboolean
ptyxis_quake_service_is_available (void)
{
#ifdef GDK_WINDOWING_X11
  GdkDisplay *display = gdk_display_get_default ();

  if (display != NULL && GDK_IS_X11_DISPLAY (display))
    return FALSE;
#endif

  return TRUE;
}

static void
check_supported_call_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GVariant) value = NULL;
  g_autoptr(GVariant) inner = NULL;
  g_autoptr(GError) error = NULL;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (object), result, &error);
  if (reply == NULL)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_task_return_error (task, g_steal_pointer (&error));
      else
        g_task_return_boolean (task, FALSE);
      return;
    }

  g_variant_get (reply, "(@v)", &value);
  inner = g_variant_get_variant (value);
  g_task_return_boolean (task,
                         g_variant_is_of_type (inner, G_VARIANT_TYPE_UINT32) &&
                         g_variant_get_uint32 (inner) >= 1);
}

static void
check_supported_bus_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GError) error = NULL;
  GCancellable *cancellable;

  connection = g_bus_get_finish (result, &error);
  if (connection == NULL)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_task_return_error (task, g_steal_pointer (&error));
      else
        g_task_return_boolean (task, FALSE);
      return;
    }

  cancellable = g_task_get_cancellable (task);
  g_dbus_connection_call (connection,
                          PORTAL_BUS_NAME,
                          PORTAL_OBJECT_PATH,
                          "org.freedesktop.DBus.Properties",
                          "Get",
                          g_variant_new ("(ss)",
                                         PORTAL_GLOBAL_SHORTCUTS_INTERFACE,
                                         "version"),
                          G_VARIANT_TYPE ("(v)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          cancellable,
                          check_supported_call_cb,
                          g_steal_pointer (&task));
}

void
ptyxis_quake_service_check_supported_async (GCancellable        *cancellable,
                                             GAsyncReadyCallback  callback,
                                             gpointer             user_data)
{
  GTask *task = g_task_new (NULL, cancellable, callback, user_data);

  g_task_set_source_tag (task, ptyxis_quake_service_check_supported_async);
  if (!ptyxis_quake_service_is_available ())
    {
      g_task_return_boolean (task, FALSE);
      g_object_unref (task);
      return;
    }

  g_bus_get (G_BUS_TYPE_SESSION,
             cancellable,
             check_supported_bus_cb,
             task);
}

gboolean
ptyxis_quake_service_check_supported_finish (GAsyncResult  *result,
                                              GError       **error)
{
  g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) ==
                        ptyxis_quake_service_check_supported_async, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

/* Serialize operations, including starts requested by Preferences, so a stop
 * cannot finish while a process we just launched is still acquiring its name.
 * Blocking D-Bus calls and bounded owner waits run off the GTK thread.
 */
typedef struct
{
  char *name;
  char *executable;
  guint timeout_msec;
  gboolean running;
} ServiceOperation;

static GQueue service_operations = G_QUEUE_INIT;
static guint service_generation;

guint
ptyxis_quake_service_get_generation (void)
{
  return service_generation;
}

static void
service_operation_free (ServiceOperation *operation)
{
  g_free (operation->name);
  g_free (operation->executable);
  g_free (operation);
}

static gboolean
service_has_owner (GDBusConnection *connection,
                   const char      *name,
                   GCancellable    *cancellable,
                   gboolean        *has_owner,
                   GError         **error)
{
  g_autoptr(GVariant) reply = NULL;

  reply = g_dbus_connection_call_sync (connection,
                                       "org.freedesktop.DBus",
                                       "/org/freedesktop/DBus",
                                       "org.freedesktop.DBus",
                                       "NameHasOwner",
                                       g_variant_new ("(s)", name),
                                       G_VARIANT_TYPE ("(b)"),
                                       G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                       1000, cancellable, error);
  if (reply == NULL)
    return FALSE;

  g_variant_get (reply, "(b)", has_owner);
  return TRUE;
}

static void
service_operation_thread (GTask        *task,
                          gpointer      source_object,
                          gpointer      task_data,
                          GCancellable *cancellable)
{
  ServiceOperation *operation = task_data;
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GSubprocess) process = NULL;
  g_autoptr(GError) error = NULL;
  gboolean has_owner;
  gint64 deadline = g_get_monotonic_time () + operation->timeout_msec * 1000LL;

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, cancellable, &error);
  if (connection == NULL ||
      !service_has_owner (connection, operation->name, cancellable, &has_owner, &error))
    goto failure;

  if (has_owner == operation->running)
    {
      g_task_return_boolean (task, TRUE);
      return;
    }

  if (g_cancellable_set_error_if_cancelled (cancellable, &error))
    goto failure;

  if (operation->running)
    {
      process = g_subprocess_new (G_SUBPROCESS_FLAGS_NONE, &error,
                                  operation->executable, NULL);
      if (process == NULL)
        goto failure;
    }
  else
    {
      g_autofree char *path = g_strdup_printf ("/%s", operation->name);
      g_autoptr(GVariant) reply = NULL;

      g_strdelimit (path, ".", '/');
      reply = g_dbus_connection_call_sync (connection,
                                           operation->name, path,
                                           "org.freedesktop.Application", "ActivateAction",
                                           g_variant_new ("(s@av@a{sv})", QUAKE_QUIT_ACTION,
                                                          g_variant_new_array (G_VARIANT_TYPE_VARIANT, NULL, 0),
                                                          g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0)),
                                           NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                           1000, cancellable, &error);
      if (reply == NULL)
        {
          if (g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER) ||
              g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN))
            g_clear_error (&error);
          else
            goto failure;
        }
    }

  do
    {
      if (!service_has_owner (connection, operation->name, cancellable, &has_owner, &error))
        goto failure;
      if (has_owner == operation->running)
        {
          if (g_cancellable_set_error_if_cancelled (cancellable, &error))
            goto failure;
          g_task_return_boolean (task, TRUE);
          return;
        }
      g_usleep (50 * 1000);
    }
  while (g_get_monotonic_time () < deadline);

  g_set_error (&error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
               "The Quake shortcut service did not %s", operation->running ? "start" : "stop");

failure:
  /* A failed/cancelled start must not leave a child that acquires the name
   * after a queued stop has already observed that it is absent.
   */
  if (process != NULL)
    {
      g_subprocess_force_exit (process);
      g_subprocess_wait (process, NULL, NULL);
    }
  g_task_return_error (task, g_steal_pointer (&error));
}

static void service_operation_next (void);

static void
service_operation_done (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr(GTask) task = g_queue_pop_head (&service_operations);
  g_autoptr(GError) error = NULL;

  if (g_task_propagate_boolean (G_TASK (result), &error))
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_error (task, g_steal_pointer (&error));

  service_operation_next ();
}

static void
service_operation_next (void)
{
  GTask *task = g_queue_peek_head (&service_operations);
  g_autoptr(GTask) worker = NULL;

  if (task == NULL)
    return;

  worker = g_task_new (NULL, g_task_get_cancellable (task), service_operation_done, NULL);
  g_task_set_task_data (worker, g_task_get_task_data (task), NULL);
  g_task_run_in_thread (worker, service_operation_thread);
}

void
_ptyxis_quake_service_set_running_async (const char          *name,
                                         const char          *executable,
                                         gboolean             running,
                                         guint                timeout_msec,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
  GTask *task = g_task_new (NULL, cancellable, callback, user_data);
  ServiceOperation *operation = g_new0 (ServiceOperation, 1);

  if (!running)
    service_generation++;

  operation->name = g_strdup (name);
  operation->executable = g_strdup (executable);
  operation->running = running;
  operation->timeout_msec = timeout_msec;
  g_task_set_task_data (task, operation, (GDestroyNotify)service_operation_free);
  g_queue_push_tail (&service_operations, task);
  if (service_operations.length == 1)
    service_operation_next ();
}

void
ptyxis_quake_service_ensure_running_async (GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
  if (!ptyxis_quake_service_is_available ())
    {
      g_autoptr(GTask) task = g_task_new (NULL, cancellable, callback, user_data);
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                               "The Quake shortcut service is not available on X11");
      return;
    }

  _ptyxis_quake_service_set_running_async (APP_ID ".QuakeDaemon", QUAKE_DAEMON_PATH,
                                           TRUE, 5000, cancellable, callback, user_data);
}

gboolean
ptyxis_quake_service_ensure_running_finish (GAsyncResult  *result,
                                            GError       **error)
{
  g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);
  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
service_start_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  g_autoptr(GError) error = NULL;

  if (!ptyxis_quake_service_ensure_running_finish (result, &error) &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_warning ("Failed to start the Quake shortcut service: %s", error->message);
}

void
ptyxis_quake_service_start (void)
{
  if (ptyxis_quake_service_is_available ())
    ptyxis_quake_service_ensure_running_async (NULL, service_start_cb, NULL);
}

/* Keep the launch policy separate from service availability and execution. */
gboolean
ptyxis_quake_service_should_start_on_launch (GSettings *settings)
{
  g_autofree char *description = g_settings_get_string (settings, PTYXIS_QUAKE_SHORTCUT_DESCRIPTION_KEY);
  gboolean used = g_settings_get_boolean (settings, PTYXIS_QUAKE_USED_KEY);

  if (!used && (g_settings_get_boolean (settings, PTYXIS_QUAKE_PROMPTED_KEY) ||
                g_settings_get_boolean (settings, PTYXIS_QUAKE_AUTOSTART_KEY) ||
                description[0] != '\0'))
    {
      used = TRUE;
      g_settings_set_boolean (settings, PTYXIS_QUAKE_USED_KEY, TRUE);
    }

  return used && g_settings_get_boolean (settings, PTYXIS_QUAKE_START_ON_LAUNCH_KEY);
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

  if (g_task_return_error_if_cancelled (task))
    return G_SOURCE_REMOVE;

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
configure_started_cb (GObject      *object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  GDBusConnection *connection = g_task_get_task_data (task);
  g_autofree char *path = g_strdup_printf ("/%s/QuakeDaemon", APP_ID);
  GCancellable *cancellable = g_task_get_cancellable (task);

  if (!ptyxis_quake_service_ensure_running_finish (result, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  if (GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (task), "service-generation")) != service_generation)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                               "The Quake shortcut service was stopped");
      return;
    }

  g_strdelimit (path, ".", '/');
  g_dbus_connection_call (connection, APP_ID ".QuakeDaemon", path,
                          "org.freedesktop.Application", "ActivateAction",
                          g_variant_new ("(s@av@a{sv})", QUAKE_CONFIGURE_ACTION,
                                         g_variant_new_array (G_VARIANT_TYPE_VARIANT, NULL, 0),
                                         g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0)),
                          NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START, 5000, cancellable,
                          configure_call_cb, g_steal_pointer (&task));
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
  GCancellable *cancellable;
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

  if (GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (task), "service-generation")) != service_generation)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                               "The Quake shortcut service was stopped");
      return;
    }

  g_task_set_task_data (task, g_object_ref (connection), g_object_unref);
  cancellable = g_task_get_cancellable (task);
  ptyxis_quake_service_ensure_running_async (cancellable, configure_started_cb,
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
  GCancellable *cancellable;

  connection = g_bus_get_finish (result, &error);
  if (connection == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  cancellable = g_task_get_cancellable (task);

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
                          cancellable,
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
  g_object_set_data (G_OBJECT (task), "service-generation", GUINT_TO_POINTER (service_generation));
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

void
ptyxis_quake_service_stop_async (GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  _ptyxis_quake_service_set_running_async (APP_ID ".QuakeDaemon", NULL,
                                           FALSE, 5000, cancellable, callback, user_data);
}

gboolean
ptyxis_quake_service_stop_finish (GAsyncResult  *result,
                                  GError       **error)
{
  g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);
  return g_task_propagate_boolean (G_TASK (result), error);
}
