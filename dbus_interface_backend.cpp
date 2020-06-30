extern "C"
{
#define class class_t
#define static
#include <wlr/xwayland.h>
#include <X11/Xatom.h>
#include <xcb/xcb.h>
#include <xcb/res.h>
//#include <xwayland/xwm.h>
#undef static
#undef class
#include <gio/gio.h>
#include <sys/socket.h>
#include <wlr/types/wlr_idle.h>
};

#include <ctime>
#include <iostream>
#include <set>
#include <list>

#include <unistd.h>
#include <functional>

#include <wayfire/output.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/plugins/common/view-change-viewport-signal.hpp>
#include <wayfire/core.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/view.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/compositor-view.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util.hpp>
#include <wayfire/view-access-interface.hpp>

//TODO: Is this the best approach for the lifetime of these objects?
GDBusNodeInfo *introspection_data = nullptr; //set fom introspection_xml during acquire_bus()
GMainContext *wf_context;
GMainLoop *wf_event_loop;

GMainContext *dbus_context;
GMainLoop *dbus_event_loop;
GThread *dbus_thread;
guint owner_id;
GDBusConnection *dbus_connection;
std::vector<wf::output_t *> wf_outputs = wf::get_core().output_layout->get_outputs();
std::set<wf::output_t *> connected_wf_outputs;

// std::vector<nonstd::observer_ptr<wf::sublayer_t>> sticky_layers;

uint focused_view_id;
// HAAACK
uint queued_view_id = 0;
bool queued_view_param = false;

static nonstd::observer_ptr<wf::view_interface_t>
get_view_from_view_id(uint view_id)
{
    std::vector<nonstd::observer_ptr<wf::view_interface_t>> view_vector = wf::get_core().get_all_views();

    //there is no view_id 0 use it as get_active_view(hint)
    if (view_id == 0)
    {
        nonstd::observer_ptr<wf::view_interface_t> view = wf::get_core().get_cursor_focus_view();
        if (view->role == wf::VIEW_ROLE_TOPLEVEL && view->is_mapped())
            return view;
    }
    for (auto it = begin(view_vector); it != end(view_vector); ++it)
    {
        if (it->get()->get_id() == view_id)
        {
            return it->get();
        }
    }
    return nullptr;
}

static wf::output_t *
get_output_from_output_id(uint output_id)
{
    std::vector<wf::output_t *> wf_outputs = wf::get_core().output_layout->get_outputs();

    for (wf::output_t *wf_output : wf_outputs)
    {
        if (wf_output->get_id() == output_id)
        {
            return wf_output;
        }
    }

    return nullptr;
}
static void local_thread_request_view_sticky(void *data)
{
    wayfire_view view = get_view_from_view_id(queued_view_id);

    if (!view || view == nullptr || view->role != wf::VIEW_ROLE_TOPLEVEL)
    {
        return;
    }
    wf::output_t *output = view->get_output();
    // auto sticky_layer = get_sticky_layer_from_output_id(output->get_id());

    if (queued_view_param)
    {
        output->workspace->add_view_to_sublayer(view, output->workspace->above_layer);
        view->desired_layer = 1;
    }
    else
    {
        output->workspace->add_view(view,
                                    (wf::layer_t)output->workspace->get_view_layer(view));
        view->desired_layer = 2;
    }
    LOG(wf::log::LOG_LEVEL_ERROR, "make view sticky: ",
        view->get_id(),
        " desired_layer should be", view->desired_layer);

    GVariant *signal_data;
    signal_data = g_variant_new("(uu)", view->get_id(),
                                view->desired_layer);
    g_dbus_connection_emit_signal(dbus_connection,
                                  nullptr,
                                  "/org/wayland/compositor",
                                  "org.wayland.compositor",
                                  "view_layer_changed",
                                  signal_data,
                                  nullptr);
}

static void local_thread_minimize(void *data)
{
    guint view_id;
    guint action;

    g_variant_get((GVariant *)data, "(uu)", &view_id, &action);
    wayfire_view view = get_view_from_view_id(view_id);

    if (view != nullptr)
    {
        std::cerr << "not null view Gvariant22" << std::endl;
        if (action == 0 && view->minimized)
            view->minimize_request(false);

        else if (action == 1 && !view->minimized)
            view->minimize_request(true);

        else if (action == 2)
            view->minimize_request(!view->minimized);
    }

    g_variant_unref((GVariant *)data);
}
static void local_thread_maximize(void *data)
{
    guint view_id;
    guint action;
    g_variant_get((GVariant *)data, "(uu)", &view_id, &action);
    wayfire_view view = get_view_from_view_id(view_id);

    if (view != nullptr)
    {

        if (action == 0)
            view->tile_request(0);

        else if (action == 1)
            view->tile_request(wf::TILED_EDGES_ALL);

        else if (action == 2)
        {
            if (view->tiled_edges == wf::TILED_EDGES_ALL)
                view->tile_request(0);
            else
                view->tile_request(wf::TILED_EDGES_ALL);
        }
    }
    g_variant_unref((GVariant *)data);
}
static void local_thread_fullscreen(void *data)
{
    guint view_id;
    guint action;
    g_variant_get((GVariant *)data, "(uu)", &view_id, &action);
    wayfire_view view = get_view_from_view_id(view_id);

    wf::output_t *out = wf::get_core().get_active_output();
    if (view != nullptr)
    {
        if (action == 0)
            view->fullscreen_request(out, false);

        else if (action == 1)
            view->fullscreen_request(out, true);

        else if (action == 2)
            view->fullscreen_request(out, !view->fullscreen);
    }
    g_variant_unref((GVariant *)data);
}

static void local_thread_view_focus(void *data)
{
    guint view_id;
    guint action;
    g_variant_get((GVariant *)data, "(uu)", &view_id, &action);
    wayfire_view view = get_view_from_view_id(view_id);

    if (view != nullptr)
    {
        if (action == 0)
        {
            view->set_activated(false);
        }

        else if (action == 1)
            view->focus_request();
    }

    //wf::get_core().set_active_view(view); // Does not brint it to front

    g_variant_unref((GVariant *)data);
}
static void local_thread_change_view_minimize_hint(void *data)
{
    guint view_id;
    guint x;
    guint y;
    guint width;
    guint height;

    g_variant_get((GVariant *)data, "(uuuuu)", &view_id, &x, &y, &width, &height);
    wayfire_view view = get_view_from_view_id(view_id);
    wlr_box hint = {x, y, width, height};

    if (view != nullptr)
    {
        view->set_minimize_hint(hint);
    }
    //wf::get_core().set_active_view(view); // Does not brint it to front

    g_variant_unref((GVariant *)data);
}
static void local_thread_view_close(void *data)
{
    guint view_id;
    g_variant_get((GVariant *)data, "(u)", &view_id);
    wayfire_view view = get_view_from_view_id(view_id);
    if (view != nullptr)
        view->close();

    //wf::get_core().set_active_view(view); // Does not brint it to front

    g_variant_unref((GVariant *)data);
}

static void local_thread_change_view_workspace(void *data)
{
    guint view_id;
    guint new_workspace_x = 0;
    guint new_workspace_y = 0;
    g_variant_get((GVariant *)data, "(uuu)", &view_id, &new_workspace_x, &new_workspace_y);
    wayfire_view view = get_view_from_view_id(view_id);
    wf::point_t new_workspace_coord = {new_workspace_x, new_workspace_y};

    if (view != nullptr)
        view->get_output()->workspace->move_to_workspace(view, new_workspace_coord);

    g_variant_unref((GVariant *)data);
}

static void local_thread_change_view_output(void *data)
{
    guint view_id;
    guint output_id;
    g_variant_get((GVariant *)data, "(uu)", &view_id, &output_id);
    wayfire_view view = get_view_from_view_id(view_id);
    wf::output_t *output = get_output_from_output_id(output_id);

    if (view != nullptr && output != nullptr)
        wf::get_core().move_view_to_output(view, output);

    g_variant_unref((GVariant *)data);
}

static void local_thread_change_workspace_output(void *data)
{
    guint output_id;
    guint new_workspace_x;
    guint new_workspace_y;
    g_variant_get((GVariant *)data, "(uuu)", &output_id, &new_workspace_x, &new_workspace_y);
    wf::output_t *output = get_output_from_output_id(output_id);
    wf::point_t new_workspace_coord = {new_workspace_x, new_workspace_y};

    if (output != nullptr)
        output->workspace->request_workspace(new_workspace_coord); // Provides animation if available

    g_variant_unref((GVariant *)data);
}

static void local_thread_change_workspace_all_outputs(void *data)
{
    guint new_workspace_x;
    guint new_workspace_y;
    g_variant_get((GVariant *)data, "(uu)", &new_workspace_x, &new_workspace_y);
    wf::point_t new_workspace_coord = {new_workspace_x, new_workspace_y};

    std::vector<wf::output_t *> wf_outputs = wf::get_core().output_layout->get_outputs();

    for (wf::output_t *output : wf_outputs)
    {
        output->workspace->request_workspace(new_workspace_coord);
    }

    g_variant_unref((GVariant *)data);
}

/*
 It is a deliberate design choice to have methods / signals instead of properties 
 !!Not all clients fully support automatic property change notifcations!!
 */
const gchar introspection_xml[] =
    "<node>"
    "  <interface name='org.wayland.compositor'>"
    // "    <property type='au' name='view_vector_ids' access='read'/>" kept as example
    /************************* Methods ************************/
    "    <method name='inhibit_output_start'/>"
    "    <method name='inhibit_output_stop'/>"

    "    <method name='trigger_show_desktop'/>"
    "    <method name='trigger_show_overview'/>"

    /************************* Output Methods ************************/
    "    <method name='query_output_ids'>"
    "      <arg direction='out' type='au' />"
    "    </method>"
    "    <method name='query_output_name'>"
    "      <arg type='u' name='output_id' direction='in'/>"
    "      <arg type='s' name='name' direction='out'/>"
    "    </method>"
    "    <method name='query_output_manufacturer'>"
    "      <arg type='u' name='output_id' direction='in'/>"
    "      <arg type='s' name='name' direction='out'/>"
    "    </method>"
    "    <method name='query_output_model'>"
    "      <arg type='u' name='output_id' direction='in'/>"
    "      <arg type='s' name='name' direction='out'/>"
    "    </method>"
    "    <method name='query_output_serial'>"
    "      <arg type='u' name='output_id' direction='in'/>"
    "      <arg type='s' name='name' direction='out'/>"
    "    </method>"
    "    <method name='query_output_workspace'>"
    "      <arg type='u' name='output_id' direction='in'/>"
    "      <arg type='u' name='xHorizontal' direction='out'/>"
    "      <arg type='u' name='yVertical' direction='out'/>"
    "    </method>"
    "    <method name='query_xwayland_display'>"
    "      <arg type='s' name='xdisplay' direction='out'/>"
    "    </method>"
    /************************* View Methods ************************/
    "    <method name='query_view_vector_ids'>"
    "      <arg direction='out' type='au' />"
    "    </method>"
    "    <method name='query_view_vector_taskman_ids'>"
    "      <arg direction='out' type='au' />"
    "    </method>"
    "    <method name='query_view_app_id'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='s' name='app_id' direction='out'/>"
    "    </method>"
    "    <method name='query_view_app_id_gtk_shell'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='s' name='app_id' direction='out'/>"
    "    </method>"
    "    <method name='query_view_app_id_xwayland_net_wm_name'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='s' name='app_id' direction='out'/>"
    "    </method>"
    "    <method name='query_view_title'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='s' name='title' direction='out'/>"
    "    </method>"
    "    <method name='query_view_credentials'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='i' name='pid' direction='out'/>"
    "      <arg type='u' name='uid' direction='out'/>"
    "      <arg type='u' name='gid' direction='out'/>"
    "    </method>"
    "    <method name='query_view_active'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='b' name='view_state' direction='out'/>"
    "    </method>"
    "    <method name='query_view_minimized'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='b' name='view_state' direction='out'/>"
    "    </method>"
    "    <method name='query_view_maximized'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='b' name='view_state' direction='out'/>"
    "    </method>"
    "    <method name='query_view_fullscreen'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='b' name='view_state' direction='out'/>"
    "    </method>"
    "    <method name='query_view_output'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='u' name='output' direction='out'/>"
    "    </method>"
    "    <method name='query_view_layer'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='u' name='layer' direction='out'/>"
    "    </method>"
    "    <method name='query_view_workspaces'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='a(ii)' name='workspaces' direction='out'/>"
    "    </method>"
    "    <method name='query_view_group_leader'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='u' name='view_group_leader_view_id' direction='out'/>"
    "    </method>"
    "    <method name='query_view_role'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='u' name='view_state' direction='out'/>"
    "    </method>"
    "    <method name='query_workspace_grid_size'>"
    "      <arg type='i' name='rows' direction='out'/>"
    "      <arg type='i' name='columns' direction='out'/>"
    "    </method>"
    "    <method name='query_view_attention'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='b' name='attention' direction='out'/>"
    "    </method>"
    "    <method name='query_view_xwayland_wid'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='u' name='xwayland_wid' direction='out'/>"
    "    </method>"
    "    <method name='query_view_xwayland_atom_cardinal'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='s' name='atom' direction='in'/>"
    "      <arg type='u' name='atom_value' direction='out'/>"
    "    </method>"
    "    <method name='query_view_xwayland_atom_string'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='s' name='atom' direction='in'/>"
    "      <arg type='s' name='atom_value' direction='out'/>"
    "    </method>"
    "    <method name='query_view_test_data'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='u' name='value' direction='out'/>"
    "      <arg type='u' name='value2' direction='out'/>"
    "    </method>"
    "    <method name='minimize_view'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='u' name='action' direction='in'/>"
    "    </method>"
    "    <method name='maximize_view'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='u' name='action' direction='in'/>"
    "    </method>"
    "    <method name='focus_view'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='u' name='action' direction='in'/>"
    "    </method>"
    "    <method name='fullscreen_view'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='u' name='action' direction='in'/>"
    "    </method>"
    "    <method name='close_view'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "    </method>"
    "    <method name='change_view_minimize_hint'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='u' name='a' direction='in'/>"
    "      <arg type='u' name='b' direction='in'/>"
    "      <arg type='u' name='c' direction='in'/>"
    "      <arg type='u' name='d' direction='in'/>"
    "    </method>"
    "    <method name='change_output_view'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='u' name='output' direction='in'/>"
    "    </method>"
    "    <method name='change_workspace_view'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='u' name='workspace_horizontal' direction='in'/>"
    "      <arg type='u' name='workspace_vertical' direction='in'/>"
    "    </method>"
    "    <method name='change_workspace_output'>"
    "      <arg type='u' name='output' direction='in'/>"
    "      <arg type='u' name='workspace_horizontal' direction='in'/>"
    "      <arg type='u' name='workspace_vertical' direction='in'/>"
    "    </method>"
    "    <method name='change_workspace_all_outputs'>"
    "      <arg type='u' name='workspace_horizontal' direction='in'/>"
    "      <arg type='u' name='workspace_vertical' direction='in'/>"
    "    </method>"
    "    <method name='request_view_sticky'>"
    "      <arg type='u' name='view_id' direction='in'/>"
    "      <arg type='b' name='sticky' direction='in'/>"
    "    </method>"
    /************************* Signals ************************/
    "    <signal name='view_added'>"
    "      <arg type='u' name='view_id'/>"
    "    </signal>"
    "    <signal name='view_closed'>"
    "      <arg type='u' name='view_id'/>"
    "    </signal>"
    "    <signal name='view_app_id_changed'>"
    "      <arg type='u' name='view_id'/>"
    "      <arg type='s' name='new_app_id'/>"
    "    </signal>"
    "    <signal name='view_title_changed'>"
    "      <arg type='u' name='view_id'/>"
    "      <arg type='s' name='new_title'/>"
    "    </signal>"
    "    <signal name='view_state_changed'>"
    "      <arg type='u' name='view_id'/>"
    "      <arg type='i' name='new_state'/>"
    "    </signal>"
    "    <signal name='view_output_move_requested'>"
    "      <arg type='u' name='view_id'/>"
    "      <arg type='u' name='old_output'/>"
    "      <arg type='u' name='new_output'/>"
    "    </signal>"
    "    <signal name='view_output_moved'>"
    "      <arg type='u' name='view_id'/>"
    "      <arg type='u' name='old_output'/>"
    "      <arg type='u' name='new_output'/>"
    "    </signal>"
    "    <signal name='view_workspaces_changed'>"
    "      <arg type='u' name='view_id'/>"
    "    </signal>"
    "    <signal name='view_attention_changed'>"
    "      <arg type='u' name='view_id'/>"
    "      <arg type='b' name='attention'/>"
    "    </signal>"
    "    <signal name='view_group_leader_changed'>"
    "      <arg type='u' name='view_id'/>"
    "      <arg type='u' name='view_group_leader_view_id'/>"
    "    </signal>"
    "    <signal name='view_tiling_changed'>"
    "      <arg type='s' name='test'/>"
    "    </signal>"
    "    <signal name='view_geometry_changed'>"
    "      <arg type='s' name='test'/>"
    "    </signal>"
    "    <signal name='view_moving_changed'>"
    "      <arg type='s' name='test'/>"
    "    </signal>"
    "    <signal name='view_resizing_changed'>"
    "      <arg type='s' name='test'/>"
    "    </signal>"
    "    <signal name='output_view_resizing'>"
    "      <arg type='s' name='test'/>"
    "    </signal>"
    "    <signal name='view_role_changed'>"
    "      <arg type='u' name='view_id'/>"
    "      <arg type='u' name='view_role'/>"
    "    </signal>"

    /****
    * Core Input Events
    ****/
    "    <signal name='pointer_clicked'/>"
    "    <signal name='tablet_touched'/>"

    /****
    * Comes from output no info which view atm
    ****/
    "    <signal name='view_maximized_changed'>"
    "      <arg type='u' name='view_id'/>"
    "      <arg type='b' name='maximized'/>"
    "    </signal>"
    "    <signal name='view_minimized_changed'>"
    "      <arg type='u' name='view_id'/>"
    "      <arg type='b' name='minimized'/>"
    "    </signal>"
    "    <signal name='view_fullscreen_changed'>"
    "      <arg type='u' name='view_id'/>"
    "      <arg type='b' name='fullscreened'/>"
    "    </signal>"
    "    <signal name='view_focus_changed'>"
    "      <arg type='u' name='view_id'/>"
    "    </signal>"
    "    <signal name='view_focus_changed'>"
    "      <arg type='u' name='view_id'/>"
    "    </signal>"
    "    <signal name='view_layer_changed'>"
    "      <arg type='u' name='view_id'/>"
    "      <arg type='u' name='layer'/>"
    "    </signal>"
    "    <signal name='hotspot_edge_trigger_stop'>"
    "      <arg type='u' name='hotspot_edge'/>"
    "    </signal>"

    "    <signal name='hotspot_edge_triggered'>"
    "      <arg type='i' name='hotspot_edge'/>"
    "    </signal>"
    "    <signal name='hotspot_edge_trigger_stop'>"
    "      <arg type='i' name='hotspot_edge'/>"
    "    </signal>"

    "    <signal name='inhibit_output_started'/>"
    "    <signal name='inhibit_output_stopped'/>"

    "    <signal name='output_workspace_changed'>"
    "      <arg type='u' name='output_id'/>"
    "      <arg type='i' name='workspace_horizontal'/>"
    "      <arg type='i' name='workspace_vertical'/>"
    "    </signal>"

    "  </interface>"
    "</node>";

static void handle_method_call(GDBusConnection *connection, const gchar *sender,
                               const gchar *object_path, const gchar *interface_name,
                               const gchar *method_name, GVariant *parameters,
                               GDBusMethodInvocation *invocation, gpointer user_data)
{
    LOG(wf::log::LOG_LEVEL_ERROR, "handle_method_call bus called", method_name);

    if (g_strcmp0(method_name, "request_view_sticky") == 0)
    {
        guint view_id;
        bool sticky;
        g_variant_get(parameters, "(ub)", &view_id, &sticky);

        queued_view_id = view_id;
        queued_view_param = sticky;
        wayfire_view view = get_view_from_view_id(view_id);

        /*************************************************************
         * For some glorious reasons if parameters is passed to
         * local_thread_request_view_sticky data will be invalid
         * 
         * Beware of crashes if before a view is made sticky or not sticky
         * if another output is focused or some other stuff happens 
         * compositor might crash... so it is not a good test case
         * for doing stuff very fast while doing other stuff
         *************************************************************/

        wl_event_loop_add_idle(wf::get_core().ev_loop,
                               local_thread_request_view_sticky,
                               nullptr);

        g_dbus_method_invocation_return_value(invocation,
                                              nullptr);
        return;
    }

    /*************** View Actions ****************/
    else if (g_strcmp0(method_name, "minimize_view") == 0)
    {
        g_variant_ref(parameters);
        wl_event_loop_add_idle(wf::get_core().ev_loop,
                               local_thread_minimize,
                               static_cast<void *>(parameters));
        g_dbus_method_invocation_return_value(invocation,
                                              nullptr);
        return;
    }
    else if (g_strcmp0(method_name, "maximize_view") == 0)
    {
        g_variant_ref(parameters);
        wl_event_loop_add_idle(wf::get_core().ev_loop,
                               local_thread_maximize,
                               static_cast<void *>(parameters));
        g_dbus_method_invocation_return_value(invocation,
                                              nullptr);
        return;
    }
    else if (g_strcmp0(method_name, "focus_view") == 0)
    {
        g_variant_ref(parameters);
        wl_event_loop_add_idle(wf::get_core().ev_loop,
                               local_thread_view_focus,
                               static_cast<void *>(parameters));
        g_dbus_method_invocation_return_value(invocation,
                                              nullptr);
        return;
    }
    else if (g_strcmp0(method_name, "fullscreen_view") == 0)
    {
        g_variant_ref(parameters);
        wl_event_loop_add_idle(wf::get_core().ev_loop,
                               local_thread_fullscreen,
                               static_cast<void *>(parameters));
        g_dbus_method_invocation_return_value(invocation,
                                              nullptr);
        return;
    }
    else if (g_strcmp0(method_name, "close_view") == 0)
    {
        g_variant_ref(parameters);
        wl_event_loop_add_idle(wf::get_core().ev_loop,
                               local_thread_view_close,
                               static_cast<void *>(parameters));
        g_dbus_method_invocation_return_value(invocation,
                                              nullptr);
        return;
    }
    else if (g_strcmp0(method_name, "change_view_minimize_hint") == 0)
    {
        g_variant_ref(parameters);
        wl_event_loop_add_idle(wf::get_core().ev_loop,
                               local_thread_change_view_minimize_hint,
                               static_cast<void *>(parameters));
        g_dbus_method_invocation_return_value(invocation,
                                              nullptr);
        return;
    }

    else if (g_strcmp0(method_name, "change_output_view") == 0)
    {
        g_variant_ref(parameters);
        wl_event_loop_add_idle(wf::get_core().ev_loop,
                               local_thread_change_view_output,
                               static_cast<void *>(parameters));
        g_dbus_method_invocation_return_value(invocation,
                                              nullptr);
        return;
    }
    else if (g_strcmp0(method_name, "change_workspace_view") == 0)
    {
        g_variant_ref(parameters);
        wl_event_loop_add_idle(wf::get_core().ev_loop,
                               local_thread_change_view_workspace,
                               static_cast<void *>(parameters));
        g_dbus_method_invocation_return_value(invocation,
                                              nullptr);
        return;
    }
    else if (g_strcmp0(method_name, "change_workspace_output") == 0)
    {
        g_variant_ref(parameters);
        wl_event_loop_add_idle(wf::get_core().ev_loop,
                               local_thread_change_workspace_output,
                               static_cast<void *>(parameters));
        g_dbus_method_invocation_return_value(invocation,
                                              nullptr);
        return;
    }
    else if (g_strcmp0(method_name, "change_workspace_all_outputs") == 0)
    {
        g_variant_ref(parameters);
        wl_event_loop_add_idle(wf::get_core().ev_loop,
                               local_thread_change_workspace_all_outputs,
                               static_cast<void *>(parameters));
        g_dbus_method_invocation_return_value(invocation,
                                              nullptr);
        return;
    }

    /*************** Non-reffing actions at end ****************/

    else if (g_strcmp0(method_name, "query_output_ids") == 0)
    {
        std::vector<wf::output_t *> wf_outputs = wf::get_core().output_layout->get_outputs();
        GVariantBuilder builder;
        GVariant *value;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("au"));
        for (wf::output_t *wf_output : wf_outputs)
        {
            g_variant_builder_add(&builder, "u", wf_output->get_id());
        }
        value = g_variant_new("(au)", &builder);
        g_dbus_method_invocation_return_value(invocation,
                                              value);
        return;
    }
    else if (g_strcmp0(method_name, "query_view_vector_ids") == 0)
    {
        std::vector<nonstd::observer_ptr<wf::view_interface_t>> view_vector = wf::get_core().get_all_views();
        GVariantBuilder builder;
        GVariant *value;

        g_variant_builder_init(&builder, G_VARIANT_TYPE("au"));
        for (auto it = begin(view_vector); it != end(view_vector); ++it)
            g_variant_builder_add(&builder, "u", it->get()->get_id());

        value = g_variant_new("(au)", &builder);
        g_dbus_method_invocation_return_value(invocation,
                                              value);
        return;
    }
    else if (g_strcmp0(method_name, "query_view_vector_taskman_ids") == 0)
    {
        std::vector<nonstd::observer_ptr<wf::view_interface_t>> view_vector = wf::get_core().get_all_views();
        GVariantBuilder builder;
        GVariant *value;

        g_variant_builder_init(&builder, G_VARIANT_TYPE("au"));
        for (auto it = begin(view_vector); it != end(view_vector); ++it)
        {
            if (it->get()->role != wf::VIEW_ROLE_TOPLEVEL || !it->get()->is_mapped())
                continue;
            else
                g_variant_builder_add(&builder, "u", it->get()->get_id());
        }

        value = g_variant_new("(au)", &builder);
        g_dbus_method_invocation_return_value(invocation,
                                              value);
        return;
    }
    /*************** Output Properties ****************/
    else if (g_strcmp0(method_name, "query_output_name") == 0)
    {
        guint output_id;
        gchar *response = "nullptr";
        g_variant_get(parameters, "(u)", &output_id);
        wf::output_t *wf_output = get_output_from_output_id(output_id);

        if (wf_output != nullptr)
            response = g_strdup_printf(wf_output->to_string().c_str());

        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", response));
        if (wf_output != nullptr)
            g_free(response);
        return;
    }
    else if (g_strcmp0(method_name, "query_output_manufacturer") == 0)
    {
        guint output_id;
        gchar *response = "nullptr";
        g_variant_get(parameters, "(u)", &output_id);
        wf::output_t *wf_output = get_output_from_output_id(output_id);
        wlr_output *wlr_output = wf_output->handle;

        if (wlr_output != nullptr)
            response = g_strdup_printf(wlr_output->make);

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(s)",
                                                            response));
        if (wlr_output != nullptr)
            g_free(response);
        return;
    }
    else if (g_strcmp0(method_name, "query_output_model") == 0)
    {
        guint output_id;
        gchar *response = "nullptr";
        g_variant_get(parameters, "(u)", &output_id);
        wf::output_t *wf_output = get_output_from_output_id(output_id);
        wlr_output *wlr_output = wf_output->handle;

        if (wlr_output != nullptr)
            response = g_strdup_printf(wlr_output->model);

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(s)",
                                                            response));
        if (wlr_output != nullptr)
            g_free(response);
        return;
    }
    else if (g_strcmp0(method_name, "query_output_serial") == 0)
    {
        guint output_id;
        gchar *response = "nullptr";
        g_variant_get(parameters, "(u)", &output_id);
        wf::output_t *wf_output = get_output_from_output_id(output_id);
        wlr_output *wlr_output = wf_output->handle;

        if (wlr_output != nullptr)
            response = g_strdup_printf(wlr_output->serial);

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(s)", response));
        if (wlr_output != nullptr)
            g_free(response);
        return;
    }
    else if (g_strcmp0(method_name, "query_output_workspace") == 0)
    {
        guint output_id;
        gchar *response = "nullptr";
        g_variant_get(parameters, "(u)", &output_id);
        wf::output_t *wf_output = get_output_from_output_id(output_id);
        wf::point_t ws = wf_output->workspace->get_current_workspace();
        uint horizontal_workspace = ws.x;
        uint vertical_workspace = ws.y;

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(uu)",
                                                            horizontal_workspace,
                                                            vertical_workspace));
        return;
    }
    else if (g_strcmp0(method_name, "query_workspace_grid_size") == 0)
    {
        wf::dimensions_t workspaces;
        workspaces = wf::get_core().get_active_output()->workspace->get_workspace_grid_size();

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(ii)",
                                                            workspaces.width,
                                                            workspaces.height));
        return;
    }
    /*************** View Properties ****************/
    else if (g_strcmp0(method_name, "query_view_app_id") == 0)
    {
        guint view_id;
        gchar *response = "nullptr";
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);

        // wf::view_access_interface_t vai;
        // vai.set_view(view);
        // bool &callback = false;
        //    vai.get("app_id", &callback)

        if (view)
            response = g_strdup_printf(view->get_app_id().c_str());

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(s)",
                                                            response));
        if (view)
            g_free(response);
        return;
    }
    else if (g_strcmp0(method_name, "query_view_app_id_gtk_shell") == 0)
    {
        guint view_id;
        gchar *response = "nullptr";
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);

        if (view)
            response = g_strdup_printf(view->get_app_id_gtk_shell().c_str());

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(s)",
                                                            response));
        if (view)
            g_free(response);
        return;
    }
    else if (g_strcmp0(method_name, "query_view_app_id_xwayland_net_wm_name") == 0)
    {
        guint view_id;
        gchar *response = "nullptr";
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);

        if (view)
        {
            auto wlr_surf = view->get_wlr_surface();
            if (wlr_surface_is_xwayland_surface(wlr_surf))
            {
                struct wlr_xwayland_surface *xsurf;
                xsurf = wlr_xwayland_surface_from_wlr_surface(wlr_surf);
                std::string wm_name_app_id = nonull(xsurf->instance);
                response = g_strdup_printf(wm_name_app_id.c_str());
            }
        }

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(s)", response));
        if (view)
            g_free(response);
        return;
    }
    else if (g_strcmp0(method_name, "query_view_title") == 0)
    {
        guint view_id;
        gchar *response = "nullptr";
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);

        if (view)
            response = g_strdup_printf(view->get_title().c_str());

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(s)", response));
        if (view)
            g_free(response);
        return;
    }
    else if (g_strcmp0(method_name, "query_view_attention") == 0)
    {
        guint view_id;
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);
        bool attention = false;
        if (view)
        {
            if (view->has_data("view-demands-attention"))
                attention = true;
        }
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(b)",
                                                            attention));
        return;
    }
    else if (g_strcmp0(method_name, "query_xwayland_display") == 0)
    {
        const char *xdisplay = wf::get_core().get_xwayland_display().c_str();

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(s)",
                                                            xdisplay));
        return;
    }
    else if (g_strcmp0(method_name, "query_view_xwayland_wid") == 0)
    {
        guint view_id;
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);
        if (!view)
        {

            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(u)",
                                                                0));
            return;
        }
        wf::option_wrapper_t<bool> xwayland_enabled("core/xwayland");
        if (xwayland_enabled == 1)
        {
            auto main_wlr_surface = view->get_main_surface()->get_wlr_surface();
            if (wlr_surface_is_xwayland_surface(main_wlr_surface))
            {
                LOG(wf::log::LOG_LEVEL_ERROR,
                    "xwayland is the surface type.", "DBUS Plugin", 0);
                struct wlr_xwayland_surface *main_xsurf;
                main_xsurf = wlr_xwayland_surface_from_wlr_surface(main_wlr_surface);
                g_dbus_method_invocation_return_value(invocation,
                                                      g_variant_new("(u)",
                                                                    main_xsurf->window_id));
                return;
            }
        }
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(u)", 0));
        return;
    }
    else if (g_strcmp0(method_name, "query_view_xwayland_atom_cardinal") == 0)
    {
        LOG(wf::log::LOG_LEVEL_ERROR,
            "query_view_xwayland_atom_cardinal.");
        guint view_id;
        gchar *atom_name;
        guint atom_value_cardinal = 0;
        g_variant_get(parameters, "(us)", &view_id, &atom_name);

        wayfire_view view = get_view_from_view_id(view_id);
        if (!view)
        {

            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(u)",
                                                                0));
            return;
        }
        auto main_wlr_surface = view->get_main_surface()->get_wlr_surface();

        wf::option_wrapper_t<bool> xwayland_enabled("core/xwayland");
        if (xwayland_enabled != 1 || !wlr_surface_is_xwayland_surface(main_wlr_surface))
        {

            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(u)",
                                                                0));
            return;
        }

        struct wlr_xwayland_surface *main_xsurf;
        main_xsurf = wlr_xwayland_surface_from_wlr_surface(main_wlr_surface);

        const char *xdisplay = wf::get_core().get_xwayland_display().c_str();
        int screen;
        xcb_connection_t *conn = xcb_connect(xdisplay, &screen);
        xcb_intern_atom_cookie_t atom_cookie;
        xcb_atom_t atom;
        xcb_intern_atom_reply_t *reply;
        atom_cookie = xcb_intern_atom(conn, 0, strlen(atom_name), atom_name);
        reply = xcb_intern_atom_reply(conn, atom_cookie, NULL);
        if (reply != NULL)
        {
            atom = reply->atom;
            free(reply);
        }
        else
        {
            LOG(wf::log::LOG_LEVEL_ERROR,
                "reply for querying the atom is empty.");
            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(u)",
                                                                atom_value_cardinal));
            return;
        }

        xcb_get_property_cookie_t reply_cookie;
        xcb_get_property_reply_t *reply_value;
        reply_cookie = xcb_get_property(conn,
                                        0,
                                        main_xsurf->window_id,
                                        atom,
                                        XCB_ATOM_ANY,
                                        0,
                                        2048);
        reply_value = xcb_get_property_reply(conn, reply_cookie, NULL);
        xcb_disconnect(conn);

        if (reply_value->type == XCB_ATOM_CARDINAL)
        {
            uint *uvalue = (uint *)xcb_get_property_value(reply_value);
            atom_value_cardinal = *uvalue;
            LOG(wf::log::LOG_LEVEL_ERROR,
                "value to uint.", atom_value_cardinal);
        }
        else
        {
            LOG(wf::log::LOG_LEVEL_ERROR,
                "requested value is not a cardinal");
        }

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(u)",
                                                            atom_value_cardinal));
        return;
    }
    else if (g_strcmp0(method_name, "query_view_xwayland_atom_string") == 0)
    {
        LOG(wf::log::LOG_LEVEL_ERROR,
            "query_view_xwayland_atom.");
        guint view_id;
        gchar *atom_name;
        gchar *atom_value_string = "No atom value received.";
        g_variant_get(parameters, "(us)", &view_id, &atom_name);

        wayfire_view view = get_view_from_view_id(view_id);

        if (!view)
        {

            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(s)",
                                                                "View not found."));
            return;
        }

        auto main_wlr_surface = view->get_main_surface()->get_wlr_surface();

        wf::option_wrapper_t<bool> xwayland_enabled("core/xwayland");
        if (xwayland_enabled != 1 || !wlr_surface_is_xwayland_surface(main_wlr_surface))
        {

            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(s)",
                                                                "Not an xwayland surface."));
            return;
        }
        struct wlr_xwayland_surface *main_xsurf;

        LOG(wf::log::LOG_LEVEL_ERROR,
            "xwayland is the surface type.");
        main_xsurf = wlr_xwayland_surface_from_wlr_surface(main_wlr_surface);

        const char *xdisplay = wf::get_core().get_xwayland_display().c_str();
        int screen;
        xcb_connection_t *conn = xcb_connect(xdisplay, &screen);
        xcb_intern_atom_cookie_t atom_cookie;
        xcb_atom_t atom;
        xcb_intern_atom_reply_t *reply;
        atom_cookie = xcb_intern_atom(conn, 0, strlen(atom_name), atom_name);
        reply = xcb_intern_atom_reply(conn, atom_cookie, NULL);
        if (reply != NULL)
        {
            atom = reply->atom;
            free(reply);
        }
        else
        {
            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(s)",
                                                                "reply for querying the atom is empty."));
            return;
        }

        xcb_get_property_cookie_t reply_cookie = xcb_get_property(conn,
                                                                  0,
                                                                  main_xsurf->window_id,
                                                                  atom,
                                                                  XCB_ATOM_ANY,
                                                                  0,
                                                                  2048);
        xcb_get_property_reply_t *reply_value = xcb_get_property_reply(conn,
                                                                       reply_cookie,
                                                                       NULL);

        char *value = static_cast<char *>(xcb_get_property_value(reply_value));

        xcb_disconnect(conn);

        if (reply_value->type != XCB_ATOM_CARDINAL)
        {
            atom_value_string = value;
            LOG(wf::log::LOG_LEVEL_ERROR,
                "value to char.", atom_value_string);
            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(s)",
                                                                atom_value_string));
            return;
        }
        else
        {
            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(s)",
                                                                "XCB_ATOM_CARDINAL type requested."));
            return;
        }
    }
    else if (g_strcmp0(method_name, "query_view_credentials") == 0)
    {
        guint view_id;
        pid_t pid = -1;
        uid_t uid = -1;
        gid_t gid = -1;
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);
        if (!view)
        {
            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(iuu)",
                                                                0,
                                                                0,
                                                                0));
        }
        wf::option_wrapper_t<bool> xwayland_enabled("core/xwayland");
        if (xwayland_enabled == 1)
        {
            auto main_surface = view->get_main_surface()->get_wlr_surface();

            if (wlr_surface_is_xwayland_surface(main_surface))
            {
                struct wlr_xwayland_surface *main_xsurf;
                xcb_res_client_id_spec_t spec = {0};
                xcb_generic_error_t *err = NULL;
                xcb_res_query_client_ids_cookie_t cookie;
                xcb_res_query_client_ids_reply_t *reply;
                int screen;
                const char *xdisplay = wf::get_core().get_xwayland_display().c_str();
                xcb_connection_t *conn = xcb_connect(xdisplay, &screen);

                main_xsurf = wlr_xwayland_surface_from_wlr_surface(main_surface);
                spec.client = main_xsurf->window_id;
                spec.mask = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID;
                cookie = xcb_res_query_client_ids(conn, 1, &spec);
                reply = xcb_res_query_client_ids_reply(conn, cookie, &err);

                if (reply == NULL)
                {
                    LOG(wf::log::LOG_LEVEL_ERROR,
                        "could not get pid from xserver, empty reply");
                }
                else
                {
                    xcb_res_client_id_value_iterator_t it;
                    it = xcb_res_query_client_ids_ids_iterator(reply);
                    for (; it.rem; xcb_res_client_id_value_next(&it))
                    {
                        spec = it.data->spec;
                        if (spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID)
                        {
                            // uint32_t *_pid = xcb_res_client_id_value_value(it.data);
                            pid = *xcb_res_client_id_value_value(it.data);
                            break;
                        }
                    }
                }

                free(reply);
                xcb_disconnect(conn);

                if (pid != -1)
                {
                    LOG(wf::log::LOG_LEVEL_ERROR, "returning xwayland window credentials.");
                    g_dbus_method_invocation_return_value(invocation,
                                                          g_variant_new("(iuu)",
                                                                        pid,
                                                                        uid,
                                                                        gid));
                    return;
                }
            }
        }
        LOG(wf::log::LOG_LEVEL_ERROR, "returning standard credentials.");
        wl_client_get_credentials(view->get_client(),
                                  &pid,
                                  &uid,
                                  &gid);
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(iuu)",
                                                            pid,
                                                            uid,
                                                            gid));
        return;
    }
    else if (g_strcmp0(method_name, "query_view_layer") == 0)
    {
        guint view_id;
        guint response = 0;
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);

        if (view)
            response = view->desired_layer;

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(u)",
                                                            response));
        return;
    }
    else if (g_strcmp0(method_name, "query_view_maximized") == 0)
    {
        guint view_id;
        bool response = false;
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);

        if (view)
        {
            response = (view->tiled_edges == wf::TILED_EDGES_ALL);
        }
        else
            LOG(wf::log::LOG_LEVEL_ERROR, "Error window state - no view");

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(b)",
                                                            response));
        return;
    }

    else if (g_strcmp0(method_name, "query_view_active") == 0)
    {
        guint view_id;
        bool response = false;
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);

        if (view)
        {
            response = view->activated;
        }
        else
            LOG(wf::log::LOG_LEVEL_ERROR, "Error window state - no view");

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(b)",
                                                            response));
        return;
    }

    else if (g_strcmp0(method_name, "query_view_minimized") == 0)
    {
        guint view_id;
        bool response = false;
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);

        if (view)
        {
            response = view->minimized;
        }
        else
            LOG(wf::log::LOG_LEVEL_ERROR, "Error window state - no view");

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(b)",
                                                            response));
        return;
    }

    else if (g_strcmp0(method_name, "query_view_fullscreen") == 0)
    {
        guint view_id;
        bool response;
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);

        if (view)
        {
            response = view->fullscreen;
        }
        else
            LOG(wf::log::LOG_LEVEL_ERROR, "Error window state - no view");

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(b)",
                                                            response));
        return;
    }
    else if (g_strcmp0(method_name, "query_view_output") == 0)
    {
        guint view_id;
        uint output_id = 0;
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);

        if (view)
            output_id = view->get_output()->get_id();

        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(u)",
                                                            output_id));
        return;
    }
    else if (g_strcmp0(method_name, "query_view_workspaces") == 0)
    {
        LOG(wf::log::LOG_LEVEL_ERROR, "query_view_workspaces ");

        guint view_id;
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);

        if (!view || view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
        {
            LOG(wf::log::LOG_LEVEL_ERROR, "view == nullptr");
            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(a(ii))", nullptr));
            return;
        }
        wf::dimensions_t workspaces;
        workspaces = wf::get_core().get_active_output()->workspace->get_workspace_grid_size();

        LOG(wf::log::LOG_LEVEL_ERROR, "query_view_workspaces builder called");

        wf::geometry_t intersection;
        double area;

        GVariantBuilder builder;
        wf::output_t *output = view->get_output();
        wlr_box view_relative_geometry;
        wf::geometry_t workspace_relative_geometry;
        view_relative_geometry = view->get_bounding_box();

        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ii)"));
        GVariant *value;

        for (int horizontal_workspace = 0; horizontal_workspace < workspaces.width; horizontal_workspace++)
        {
            for (int vertical_workspace = 0; vertical_workspace < workspaces.height; vertical_workspace++)
            {
                wf::point_t ws = {horizontal_workspace, vertical_workspace};
                if (output->workspace->view_visible_on(view, ws))
                {
                    workspace_relative_geometry = output->render->get_ws_box(ws);
                    intersection = wf::geometry_intersection(view_relative_geometry, workspace_relative_geometry);
                    area = 1.0 * intersection.width * intersection.height;
                    area /= 1.0 * view_relative_geometry.width * view_relative_geometry.height;
                    // LOG(wf::log::LOG_LEVEL_ERROR, "query_view_workspaces area?",area);

                    if (area > 0.1)
                        g_variant_builder_add(&builder, "(ii)", horizontal_workspace, vertical_workspace);
                }
            }
        }

        value = g_variant_new("(a(ii))", &builder);
        g_dbus_method_invocation_return_value(invocation, value);
        return;
    }
    else if (g_strcmp0(method_name, "query_view_group_leader") == 0)
    {

        LOG(wf::log::LOG_LEVEL_ERROR, "query_view_group_leader bus called", "DBUS Plugin", 0);
        guint view_id;
        guint group_leader_view_id;

        g_variant_get(parameters, "(u)", &view_id);
        group_leader_view_id = view_id;

        wayfire_view view = get_view_from_view_id(view_id);
        if (view)
        {
            while (view->parent)
            {
                view = view->parent;

                LOG(wf::log::LOG_LEVEL_ERROR, "query_view_group_leader view has p");
            }
            group_leader_view_id = view->get_id();
            LOG(wf::log::LOG_LEVEL_ERROR, "query_view_group_leader found returning");
        }
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(u)",
                                                            group_leader_view_id));
        return;
    }
    else if (g_strcmp0(method_name, "query_view_role") == 0)
    {
        guint view_id;
        uint response = 0;
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);

        if (view)
        {
            const bool is_modal_dialog = view->has_data("gtk-shell-modal");
            if (view->role == wf::VIEW_ROLE_TOPLEVEL && !is_modal_dialog)
                response = 1;
            else if (view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
                response = 2;
            else if (view->role == wf::VIEW_ROLE_UNMANAGED)
                response = 3;
        }
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(u)",
                                                            response));
        return;
    }
    else if (g_strcmp0(method_name, "query_view_test_data") == 0)
    {
        guint view_id;
        g_variant_get(parameters, "(u)", &view_id);
        wayfire_view view = get_view_from_view_id(view_id);
        auto wlr_surf = view->get_wlr_surface();

        if (wlr_surface_is_xwayland_surface(wlr_surf))
        {
            struct wlr_xwayland_surface *xsurf;
            xsurf = wlr_xwayland_surface_from_wlr_surface(wlr_surf);
            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(uu)",
                                                                xsurf->width,
                                                                xsurf->height));
        }

        return;
    }

    /*************** Other Actions ****************/
    else if (g_strcmp0(method_name, "inhibit_output_start") == 0)
    {
        LOG(wf::log::LOG_LEVEL_DEBUG, "inhibit_output_start bus called", "DBUS Plugin", 0);
        g_variant_unref(parameters);
    }
    else if (g_strcmp0(method_name, "inhibit_output_stop") == 0)
    {
        LOG(wf::log::LOG_LEVEL_DEBUG, "inhibit_output_stop bus called", "DBUS Plugin", 0);
        g_variant_unref(parameters);
    }
    else if (g_strcmp0(method_name, "trigger_show_desktop") == 0)
    {
        LOG(wf::log::LOG_LEVEL_DEBUG, "trigger_show_desktop bus called", "DBUS Plugin", 0);
        g_variant_unref(parameters);
    }
    else if (g_strcmp0(method_name, "trigger_show_overview") == 0)
    {
        LOG(wf::log::LOG_LEVEL_DEBUG, "trigger_show_overview bus called", "DBUS Plugin", 0);
        g_variant_unref(parameters);
    }
}

static GVariant *handle_get_property(GDBusConnection *connection, const gchar *sender,
                                     const gchar *object_path, const gchar *interface_name,
                                     const gchar *property_name, GError **error, gpointer user_data)
{
    //returning nullptr would crash everything
    GVariant *ret = g_variant_new_string("nullptr");
    //ketps as example
    // if (g_strcmp0(property_name, "view_vector_ids") == 0)
    // {
    //     std::vector<nonstd::observer_ptr<wf::view_interface_t>> view_vector = wf::get_core().get_all_views();

    //     GVariantBuilder builder;
    //     g_variant_builder_init(&builder, G_VARIANT_TYPE("au"));
    //     for (auto it = begin(view_vector); it != end(view_vector); ++it)
    //         g_variant_builder_add(&builder, "u", it->get()->get_id());

    //     ret = g_variant_builder_end(&builder);
    // }
    // /* unused */
    return ret;
}

static gboolean handle_set_property(GDBusConnection *connection,
                                    const gchar *sender,
                                    const gchar *object_path,
                                    const gchar *interface_name,
                                    const gchar *property_name,
                                    GVariant *value,
                                    GError **error,
                                    gpointer user_data)
{
    /* unused */
    return false;
}
static const GDBusInterfaceVTable interface_vtable = {
    handle_method_call,
    handle_get_property,
    handle_set_property};

static gboolean bus_emit_signal(gchar *signal_name, GVariant *signal_data)
{
    GError *local_error = nullptr;

    g_dbus_connection_emit_signal(dbus_connection,
                                  nullptr,
                                  "/org/wayland/compositor",
                                  "org.wayland.compositor",
                                  signal_name,
                                  signal_data,
                                  &local_error);
    g_assert_no_error(local_error);
    if (signal_data != nullptr)
        g_variant_unref(signal_data);
    return true;
}

static void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    dbus_connection = connection;
    guint registration_id;

    registration_id = g_dbus_connection_register_object(connection,
                                                        "/org/wayland/compositor",
                                                        introspection_data->interfaces[0],
                                                        &interface_vtable,
                                                        nullptr,  /* user_data */
                                                        nullptr,  /* user_data_free_func */
                                                        nullptr); /* GError** */
    LOG(wf::log::LOG_LEVEL_DEBUG, "Acquired the name " + std::string(name) + "on the session bus", "DBUS Plugin", 0);
}

static void on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    // g_print("Acquired the name %s on the session bus\n", name);
    LOG(wf::log::LOG_LEVEL_DEBUG, "Acquired the name " + std::string(name) + "on the session bus\n", "DBUS Plugin", 0);
}

static void on_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    LOG(wf::log::LOG_LEVEL_ERROR, "Lost the name " + std::string(name) + "on the session bus", " DBUS Plugin", 0);
}

static void acquire_bus()
{
    // Fail if not available - Do *not* try to replace;
    // E.g if running wayfire and starting a nested wayfire
    // in the future it could be possible to have a seperate object path
    // if a nested compostor is running
    // but nested compositors are not working with wloots at the moment
    GBusNameOwnerFlags flags = G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE;
    introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, nullptr);

    owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
                              "org.wayland.compositor",
                              flags,
                              on_bus_acquired,
                              on_name_acquired,
                              on_name_lost,
                              nullptr,
                              nullptr);

    g_print("Increase this later: 42");
}

static gpointer dbus_thread_exec_function(gpointer user_data)
{
    GMainContext *dbus_context = static_cast<GMainContext *>(user_data);

    /* Set up the thread’s context and run it forever. */
    g_main_context_push_thread_default(dbus_context);
    dbus_event_loop = g_main_loop_new(dbus_context, FALSE);
    g_main_loop_run(dbus_event_loop);

    /* Should only reach below here if event loop is killed. */
    LOG(wf::log::LOG_LEVEL_ERROR, "THIS SHOULD NEVER BE REACHED", " DBUS Plugin", 0);
    g_main_loop_unref(dbus_event_loop);
    g_main_context_pop_thread_default(dbus_context);
    g_main_context_unref(dbus_context);
    return nullptr;
}