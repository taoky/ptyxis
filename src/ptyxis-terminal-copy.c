/* ptyxis-terminal-copy.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <glib.h>

#include "ptyxis-terminal-copy.h"

char *
ptyxis_terminal_copy_trim_trailing_spaces (const char *text)
{
  g_autoptr(GString) result = NULL;
  const char *line;

  g_return_val_if_fail (text != NULL, NULL);

  result = g_string_sized_new (strlen (text));
  line = text;

  while (*line != '\0')
    {
      const char *line_end = strchr (line, '\n');
      const char *content_end;
      gboolean has_carriage_return = FALSE;

      if (line_end == NULL)
        line_end = line + strlen (line);

      content_end = line_end;
      if (content_end > line && content_end[-1] == '\r')
        {
          content_end--;
          has_carriage_return = TRUE;
        }

      while (content_end > line &&
             (content_end[-1] == ' ' || content_end[-1] == '\t'))
        content_end--;

      g_string_append_len (result, line, content_end - line);

      if (has_carriage_return)
        g_string_append_c (result, '\r');

      if (*line_end == '\n')
        {
          g_string_append_c (result, '\n');
          line = line_end + 1;
        }
      else
        break;
    }

  return g_string_free (g_steal_pointer (&result), FALSE);
}
