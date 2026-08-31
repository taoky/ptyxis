/* main.c
 *
 * Copyright 2023 Christian Hergert
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

#include "config.h"

#include <errno.h>
#include <sys/resource.h>

#include <glib/gi18n.h>

#include "ptyxis-application.h"
#include "ptyxis-palette.h"
#include "ptyxis-util.h"

static gint64 default_rlimit_nofile;

static void
check_early_opts (int        *argc,
                  char     ***argv,
                  gboolean   *standalone)
{
  g_autoptr(GOptionContext) context = NULL;
  g_autofree char *palette = NULL;
  gboolean version = FALSE;
  gboolean ignore_standalone = FALSE;
  gboolean real_standalone = FALSE;
  GOptionEntry entries[] = {
    { "import-palette", 0, 0, G_OPTION_ARG_FILENAME, &palette },
    { "standalone", 's', 0, G_OPTION_ARG_NONE, standalone },
    { "version", 0, 0, G_OPTION_ARG_NONE, &version },
    { NULL }
  };

  /* If we see a -- then take all of the arguments after that and
   * replace it into an escaped string suitable to pass as the value
   * for `-c 'command ...'.
   *
   * However, if we see --tab, --new-window, or --tab-with-profile,
   * then we will not use standalone mode.
   */
  for (int i = 0; i < *argc; i++)
    {
      char *arg = (*argv)[i];

      if (g_str_equal (arg, "--tab") ||
          g_str_equal (arg , "--new-window") ||
          g_str_equal (arg, "--toggle-quake") ||
          (g_str_equal (arg, "--tab-with-profile") || g_str_has_prefix (arg, "--tab-with-profile=")))
        ignore_standalone = TRUE;

      if (g_str_equal (arg, "-x") || g_str_equal (arg, "--execute") || g_str_has_prefix (arg, "--execute="))
        real_standalone = TRUE;

      /* Convert -e to -- for x-terminal-emulator */
      if (strcmp (arg, "-e") == 0)
        arg[1] = '-';

      if (g_str_equal ((*argv)[i], "--") && (*argv)[i+1] != NULL)
        {
          GString *str = g_string_new (NULL);

          for (int j = i+1; j < *argc; j++)
            {
              g_autofree char *quoted = g_shell_quote ((*argv)[j]);

              g_string_append (str, quoted);
              g_string_append_c (str, ' ');
              (*argv)[j] = NULL;
            }

          *argc = i + 1 + 1;
          (*argv)[i] = g_strdup ("-x");
          (*argv)[i+1] = g_string_free (str, FALSE);

          real_standalone = TRUE;

          break;
        }
    }

  if (!ignore_standalone && real_standalone)
    *standalone = real_standalone;

  context = g_option_context_new (NULL);
  g_option_context_set_ignore_unknown_options (context, TRUE);
  g_option_context_set_help_enabled (context, FALSE);
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_parse (context, argc, argv, NULL);

  if (version)
    {
      g_print ("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
      g_print ("\n");
      g_print ("  GTK: %d.%d.%d (Compiled against %d.%d.%d)\n",
                  gtk_get_major_version (), gtk_get_minor_version (), gtk_get_micro_version (),
                  GTK_MAJOR_VERSION, GTK_MINOR_VERSION, GTK_MICRO_VERSION);
      g_print ("  VTE: %d.%d.%d (Compiled against %d.%d.%d)\n",
                  vte_get_major_version (), vte_get_minor_version (), vte_get_micro_version (),
                  VTE_MAJOR_VERSION, VTE_MINOR_VERSION, VTE_MICRO_VERSION);

      g_print ("\n\
Copyright 2020-2024 Christian Hergert, et al.\n\
This is free software; see the source for copying conditions. There is NO\n\
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\
");

      exit (EXIT_SUCCESS);
    }

  /* We handle this here instead of in the main application so we can
   * synchronously copy without blocking the main loop and also to not
   * complicate the "activation" path that would normally need to
   * happen in the remote instance.
   */
  if (palette != NULL)
    {
      g_autoptr(GError) error = NULL;
      g_autofree char *contents = NULL;
      g_autofree char *basename = g_path_get_basename (palette);
      g_autofree char *user_palettes_dir = ptyxis_get_user_palettes_dir ();
      g_autofree char *dest = g_build_filename (user_palettes_dir, basename, NULL);
      gsize len;

      if (!g_file_get_contents (palette, &contents, &len, &error))
        {
          g_printerr ("%s: %s\n", _("Error"), error->message);
          exit (EXIT_FAILURE);
        }

      if (g_mkdir_with_parents (user_palettes_dir, 0750) != 0)
        {
          int errsv = errno;
          g_printerr ("%s: %s\n", _("Error"), g_strerror (errsv));
          exit (EXIT_FAILURE);
        }

      if (g_file_test (dest, G_FILE_TEST_EXISTS))
        {
          g_printerr (_("Error: %s already exists.\nPlease remove it first.\n"), dest);
          exit (EXIT_FAILURE);
        }

      if (!g_file_set_contents (dest, contents, len, &error))
        {
          g_printerr ("%s: %s\n", _("Error"), error->message);
          exit (EXIT_FAILURE);
        }

      exit (EXIT_SUCCESS);
    }
}

static gint64
get_current_fd_limit (void)
{
  struct rlimit limit;

  if (getrlimit (RLIMIT_NOFILE, &limit) == 0)
    return limit.rlim_cur;

  return 0;
}

static void
bump_to_max_fd_limit (void)
{
  struct rlimit limit;

  if (getrlimit (RLIMIT_NOFILE, &limit) == 0)
    {
      limit.rlim_cur = limit.rlim_max;

      if (setrlimit (RLIMIT_NOFILE, &limit) != 0)
        g_warning ("Failed to set FD limit to %"G_GSSIZE_FORMAT"",
                    (gssize)limit.rlim_max);
      else
        g_debug ("Set RLIMIT_NOFILE to %"G_GSSIZE_FORMAT"",
                 (gssize)limit.rlim_max);
    }
}

gint64
ptyxis_application_get_default_rlimit_nofile (void)
{
  return default_rlimit_nofile;
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(PtyxisApplication) app = NULL;
  GApplicationFlags flags = G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_HANDLES_OPEN;
  gboolean standalone = FALSE;
  int ret;

  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  g_set_prgname ("ptyxis");
  g_set_application_name (ptyxis_app_name ());

  check_early_opts (&argc, &argv, &standalone);

  if (standalone)
    flags |= G_APPLICATION_NON_UNIQUE | G_APPLICATION_CAN_OVERRIDE_APP_ID;

  default_rlimit_nofile = get_current_fd_limit ();
  bump_to_max_fd_limit ();

  gtk_init ();

  app = ptyxis_application_new (APP_ID, flags);
  ret = g_application_run (G_APPLICATION (app), argc, argv);

  return ret;
}
