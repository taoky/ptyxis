/* test-terminal-copy.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib.h>

#include "ptyxis-terminal-copy.h"

static void
test_trim_trailing_spaces (void)
{
  g_autofree char *trimmed = NULL;

  trimmed = ptyxis_terminal_copy_trim_trailing_spaces ("one   \ntwo\t \nthree  ");
  g_assert_cmpstr (trimmed, ==, "one\ntwo\nthree");
}

static void
test_preserve_line_endings (void)
{
  g_autofree char *trimmed = NULL;

  trimmed = ptyxis_terminal_copy_trim_trailing_spaces ("one  \r\n   \r\ntwo\r\n");
  g_assert_cmpstr (trimmed, ==, "one\r\n\r\ntwo\r\n");
}

static void
test_preserve_content (void)
{
  g_autofree char *trimmed = NULL;

  trimmed = ptyxis_terminal_copy_trim_trailing_spaces ("  hello world\n终端\302\240\n");
  g_assert_cmpstr (trimmed, ==, "  hello world\n终端\302\240\n");
}

static void
test_empty_text (void)
{
  g_autofree char *trimmed = NULL;

  trimmed = ptyxis_terminal_copy_trim_trailing_spaces ("");
  g_assert_cmpstr (trimmed, ==, "");
}

int
main (int argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/terminal-copy/trim-trailing-spaces", test_trim_trailing_spaces);
  g_test_add_func ("/terminal-copy/preserve-line-endings", test_preserve_line_endings);
  g_test_add_func ("/terminal-copy/preserve-content", test_preserve_content);
  g_test_add_func ("/terminal-copy/empty-text", test_empty_text);
  return g_test_run ();
}
