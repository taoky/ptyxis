/* test-terminal-picker.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib.h>

#include "ptyxis-terminal-picker-match.h"

static void
test_case_and_unicode (void)
{
  g_assert_cmpint (ptyxis_terminal_picker_match ("ssh", "SSH Server", ""), >, 0);
  g_assert_cmpint (ptyxis_terminal_picker_match ("é", "Cafe\314\201", ""), >, 0);
  g_assert_cmpint (ptyxis_terminal_picker_match ("终端", "开发终端", ""), >, 0);
}

static void
test_tokens (void)
{
  g_assert_cmpint (ptyxis_terminal_picker_match ("ssh proj", "SSH server", "~/project"), >, 0);
  g_assert_cmpint (ptyxis_terminal_picker_match ("ssh missing", "SSH server", "~/project"), ==, -1);
}

static void
test_ranking (void)
{
  int title = ptyxis_terminal_picker_match ("server", "Server", "/tmp");
  int path = ptyxis_terminal_picker_match ("server", "Terminal", "/srv/server");
  int contiguous = ptyxis_terminal_picker_match ("term", "Terminal", "");
  int sparse = ptyxis_terminal_picker_match ("term", "The experimental remote machine", "");

  g_assert_cmpint (title, >, path);
  g_assert_cmpint (contiguous, >, sparse);
}

int
main (int argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/terminal-picker/case-and-unicode", test_case_and_unicode);
  g_test_add_func ("/terminal-picker/tokens", test_tokens);
  g_test_add_func ("/terminal-picker/ranking", test_ranking);
  return g_test_run ();
}
