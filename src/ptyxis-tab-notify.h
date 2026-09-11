/*
 * ptyxis-tab-notify.h
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#pragma once

#include <gtk/gtk.h>

#ifdef GDK_WINDOWING_X11
# include <gdk/x11/gdkx.h>
#endif

#include <glib/gi18n.h>

#include "ptyxis-window.h"

#define TIME_WATERMARK_MSEC 250

G_BEGIN_DECLS

/* Owned by the tab while the pane is attached. Pending polls hold a separate
 * reference, and must check both attachment and command generation on return.
 */
typedef struct _PtyxisTabNotify
{
  PtyxisTab *tab;
  PtyxisPane *pane;
  char *current_cmdline;
  gulong shell_precmd_handler;
  gulong shell_preexec_handler;
  gint64 command_start_time;
  guint64 command_generation;
} PtyxisTabNotify;

typedef struct
{
  PtyxisTabNotify *notify;
  guint64 command_generation;
} PtyxisTabNotifyPoll;

static inline void
ptyxis_tab_notify_withdraw (PtyxisPane *pane)
{
  g_autofree char *id = g_strconcat ("command-completed-", ptyxis_pane_get_uuid (pane), NULL);

  g_application_withdraw_notification (G_APPLICATION (PTYXIS_APPLICATION_DEFAULT), id);
}

static inline void
ptyxis_tab_notify_show_notification (PtyxisTabNotify *notify,
                                     const char      *cmdline)
{
  GtkRoot *window = gtk_widget_get_root (GTK_WIDGET (notify->tab));
  g_autoptr(GNotification) notification = NULL;
  g_autoptr(GIcon) icon = NULL;
  g_autofree char *cmdline_sanitized = NULL;
  g_autofree char *id = NULL;

  if (!PTYXIS_IS_WINDOW (window))
    return;

  if (gtk_window_is_active (GTK_WINDOW (window)) &&
      ptyxis_window_get_active_tab (PTYXIS_WINDOW (window)) == notify->tab &&
      ptyxis_tab_get_active_pane (notify->tab) == notify->pane)
    return;

#ifdef GDK_WINDOWING_X11
  if (!gtk_window_is_active (GTK_WINDOW (window)))
    {
      GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (window));

      if (GDK_IS_X11_SURFACE (surface))
        gdk_x11_surface_set_urgency_hint (surface, TRUE);
    }
#endif

  icon = g_themed_icon_new (APP_ID "-symbolic");
  cmdline_sanitized = g_utf8_make_valid (cmdline, -1);
  id = g_strconcat ("command-completed-", ptyxis_pane_get_uuid (notify->pane), NULL);
  notification = g_notification_new (_("Command completed"));
  g_notification_set_body (notification, cmdline_sanitized);
  g_notification_set_icon (notification, icon);
  g_notification_set_default_action_and_target (notification,
                                                "app.focus-pane-by-uuid",
                                                "(ss)",
                                                ptyxis_tab_get_uuid (notify->tab),
                                                ptyxis_pane_get_uuid (notify->pane));
  g_application_send_notification (G_APPLICATION (PTYXIS_APPLICATION_DEFAULT),
                                   id, notification);
  ptyxis_tab_set_needs_attention (notify->tab, TRUE);
}

static void
ptyxis_tab_notify_shell_precmd_cb (PtyxisTerminal  *terminal,
                                   PtyxisTabNotify *notify)
{
  notify->command_generation++;

  if (notify->current_cmdline != NULL)
    {
      gint64 elapsed_time = g_get_monotonic_time () - notify->command_start_time;

      if (elapsed_time >= TIME_WATERMARK_MSEC * 1000)
        ptyxis_tab_notify_show_notification (notify, notify->current_cmdline);

      g_clear_pointer (&notify->current_cmdline, g_free);
    }
}

static void
ptyxis_tab_notify_shell_preexec_poll_cb (GObject      *object,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
  PtyxisTab *tab = (PtyxisTab *)object;
  g_autoptr(GError) error = NULL;
  g_autofree PtyxisTabNotifyPoll *poll = user_data;
  PtyxisTabNotify *notify = poll->notify;

  /* The boolean reports whether process metadata changed, not success. */
  ptyxis_tab_poll_agent_finish (tab, result, &error);

  if (error == NULL &&
      notify->pane != NULL &&
      notify->command_generation == poll->command_generation)
    g_set_str (&notify->current_cmdline,
               ptyxis_pane_get_command_line (notify->pane));

  g_rc_box_release (notify);
}

static void
ptyxis_tab_notify_shell_preexec_cb (PtyxisTerminal  *terminal,
                                    PtyxisTabNotify *notify)
{
  PtyxisTabNotifyPoll *poll = g_new0 (PtyxisTabNotifyPoll, 1);

  notify->command_generation++;
  notify->command_start_time = g_get_monotonic_time ();
  g_clear_pointer (&notify->current_cmdline, g_free);

  poll->notify = g_rc_box_acquire (notify);
  poll->command_generation = notify->command_generation;
  ptyxis_tab_poll_pane_agent_async (notify->tab,
                                    notify->pane,
                                    NULL,
                                    ptyxis_tab_notify_shell_preexec_poll_cb,
                                    poll);
}

static inline PtyxisTabNotify *
ptyxis_tab_notify_new (PtyxisTab  *tab,
                       PtyxisPane *pane)
{
  PtyxisTabNotify *notify = g_rc_box_new0 (PtyxisTabNotify);
  PtyxisTerminal *terminal = ptyxis_pane_get_terminal (pane);

  notify->tab = tab;
  notify->pane = pane;
  notify->shell_precmd_handler =
    g_signal_connect (terminal, "shell-precmd",
                      G_CALLBACK (ptyxis_tab_notify_shell_precmd_cb), notify);
  notify->shell_preexec_handler =
    g_signal_connect (terminal, "shell-preexec",
                      G_CALLBACK (ptyxis_tab_notify_shell_preexec_cb), notify);

  return notify;
}

static inline void
ptyxis_tab_notify_free (PtyxisTabNotify *notify)
{
  PtyxisTerminal *terminal = ptyxis_pane_get_terminal (notify->pane);

  ptyxis_tab_notify_withdraw (notify->pane);
  g_clear_signal_handler (&notify->shell_precmd_handler, terminal);
  g_clear_signal_handler (&notify->shell_preexec_handler, terminal);
  g_clear_pointer (&notify->current_cmdline, g_free);
  notify->tab = NULL;
  notify->pane = NULL;
  g_rc_box_release (notify);
}

G_END_DECLS
