extern "C"
{
#include <gio/gio.h>
#include <sys/socket.h>
#include <sys/types.h>
};
//these are in wf-utils
//#include <wayfire/action/action.hpp>
//#include <wayfire/action/action_interface.hpp>
#include <iostream>
#include <string>
#include <charconv>
#include <algorithm>
#include <cmath>
#include <linux/input.h>

#include <wayfire/singleton-plugin.hpp>
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
#include <wayfire/util.hpp>
#include <wayfire/input-device.hpp>

#include <wayfire/signal-definitions.hpp>

#include "dbus_interface_backend.cpp"

class dbus_interface_t
{
  wf::option_wrapper_t<std::string> test_string{"dbus_interface/test-string"};

public:
  void constrain_pointer(const bool constrain)
  {
    struct wlr_xwayland_surface *xsurf;

    return;
  }
  dbus_interface_t()
  {
    /************* Connect all signals for already existing objects *************/
    for (wf::output_t *wf_output : wf_outputs)
    {
      wf_output->connect_signal("map-view",
                                &output_view_added);

      wf_output->connect_signal("output-configuration-changed",
                                &output_configuration_changed);

      wf_output->connect_signal("view-minimize-request",
                                &output_view_minimized);

      wf_output->connect_signal("view-maximized-request",
                                &output_view_maximized);

      wf_output->connect_signal("move-request",
                                &output_view_moving);

      wf_output->connect_signal("resize-request",
                                &output_view_resizing);

      wf_output->connect_signal("view-change-viewport",
                                &view_workspaces_changed);

      ///////////// wf_output->connect_signal("set-workspace-request",
      //  &output_workspace_changed);

      wf_output->connect_signal("viewport-changed",
                                &output_workspace_changed);

      wf_output->connect_signal("layer-attach-view",
                                &role_changed);

      wf_output->connect_signal("layer-detach-view",
                                &role_changed);

      wf_output->connect_signal("focus-view",
                                &output_view_focus_changed);

      wf_output->connect_signal("view-fullscreen-request",
                                &view_fullscreen_changed);

      wf_output->connect_signal("view-self-request-focus",
                                &view_self_request_focus);

      wf_output->connect_signal("view-hints-changed",
                                &view_hints_changed);

      LOG(wf::log::LOG_LEVEL_ERROR, "output connected ");
      connected_wf_outputs.insert(wf_output);

      if (wf_output->workspace->above_layer == nullptr)
        wf_output->workspace->above_layer = wf_output->workspace->create_sublayer(wf::LAYER_WORKSPACE,
                                                                                  wf::SUBLAYER_DOCKED_ABOVE);

      // nonstd::observer_ptr<wf::sublayer_t> sticky_layer;
      // sticky_layer = wf_output->workspace->create_sublayer(wf::LAYER_WORKSPACE,
      //                                                      wf::SUBLAYER_DOCKED_ABOVE);
      // sticky_layers.push_back(sticky_layer);
    }
    LOG(wf::log::LOG_LEVEL_ERROR, "output count: " + wf::get_core().output_layout->get_outputs().size(), "DBUS PLUGIN", 32);

    for (wayfire_view m_view : wf::get_core().get_all_views())
    {
      m_view->connect_signal("app-id-changed",
                             &view_app_id_changed);

      m_view->connect_signal("title-changed",
                             &view_title_changed);

      m_view->connect_signal("geometry-changed",
                             &view_geometry_changed);

      m_view->connect_signal("unmap",
                             &view_closed);

      m_view->connect_signal("tiled",
                             &view_tiled);

      // m_view->connect_signal("fullscreen", &view_fullscreened);

      // this->emit_signal("decoration-state-updated", &data);
      // view->emit_signal("map", &data);
      // emit_signal("unmap", &data);
      // emit_signal("disappeared", &data);
      // emit_signal("pre-unmap", &data);
    }
    wf::get_core().connect_signal("view-move-to-output",
                                  &view_output_move_requested);

    wf::get_core().connect_signal("view-moved-to-output",
                                  &view_output_moved);

    // wf::get_core().output_layout->connect_signal("configuration-changed", &output_layout_configuration_changed);
    wf::get_core().output_layout->connect_signal("output-added",
                                                 &output_layout_output_added);

    wf::get_core().output_layout->connect_signal("output-removed",
                                                 &output_layout_output_removed);

    wf::get_core().connect_signal("pointer_button",
                                  &pointer_button_signal);

    wf::get_core().connect_signal("tablet_button",
                                  &tablet_button_signal);

    /************* LOAD DBUS SERVICE THREAD *************/

    // g_main_context_push_thread_default(wf_context);

    // GMainContext *wwf_context = g_main_context_new();
    //the end of the attached source needs to quit the event loop
    // wf_context = g_main_context_new();
    // wf_event_loop = g_main_loop_new(wf_context, FALSE);

    dbus_context = g_main_context_new();
    g_thread_new("dbus_thread",
                 dbus_thread_exec_function,
                 g_main_context_ref(dbus_context));

    g_main_context_invoke_full(dbus_context,
                               G_PRIORITY_DEFAULT, //int priority
                               reinterpret_cast<GSourceFunc>(acquire_bus),
                               nullptr, //data to pass
                               nullptr);
  }

  ~dbus_interface_t()
  {
    LOG(wf::log::LOG_LEVEL_ERROR, "DBUS UNLOAD", "DBUS PLUGIN", 42);
    // for (int i = sticky_layers.size(); i -- > 0; )
    // {
    //   output->workspace->destroy_sublayer(sticky_layer);
    // }
    // for (int i = wf_outputs.size(); i-- > 0;)
    // {
    //   wf::output_t *output = wf_outputs[i];
    //   output->workspace->destroy_sublayer(sticky_layers[i]);
    // }
    g_bus_unown_name(owner_id);
    g_main_loop_quit(dbus_event_loop);
    g_dbus_node_info_unref(introspection_data);
  }
  wf::signal_connection_t pointer_button_signal{[=](wf::signal_data_t *data) {
    LOG(wf::log::LOG_LEVEL_ERROR, "Some input signal:");
    auto ev = static_cast<wf::input_event_signal<wlr_event_pointer_button> *>(data);

    // nonstd::observer_ptr<input_event_signal> device;
    // device = static_cast<nonstd::observer_ptr<input_event_signal>>(data);
    LOG(wf::log::LOG_LEVEL_ERROR, "Some input signal casted:");

    // GVariant *signal_data = g_variant_new("(u)", m_view->get_id());
    // g_variant_ref(signal_data);
    bus_emit_signal("pointer_clicked", nullptr);
  }};
  wf::signal_connection_t tablet_button_signal{[=](wf::signal_data_t *data) {
    LOG(wf::log::LOG_LEVEL_ERROR, "Some tablet_button_signal signal:");
    bus_emit_signal("tablet_touched", nullptr);

    // LOG(wf::log::LOG_LEVEL_ERROR, "Some tablet_button_signal signal casted:");
  }};

  wf::signal_connection_t dbus_activation_test{[=](wf::signal_data_t *data) {
    LOG(wf::log::LOG_LEVEL_ERROR, "dbus_activation_test");

    // LOG(wf::log::LOG_LEVEL_ERROR, "Some tablet_button_signal signal casted:");
  }};

  wf::signal_connection_t output_view_added{[=](wf::signal_data_t *data) {
    wayfire_view m_view = get_signaled_view(data);
    if (m_view == nullptr)
    {
      LOG(wf::log::LOG_LEVEL_ERROR, "WAYFIRE BUG: ",
          "output_view_added but signaled view is nullptr");
      return;
    }
    GVariant *signal_data = g_variant_new("(u)", m_view->get_id());
    g_variant_ref(signal_data);
    bus_emit_signal("view_added", signal_data);

    m_view->connect_signal("app-id-changed", &view_app_id_changed);
    m_view->connect_signal("title-changed", &view_title_changed);
    m_view->connect_signal("geometry-changed", &view_geometry_changed);
    m_view->connect_signal("unmap", &view_closed);
    m_view->connect_signal("tiled", &view_tiled);

    //wf_gtk_shell s = wf::get_core().gtk_shell;

    // m_view->connect_signal("fullscreen", &view_fullscreened);
    // wf::get_core().get_active_output()->wor
    // wf::get_core()
    // grab_interface->name = "minimize";
    // grab_interface->capabilities = wf::CAPABILITY_MANAGE_DESKTOP;
    //xdotool
    // wf::get_core().warp_cursor
    //for emit click
    //you could try to simulate a wlr event
    //shouldn't be too hard
    //for example, there's the virtual-pointer protocol so it is possible to have virtual devices, you can then simulate everything you want

    // wayfire's architecture is that plugins rely on each other as little as possible, so currently there are very few signals between them
    // tux2020	the thing is for example, if I want to trigger expo plugin
    // I will now have to change the expo plugin
    // at some point I may want to trigger a different plugin via dbus
    // than I have to change that plugin
    // ammen99_	I think that in this particular case, what we are missing is activating an activator binding programmatically
    // which is indeed something we could add
    // because activator bindings can be activated by just about anything
    // tux2020	but, any application that is focused would also get this binding
    // ammen99_	nooooo
    // activator binding can be triggered by different sources
    // so you could add a virtual source for activator bindings
    // tux2020	I guess this is the same what  I mean that
    // than*
    // ammen99_	yes but it works only for activator bindings
    // you can't start the move plugin for example, because it does require a pointer button
  }};

  wf::signal_connection_t view_closed{[=](wf::signal_data_t *data) {
    wayfire_view m_view = get_signaled_view(data);
    if (m_view == nullptr)
    {
      LOG(wf::log::LOG_LEVEL_ERROR, "WAYFIRE BUG: ",
          "view_closed but signaled view is nullptr");
      return;
    }
    GVariant *signal_data = g_variant_new("(u)", m_view->get_id());
    g_variant_ref(signal_data);
    bus_emit_signal("view_closed", signal_data);
  }};

  wf::signal_connection_t view_app_id_changed{[=](wf::signal_data_t *data) {
    wayfire_view m_view = get_signaled_view(data);
    if (m_view == nullptr)
    {
      LOG(wf::log::LOG_LEVEL_ERROR, "WAYFIRE BUG: ",
          "view_app_id_changed but signaled view is nullptr");
      return;
    }
    GVariant *signal_data = g_variant_new("(us)",
                                          m_view->get_id(),
                                          m_view->get_app_id());
    g_variant_ref(signal_data);
    bus_emit_signal("view_app_id_changed", signal_data);

    LOG(wf::log::LOG_LEVEL_ERROR, "view_app_id_changed: " + m_view->get_app_id() + " on " + m_view->get_output()->to_string(), "DBUS PLUGIN", 32);
  }};

  wf::signal_connection_t view_title_changed{[=](wf::signal_data_t *data) {
    wayfire_view m_view = get_signaled_view(data);
    if (m_view == nullptr)
    {
      LOG(wf::log::LOG_LEVEL_ERROR, "WAYFIRE BUG: ",
          "view_title_changed but signaled view is nullptr");
      return;
    }
    GVariant *signal_data = g_variant_new("(us)", m_view->get_id(), m_view->get_title().c_str());
    g_variant_ref(signal_data);
    bus_emit_signal("view_title_changed", signal_data);

    LOG(wf::log::LOG_LEVEL_ERROR, "view_title_changed: " + m_view->get_app_id() + " on " + m_view->get_title(), "DBUS PLUGIN", 32);
  }};

  wf::signal_connection_t view_fullscreen_changed{[=](wf::signal_data_t *data) {
    LOG(wf::log::LOG_LEVEL_ERROR, "view_state_changed: view_fullscreened");

    view_fullscreen_signal *signal = static_cast<view_fullscreen_signal *>(data);
    wayfire_view m_view = signal->view;

    GVariant *signal_data = g_variant_new("(ub)", m_view->get_id(), signal->state);
    g_variant_ref(signal_data);
    bus_emit_signal("view_fullscreen_changed", signal_data);
  }};

  wf::signal_connection_t view_geometry_changed{[=](wf::signal_data_t *data) {
    // wayfire_view closed_view = get_signaled_view(data);
    // LOG(wf::log::LOG_LEVEL_ERROR, "geometry_changed VIEW: " + closed_view->get_app_id() + " on " + closed_view->get_output()->to_string(), "DBUS PLUGIN", 32);
    GVariant *signal_data = g_variant_new("(s)", "test");
    g_variant_ref(signal_data);
    bus_emit_signal("view_geometry_changed", signal_data);
  }};

  wf::signal_connection_t view_tiled{[=](wf::signal_data_t *data) {
    // LOG(wf::log::LOG_LEVEL_ERROR, "tiled VIEW: ", "DBUS PLUGIN", 32);
    GVariant *signal_data = g_variant_new("(s)", "test");
    g_variant_ref(signal_data);
    bus_emit_signal("view_tiling_changed", signal_data);
  }};

  wf::signal_connection_t view_output_moved{[=](wf::signal_data_t *data) {
    LOG(wf::log::LOG_LEVEL_ERROR, "view_output_moved: ", "DBUS PLUGIN");
    wf::view_move_to_output_signal *signal = static_cast<wf::view_move_to_output_signal *>(data);
    wayfire_view view = signal->view;
    wf::output_t *old_output = signal->old_output;
    wf::output_t *new_output = signal->new_output;

    if (view->desired_layer == 1)
    {
      LOG(wf::log::LOG_LEVEL_ERROR, "view_output_movedmove SUBLAYER!!!");

      // nonstd::observer_ptr<wf::sublayer_t> sticky_layer;
      // sticky_layer = new_output->workspace->create_sublayer(wf::LAYER_WORKSPACE,
      //  wf::SUBLAYER_DOCKED_ABOVE);
      //  sticky_layer = get_sticky_layer_from_output_id(new_output->get_id());
      // workspace->above_layer = create_sublayer(wf::LAYER_WORKSPACE, wf::SUBLAYER_DOCKED_ABOVE);
      LOG(wf::log::LOG_LEVEL_ERROR, "output_changed got SUBLAYER from vec");
      new_output->workspace->add_view_to_sublayer(view,
                                                  new_output->workspace->above_layer);
    }

    GVariant *signal_data = g_variant_new("(uuu)", view->get_id(),
                                          old_output->get_id(),
                                          new_output->get_id());
    g_variant_ref(signal_data);
    bus_emit_signal("view_output_moved", signal_data);
  }};
  wf::signal_connection_t view_output_move_requested{[=](wf::signal_data_t *data) {
    // See force fullscreen
    LOG(wf::log::LOG_LEVEL_ERROR, "output_changed not connected signal may cause crash?: ", "DBUS PLUGIN", 32);
    wf::view_move_to_output_signal *signal = static_cast<wf::view_move_to_output_signal *>(data);
    wayfire_view view = signal->view;

    wf::output_t *old_output = signal->old_output;
    wf::output_t *new_output = signal->new_output;

    if (view->desired_layer == 1)
    {
      LOG(wf::log::LOG_LEVEL_ERROR, "output_changed move SUBLAYER!!!",
          "remove from old output");
      old_output->workspace->add_view(view,
                                      (wf::layer_t)old_output->workspace->get_view_layer(view));
    }

    //    no one listens
    // GVariant *signal_data = g_variant_new("(uuu)", view->get_id(),
    //                                       old_output->get_id(),
    //                                       new_output->get_id());
    // g_variant_ref(signal_data);
    // bus_emit_signal("view_output_move_requested", signal_data);
  }};
  //******************************Custom Slots***************************//
  wf::signal_connection_t role_changed{[=](wf::signal_data_t *data) {
    LOG(wf::log::LOG_LEVEL_ERROR, "view_state_changed: role_changed");
    wayfire_view m_view = get_signaled_view(data);
    if (m_view == nullptr)
    {
      LOG(wf::log::LOG_LEVEL_ERROR, "WAYFIRE BUG: ",
          "m_view but signaled view is nullptr");
      return;
    }
    uint role = 0;

    if (m_view->role == wf::VIEW_ROLE_TOPLEVEL)
      role = 1;
    else if (m_view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
      role = 2;
    else if (m_view->role == wf::VIEW_ROLE_UNMANAGED)
      role = 3;

    GVariant *signal_data = g_variant_new("(uu)", m_view->get_id(), role);
    g_variant_ref(signal_data);
    bus_emit_signal("view_role_changed", signal_data);
  }};
  //******************************Output Slots***************************//
  wf::signal_connection_t output_configuration_changed{[=](wf::signal_data_t *data) {
    LOG(wf::log::LOG_LEVEL_ERROR, "output_configuration_changed VIEW: ", "DBUS PLUGIN", 32);
  }};

  wf::signal_connection_t view_workspaces_changed{[=](wf::signal_data_t *data) {
    LOG(wf::log::LOG_LEVEL_ERROR, "view viewport_changed : ", "DBUS PLUGIN");
    view_change_viewport_signal *signal = static_cast<view_change_viewport_signal *>(data);
    wayfire_view view = signal->view;
    /****************************************************************
     * It it useless to take signal from and to as they only condier
     * One workspace for to and from
     * though a user may put in the square of 4
     ****************************************************************/
    GVariant *signal_data;
    signal_data = g_variant_new("(u)", view->get_id());

    g_variant_ref(signal_data);
    bus_emit_signal("view_workspaces_changed", signal_data);
  }};

  wf::signal_connection_t output_workspace_changed{[=](wf::signal_data_t *data) {
    change_viewport_signal *signal = static_cast<change_viewport_signal *>(data);
    int newHorizontalWorkspace = signal->new_viewport.x;
    int newVerticalWorkspace = signal->new_viewport.y;
    wf::output_t *output = signal->output;

    GVariant *signal_data = g_variant_new("(uii)", output->get_id(),
                                          newHorizontalWorkspace,
                                          newVerticalWorkspace);

    g_variant_ref(signal_data);
    bus_emit_signal("output_workspace_changed", signal_data);
  }};

  wf::signal_connection_t output_view_maximized{[=](wf::signal_data_t *data) {
    LOG(wf::log::LOG_LEVEL_ERROR, "view_state_changed: output_view_minimized");
    view_tiled_signal *signal = static_cast<view_tiled_signal *>(data);
    wayfire_view m_view = signal->view;
    const bool maximized = (signal->edges == wf::TILED_EDGES_ALL);

    GVariant *signal_data = g_variant_new("(ub)",
                                          m_view->get_id(),
                                          maximized);
    g_variant_ref(signal_data);
    bus_emit_signal("view_maximized_changed", signal_data);
  }};

  wf::signal_connection_t output_view_minimized{[=](wf::signal_data_t *data) {
    LOG(wf::log::LOG_LEVEL_ERROR, "view_state_changed: output_view_minimized");

    view_minimize_request_signal *signal = static_cast<view_minimize_request_signal *>(data);
    wayfire_view m_view = signal->view;
    const bool minimized = signal->state;

    if (m_view->desired_layer == 1 && !minimized)
    {
      m_view->get_output()->workspace->add_view_to_sublayer(m_view,
                                                            m_view->get_output()->workspace->above_layer);
    }
    GVariant *signal_data = g_variant_new("(ub)",
                                          m_view->get_id(),
                                          minimized);
    g_variant_ref(signal_data);
    bus_emit_signal("view_minimized_changed", signal_data);
  }};

  wf::signal_connection_t output_view_focus_changed{[=](wf::signal_data_t *data) {
    focus_view_signal *signal = static_cast<focus_view_signal *>(data);
    wayfire_view view = signal->view;
    uint view_id = view->get_id();

    if (view_id == focused_view_id || view->role != wf::VIEW_ROLE_TOPLEVEL)
    {
      LOG(wf::log::LOG_LEVEL_ERROR, "focus_view_signal: ignoring");
      return;
    }
    LOG(wf::log::LOG_LEVEL_ERROR, "focus_view_signal: ", view_id, view->get_title());
    if (view->has_data("view-demands-attention"))
      view->erase_data("view-demands-attention");

    focused_view_id = view_id;
    GVariant *signal_data = g_variant_new("(u)", view_id);
    g_variant_ref(signal_data);
    bus_emit_signal("view_focus_changed", signal_data);
  }};
  wf::signal_connection_t view_self_request_focus{[=](wf::signal_data_t *data) {
    view_self_request_focus_signal *signal = static_cast<view_self_request_focus_signal *>(data);
    wayfire_view view = signal->view;
    wf::output_t *active_output = wf::get_core().get_active_output();
    wf::get_core().move_view_to_output(view, active_output);

    if (view->desired_layer == 1)
    {
      active_output->workspace->add_view_to_sublayer(view,
                                                     active_output->workspace->above_layer);
    }
    else
    {
      active_output->workspace->add_view(view, wf::LAYER_WORKSPACE);
    }

    view->set_activated(true);
  }};

  wf::signal_connection_t view_hints_changed{[=](wf::signal_data_t *data) {
    view_hints_changed_signal *signal = static_cast<view_hints_changed_signal *>(data);
    wayfire_view view = signal->view;
    bool view_wants_attention = false;
    if (!view)
    {
      LOG(wf::log::LOG_LEVEL_ERROR, "view_hints_changed",
          "no such view");
    }
    LOG(wf::log::LOG_LEVEL_ERROR, "view_hints_changed",
        view->has_data("view-demands-attention"));
    LOG(wf::log::LOG_LEVEL_ERROR, "view_hints_changed",
        view->has_data("view-demands-attention"));
    LOG(wf::log::LOG_LEVEL_ERROR, "view_hints_changed",
        view->has_data("view-demands-attention"));
    LOG(wf::log::LOG_LEVEL_ERROR, "view_hints_changed",
        view->has_data("view-demands-attention"));
    LOG(wf::log::LOG_LEVEL_ERROR, "view_hints_changed",
        view->has_data("view-demands-attention"));
    LOG(wf::log::LOG_LEVEL_ERROR, "view_hints_changed",
        view->has_data("view-demands-attention"));

    if (view->has_data("view-demands-attention"))
      view_wants_attention = true;

    GVariant *signal_data = g_variant_new("(ub)",
                                          view->get_id(),
                                          view_wants_attention);
    g_variant_ref(signal_data);
    bus_emit_signal("view_attention_changed", signal_data);
  }};

  wf::signal_connection_t output_detach_view{[=](wf::signal_data_t *data) {
    // wayfire_view closed_view = get_signaled_view(data);
    // LOG(wf::log::LOG_LEVEL_ERROR, "detach_view VIEW: " + closed_view->get_app_id() + " on " + closed_view->get_output()->to_string(), "DBUS PLUGIN", 32);
  }};

  wf::signal_connection_t output_view_disappeared{[=](wf::signal_data_t *data) {
    // wayfire_view closed_view = get_signaled_view(data);
    // LOG(wf::log::LOG_LEVEL_ERROR, "view_disappeared VIEW: " + closed_view->get_app_id() + " on " + closed_view->get_output()->to_string(), "DBUS PLUGIN", 32);
  }};
  wf::signal_connection_t output_view_attached{[=](wf::signal_data_t *data) {
    // wayfire_view closed_view = get_signaled_view(data);
    // LOG(wf::log::LOG_LEVEL_ERROR, "view_attached VIEW: " + closed_view->get_app_id() + " on " + std::to_string(closed_view->role), "DBUS PLUGIN", 32);
  }};
  wf::signal_connection_t output_view_moving{[=](wf::signal_data_t *data) {
    //   // wayfire_view closed_view = get_signaled_view(data);
    //   // LOG(wf::log::LOG_LEVEL_ERROR, "moving VIEW: " + closed_view->get_app_id() + " on " + closed_view->get_output()->to_string(), "DBUS PLUGIN", 32);
    GVariant *signal_data = g_variant_new("(s)", "test");
    g_variant_ref(signal_data);
    bus_emit_signal("view_moving_changed", signal_data);
  }};
  wf::signal_connection_t output_view_resizing{[=](wf::signal_data_t *data) {
    //   // wayfire_view closed_view = get_signaled_view(data);
    //   // LOG(wf::log::LOG_LEVEL_ERROR, "moving VIEW: " + closed_view->get_app_id() + " on " + closed_view->get_output()->to_string(), "DBUS PLUGIN", 32);
    GVariant *signal_data = g_variant_new("(s)", "test");
    g_variant_ref(signal_data);
    bus_emit_signal("view_resizing_changed", signal_data);
  }};

  wf::signal_connection_t output_view_decoration_changed{[=](wf::signal_data_t *data) {
    //   // wayfire_view d_view = get_signaled_view(data);
    //   // LOG(wf::log::LOG_LEVEL_ERROR, "decoration-state-updated-view" + d_view->get_app_id() + " on " + d_view->get_output()->to_string(), "DBUS PLUGIN", 32);
  }};

  //******************************Output Layout Slots***************************//
  // wf::signal_connection_t output_layout_configuration_changed{[=](wf::signal_data_t *data) {
  //   LOG(wf::log::LOG_LEVEL_ERROR, "output_config_changed VIEW: ", "DBUS PLUGIN", 32);
  // }};

  wf::signal_connection_t output_layout_output_added{[=](wf::signal_data_t *data) {
    LOG(wf::log::LOG_LEVEL_ERROR, "output_added VIEW: ", "DBUS PLUGIN", 32);
    wf::output_t *wf_output = get_signaled_output(data);
    /**
      These are better to listen to from the view itself
      each view signal cause the corresponding output to call them
      **/
    // wf_output->connect_signal("attach-view", &output_view_attached);
    // wf_output->connect_signal("detach-view", &detach_view);
    // wf_output->connect_signal("view-disappeared", &view_disappeared);
    // wf_output->connect_signal("decoration-state-updated-view", &output_view_decoration_changed);
    // Skip already connected outputs
    // if (std::find(connected_wf_outputs.begin(), connected_wf_outputs.end(), wf_output) != connected_wf_outputs.end())
    auto search = connected_wf_outputs.find(wf_output);
    if (search != connected_wf_outputs.end())
      return;

    wf_output->connect_signal("view-fullscreen-request",
                              &view_fullscreen_changed);

    wf_output->connect_signal("map-view",
                              &output_view_added);

    wf_output->connect_signal("output-configuration-changed",
                              &output_configuration_changed);

    wf_output->connect_signal("view-minimize-request",
                              &output_view_minimized);

    wf_output->connect_signal("view-maximized-request",
                              &output_view_maximized);

    wf_output->connect_signal("move-request",
                              &output_view_moving);

    wf_output->connect_signal("view-change-viewport",
                              &view_workspaces_changed);

    wf_output->connect_signal("viewport-changed",
                              &output_workspace_changed);

    wf_output->connect_signal("view-self-request-focus",
                              &view_self_request_focus);

    wf_output->connect_signal("view-hints-changed",
                              &view_hints_changed);

    wf_output->connect_signal("resize-request",
                              &output_view_resizing);

    // wf_output->connect_signal("fullscreen-layer-focused", &fullscreen_focus_changed);
    // wf_output->connect_signal("set-workspace-request", &output_workspace_changed);
    wf_output->connect_signal("focus-view", &output_view_focus_changed);
    //    view->get_output()->emit_signal("wm-focus-request", &data);
    // utput.cpp:        emit_signal("focus-view", &data);
    // active_output->emit_signal("output-gain-focus", nullptr);
    //  view->get_output()->emit_signal("wm-focus-request", &data);

    wf_output->connect_signal("layer-attach-view", &role_changed);
    wf_output->connect_signal("layer-detach-view", &role_changed);

    if (wf_output->workspace->above_layer == nullptr)
      wf_output->workspace->above_layer = wf_output->workspace->create_sublayer(wf::LAYER_WORKSPACE,
                                                                                wf::SUBLAYER_DOCKED_ABOVE);
    connected_wf_outputs.insert(wf_output);

    // nonstd::observer_ptr<wf::sublayer_t> sticky_layer;
    // sticky_layer = wf_output->workspace->create_sublayer(wf::LAYER_WORKSPACE,
    //                                                      wf::SUBLAYER_DOCKED_ABOVE);
    // sticky_layers.push_back(sticky_layer);
  }};

  wf::signal_connection_t output_layout_output_removed{[=](wf::signal_data_t *data) {
    LOG(wf::log::LOG_LEVEL_ERROR, "output_removed VIEW: ", "DBUS PLUGIN", 32);
    wf::output_t *wf_output = get_signaled_output(data);

    auto search = connected_wf_outputs.find(wf_output);
    if (search != connected_wf_outputs.end())
      connected_wf_outputs.erase(wf_output);

    //maybe use pre-removed instead?
  }};
};

DECLARE_WAYFIRE_PLUGIN((wf::singleton_plugin_t<dbus_interface_t, true>)); //bool = unloadable
