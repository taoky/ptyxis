/* test-quake-service.c
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <glib/gstdio.h>

#include "ptyxis-quake-service.h"

static const char *template_path;

static void
test_native_autostart (void)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *config_dir = g_dir_make_tmp ("ptyxis-quake-test-XXXXXX", &error);
  g_autofree char *filename = g_strconcat (APP_ID, ".QuakeDaemon.desktop", NULL);
  g_autofree char *path = NULL;
  g_autofree char *expected = NULL;
  g_autofree char *actual = NULL;

  g_assert_no_error (error);
  path = g_build_filename (config_dir, "autostart", filename, NULL);

  g_assert_true (_ptyxis_quake_service_set_native_autostart (template_path,
                                                              config_dir,
                                                              TRUE,
                                                              &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (template_path, &expected, NULL, &error));
  g_assert_no_error (error);
  g_assert_true (g_file_get_contents (path, &actual, NULL, &error));
  g_assert_no_error (error);
  g_assert_cmpstr (actual, ==, expected);

  g_assert_true (_ptyxis_quake_service_set_native_autostart (template_path,
                                                              config_dir,
                                                              FALSE,
                                                              &error));
  g_assert_no_error (error);
  g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));

  g_assert_true (_ptyxis_quake_service_set_native_autostart (template_path,
                                                              config_dir,
                                                              FALSE,
                                                              &error));
  g_assert_no_error (error);
}

#define TEST_NAME "org.gnome.Ptyxis.TestQuake"

static const char *mock_path;
static char *wrapper_dir;
static GDBusConnection *test_connection;

typedef struct
{
  gboolean done;
  gboolean success;
  GError *error;
} Result;

static void
operation_done (GObject *object, GAsyncResult *result, gpointer data)
{
  Result *out = data;

  out->success = ptyxis_quake_service_ensure_running_finish (result, &out->error);
  out->done = TRUE;
}

static void
wait_result (Result *result)
{
  while (!result->done)
    g_main_context_iteration (NULL, TRUE);
}

static gboolean
has_owner (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = g_dbus_connection_call_sync (
    test_connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
    "org.freedesktop.DBus", "NameHasOwner", g_variant_new ("(s)", TEST_NAME),
    G_VARIANT_TYPE ("(b)"), G_DBUS_CALL_FLAGS_NONE, 1000, NULL, &error);
  gboolean owner;

  g_assert_no_error (error);
  g_variant_get (reply, "(b)", &owner);
  return owner;
}

static char *
make_wrapper (const char *mode)
{
  char *path = g_build_filename (wrapper_dir, mode, NULL);
  g_autofree char *quoted = g_shell_quote (mock_path);
  g_autofree char *contents = g_strdup_printf ("#!/bin/sh\nexec %s %s\n", quoted, mode);
  g_autoptr(GError) error = NULL;

  g_file_set_contents (path, contents, -1, &error);
  g_assert_no_error (error);
  g_assert_cmpint (g_chmod (path, 0700), ==, 0);
  return path;
}

static void
operate (gboolean running, const char *path, guint timeout, GCancellable *cancel, Result *result)
{
  _ptyxis_quake_service_set_running_async (TEST_NAME, path, running, timeout,
                                           cancel, operation_done, result);
}

static void
assert_success (Result *result)
{
  wait_result (result);
  g_assert_no_error (result->error);
  g_assert_true (result->success);
}

static void
test_lifecycle (void)
{
  Result absent = {0}, start = {0}, existing = {0}, stop = {0};

  operate (FALSE, NULL, 1000, NULL, &absent);
  assert_success (&absent);
  operate (TRUE, mock_path, 1000, NULL, &start);
  assert_success (&start);
  g_assert_true (has_owner ());
  /* An existing service must not require spawning another executable. */
  operate (TRUE, "/nonexistent/quake-daemon", 1000, NULL, &existing);
  assert_success (&existing);
  operate (FALSE, NULL, 1000, NULL, &stop);
  assert_success (&stop);
  g_assert_false (has_owner ());
}

static void
test_start_then_stop (void)
{
  g_autofree char *path = make_wrapper ("delay");
  Result start = {0}, stop = {0};

  operate (TRUE, path, 1000, NULL, &start);
  operate (FALSE, NULL, 1000, NULL, &stop);
  assert_success (&start);
  assert_success (&stop);
  g_assert_false (has_owner ());
}

static gboolean
cancel_start (gpointer data)
{
  g_cancellable_cancel (data);
  return G_SOURCE_REMOVE;
}

static void
test_cancel_start (void)
{
  g_autofree char *path = make_wrapper ("slow");
  g_autoptr(GCancellable) cancel = g_cancellable_new ();
  Result start = {0}, stop = {0};

  operate (TRUE, path, 1000, cancel, &start);
  operate (FALSE, NULL, 1000, NULL, &stop);
  g_timeout_add (100, cancel_start, cancel);
  wait_result (&start);
  g_assert_error (start.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error (&start.error);
  assert_success (&stop);
  g_assert_false (has_owner ());
}

static void
test_failed_start (void)
{
  g_autofree char *path = make_wrapper ("slow");
  Result missing = {0}, timeout = {0}, stop = {0};

  operate (TRUE, "/nonexistent/quake-daemon", 1000, NULL, &missing);
  wait_result (&missing);
  g_assert_false (missing.success);
  g_assert_nonnull (missing.error);
  g_clear_error (&missing.error);
  operate (TRUE, path, 100, NULL, &timeout);
  operate (FALSE, NULL, 1000, NULL, &stop);
  wait_result (&timeout);
  g_assert_error (timeout.error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
  g_clear_error (&timeout.error);
  assert_success (&stop);
  g_assert_false (has_owner ());
}

static void
test_failed_stop (gconstpointer data)
{
  const char *mode = data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GSubprocess) process = g_subprocess_new (G_SUBPROCESS_FLAGS_NONE, &error,
                                                    mock_path, mode, NULL);
  gint64 deadline = g_get_monotonic_time () + G_USEC_PER_SEC * 3;
  Result stop = {0};

  g_assert_no_error (error);
  while (!has_owner () && g_get_monotonic_time () < deadline)
    g_usleep (10000);
  g_assert_true (has_owner ());
  operate (FALSE, NULL, 150, NULL, &stop);
  wait_result (&stop);
  g_assert_false (stop.success);
  if (g_str_equal (mode, "reject"))
    g_assert_error (stop.error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED);
  else
    g_assert_error (stop.error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
  g_clear_error (&stop.error);
  g_assert_true (has_owner ());
  g_subprocess_force_exit (process);
  g_subprocess_wait (process, NULL, NULL);
}

static void
test_launch_policy (void)
{
  g_autoptr(GSettings) settings = g_settings_new (APP_SCHEMA_ID);
  const char *legacy[] = { PTYXIS_QUAKE_PROMPTED_KEY, PTYXIS_QUAKE_AUTOSTART_KEY,
                          PTYXIS_QUAKE_SHORTCUT_DESCRIPTION_KEY };

  g_assert_false (ptyxis_quake_service_should_start_on_launch (settings));
  g_assert_true (g_settings_get_boolean (settings, PTYXIS_QUAKE_START_ON_LAUNCH_KEY));
  g_settings_set_boolean (settings, PTYXIS_QUAKE_USED_KEY, TRUE);
  g_assert_true (ptyxis_quake_service_should_start_on_launch (settings));
  g_settings_set_boolean (settings, PTYXIS_QUAKE_START_ON_LAUNCH_KEY, FALSE);
  g_assert_false (ptyxis_quake_service_should_start_on_launch (settings));

  for (guint i = 0; i < G_N_ELEMENTS (legacy); i++)
    {
      g_settings_reset (settings, PTYXIS_QUAKE_USED_KEY);
      if (i == 2)
        g_settings_set_string (settings, legacy[i], "F12");
      else
        g_settings_set_boolean (settings, legacy[i], TRUE);
      /* Migration must not overwrite an explicit opt-out. */
      g_assert_false (ptyxis_quake_service_should_start_on_launch (settings));
      g_assert_true (g_settings_get_boolean (settings, PTYXIS_QUAKE_USED_KEY));
      g_settings_reset (settings, PTYXIS_QUAKE_START_ON_LAUNCH_KEY);
      g_assert_true (ptyxis_quake_service_should_start_on_launch (settings));
      g_settings_set_boolean (settings, PTYXIS_QUAKE_START_ON_LAUNCH_KEY, FALSE);
      g_settings_reset (settings, legacy[i]);
    }
}

int
main (int argc, char *argv[])
{
  g_autoptr(GTestDBus) bus = NULL;
  g_autoptr(GError) error = NULL;
  int status;

  g_test_init (&argc, &argv, NULL);
  g_assert_cmpint (argc, ==, 3);
  template_path = argv[1];
  mock_path = argv[2];
  wrapper_dir = g_dir_make_tmp ("ptyxis-quake-wrappers-XXXXXX", &error);
  g_assert_no_error (error);
  bus = g_test_dbus_new (G_TEST_DBUS_NONE);
  g_test_dbus_up (bus);
  test_connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);

  g_test_add_func ("/ptyxis/quake-service/native-autostart", test_native_autostart);
  g_test_add_func ("/ptyxis/quake-service/launch-policy", test_launch_policy);
  g_test_add_func ("/ptyxis/quake-service/lifecycle", test_lifecycle);
  g_test_add_func ("/ptyxis/quake-service/start-then-stop", test_start_then_stop);
  g_test_add_func ("/ptyxis/quake-service/cancel-start", test_cancel_start);
  g_test_add_func ("/ptyxis/quake-service/failed-start", test_failed_start);
  g_test_add_data_func ("/ptyxis/quake-service/rejected-stop", "reject", test_failed_stop);
  g_test_add_data_func ("/ptyxis/quake-service/stop-timeout", "ignore", test_failed_stop);
  status = g_test_run ();
  g_clear_object (&test_connection);
  g_test_dbus_down (bus);
  {
    g_autoptr(GDir) dir = g_dir_open (wrapper_dir, 0, NULL);
    const char *name;
    while ((name = g_dir_read_name (dir)))
      {
        g_autofree char *path = g_build_filename (wrapper_dir, name, NULL);
        g_remove (path);
      }
  }
  g_rmdir (wrapper_dir);
  g_free (wrapper_dir);
  return status;
}
