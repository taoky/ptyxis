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

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_assert_cmpint (argc, ==, 2);
  template_path = argv[1];
  g_test_add_func ("/ptyxis/quake-service/native-autostart", test_native_autostart);
  return g_test_run ();
}
