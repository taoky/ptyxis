/* ptyxis-quake-daemon.c
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <gio/gio.h>
#include <glib/gi18n.h>

#include "ptyxis-global-shortcuts.h"

#define QUAKE_ACTION "toggle-quake"
#define CONFIGURE_ACTION "configure-shortcut"

typedef struct
{
  GApplication          *application;
  PtyxisGlobalShortcuts *shortcuts;
} QuakeDaemon;

static char *
application_object_path (void)
{
  g_autofree char *id = g_strdup (APP_ID);

  g_strdelimit (id, ".", '/');
  return g_strconcat ("/", id, NULL);
}

static void
activate_action_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GError) error = NULL;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (object), result, &error);
  if (reply == NULL &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_warning ("Failed to activate the Ptyxis Quake action: %s", error->message);
}

static void
shortcuts_activated_cb (QuakeDaemon          *self,
                        const char           *activation_token,
                        PtyxisGlobalShortcuts *shortcuts)
{
  g_autofree char *object_path = NULL;
  GVariantBuilder parameters;
  GVariantBuilder platform_data;
  GDBusConnection *connection;

  g_assert (self != NULL);
  g_assert (PTYXIS_IS_GLOBAL_SHORTCUTS (shortcuts));

  connection = g_application_get_dbus_connection (self->application);
  if (connection == NULL)
    return;

  object_path = application_object_path ();
  g_variant_builder_init (&parameters, G_VARIANT_TYPE ("av"));
  g_variant_builder_init (&platform_data, G_VARIANT_TYPE_VARDICT);

  if (activation_token != NULL)
    g_variant_builder_add (&platform_data,
                           "{sv}",
                           "activation-token",
                           g_variant_new_string (activation_token));

  g_dbus_connection_call (connection,
                          APP_ID,
                          object_path,
                          "org.freedesktop.Application",
                          "ActivateAction",
                          g_variant_new ("(s@av@a{sv})",
                                         QUAKE_ACTION,
                                         g_variant_builder_end (&parameters),
                                         g_variant_builder_end (&platform_data)),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          NULL,
                          activate_action_cb,
                          NULL);
}

static void
shortcuts_changed_cb (QuakeDaemon          *self,
                      const char           *description,
                      PtyxisGlobalShortcuts *shortcuts)
{
  g_autoptr(GSettings) settings = g_settings_new (APP_SCHEMA_ID);

  g_settings_set_string (settings,
                         "quake-shortcut-description",
                         description);
}

static void
daemon_activate_cb (GApplication *application,
                    gpointer      user_data)
{
  QuakeDaemon *self = user_data;

  g_assert (G_IS_APPLICATION (application));

  if (self->shortcuts != NULL)
    return;

  self->shortcuts = ptyxis_global_shortcuts_new (APP_ID);
  g_signal_connect_swapped (self->shortcuts,
                            "activated",
                            G_CALLBACK (shortcuts_activated_cb),
                            self);
  g_signal_connect_swapped (self->shortcuts,
                            "changed",
                            G_CALLBACK (shortcuts_changed_cb),
                            self);
  ptyxis_global_shortcuts_register (self->shortcuts);
  ptyxis_global_shortcuts_start (self->shortcuts);
  ptyxis_global_shortcuts_ensure_bound (self->shortcuts);

  g_application_hold (application);
}

static void
configure_shortcut_cb (GSimpleAction *action,
                       GVariant      *parameter,
                       gpointer       user_data)
{
  QuakeDaemon *self = user_data;

  if (self->shortcuts == NULL)
    daemon_activate_cb (self->application, self);

  ptyxis_global_shortcuts_configure (self->shortcuts, "");
}

int
main (int   argc,
      char *argv[])
{
  g_autofree char *daemon_id = NULL;
  QuakeDaemon self = {0};
  int ret;

  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  daemon_id = g_strconcat (APP_ID, ".QuakeDaemon", NULL);
  self.application = g_application_new (daemon_id, G_APPLICATION_DEFAULT_FLAGS);
  {
    g_autoptr(GSimpleAction) configure_action =
      g_simple_action_new (CONFIGURE_ACTION, NULL);

    g_signal_connect (configure_action,
                      "activate",
                      G_CALLBACK (configure_shortcut_cb),
                      &self);
    g_action_map_add_action (G_ACTION_MAP (self.application),
                             G_ACTION (configure_action));
  }
  g_signal_connect (self.application,
                    "activate",
                    G_CALLBACK (daemon_activate_cb),
                    &self);

  ret = g_application_run (self.application, argc, argv);

  g_clear_object (&self.shortcuts);
  g_clear_object (&self.application);

  return ret;
}
