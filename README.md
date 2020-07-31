DBus Wayfire plugin

Currently requires: https://github.com/WayfireWM/wayfire/pull/645

Currently requires: https://github.com/WayfireWM/wayfire/pull/644

Authors: Damian Ivanov <damianatorrpm@gmail.com>

Contributors: Scott Moreau <soureau@gmail.com>

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
