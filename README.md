DBus Wayfire plugin

Authors: Damian Ivanov

Contributors: Scott Moreau

### Installation
Standard:
meson build && ninja -C build && meson install -C build

### Coding style
* uncrustify.ini in the repo
* follow the style used
* using 'auto' is strongly discouraged

### wf-prop
 * wf-prop l / wf-prop list for a detailed list of all taskmanger relevant (toplevel) windows.
 * wf-prop + click on a window to query details about that window

### other examples

* To continuously monitor for signals 
>gdbus monitor --session --dest org.wayland.compositor --object-path /org/wayland/compositor"

* To query taskamanager relevant windows
>gdbus call --session --dest org.wayland.compositor --object-path /org/wayland/compositor --method org.wayland.compositor.query_view_vector_taskman_ids 

* To fullscreen a window (query the id you want from the properties)
>gdbus call --session --dest org.wayland.compositor --object-path /org/wayland/compositor --method org.wayland.compositor.fullscreen_view $id 1

* To restore previous state from fullscreen
>gdbus call --session --dest org.wayland.compositor --object-path /org/wayland/compositor --method org.wayland.compositor.fullscreen_view $id 0
