/* ptyxis-global-shortcuts.c
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <glib/gi18n.h>

#include "ptyxis-global-shortcuts.h"

#define PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define PORTAL_GLOBAL_SHORTCUTS_INTERFACE "org.freedesktop.portal.GlobalShortcuts"
#define PORTAL_HOST_REGISTRY_INTERFACE "org.freedesktop.host.portal.Registry"
#define PORTAL_REQUEST_INTERFACE "org.freedesktop.portal.Request"
#define QUAKE_SHORTCUT_ID "toggle-quake"

typedef enum
{
  REQUEST_NONE,
  REQUEST_CREATE_SESSION,
  REQUEST_LIST_SHORTCUTS,
  REQUEST_BIND_SHORTCUTS,
} RequestKind;

typedef struct
{
  PtyxisGlobalShortcuts *self;
  RequestKind            request;
  guint                  serial;
} PortalCall;

struct _PtyxisGlobalShortcuts
{
  GObject          parent_instance;
  GDBusConnection *connection;
  GCancellable    *cancellable;
  char            *application_id;
  char            *session_handle;
  guint            response_subscription;
  guint            activated_subscription;
  guint            changed_subscription;
  guint            portal_owner_subscription;
  guint            request_serial;
  RequestKind      request;
  guint            bound : 1;
  guint            bind_requested : 1;
  guint            configure_requested : 1;
  guint            started : 1;
};

enum {
  ACTIVATED,
  CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (PtyxisGlobalShortcuts, ptyxis_global_shortcuts, G_TYPE_OBJECT)

static void ptyxis_global_shortcuts_list (PtyxisGlobalShortcuts *self);
static void ptyxis_global_shortcuts_bind (PtyxisGlobalShortcuts *self);
static void ptyxis_global_shortcuts_register_app_id (PtyxisGlobalShortcuts *self);

static gboolean
shortcuts_contains_quake (GVariant *shortcuts)
{
  GVariantIter iter;
  g_autoptr(GVariant) shortcut_properties = NULL;
  g_autofree char *shortcut_id = NULL;

  if (shortcuts == NULL)
    return FALSE;

  g_variant_iter_init (&iter, shortcuts);
  while (g_variant_iter_next (&iter, "(s@a{sv})", &shortcut_id, &shortcut_properties))
    {
      if (g_str_equal (shortcut_id, QUAKE_SHORTCUT_ID))
        return TRUE;

      g_clear_pointer (&shortcut_id, g_free);
      g_clear_pointer (&shortcut_properties, g_variant_unref);
    }

  return FALSE;
}

static void
shortcuts_emit_changed (PtyxisGlobalShortcuts *self,
                        GVariant              *shortcuts)
{
  GVariantIter iter;
  g_autoptr(GVariant) properties = NULL;
  g_autofree char *shortcut_id = NULL;

  if (shortcuts != NULL)
    {
      g_variant_iter_init (&iter, shortcuts);
      while (g_variant_iter_next (&iter, "(s@a{sv})", &shortcut_id, &properties))
        {
          if (g_str_equal (shortcut_id, QUAKE_SHORTCUT_ID))
            {
              const char *description = NULL;

              g_variant_lookup (properties, "trigger_description", "&s", &description);
              g_signal_emit (self, signals[CHANGED], 0, description != NULL ? description : "");
              return;
            }

          g_clear_pointer (&shortcut_id, g_free);
          g_clear_pointer (&properties, g_variant_unref);
        }
    }

  g_signal_emit (self, signals[CHANGED], 0, "");
}

static char *
make_token (const char *prefix)
{
  g_autofree char *uuid = g_uuid_string_random ();
  char *token = g_strdup_printf ("%s_%s", prefix, uuid);

  for (char *iter = token; *iter; iter++)
    {
      if (*iter == '-')
        *iter = '_';
    }

  return token;
}

static char *
make_request_path (GDBusConnection *connection,
                   const char      *token)
{
  const char *unique_name = g_dbus_connection_get_unique_name (connection);
  g_autofree char *sender = NULL;

  if (unique_name == NULL)
    return NULL;

  sender = g_strdup (unique_name + 1);
  for (char *iter = sender; *iter; iter++)
    {
      if (*iter == '.')
        *iter = '_';
    }

  return g_strdup_printf ("/org/freedesktop/portal/desktop/request/%s/%s",
                          sender,
                          token);
}

static void
portal_call_cb (GObject      *object,
                GAsyncResult *result,
                gpointer      user_data)
{
  PortalCall *call = user_data;
  PtyxisGlobalShortcuts *self = call->self;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GError) error = NULL;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (object), result, &error);

  if (reply == NULL &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_debug ("Global shortcuts portal call failed: %s", error->message);

      if (self->request == call->request &&
          self->request_serial == call->serial)
        {
          self->request = REQUEST_NONE;

          if (self->response_subscription != 0)
            {
              g_dbus_connection_signal_unsubscribe (self->connection,
                                                    self->response_subscription);
              self->response_subscription = 0;
            }
        }
    }

  g_object_unref (call->self);
  g_free (call);
}

static void
portal_response_cb (GDBusConnection *connection,
                    const char      *sender_name,
                    const char      *object_path,
                    const char      *interface_name,
                    const char      *signal_name,
                    GVariant        *parameters,
                    gpointer         user_data)
{
  PtyxisGlobalShortcuts *self = user_data;
  g_autoptr(GVariant) results = NULL;
  RequestKind request;
  guint response;

  g_assert (PTYXIS_IS_GLOBAL_SHORTCUTS (self));

  request = self->request;
  self->request = REQUEST_NONE;

  if (self->response_subscription != 0)
    {
      g_dbus_connection_signal_unsubscribe (connection, self->response_subscription);
      self->response_subscription = 0;
    }

  g_variant_get (parameters, "(u@a{sv})", &response, &results);
  if (response != 0)
    {
      g_debug ("Global shortcuts portal request was not accepted (response %u)", response);
      return;
    }

  if (request == REQUEST_CREATE_SESSION)
    {
      const char *session_handle = NULL;

      if (!g_variant_lookup (results, "session_handle", "&s", &session_handle))
        return;

      g_free (self->session_handle);
      self->session_handle = g_strdup (session_handle);
      ptyxis_global_shortcuts_list (self);
    }
  else if (request == REQUEST_LIST_SHORTCUTS ||
           request == REQUEST_BIND_SHORTCUTS)
    {
      g_autoptr(GVariant) shortcuts =
        g_variant_lookup_value (results, "shortcuts", G_VARIANT_TYPE ("a(sa{sv})"));

      self->bound = shortcuts_contains_quake (shortcuts);
      shortcuts_emit_changed (self, shortcuts);

      if (request == REQUEST_BIND_SHORTCUTS)
        self->configure_requested = FALSE;
      else if (self->configure_requested)
        ptyxis_global_shortcuts_configure (self, "");
      else if (!self->bound && self->bind_requested)
        ptyxis_global_shortcuts_ensure_bound (self);
    }
}

static void
portal_request (PtyxisGlobalShortcuts *self,
                RequestKind            request,
                const char            *method,
                const char            *token,
                GVariant              *parameters)
{
  g_autofree char *request_path = NULL;
  PortalCall *call;

  g_assert (PTYXIS_IS_GLOBAL_SHORTCUTS (self));
  g_assert (G_IS_DBUS_CONNECTION (self->connection));
  g_assert (self->request == REQUEST_NONE);

  request_path = make_request_path (self->connection, token);
  if (request_path == NULL)
    return;

  self->request = request;
  self->response_subscription =
    g_dbus_connection_signal_subscribe (self->connection,
                                        PORTAL_BUS_NAME,
                                        PORTAL_REQUEST_INTERFACE,
                                        "Response",
                                        request_path,
                                        NULL,
                                        G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE,
                                        portal_response_cb,
                                        self,
                                        NULL);

  call = g_new0 (PortalCall, 1);
  call->self = g_object_ref (self);
  call->request = request;
  call->serial = ++self->request_serial;

  g_dbus_connection_call (self->connection,
                          PORTAL_BUS_NAME,
                          PORTAL_OBJECT_PATH,
                          PORTAL_GLOBAL_SHORTCUTS_INTERFACE,
                          method,
                          parameters,
                          G_VARIANT_TYPE ("(o)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          self->cancellable,
                          portal_call_cb,
                          call);
}

static void
ptyxis_global_shortcuts_list (PtyxisGlobalShortcuts *self)
{
  GVariantBuilder options;
  g_autofree char *token = make_token ("list");

  g_variant_builder_init (&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&options, "{sv}", "handle_token", g_variant_new_string (token));

  portal_request (self,
                  REQUEST_LIST_SHORTCUTS,
                  "ListShortcuts",
                  token,
                  g_variant_new ("(o@a{sv})",
                                 self->session_handle,
                                 g_variant_builder_end (&options)));
}

static void
ptyxis_global_shortcuts_bind (PtyxisGlobalShortcuts *self)
{
  GVariantBuilder options;
  GVariantBuilder shortcut_properties;
  GVariantBuilder shortcuts;
  g_autofree char *token = make_token ("bind");

  g_variant_builder_init (&shortcut_properties, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&shortcut_properties,
                         "{sv}",
                         "description",
                         g_variant_new_string (_("Show or hide the Quake terminal window")));
  g_variant_builder_add (&shortcut_properties,
                         "{sv}",
                         "preferred_trigger",
                         g_variant_new_string ("CTRL+grave"));

  g_variant_builder_init (&shortcuts, G_VARIANT_TYPE ("a(sa{sv})"));
  g_variant_builder_add (&shortcuts,
                         "(s@a{sv})",
                         QUAKE_SHORTCUT_ID,
                         g_variant_builder_end (&shortcut_properties));

  g_variant_builder_init (&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&options, "{sv}", "handle_token", g_variant_new_string (token));

  portal_request (self,
                  REQUEST_BIND_SHORTCUTS,
                  "BindShortcuts",
                  token,
                  g_variant_new ("(o@a(sa{sv})s@a{sv})",
                                 self->session_handle,
                                 g_variant_builder_end (&shortcuts),
                                 "",
                                 g_variant_builder_end (&options)));
}

static void
activated_cb (GDBusConnection *connection,
              const char      *sender_name,
              const char      *object_path,
              const char      *interface_name,
              const char      *signal_name,
              GVariant        *parameters,
              gpointer         user_data)
{
  PtyxisGlobalShortcuts *self = user_data;
  g_autoptr(GVariant) options = NULL;
  const char *activation_token = NULL;
  const char *session_handle;
  const char *shortcut_id;
  guint64 timestamp;

  g_variant_get (parameters,
                 "(&o&st@a{sv})",
                 &session_handle,
                 &shortcut_id,
                 &timestamp,
                 &options);

  if (!g_str_equal (session_handle, self->session_handle) ||
      !g_str_equal (shortcut_id, QUAKE_SHORTCUT_ID))
    return;

  g_variant_lookup (options, "activation_token", "&s", &activation_token);
  g_signal_emit (self, signals[ACTIVATED], 0, activation_token);
}

static void
shortcuts_changed_cb (GDBusConnection *connection,
                      const char      *sender_name,
                      const char      *object_path,
                      const char      *interface_name,
                      const char      *signal_name,
                      GVariant        *parameters,
                      gpointer         user_data)
{
  PtyxisGlobalShortcuts *self = user_data;
  g_autoptr(GVariant) shortcuts = NULL;
  const char *session_handle;

  g_variant_get (parameters, "(&o@a(sa{sv}))", &session_handle, &shortcuts);
  if (g_strcmp0 (session_handle, self->session_handle) == 0)
    shortcuts_emit_changed (self, shortcuts);
}

static void
ptyxis_global_shortcuts_reset_session (PtyxisGlobalShortcuts *self)
{
  g_assert (PTYXIS_IS_GLOBAL_SHORTCUTS (self));

  if (self->response_subscription != 0)
    {
      g_dbus_connection_signal_unsubscribe (self->connection,
                                            self->response_subscription);
      self->response_subscription = 0;
    }

  self->request = REQUEST_NONE;
  self->request_serial++;
  self->bound = FALSE;
  self->started = FALSE;
  g_clear_pointer (&self->session_handle, g_free);
}

static void
portal_owner_changed_cb (GDBusConnection *connection,
                         const char      *sender_name,
                         const char      *object_path,
                         const char      *interface_name,
                         const char      *signal_name,
                         GVariant        *parameters,
                         gpointer         user_data)
{
  PtyxisGlobalShortcuts *self = user_data;
  const char *name;
  const char *old_owner;
  const char *new_owner;

  g_variant_get (parameters, "(&s&s&s)", &name, &old_owner, &new_owner);
  if (!g_str_equal (name, PORTAL_BUS_NAME))
    return;

  /* Sessions and host Registry registrations belong to a particular portal
   * owner. Reset even for a direct old-owner to new-owner transition so the
   * replacement process gets a fresh registration and session. */
  ptyxis_global_shortcuts_reset_session (self);

  if (new_owner[0] == '\0')
    return;

  ptyxis_global_shortcuts_register_app_id (self);
  ptyxis_global_shortcuts_start (self);
}

static void
ptyxis_global_shortcuts_dispose (GObject *object)
{
  PtyxisGlobalShortcuts *self = (PtyxisGlobalShortcuts *)object;

  if (self->cancellable != NULL)
    g_cancellable_cancel (self->cancellable);

  if (self->connection != NULL)
    {
      if (self->response_subscription != 0)
        {
          g_dbus_connection_signal_unsubscribe (self->connection,
                                                self->response_subscription);
          self->response_subscription = 0;
        }

      if (self->activated_subscription != 0)
        {
          g_dbus_connection_signal_unsubscribe (self->connection,
                                                self->activated_subscription);
          self->activated_subscription = 0;
        }

      if (self->changed_subscription != 0)
        {
          g_dbus_connection_signal_unsubscribe (self->connection,
                                                self->changed_subscription);
          self->changed_subscription = 0;
        }

      if (self->portal_owner_subscription != 0)
        {
          g_dbus_connection_signal_unsubscribe (self->connection,
                                                self->portal_owner_subscription);
          self->portal_owner_subscription = 0;
        }
    }

  g_clear_object (&self->connection);
  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (ptyxis_global_shortcuts_parent_class)->dispose (object);
}

static void
ptyxis_global_shortcuts_finalize (GObject *object)
{
  PtyxisGlobalShortcuts *self = (PtyxisGlobalShortcuts *)object;

  g_clear_pointer (&self->application_id, g_free);
  g_clear_pointer (&self->session_handle, g_free);

  G_OBJECT_CLASS (ptyxis_global_shortcuts_parent_class)->finalize (object);
}

static void
ptyxis_global_shortcuts_class_init (PtyxisGlobalShortcutsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = ptyxis_global_shortcuts_dispose;
  object_class->finalize = ptyxis_global_shortcuts_finalize;

  signals[ACTIVATED] =
    g_signal_new ("activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_STRING | G_SIGNAL_TYPE_STATIC_SCOPE);
  signals[CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_STRING | G_SIGNAL_TYPE_STATIC_SCOPE);
}

static void
ptyxis_global_shortcuts_init (PtyxisGlobalShortcuts *self)
{
}

PtyxisGlobalShortcuts *
ptyxis_global_shortcuts_new (const char *application_id)
{
  PtyxisGlobalShortcuts *self;

  g_return_val_if_fail (application_id != NULL, NULL);

  self = g_object_new (PTYXIS_TYPE_GLOBAL_SHORTCUTS, NULL);
  self->application_id = g_strdup (application_id);

  return self;
}

static void
ptyxis_global_shortcuts_register_app_id (PtyxisGlobalShortcuts *self)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = NULL;

  g_return_if_fail (PTYXIS_IS_GLOBAL_SHORTCUTS (self));
  g_return_if_fail (G_IS_DBUS_CONNECTION (self->connection));

  /* Newer host portals require unsandboxed applications to associate their
   * D-Bus peer with an installed desktop application before any portal call.
   * Older portals simply do not implement this interface. */
  reply = g_dbus_connection_call_sync (
    self->connection,
    PORTAL_BUS_NAME,
    PORTAL_OBJECT_PATH,
    PORTAL_HOST_REGISTRY_INTERFACE,
    "Register",
    g_variant_new ("(s@a{sv})",
                   self->application_id,
                   g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0)),
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    self->cancellable,
    &error);

  if (reply == NULL)
    g_debug ("Could not register application ID with the host portal: %s",
             error->message);
}

void
ptyxis_global_shortcuts_register (PtyxisGlobalShortcuts *self)
{
  g_autofree char *address = NULL;
  g_autoptr(GError) error = NULL;

  g_return_if_fail (PTYXIS_IS_GLOBAL_SHORTCUTS (self));
  g_return_if_fail (self->connection == NULL);

  self->cancellable = g_cancellable_new ();

  address = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SESSION,
                                             self->cancellable,
                                             &error);
  if (address == NULL)
    {
      g_debug ("Failed to get session bus address for global shortcuts: %s",
               error->message);
      return;
    }

  /* Use a private connection because Registry.Register() associates the
   * calling D-Bus peer with exactly one application ID. The shared connection
   * returned by g_bus_get_sync() may already have made unrelated portal calls. */
  self->connection =
    g_dbus_connection_new_for_address_sync (
      address,
      G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
      G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
      NULL,
      self->cancellable,
      &error);
  if (self->connection == NULL)
    {
      g_debug ("Failed to open session bus connection for global shortcuts: %s",
               error->message);
      return;
    }

  self->portal_owner_subscription =
    g_dbus_connection_signal_subscribe (self->connection,
                                        "org.freedesktop.DBus",
                                        "org.freedesktop.DBus",
                                        "NameOwnerChanged",
                                        "/org/freedesktop/DBus",
                                        PORTAL_BUS_NAME,
                                        G_DBUS_SIGNAL_FLAGS_NONE,
                                        portal_owner_changed_cb,
                                        self,
                                        NULL);

  ptyxis_global_shortcuts_register_app_id (self);
}

void
ptyxis_global_shortcuts_start (PtyxisGlobalShortcuts *self)
{
  GVariantBuilder options;
  g_autofree char *handle_token = NULL;
  g_autofree char *session_token = NULL;

  g_return_if_fail (PTYXIS_IS_GLOBAL_SHORTCUTS (self));

  if (self->started || self->connection == NULL)
    return;

  self->started = TRUE;
  /* A newly-created session has no in-memory shortcuts yet.  Ask the
   * portal to restore the application's persisted bindings through
   * BindShortcuts after ListShortcuts completes. */
  self->bind_requested = TRUE;
  if (self->activated_subscription == 0)
    {
      self->activated_subscription =
      g_dbus_connection_signal_subscribe (self->connection,
                                          PORTAL_BUS_NAME,
                                          PORTAL_GLOBAL_SHORTCUTS_INTERFACE,
                                          "Activated",
                                          PORTAL_OBJECT_PATH,
                                          NULL,
                                          G_DBUS_SIGNAL_FLAGS_NONE,
                                          activated_cb,
                                          self,
                                          NULL);
      self->changed_subscription =
        g_dbus_connection_signal_subscribe (self->connection,
                                            PORTAL_BUS_NAME,
                                            PORTAL_GLOBAL_SHORTCUTS_INTERFACE,
                                            "ShortcutsChanged",
                                            PORTAL_OBJECT_PATH,
                                            NULL,
                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                            shortcuts_changed_cb,
                                            self,
                                            NULL);
    }

  handle_token = make_token ("create");
  session_token = make_token ("quake");
  g_variant_builder_init (&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&options, "{sv}", "handle_token", g_variant_new_string (handle_token));
  g_variant_builder_add (&options, "{sv}", "session_handle_token", g_variant_new_string (session_token));

  portal_request (self,
                  REQUEST_CREATE_SESSION,
                  "CreateSession",
                  handle_token,
                  g_variant_new ("(@a{sv})", g_variant_builder_end (&options)));
}

void
ptyxis_global_shortcuts_ensure_bound (PtyxisGlobalShortcuts *self)
{
  g_return_if_fail (PTYXIS_IS_GLOBAL_SHORTCUTS (self));

  self->bind_requested = TRUE;

  if (self->session_handle != NULL &&
      self->request == REQUEST_NONE &&
      !self->bound)
    ptyxis_global_shortcuts_bind (self);
}

void
ptyxis_global_shortcuts_configure (PtyxisGlobalShortcuts *self,
                                   const char            *parent_window)
{
  GVariantBuilder options;

  g_return_if_fail (PTYXIS_IS_GLOBAL_SHORTCUTS (self));

  self->configure_requested = TRUE;
  if (self->connection == NULL ||
      self->session_handle == NULL ||
      self->request != REQUEST_NONE)
    return;

  if (!self->bound)
    {
      ptyxis_global_shortcuts_ensure_bound (self);
      return;
    }

  self->configure_requested = FALSE;
  g_variant_builder_init (&options, G_VARIANT_TYPE_VARDICT);
  g_dbus_connection_call (self->connection,
                          PORTAL_BUS_NAME,
                          PORTAL_OBJECT_PATH,
                          PORTAL_GLOBAL_SHORTCUTS_INTERFACE,
                          "ConfigureShortcuts",
                          g_variant_new ("(os@a{sv})",
                                         self->session_handle,
                                         parent_window != NULL ? parent_window : "",
                                         g_variant_builder_end (&options)),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          self->cancellable,
                          NULL,
                          NULL);
}
