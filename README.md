DBus Wayfire plugin


### Examples

* To continuously monitor for signals 
>gdbus monitor --session --dest org.wayland.compositor --object-path /org/wayland/compositor"

* To query taskamanager relevant windows
>gdbus call --session --dest org.wayland.compositor --object-path /org/wayland/compositor --method org.wayland.compositor.query_view_vector_taskman_ids 

* To fullscreen a window (query the id you want from the properties)
>gdbus call --session --dest org.wayland.compositor --object-path /org/wayland/compositor --method org.wayland.compositor.fullscreen_view $id true

* To restore previous state from fullscreen
>gdbus call --session --dest org.wayland.compositor --object-path /org/wayland/compositor --method org.wayland.compositor.fullscreen_view $id false

