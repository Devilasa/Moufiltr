import tkinter as tk
from tkinter import ttk, messagebox
import struct
import win32con, win32file, pywintypes, subprocess

# ----- costanti driver -----
FILE_DEVICE_MOUFILTR = 0x8000
METHOD_BUFFERED = 0
FILE_ANY_ACCESS = 0
def CTL_CODE(dev, func, method, access):
    return (dev << 16) | (access << 14) | (func << 2) | method

IOCTL_GET_MODE  = CTL_CODE(FILE_DEVICE_MOUFILTR, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
IOCTL_SET_MODE  = CTL_CODE(FILE_DEVICE_MOUFILTR, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
IOCTL_SHUTDOWN  = CTL_CODE(FILE_DEVICE_MOUFILTR, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

MODES = {
    0: "NONE",
    1: "INVERT_XY",
    2: "GAIN_X2",
    3: "GAIN_X4",
    4: "DEADZONE"
}

shutdown_this_session = False


# ----- funzioni base -----
def open_device():
    return win32file.CreateFile(
        r"\\.\MouFiltrCtl",
        win32con.GENERIC_READ | win32con.GENERIC_WRITE,
        0, None,
        win32con.OPEN_EXISTING,
        0, None
    )

def try_start_service_once():
    try:
        subprocess.run(
            ["sc.exe", "start", "moufiltr"],
            capture_output=True, text=True, timeout=2
        )
    except Exception:
        pass

def get_mode(show_errors=False):
    global shutdown_this_session
    if shutdown_this_session:
        if show_errors:
            print("Control device was shut down. Reopen this app after restarting the mouse stack.")
        return -1

    try:
        h = open_device()
        data = win32file.DeviceIoControl(h, IOCTL_GET_MODE, None, 4)
        return struct.unpack("i", data)[0]
    except pywintypes.error as e:
        if e.winerror == 2 and show_errors:
            messagebox.showinfo("Info", "Control device not found.\nUnplug and plug back the mouse (or disable/enable).")
        return -1

def set_mode(m):
    try:
        h = open_device()
        win32file.DeviceIoControl(h, IOCTL_SET_MODE, struct.pack("i", m), 0)
        return True
    except pywintypes.error as e:
        if e.winerror == 2:
            messagebox.showinfo("Info", "Control device not found.\nUnplug and plug back the mouse (or disable/enable).")
        elif e.winerror == 5:
            messagebox.showerror("Error", "Access denied. Run this app as Administrator.")
        else:
            messagebox.showerror("Error", f"SetMode failed (0x{e.winerror:X}).")
        return False

def shutdown():
    global shutdown_this_session
    if shutdown_this_session:
        messagebox.showinfo("Info", "Already shut down in this session.")
        return
    try:
        h = open_device()
        win32file.DeviceIoControl(h, IOCTL_SET_MODE, struct.pack("i", 0), 0)
        h = open_device()
        win32file.DeviceIoControl(h, IOCTL_SHUTDOWN, None, 0)
        shutdown_this_session = True
        messagebox.showinfo("Shutdown",
            "Shutdown sent.\nNow you can 'sc stop moufiltr' and replace the .sys if needed.\n"
            "To use again, replug or disable/enable the mouse, then reopen this app.")
    except pywintypes.error as e:
        if e.winerror == 2:
            messagebox.showinfo("Info", "Control device not found.\nUnplug and plug back the mouse (or disable/enable).")
        else:
            messagebox.showerror("Error", f"Shutdown failed (0x{e.winerror:X}).")


# ----- GUI -----
def refresh_mode():
    mode = get_mode(show_errors=False)
    if mode in MODES:
        lbl_var.set(f"Current mode: {mode} ({MODES[mode]})")
        mode_combo.state(["!disabled"])
        btn_apply.state(["!disabled"])
    else:
        lbl_var.set("Current mode: -1 (device not connected)")
        mode_combo.state(["disabled"])
        btn_apply.state(["disabled"])

def apply_mode():
    sel = mode_combo.current()
    if shutdown_this_session:
        messagebox.showinfo("Info", "Control device was shut down.\nReplug/disable-enable mouse then reopen this app.")
        return
    if set_mode(sel):
        lbl_var.set(f"Current mode: {sel} ({MODES[sel]})")

def shutdown_click():
    shutdown()
    refresh_mode()


# ----- Main window -----
try_start_service_once()

root = tk.Tk()
root.title("MouFiltr Controller")
root.geometry("360x220")
root.resizable(False, False)

style = ttk.Style()
style.configure("TLabel", font=("Segoe UI", 10))
style.configure("TButton", font=("Segoe UI", 10))
style.configure("TCombobox", 
    font=("Segoe UI", 10),
    padding=4,
    relief="flat"
)

frm = ttk.Frame(root, padding=10)
frm.pack(fill="both", expand=True)

lbl_var = tk.StringVar()
lbl = ttk.Label(frm, textvariable=lbl_var, font=("Segoe UI", 10, "bold"))
lbl.pack(pady=(0,10))

# --- Combobox con look più moderno ---
combo_frame = ttk.Frame(frm)
combo_frame.pack(pady=5)
ttk.Label(combo_frame, text="Select mode:", font=("Segoe UI", 10)).pack(side="left", padx=(0,8))

mode_combo = ttk.Combobox(combo_frame, values=[MODES[i] for i in range(5)], state="readonly", font=("Segoe UI", 10))
mode_combo.current(0)
mode_combo.pack(side="left")

btn_apply = ttk.Button(frm, text="Apply", command=apply_mode)
btn_apply.pack(fill="x", pady=3)
btn_refresh = ttk.Button(frm, text="Refresh", command=refresh_mode)
btn_refresh.pack(fill="x", pady=3)
btn_shutdown = ttk.Button(frm, text="Shutdown", command=shutdown_click)
btn_shutdown.pack(fill="x", pady=3)

refresh_mode()
root.mainloop()
