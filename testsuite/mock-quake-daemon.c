/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <gio/gio.h>
#include <string.h>

#define TEST_NAME "org.gnome.Ptyxis.TestQuake"
#define TEST_PATH "/org/gnome/Ptyxis/TestQuake"

static GMainLoop *loop;
static const char *mode;

static gboolean
quit_idle (gpointer data)
{
  g_main_loop_quit (loop);
  return G_SOURCE_REMOVE;
}

static void
method_call (GDBusConnection       *connection,
             const char            *sender,
             const char            *path,
             const char            *interface,
             const char            *method,
             GVariant              *parameters,
             GDBusMethodInvocation *invocation,
             gpointer               data)
{
  if (g_str_equal (mode, "reject"))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED, "Stop rejected");
      return;
    }

  g_dbus_method_invocation_return_value (invocation, NULL);
  if (!g_str_equal (mode, "ignore"))
    g_idle_add (quit_idle, NULL);
}

int
main (int argc, char **argv)
{
  static const GDBusInterfaceVTable vtable = { .method_call = method_call };
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GDBusNodeInfo) info = NULL;
  g_autoptr(GError) error = NULL;
  guint owner;

  mode = argc > 1 ? argv[1] : "normal";
  if (g_str_equal (mode, "delay"))
    g_usleep (300 * 1000);
  if (g_str_equal (mode, "slow"))
    g_usleep (2 * G_USEC_PER_SEC);

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);
  info = g_dbus_node_info_new_for_xml (
    "<node><interface name='org.freedesktop.Application'>"
    "<method name='ActivateAction'><arg type='s' direction='in'/>"
    "<arg type='av' direction='in'/><arg type='a{sv}' direction='in'/>"
    "</method></interface></node>", &error);
  g_assert_no_error (error);
  g_dbus_connection_register_object (connection, TEST_PATH, info->interfaces[0],
                                     &vtable, NULL, NULL, &error);
  g_assert_no_error (error);
  owner = g_bus_own_name_on_connection (connection, TEST_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
                                       NULL, NULL, NULL, NULL);
  loop = g_main_loop_new (NULL, FALSE);
  g_timeout_add_seconds (10, quit_idle, NULL);
  g_main_loop_run (loop);
  g_bus_unown_name (owner);
  g_dbus_connection_flush_sync (connection, NULL, NULL);
  g_main_loop_unref (loop);
  return 0;
}
