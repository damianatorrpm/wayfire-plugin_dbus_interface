DBus Wayfire plugin

Authors: Damian Ivanov

Contributors: Scott Moreau

### Installation
Standard:
meson -C build && ninja -C build && ninja -C build install
Although you may want to set --prefix=/usr if that's
the locaiton of your wayfire installation

### Coding style
* uncrustify.ini in the repo
* follow the style used
* using 'auto' is strongly discouraged

### Examples

* To continuously monitor for signals 
>gdbus monitor --session --dest org.wayland.compositor --object-path /org/wayland/compositor"

* To query taskamanager relevant windows
>gdbus call --session --dest org.wayland.compositor --object-path /org/wayland/compositor --method org.wayland.compositor.query_view_vector_taskman_ids 

* To fullscreen a window (query the id you want from the properties)
>gdbus call --session --dest org.wayland.compositor --object-path /org/wayland/compositor --method org.wayland.compositor.fullscreen_view $id 1

* To restore previous state from fullscreen
>gdbus call --session --dest org.wayland.compositor --object-path /org/wayland/compositor --method org.wayland.compositor.fullscreen_view $id 0
