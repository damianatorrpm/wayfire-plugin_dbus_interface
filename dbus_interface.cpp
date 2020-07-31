/*******************************************************************
 * This file is licensed under the MIT license.
 * Copyright (C) 2019 - 2020 Damian Ivanov <damianatorrpm@gmail.com>
 ********************************************************************/

#define HAS_CUSTOM 0
extern "C"
{
#include <gio/gio.h>
#include <sys/socket.h>
#include <sys/types.h>
};

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
  public:
    /************* Connect all signals for already existing objects **************/
    dbus_interface_t()
    {
        LOG(wf::log::LOG_LEVEL_DEBUG, "Loading DBus Plugin");

        for (wf::output_t* output : wf_outputs)
        {
            output->connect_signal("view-mapped",
                                   &output_view_added);

            output->connect_signal("wm-actions-view-above-changed",
                                   &on_view_keep_above);

            output->connect_signal("output-configuration-changed",
                                   &output_configuration_changed);

            output->connect_signal("view-minimize-request",
                                   &output_view_minimized);

            output->connect_signal("view-tile-request",
                                   &output_view_maximized);

            output->connect_signal("view-move-request",
                                   &output_view_moving);

            output->connect_signal("view-resize-request",
                                   &output_view_resizing);

            output->connect_signal("view-change-viewport",
                                   &view_workspaces_changed);

            output->connect_signal("workspace-changed",
                                   &output_workspace_changed);

            output->connect_signal("view-layer-attached",
                                   &role_changed);

            output->connect_signal("view-layer-detached",
                                   &role_changed);

            output->connect_signal("view-focused",
                                   &output_view_focus_changed);

            output->connect_signal("view-fullscreen-request",
                                   &view_fullscreen_changed);

            LOG(wf::log::LOG_LEVEL_DEBUG, "output connected");
            connected_wf_outputs.insert(output);
        }

        for (wayfire_view view : core.get_all_views())
        {
            view->connect_signal("app-id-changed",
                                 &view_app_id_changed);

            view->connect_signal("title-changed",
                                 &view_title_changed);

            view->connect_signal("geometry-changed",
                                 &view_geometry_changed);

            view->connect_signal("unmapped",
                                 &view_closed);

            view->connect_signal("tiled",
                                 &view_tiled);
        }

        /****************** Connect core signals ***********************/

        core.connect_signal("view-hints-changed",
                            &view_hints_changed);

        core.connect_signal("view-self-request-focus",
                            &view_self_request_focus);

        core.connect_signal("view-pre-moved-to-output",
                            &view_output_move_requested);

        core.connect_signal("view-moved-to-output",
                            &view_output_moved);

        core.connect_signal("pointer_button",
                            &pointer_button_signal);

        core.connect_signal("tablet_button",
                            &tablet_button_signal);

        core.output_layout->connect_signal("output-added",
                                           &output_layout_output_added);

        core.output_layout->connect_signal("output-removed",
                                           &output_layout_output_removed);

        /************* LOAD DBUS SERVICE THREAD *************/
        dbus_context = g_main_context_new();
        g_thread_new("dbus_thread",
                     dbus_thread_exec_function,
                     g_main_context_ref(dbus_context));

        g_main_context_invoke_full(dbus_context,
                                   G_PRIORITY_HIGH,
                                   reinterpret_cast<GSourceFunc> (acquire_bus),
                                   nullptr,
                                   nullptr);
    }

    ~dbus_interface_t()
    {
        /*
         * There are probably a lot of things missing here.
         *
         * For my use-case it should never be unloaded.
         * Feel free to open PR for clean unloading.
         */
        LOG(wf::log::LOG_LEVEL_DEBUG, "Unloading DBus Plugin");

        g_bus_unown_name(owner_id);
        g_main_loop_quit(dbus_event_loop);
        g_dbus_node_info_unref(introspection_data);
    }

    /******************************View Related Slots***************************/
    /***
     * A pointer button is interacted with
     ***/
    wf::signal_connection_t pointer_button_signal
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "pointer_button_signal");

            wf::pointf_t cursor_position;
            GVariant* signal_data;
            wf::input_event_signal<wlr_event_pointer_button>* wf_ev;
            wlr_event_pointer_button* wlr_signal;
            wlr_button_state button_state;
            bool button_released;
            uint32_t button;

            cursor_position = core.get_cursor_position();
            wf_ev = static_cast<wf::input_event_signal
                                <wlr_event_pointer_button>*> (data);
            wlr_signal = static_cast<wlr_event_pointer_button*> (wf_ev->event);
            button_state = wlr_signal->state;
            button = wlr_signal->button;
            button_released = (button_state == WLR_BUTTON_RELEASED);

            signal_data = g_variant_new("(ddub)",
                                        cursor_position.x,
                                        cursor_position.y,
                                        button,
                                        button_released);
            g_variant_ref(signal_data);
            bus_emit_signal("pointer_clicked", signal_data);
        }
    };

    /***
     * A tablet button is interacted with
     ***/
    wf::signal_connection_t tablet_button_signal
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "tablet_button_signal");
            bus_emit_signal("tablet_touched", nullptr);
        }
    };

    /***
     * A new view is added to an output.
     ***/
    wf::signal_connection_t output_view_added
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "output_view_added");

            GVariant* signal_data;
            wayfire_view view;

            view = get_signaled_view(data);
            if (!view)
            {
                LOG(wf::log::LOG_LEVEL_DEBUG, "output_view_added no view");

                return;
            }

            signal_data = g_variant_new("(u)", view->get_id());
            g_variant_ref(signal_data);
            bus_emit_signal("view_added", signal_data);

            view->connect_signal("app-id-changed", &view_app_id_changed);
            view->connect_signal("title-changed", &view_title_changed);
            view->connect_signal("geometry-changed", &view_geometry_changed);
            view->connect_signal("unmapped", &view_closed);
            view->connect_signal("tiled", &view_tiled);
        }
    };

    /***
     * The view has closed.
     ***/
    wf::signal_connection_t view_closed
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "view_closed");

            GVariant* signal_data;
            wayfire_view view;

            view = get_signaled_view(data);

            if (!view)
            {
                LOG(wf::log::LOG_LEVEL_DEBUG, "view_closed no view");

                return;
            }

            signal_data = g_variant_new("(u)", view->get_id());
            g_variant_ref(signal_data);
            bus_emit_signal("view_closed", signal_data);
        }
    };

    /***
     * The view's app_id has changed.
     ***/
    wf::signal_connection_t view_app_id_changed
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "view_app_id_changed");

            GVariant* signal_data;
            wayfire_view view;

            view = get_signaled_view(data);
            if (!view)
            {
                LOG(wf::log::LOG_LEVEL_DEBUG, "view_app_id_changed no view");

                return;
            }

            signal_data = g_variant_new("(us)",
                                        view->get_id(),
                                        view->get_app_id());
            g_variant_ref(signal_data);
            bus_emit_signal("view_app_id_changed", signal_data);
        }
    };

    /***
     * The view's title has changed.
     ***/
    wf::signal_connection_t view_title_changed
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "view_title_changed");

            GVariant* signal_data;
            wayfire_view view;

            view = get_signaled_view(data);
            if (!view)
            {
                LOG(wf::log::LOG_LEVEL_DEBUG, "view_title_changed no view");

                return;
            }

            signal_data = g_variant_new("(us)",
                                        view->get_id(),
                                        view->get_title().c_str());
            g_variant_ref(signal_data);
            bus_emit_signal("view_title_changed", signal_data);
        }
    };

    /***
     * The view's fullscreen status has changed.
     ***/
    wf::signal_connection_t view_fullscreen_changed
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "view_fullscreened");

            wf::view_fullscreen_signal* signal;
            GVariant* signal_data;
            wayfire_view view;

            signal = static_cast<wf::view_fullscreen_signal*> (data);
            view = signal->view;
            signal_data = g_variant_new("(ub)",
                                        view->get_id(),
                                        signal->state);
            g_variant_ref(signal_data);
            bus_emit_signal("view_fullscreen_changed", signal_data);
        }
    };

    /***
     * The view's geometry has changed.
     ***/
    wf::signal_connection_t view_geometry_changed
    {
        [=] (wf::signal_data_t* data)
        {
            if (!geometry_signal_enabled)
            {
                return;
            }

            LOG(wf::log::LOG_LEVEL_DEBUG, "view_geometry_changed");

            GVariant* signal_data;
            wayfire_view view;
            wf::geometry_t geometry;

            view = get_signaled_view(data);
            geometry = view->get_output_geometry();
            signal_data = g_variant_new("(uiiii)",
                                        view->get_id(),
                                        geometry.x,
                                        geometry.y,
                                        geometry.width,
                                        geometry.height);
            g_variant_ref(signal_data);
            bus_emit_signal("view_geometry_changed", signal_data);
        }
    };

    /***
     * The view's tiling status has changed.
     ***/
    wf::signal_connection_t view_tiled
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "view_tiled");

            GVariant* signal_data;
            wf::view_tiled_signal* signal;
            wayfire_view view;

            signal = static_cast<wf::view_tiled_signal*> (data);
            view = signal->view;

            if (!view)
            {
                return;
            }

            signal_data = g_variant_new("(uu)",
                                        view->get_id(),
                                        signal->new_edges);
            g_variant_ref(signal_data);
            bus_emit_signal("view_tiling_changed", signal_data);
        }
    };

    /***
     * The view's output has changed.
     ***/
    wf::signal_connection_t view_output_moved
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "view_output_moved");

            wf::view_moved_to_output_signal* signal;
            GVariant* signal_data;
            wayfire_view view;
            wf::output_t* old_output;
            wf::output_t* new_output;

            signal = static_cast<wf::view_moved_to_output_signal*> (data);
            view = signal->view;

            if (!view)
            {
                return;
            }

            old_output = signal->old_output;
            new_output = signal->new_output;

            signal_data = g_variant_new("(uuu)", view->get_id(),
                                        old_output->get_id(),
                                        new_output->get_id());
            g_variant_ref(signal_data);
            bus_emit_signal("view_output_moved", signal_data);
        }
    };

    /***
     * The view's output is about to change.
     ***/
    wf::signal_connection_t view_output_move_requested
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "view_output_move_requested");

            GVariant* signal_data;
            wf::view_pre_moved_to_output_signal* signal;
            wf::output_t* old_output;
            wf::output_t* new_output;
            wayfire_view view;

            signal = static_cast<wf::view_pre_moved_to_output_signal*> (data);
            view = signal->view;

            if (view)
            {
                old_output = signal->old_output;
                new_output = signal->new_output;
                signal_data = g_variant_new("(uuu)",
                                            view->get_id(),
                                            old_output->get_id(),
                                            new_output->get_id());
                g_variant_ref(signal_data);
                bus_emit_signal("view_output_move_requested", signal_data);
            }
        }
    };

    /***
     * The view's role has changed.
     ***/
    wf::signal_connection_t role_changed
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "role_changed");

            GVariant* signal_data;
            wayfire_view view;

            view = get_signaled_view(data);

            if (!view)
            {
                LOG(wf::log::LOG_LEVEL_DEBUG, "role_changed no view");

                return;
            }

            uint role = 0;

            if (view->role == wf::VIEW_ROLE_TOPLEVEL)
            {
                role = 1;
            }
            else
            if (view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
            {
                role = 2;
            }
            else
            if (view->role == wf::VIEW_ROLE_UNMANAGED)
            {
                role = 3;
            }

            signal_data = g_variant_new("(uu)", view->get_id(), role);
            g_variant_ref(signal_data);
            bus_emit_signal("view_role_changed", signal_data);
        }
    };

    /***
     * The view's workspaces have changed.
     ***/
    wf::signal_connection_t view_workspaces_changed
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "view_workspaces_changed");

            GVariant* signal_data;
            view_change_viewport_signal* signal;
            wayfire_view view;

            signal = static_cast<view_change_viewport_signal*> (data);
            view = signal->view;

            if (!view)
            {
                return;
            }

            signal_data = g_variant_new("(u)", view->get_id());

            g_variant_ref(signal_data);
            bus_emit_signal("view_workspaces_changed", signal_data);
        }
    };

    /***
     * The view's maximized status has changed.
     ***/
    wf::signal_connection_t output_view_maximized
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "output_view_maximized");

            wf::view_tiled_signal* signal;
            GVariant* signal_data;
            wayfire_view view;
            bool maximized;

            signal = static_cast<wf::view_tiled_signal*> (data);
            view = signal->view;

            if (!view)
            {
                return;
            }

            maximized = (signal->new_edges == wf::TILED_EDGES_ALL);
            signal_data = g_variant_new("(ub)",
                                        view->get_id(),
                                        maximized);
            g_variant_ref(signal_data);
            bus_emit_signal("view_maximized_changed", signal_data);
        }
    };

    /***
     * The view's minimized status has changed.
     ***/
    wf::signal_connection_t output_view_minimized
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "output_view_minimized");

            GVariant* signal_data;
            wf::view_minimize_request_signal* signal;
            wayfire_view view;
            bool minimized;

            signal = static_cast<wf::view_minimize_request_signal*> (data);
            view = signal->view;

            if (!view)
            {
                return;
            }

            minimized = signal->state;
            signal_data = g_variant_new("(ub)",
                                        view->get_id(),
                                        minimized);
            g_variant_ref(signal_data);
            bus_emit_signal("view_minimized_changed", signal_data);
        }
    };

    /***
     * The view's focus has changed.
     ***/
    wf::signal_connection_t output_view_focus_changed
    {
        [=] (wf::signal_data_t* data)
        {
            GVariant* signal_data;
            wf::focus_view_signal* signal;
            wayfire_view view;
            uint view_id;

            signal = static_cast<wf::focus_view_signal*> (data);
            view = signal->view;

            if (!view)
            {
                return;
            }

            view_id = view->get_id();

            if ((view_id == focused_view_id) ||
                (view->role != wf::VIEW_ROLE_TOPLEVEL))
            {
                LOG(wf::log::LOG_LEVEL_DEBUG, "output_view_focus_changed no view");

                return;
            }

            LOG(wf::log::LOG_LEVEL_DEBUG,
                "output_view_focus_changed: ",
                view_id,
                view->get_title());

            if (view->has_data("view-demands-attention"))
            {
                view->erase_data("view-demands-attention");
            }

            focused_view_id = view_id;
            signal_data = g_variant_new("(u)", view_id);
            g_variant_ref(signal_data);
            bus_emit_signal("view_focus_changed", signal_data);
        }
    };

    /***
     * The view hints demands focus
     * Examples:
     *   1) applications that get dbus activated
     *   2) Multiplayer games if game is found.
     ***/
    wf::signal_connection_t view_self_request_focus
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "view_self_request_focus");

            bool reconfigure = true;
            wf::view_self_request_focus_signal* signal;
            wayfire_view view;
            wf::output_t* active_output;
            wf::output_t* view_output;

            signal = static_cast<wf::view_self_request_focus_signal*> (data);
            view = signal->view;

            if (!view)
            {
                return;
            }

            active_output = core.get_active_output();
            view_output = view->get_output();

            if (view_output)
            {
                if (view_output != active_output)
                {
                    core.move_view_to_output(view,
                                             active_output,
                                             reconfigure);
                }
            }

            view->set_activated(true);
        }
    };

    /***
     * The view hints have changed
     * The currently ownly interesting hint
     * is view-demands-attention
     ***/
    wf::signal_connection_t view_hints_changed
    {
        [=] (wf::signal_data_t* data)
        {
            wf::view_hints_changed_signal* signal;
            GVariant* signal_data;
            bool view_wants_attention = false;
            wayfire_view view;

            signal = static_cast<wf::view_hints_changed_signal*> (data);
            view = signal->view;

            if (!view)
            {
                LOG(wf::log::LOG_LEVEL_DEBUG, "view_hints_changed no view");

                return;
            }

            LOG(wf::log::LOG_LEVEL_DEBUG, "view_hints_changed",
                view->has_data("view-demands-attention"));

            if (view->has_data("view-demands-attention"))
            {
                view_wants_attention = true;
            }

            signal_data = g_variant_new("(ub)",
                                        view->get_id(),
                                        view_wants_attention);
            g_variant_ref(signal_data);
            bus_emit_signal("view_attention_changed", signal_data);
        }
    };

    /***
     * The view may or may not be moving now.
     * The status of that has somehow changed.
     * https://github.com/WayfireWM/wayfire/issues/639
     ***/
    wf::signal_connection_t output_view_moving
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "output_view_moving");

            GVariant* signal_data;
            wayfire_view view;

            view = get_signaled_view(data);

            if (!view)
            {
                return;
            }

            signal_data = g_variant_new("(u)", view->get_id());
            g_variant_ref(signal_data);
            bus_emit_signal("view_moving_changed", signal_data);
        }
    };

    /***
     * The view may or may not be resizing now.
     * The status of that has somehow changed.
     * https://github.com/WayfireWM/wayfire/issues/639
     ***/
    wf::signal_connection_t output_view_resizing
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "output_view_resizing");

            GVariant* signal_data;
            wayfire_view view;

            view = get_signaled_view(data);

            if (!view)
            {
                return;
            }

            signal_data = g_variant_new("(u)", view->get_id());
            g_variant_ref(signal_data);
            bus_emit_signal("view_resizing_changed", signal_data);
        }
    };

    /***
     * The wm-actions plugin changed the above_layer
     * state of a view.
     ***/
    wf::signal_connection_t on_view_keep_above
    {
        [=] (wf::signal_data_t* data)
        {
            GVariant* signal_data;
            wayfire_view view;

            view = wf::get_signaled_view(data);

            if (!view)
            {
                return;
            }

            signal_data = g_variant_new("(ub)",
                                        view->get_id(),
                                        view->has_data("wm-actions-above"));
            g_variant_ref(signal_data);
            bus_emit_signal("view_keep_above_changed", signal_data);
        }
    };

    /***
     * The decoration of a view has changed
     ***/
    wf::signal_connection_t output_view_decoration_changed
    {
        [=] (wf::signal_data_t* data)
        {}
    };

    /***
     * No usecase has been found for these 3
     ***/
    wf::signal_connection_t output_detach_view
    {
        [=] (wf::signal_data_t* data)
        {}
    };
    wf::signal_connection_t output_view_disappeared
    {
        [=] (wf::signal_data_t* data)
        {}
    };
    wf::signal_connection_t output_view_attached
    {
        [=] (wf::signal_data_t* data)
        {}
    };

    /******************************Output Related Slots***************************/

    /***
     * If the output configuration is changed somehow,
     * scaling / resolution etc changes, this is emitted
     ***/
    wf::signal_connection_t output_configuration_changed
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "output_configuration_changed");
            bus_emit_signal("output_configuration_changed", nullptr);
        }
    };

    /***
     * The workspace of an output changed
     ***/
    wf::signal_connection_t output_workspace_changed
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "output_workspace_changed");

            wf::workspace_changed_signal* signal;
            GVariant* signal_data;
            wf::output_t* output;
            int newHorizontalWorkspace;
            int newVerticalWorkspace;

            signal = static_cast<wf::workspace_changed_signal*> (data);
            newHorizontalWorkspace = signal->new_viewport.x;
            newVerticalWorkspace = signal->new_viewport.y;
            output = signal->output;
            signal_data = g_variant_new("(uii)",
                                        output->get_id(),
                                        newHorizontalWorkspace,
                                        newVerticalWorkspace);

            g_variant_ref(signal_data);
            bus_emit_signal("output_workspace_changed", signal_data);
        }
    };

    /***
     * A new output has been added
     ***/
    wf::signal_connection_t output_layout_output_added
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "output_layout_output_added");

            wf::output_t* output;
            GVariant* signal_data;

            output = get_signaled_output(data);
            auto search = connected_wf_outputs.find(output);

            if (search != connected_wf_outputs.end())
            {
                return;
            }

            output->connect_signal("wm-actions-view-above-changed",
                                   &on_view_keep_above);

            output->connect_signal("view-fullscreen-request",
                                   &view_fullscreen_changed);

            output->connect_signal("view-mapped",
                                   &output_view_added);

            output->connect_signal("output-configuration-changed",
                                   &output_configuration_changed);

            output->connect_signal("view-minimize-request",
                                   &output_view_minimized);

            output->connect_signal("view-tile-request",
                                   &output_view_maximized);

            output->connect_signal("view-move-request",
                                   &output_view_moving);

            output->connect_signal("view-change-viewport",
                                   &view_workspaces_changed);

            output->connect_signal("workspace-changed",
                                   &output_workspace_changed);

            output->connect_signal("view-resize-request",
                                   &output_view_resizing);

            output->connect_signal("view-focused",
                                   &output_view_focus_changed);

            output->connect_signal("view-layer-attached",
                                   &role_changed);

            output->connect_signal("view-layer-detached",
                                   &role_changed);

            wf_outputs = core.output_layout->get_outputs();
            connected_wf_outputs.insert(output);

            signal_data = g_variant_new("(u)", output->get_id());
            g_variant_ref(signal_data);
            bus_emit_signal("output_added", signal_data);
        }
    };

    /***
     * An output has been removed
     ***/
    wf::signal_connection_t output_layout_output_removed
    {
        [=] (wf::signal_data_t* data)
        {
            LOG(wf::log::LOG_LEVEL_DEBUG, "output_layout_output_removed");

            GVariant* signal_data;
            wf::output_t* output;

            output = get_signaled_output(data);
            auto search = connected_wf_outputs.find(output);

            if (search != connected_wf_outputs.end())
            {
                wf_outputs = core.output_layout->get_outputs();
                connected_wf_outputs.erase(output);

                signal_data = g_variant_new("(u)", output->get_id());
                g_variant_ref(signal_data);
                bus_emit_signal("output_removed", signal_data);
            }

            // maybe use pre-removed instead?
        }
    };
};

DECLARE_WAYFIRE_PLUGIN((wf::singleton_plugin_t<dbus_interface_t, true>));
// bool = unloadable