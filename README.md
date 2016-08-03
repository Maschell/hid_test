# HID-TEST
This application is a small tool to view the data that comes from HID devices.

#How to use
Simply start this with the [homebrew launcer](https://github.com/dimok789/homebrew_launcher) and attach a HID device.
The app should show you the vid, pid and the incoming data.  
Read the [controller patcher wiki](https://github.com/Maschell/controller_patcher/wiki/) for more information about creating config files for controller_patcher applications.

#Building
In order to build this application, you need the custom liboGC and portlibs modified/created by dimok. You can find them on the [loadiine_gx2 repo](https://github.com/dimok789/loadiine_gx2/releases/tag/v0.2). Simply put the files in your devkit folder and run the Makefile of HID-TEST.

#Credits
- Maschell
- dimok for the dynamic libs, hbl, etc.