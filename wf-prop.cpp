/*******************************************************************
 * This file is licensed under the MIT license.
 * Copyright (C) 2019 - 2020 Damian Ivanov <damianatorrpm@gmail.com>
 ********************************************************************/

#define DBUS_ID "org.wayland.compositor"
#define DBUS_PATH "/org/wayland/compositor"

extern "C"
{
#include <gio/gio.h>
};

#include <iostream>
#include <string>
#include <vector>
#include <giomm/dbusproxy.h>
#include <giomm/dbusconnection.h>

using DBusConnection = Glib::RefPtr<Gio::DBus::Connection>;
using DBusProxy = Glib::RefPtr<Gio::DBus::Proxy>;

/*
 * Print a list of query_view_taskman_ids and output them as
 * <id>       <app_id>       <title>
 */ 
static gboolean list = FALSE;
static DBusConnection connection;
static DBusProxy proxy;

static std::vector<std::pair<int, int>> query_view_workspaces(guint view_id)
{
  std::vector<std::pair<int, int>> workspaces;

  GError *error = NULL;
  GVariant *tmp = NULL;
  int x, y;

  tmp = g_dbus_proxy_call_sync(proxy->gobj(),
                               "query_view_workspaces",
                               g_variant_new("(u)", view_id),
                               G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

  g_assert_no_error(error);

  GVariantIter iter;
  GVariantIter iter2;
  GVariantIter iter3;
  GVariant *child;
  GVariant *cchild;
  GVariant *ccchild;

  g_variant_iter_init(&iter, tmp);

  while ((child = g_variant_iter_next_value(&iter)))
  {
    g_variant_iter_init(&iter2, child);
    while ((cchild = g_variant_iter_next_value(&iter2)))
    {
      g_variant_get_child(cchild, 0, "i", &x);
      g_variant_get_child(cchild, 1, "i", &y);
      g_debug("view workspaces: %i %i", x, y);
      workspaces.push_back(std::make_pair(x, y));
    }
  }
  g_variant_unref(tmp);

  if (workspaces.size() == 0)
  {
    g_warning("No workspaces found for view: %u", view_id);
  }
  return workspaces;
}

static uint query_view_output(guint view_id)
{
  GError *error = NULL;
  GVariant *tmp = NULL;
  uint value = 0;
  tmp = g_dbus_proxy_call_sync(proxy->gobj(),
                               "query_view_output",
                               g_variant_new("(u)", view_id),
                               G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  g_assert_no_error(error);
  g_variant_get(tmp, "(u)", &value);
  g_variant_unref(tmp);

  return value;
}

static gchar *query_output_name(guint output_id)
{
  GError *error = NULL;
  GVariant *tmp = NULL;
  gchar *value = NULL;

  tmp = g_dbus_proxy_call_sync(proxy->gobj(),
                               "query_output_name",
                               g_variant_new("(u)", output_id),
                               G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  g_assert(error == NULL);
  g_variant_get(tmp, "(s)", &value);
  g_variant_unref(tmp);

  return value;
}

static std::pair<int, int> query_output_workspace(guint output_id)
{
  GError *error = NULL;
  GVariant *tmp = NULL;
  uint x = 0;
  uint y = 0;

  tmp = g_dbus_proxy_call_sync(proxy->gobj(),
                               "query_output_workspace",
                               g_variant_new("(u)", output_id),
                               G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  g_assert(error == NULL);
  g_variant_get(tmp, "(uu)", &x, &y);
  g_variant_unref(tmp);

  return std::pair<int, int>{x, y};
}

static uint query_active_output()
{
  GError *error = NULL;
  GVariant *tmp = NULL;
  uint value = 0;

  tmp = g_dbus_proxy_call_sync(proxy->gobj(),
                               "query_active_output", NULL,
                               G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  g_assert(error == NULL);
  g_variant_get(tmp, "(u)", &value);
  g_variant_unref(tmp);

  return value;
}

static uint query_view_role(uint view_id)
{
  GError *error = NULL;
  GVariant *tmp = NULL;
  uint value = 0;

  tmp = g_dbus_proxy_call_sync(proxy->gobj(),
                               "query_view_role",
                               g_variant_new("(u)", view_id),
                               G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  g_assert(error == NULL);
  g_variant_get(tmp, "(u)", &value);
  g_variant_unref(tmp);

  return value;
}

static gchar *query_view_app_id(uint view_id)
{
  GError *error = NULL;
  GVariant *tmp = NULL;
  gchar *value = NULL;

  tmp = g_dbus_proxy_call_sync(proxy->gobj(),
                               "query_view_app_id",
                               g_variant_new("(u)", view_id),
                               G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  g_assert(error == NULL);
  g_variant_get(tmp, "(s)", &value);
  g_variant_unref(tmp);

  return value;
}

static gchar *query_view_app_id_gtk_shell(uint view_id)
{
  GError *error = NULL;
  GVariant *tmp = NULL;
  gchar *value = NULL;

  tmp = g_dbus_proxy_call_sync(proxy->gobj(),
                               "query_view_app_id_gtk_shell",
                               g_variant_new("(u)", view_id),
                               G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  g_assert(error == NULL);
  g_variant_get(tmp, "(s)", &value);
  g_variant_unref(tmp);

  return value;
}

static gchar *query_view_app_id_xwayland_net_wm_name(uint view_id)
{
  GError *error = NULL;
  GVariant *tmp = NULL;
  gchar *value = NULL;

  tmp = g_dbus_proxy_call_sync(proxy->gobj(),
                               "query_view_app_id_xwayland_net_wm_name", 
                               g_variant_new("(u)", view_id),
                               G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  g_assert(error == NULL);
  g_variant_get(tmp, "(s)", &value);
  g_variant_unref(tmp);

  return value;
}

static gchar *query_view_title(uint view_id)
{
  GError *error = NULL;
  GVariant *tmp = NULL;
  gchar *value = NULL;

  tmp = g_dbus_proxy_call_sync(proxy->gobj(),
                               "query_view_title", g_variant_new("(u)", view_id),
                               G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  g_assert(error == NULL);
  g_variant_get(tmp, "(s)", &value);
  g_variant_unref(tmp);

  return value;
}

static int query_view_xwayland_atom_cardinal(uint view_id, const char *val)
{
  GError *error = NULL;
  GVariant *tmp = NULL;
  int value;

  tmp = g_dbus_proxy_call_sync(proxy->gobj(),
                               "query_view_xwayland_atom_cardinal",
                               g_variant_new("(us)", view_id, val),
                               G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  g_assert(error == NULL);
  g_variant_get(tmp, "(u)", &value);
  g_variant_unref(tmp);

  return value;
}

static void on_signal(GDBusConnection *connection, const gchar *sender_name,
                      const gchar *object_path, const gchar *interface_name,
                      const gchar *signal_name, GVariant *parameters,
                      gpointer user_data)
{
  uint view_id;
  g_variant_get(parameters, "(u)", &view_id);
  gchar* app_id = query_view_app_id(view_id);
  g_print("App Id:            %s\n", app_id);

  g_dbus_proxy_call_sync(proxy->gobj(),
                         "enable_property_mode",
                         g_variant_new("(b)", FALSE),
                         G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
  exit(0);                         
};
static GOptionEntry entries[] =
    {
        {"list", 'l', 0, G_OPTION_ARG_NONE, &list, "List taskmanager related entries and exit", NULL},
        {NULL}};

int main(int argc, char *argv[])
{
  GError *error = NULL;
  GOptionContext *context;
  GMainLoop *loop;

  context = g_option_context_new("- get window properties");
  g_option_context_add_main_entries(context, entries, NULL);

  if (!g_option_context_parse(context, &argc, &argv, &error))
  {
    g_print("%s\n", error->message);
    exit(1);
  }
  g_option_context_free (context);
  loop = g_main_loop_new (NULL, FALSE);

    std::cerr << "connect to dbus" << std::endl;
    g_main_loop_run (loop);

  auto cancellable = Gio::Cancellable::create();
  connection = Gio::DBus::Connection::get_sync(Gio::DBus::BUS_TYPE_SESSION,
                                               cancellable);
  
    g_main_loop_run (loop);

  if (!connection)
  {
    std::cerr << "Failed to connect to dbus" << std::endl;
    return false;
  }
    std::cerr << "connect to dbus done" << std::endl;

  proxy = Gio::DBus::Proxy::create_sync(connection,
                                        DBUS_ID,
                                        DBUS_PATH,
                                        DBUS_ID);
  if (!proxy)
  {
    std::cerr << "Failed to connect to dbus interface" << std::endl;
    return false;
  }

  g_dbus_connection_signal_subscribe(connection->gobj(),
                                     DBUS_ID,
                                     DBUS_ID,
                                     "view_pressed",
                                     DBUS_PATH,
                                     NULL, /* match rule */
                                     G_DBUS_SIGNAL_FLAGS_NONE,
                                     on_signal,
                                     NULL, /* user data */
                                     NULL);

  g_dbus_proxy_call_sync(proxy->gobj(),
                         "enable_property_mode",
                         g_variant_new("(b)", TRUE),
                         G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  g_main_loop_run (loop);

  g_main_loop_unref (loop);

  exit(1);
}
