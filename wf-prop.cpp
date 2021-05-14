/*******************************************************************
 * This file is licensed under the MIT license.
 * Copyright (C) 2019 - 2020 Damian Ivanov <damianatorrpm@gmail.com>
 ********************************************************************/

#define DBUS_ID "org.wayland.compositor"
#define DBUS_PATH "/org/wayland/compositor"

#include <gio/gio.h>
#include <giomm/application.h>
#include <giomm/dbusconnection.h>
#include <giomm/dbusproxy.h>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using DBusConnection = Glib::RefPtr<Gio::DBus::Connection>;
using DBusProxy = Glib::RefPtr<Gio::DBus::Proxy>;

/*
 * Print a list of query_view_taskman_ids and output them as
 * <id>       <app_id>       <title>
 */
static gboolean list = FALSE;
static DBusConnection connection;
static DBusProxy proxy;

static std::vector<std::pair<int, int>>
query_view_workspaces (guint view_id)
{
    std::vector<std::pair<int, int>> workspaces;

    GError* error = NULL;
    GVariant* tmp = NULL;
    int x, y;

    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_view_workspaces",
                                 g_variant_new("(u)", view_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

    g_assert_no_error(error);

    GVariantIter iter;
    GVariantIter iter2;
    GVariantIter iter3;
    GVariant* child;
    GVariant* cchild;
    GVariant* ccchild;

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

    if (workspaces.size() == 0) {
        g_warning("No workspaces found for view: %u", view_id);
    }

    return workspaces;
}

static uint
query_view_output (guint view_id)
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    uint value = 0;
    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_view_output",
                                 g_variant_new("(u)", view_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error(error);
    g_variant_get(tmp, "(u)", &value);
    g_variant_unref(tmp);

    return value;
}

static int
query_view_below_view (guint view_id)
{
    GVariant* tmp = NULL;
    int value = 0;
    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_view_below_view",
                                 g_variant_new("(u)", view_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    g_variant_get(tmp, "(i)", &value);
    g_variant_unref(tmp);

    return value;
}

static int
query_view_above_view (guint view_id)
{
    GVariant* tmp = NULL;
    int value = 0;
    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_view_above_view",
                                 g_variant_new("(u)", view_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    g_variant_get(tmp, "(i)", &value);
    g_variant_unref(tmp);

    return value;
}

static gboolean
query_view_minimized (guint view_id)
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    gboolean value;
    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_view_minimized",
                                 g_variant_new("(u)", view_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

    g_assert_no_error(error);
    g_variant_get(tmp, "(b)", &value);
    g_variant_unref(tmp);

    return value;
}

static gboolean
query_view_maximized (guint view_id)
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    gboolean value;
    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_view_maximized",
                                 g_variant_new("(u)", view_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

    g_assert_no_error(error);
    g_variant_get(tmp, "(b)", &value);
    g_variant_unref(tmp);

    return value;
}

static gboolean
query_view_fullscreen (guint view_id)
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    gboolean value;
    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_view_fullscreen",
                                 g_variant_new("(u)", view_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

    g_assert_no_error(error);
    g_variant_get(tmp, "(b)", &value);
    g_variant_unref(tmp);

    return value;
}

static gchar*
query_output_name (guint output_id)
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    gchar* value = NULL;

    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_output_name",
                                 g_variant_new("(u)", output_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error(error);
    g_variant_get(tmp, "(s)", &value);
    g_variant_unref(tmp);

    return value;
}

static std::pair<int, int>
query_output_workspace (guint output_id)
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    uint x = 0;
    uint y = 0;

    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_output_workspace",
                                 g_variant_new("(u)", output_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error(error);
    g_variant_get(tmp, "(uu)", &x, &y);
    g_variant_unref(tmp);

    return std::pair<int, int>{x, y};
}

static uint
query_active_output ()
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    uint value = 0;

    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_active_output", NULL,
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error(error);
    g_variant_get(tmp, "(u)", &value);
    g_variant_unref(tmp);

    return value;
}

static uint
query_view_role (uint view_id)
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    uint value = 0;

    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_view_role",
                                 g_variant_new("(u)", view_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error(error);
    g_variant_get(tmp, "(u)", &value);
    g_variant_unref(tmp);

    return value;
}

static uint
query_view_group_leader (uint view_id)
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    uint value;

    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_view_group_leader",
                                 g_variant_new("(u)", view_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error(error);
    g_variant_get(tmp, "(u)", &value);
    g_variant_unref(tmp);

    return value;
}

static gchar*
query_view_app_id (uint view_id)
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    gchar* value = NULL;

    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_view_app_id",
                                 g_variant_new("(u)", view_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error(error);
    g_variant_get(tmp, "(s)", &value);
    g_variant_unref(tmp);

    return value;
}

static gchar*
query_view_app_id_gtk_shell (uint view_id)
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    gchar* value = NULL;

    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_view_app_id_gtk_shell",
                                 g_variant_new("(u)", view_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error(error);
    g_variant_get(tmp, "(s)", &value);
    g_variant_unref(tmp);

    return value;
}

static gchar*
query_view_app_id_xwayland_net_wm_name (uint view_id)
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    gchar* value = NULL;

    tmp = g_dbus_proxy_call_sync(
        proxy->gobj(), "query_view_app_id_xwayland_net_wm_name",
        g_variant_new("(u)", view_id), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error(error);
    g_variant_get(tmp, "(s)", &value);
    g_variant_unref(tmp);

    return value;
}

static gchar*
query_view_title (uint view_id)
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    gchar* value = NULL;

    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_view_title",
                                 g_variant_new("(u)", view_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error(error);
    g_variant_get(tmp, "(s)", &value);
    g_variant_unref(tmp);

    return value;
}

static int
query_view_xwayland_atom_cardinal (uint view_id, const char* val)
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    int value;

    tmp =
        g_dbus_proxy_call_sync(proxy->gobj(), "query_view_xwayland_atom_cardinal",
                               g_variant_new("(us)", view_id, val),
                               G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error(error);
    g_variant_get(tmp, "(u)", &value);
    g_variant_unref(tmp);

    return value;
}

static gboolean
query_view_active (guint view_id)
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    gboolean value;
    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_view_active",
                                 g_variant_new("(u)", view_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

    g_assert_no_error(error);
    g_variant_get(tmp, "(b)", &value);
    g_variant_unref(tmp);

    return value;
}

static uint
query_view_xwayland_wid (uint view_id)
{
    GError* error = NULL;
    GVariant* tmp = NULL;
    uint value = 0;

    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_view_xwayland_wid",
                                 g_variant_new("(u)", view_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error(error);
    g_variant_get(tmp, "(u)", &value);
    g_variant_unref(tmp);

    return value;
}

static void
print_view_data (guint view_id)
{
    // XXX: is there any point to free memory in such a short lived program?
    gchar* app_id = query_view_app_id(view_id);
    gchar* app_id_gtk = query_view_app_id_gtk_shell(view_id);
    gchar* title = query_view_title(view_id);
    gboolean minimized = query_view_minimized(view_id);
    gboolean maximized = query_view_maximized(view_id);
    gboolean fullscreened = query_view_fullscreen(view_id);
    gboolean active = query_view_active(view_id);
    guint above_id = query_view_above_view(view_id);
    guint below_id = query_view_below_view(view_id);
    gchar* above_app_id = query_view_app_id(above_id);
    gchar* below_app_id = query_view_app_id(below_id);
    guint output = query_view_output(view_id);
    gchar* output_name = query_output_name(output);
    guint xwid = query_view_xwayland_wid(view_id);
    guint role = query_view_role(view_id);
    guint group_leader = query_view_group_leader(view_id);

    std::vector<std::pair<int, int>> workspaces = query_view_workspaces(view_id);
    GError* error = NULL;
    GVariant* tmp = NULL;
    gint pid;
    guint uid;
    guint gid;
    tmp = g_dbus_proxy_call_sync(proxy->gobj(), "query_view_credentials",
                                 g_variant_new("(u)", view_id),
                                 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error(error);
    g_variant_get(tmp, "(iuu)", &pid, &uid, &gid);
    g_variant_unref(tmp);

    g_print("View Id:           %u\n", view_id);
    g_print("App Id:            [%s, %s]\n", app_id, app_id_gtk);
    g_print("Title:             %s\n", title);
    if (role == 1) {
        g_print("Role:              %s\n", "Toplevel Window");
    }
    else
    if (role == 2)
    {
        g_print("Role:              %s\n", "Desktop Environment");
    }
    else
    if (role == 3)
    {
        g_print("Role:              %s\n", "Unmanaged Window");
    }
    else
    {
        g_print("Role:              %s\n", "Unknown");
    }

    g_print("Group Leader:      %i\n", group_leader);
    g_print("Process id:        %i\n", pid);
    g_print("User id:           %u\n", uid);
    g_print("Group id:          %u\n", gid);
    g_print("Active:            %s\n", (active ? "True" : "False"));
    g_print("Minimized:         %s\n", (minimized ? "True" : "False"));
    g_print("Maximized:         %s\n", (maximized ? "True" : "False"));
    g_print("Fullscreen:        %s\n", (fullscreened ? "True" : "False"));
    g_print("Workspaces:        ");

    for (auto it = workspaces.begin(); it != workspaces.end(); ++it)
    {
        std::pair<int, int> ws = *it;
        g_print("[%i, %i]", ws.first, ws.second);
    }

    g_print("\n");
    g_print("Output:            [%u] %s\n", output, output_name);
    g_print("Above this view:   [%i] %s\n", above_id,
            (above_id != -1 ? above_app_id : "None"));
    g_print("Below this view:   [%i] %s\n", below_id,
            (below_id != -1 ? below_app_id : "None"));

    if (xwid == 0) {
        g_print("\n == This is a native wayland window ==\n");
    }
    else
    {
        g_print("\n == This is a xwayland window ==\n\n");
        std::stringstream stream;
        stream << std::hex << xwid;
        std::string result("0x" + stream.str());
        g_print("X Window id:       %s\n", result.c_str());
        g_print("Run xwininfo -all -id %s and/or xprop -id %s for more "
                "information.\n",
                result.c_str(), result.c_str());
    }
}

static void
on_signal (GDBusConnection* connection, const gchar* sender_name,
           const gchar* object_path, const gchar* interface_name,
           const gchar* signal_name, GVariant* parameters,
           gpointer user_data)
{
    uint view_id;
    g_variant_get(parameters, "(u)", &view_id);
    uint _tmp = query_view_role(view_id);

    if ((_tmp != 1) && (_tmp != 2))
    {
        g_print("This surface is part of the desktop/compositor. Role: %u\n", _tmp);
        g_print("Please select another one.\n");

        return;
    }

    g_dbus_proxy_call_sync(proxy->gobj(), "enable_property_mode",
                           g_variant_new("(b)", FALSE), G_DBUS_CALL_FLAGS_NONE,
                           -1, NULL, NULL);
    print_view_data(view_id);
    exit(0);
}

static GOptionEntry entries [] = {
    {"list", 'l', 0, G_OPTION_ARG_NONE, &list,
        "List taskmanager related entries and exit",
        NULL},
    {NULL}
};

int
main (int argc, char* argv [])
{
    GError* error = NULL;
    GOptionContext* context;
    GMainLoop* loop;

    context = g_option_context_new("- get window properties");
    g_option_context_add_main_entries(context, entries, NULL);

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_print("%s\n", error->message);
        exit(1);
    }

    g_option_context_free(context);
    loop = g_main_loop_new(NULL, FALSE);

    Glib::RefPtr<Gio::Application> app = Gio::Application::create(
        "org.wayfire.wf-prop", Gio::APPLICATION_FLAGS_NONE);

    auto cancellable = Gio::Cancellable::create();
    connection =
        Gio::DBus::Connection::get_sync(Gio::DBus::BUS_TYPE_SESSION, cancellable);

    // g_main_loop_run(loop);

    if (!connection) {
        g_error("Failed to connect to dbus");

        return false;
    }

    proxy =
        Gio::DBus::Proxy::create_sync(connection, DBUS_ID, DBUS_PATH, DBUS_ID);
    if (!proxy) {
        g_error("Failed to connect to dbus interface");

        return false;
    }

    for (char** arg = argv; *arg; arg++)
    {
        if ((g_strcmp0(*arg, "l") == 0) || (g_strcmp0(*arg, "list") == 0)) {
            GVariant* value;
            GError* error = NULL;

            value = g_dbus_proxy_call_sync(proxy->gobj(),
                                           "query_view_vector_taskman_ids", NULL,
                                           G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
            g_assert_no_error(error);
            GVariantIter iter;
            GVariantIter iter2;
            GVariant* child;
            GVariant* cchild;

            g_variant_iter_init(&iter, value);
            while ((child = g_variant_iter_next_value(&iter)))
            {
                g_variant_iter_init(&iter2, child);
                while ((cchild = g_variant_iter_next_value(&iter2)))
                {
                    g_print("***************************************\n");
                    print_view_data(g_variant_get_uint32(cchild));
                    g_print("***************************************\n\n");
                }
            }

            g_dbus_proxy_call_sync(proxy->gobj(), "enable_property_mode",
                                   g_variant_new("(b)", FALSE),
                                   G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
            g_assert_no_error(error);
            exit(0);
        }
    }

    g_dbus_connection_signal_subscribe(
        connection->gobj(), DBUS_ID, DBUS_ID, "view_pressed", DBUS_PATH,
        NULL, /* match rule */
        G_DBUS_SIGNAL_FLAGS_NONE, on_signal, NULL, /* user data */
        NULL);

    g_dbus_proxy_call_sync(proxy->gobj(), "enable_property_mode",
                           g_variant_new("(b)", TRUE), G_DBUS_CALL_FLAGS_NONE, -1,
                           NULL, &error);
    g_main_loop_run(loop);
}
