# 🖱️ MouFiltr – Mouse Input Filter Driver

MouFiltr is a simple KMDF upper-filter driver for mouse devices on Windows.
It attaches under the system’s mouclass driver to intercept and modify raw mouse input events.

## Overview

The project includes:
A kernel-mode filter driver (moufiltr.sys) that processes mouse input packets.
A user-mode console application to switch filtering modes via IOCTL communication.
Lightweight installation and removal scripts (install_moufiltr.bat, uninstall_moufiltr.bat) for easy testing on development systems.

### How it works
The driver is registered as an UpperFilter above mouclass in the mouse driver stack.
It intercepts movement and button data in real time.
Depending on the selected mode, it applies one of filtering algorithms:
None – Pass-through (no modification)
Invert XY – Invert both X and Y axes
Gain ×2 / ×4 – Increase sensitivity
Deadzone – Ignore small movements near the center

### Quick usage
- Run install_moufiltr.bat as Administrator.
- Disable/enable or re-plug the mouse to rebuild the HID stack.
- Launch MouFiltr Console to choose the desired filter mode.
- To remove the driver, run uninstall_moufiltr.bat (Administrator required).

## Installation & Troubleshooting

To install the driver, boot Windows in Test Mode and enable unsigned driver installation (option 7 from the Advanced Startup menu).
Use the install_moufiltr.bat to install the driver, just run it as admin and plug/unplug the device every time you uninstall it or install it so the pc can rebuild the driver stack.
Remember to run shutdown from the menu before uninstalling it if you are planning to install it back or you probably will have to restart your pc.
DO NOT use the .inf to install it as it is not working for usb mice.
If you installed it with the .inf file it's not gonna place the moufiltr driver in the right place.
Anyway follow these steps to make it work if you installed it with pnputil and .inf file:

1) copy the .sys file in this directory:  C:\Windows\System32\drivers
2) run in admin prompt this command -> sc create moufiltr type= kernel start= demand error= normal ^
 binPath= \SystemRoot\System32\drivers\moufiltr.sys displayname= "Mouse Filter (moufiltr)"

If everything went fine run this command -> sc qc moufiltr
And this is what you should see as output:
SERVICE_NAME: moufiltr
        TYPE               : 1  KERNEL_DRIVER
        START_TYPE         : 3  DEMAND_START
        BINARY_PATH_NAME    : \SystemRoot\System32\drivers\moufiltr.sys
        DISPLAY_NAME        : Mouse Filter (moufiltr)

3) open regedit and go to
HKEY_LOCAL_MACHINE ──> SYSTEM ──> CurrentControlSet ──> Control ──> Class ──> {4D36E96F-E325-11CE-BFC1-08002BE10318} 

Double click on "UpperFilters" on the righ of your screen and type "moufiltr" BEFORE "mouclass"
so the while you are typing you should see 	moufiltr
						mouclass
and the output should be : moufiltr mouclass

unplug and plug back the device and it shold work.
