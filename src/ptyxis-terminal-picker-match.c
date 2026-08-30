/* ptyxis-terminal-picker-match.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib.h>

#include "ptyxis-terminal-picker-match.h"

static gboolean
is_boundary (gunichar ch)
{
  return ch == 0 || !g_unichar_isalnum (ch);
}

static int
match_token (const char *token,
             const char *text)
{
  g_autofree char *normalized = NULL;
  g_autofree char *folded_token = NULL;
  g_autofree char *folded_text = NULL;
  const char *needle;
  const char *haystack;
  const char *last = NULL;
  int score = 0;
  int gap = 0;
  guint matched = 0;

  if (token == NULL || token[0] == 0)
    return 0;
  if (text == NULL)
    return -1;

  folded_token = g_utf8_casefold (token, -1);
  normalized = g_utf8_normalize (text, -1, G_NORMALIZE_ALL_COMPOSE);
  folded_text = g_utf8_casefold (normalized, -1);
  needle = folded_token;
  haystack = folded_text;

  while (*needle)
    {
      gunichar wanted = g_utf8_get_char (needle);
      gboolean found = FALSE;

      while (*haystack)
        {
          gunichar candidate = g_utf8_get_char (haystack);
          if (candidate == wanted)
            {
              gunichar previous = haystack == folded_text ? 0 : g_utf8_get_char (g_utf8_find_prev_char (folded_text, haystack));
              score += 10;
              if (last != NULL && haystack == g_utf8_next_char (last))
                score += 8;
              if (is_boundary (previous))
                score += 12;
              if (matched == 0 && haystack == folded_text)
                score += 18;
              last = haystack;
              haystack = g_utf8_next_char (haystack);
              found = TRUE;
              matched++;
              break;
            }
          haystack = g_utf8_next_char (haystack);
          gap++;
        }
      if (!found)
        return -1;
      needle = g_utf8_next_char (needle);
    }

  return score - MIN (gap, score / 2);
}

int
ptyxis_terminal_picker_match (const char *query,
                              const char *title,
                              const char *path)
{
  g_auto(GStrv) tokens = NULL;
  g_autofree char *normalized = NULL;
  int total = 0;

  if (query == NULL || query[0] == 0)
    return 0;

  normalized = g_utf8_normalize (query, -1, G_NORMALIZE_ALL_COMPOSE);
  tokens = g_strsplit_set (normalized, " \t\r\n", -1);
  for (guint i = 0; tokens[i]; i++)
    {
      int title_score;
      int path_score;

      if (tokens[i][0] == 0)
        continue;
      title_score = match_token (tokens[i], title);
      path_score = match_token (tokens[i], path);
      if (title_score < 0 && path_score < 0)
        return -1;
      total += MAX (title_score < 0 ? -1 : title_score + 30, path_score);
    }

  return total;
}
