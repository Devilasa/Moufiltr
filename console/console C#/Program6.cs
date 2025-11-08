using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.ComponentModel;
using Microsoft.Win32.SafeHandles;

static class MouFiltrCtl
{
    const uint FILE_DEVICE_MOUFILTR = 0x8000;
    const uint METHOD_BUFFERED = 0;
    const uint FILE_ANY_ACCESS = 0;

    static uint CTL_CODE(uint dev, uint func, uint method, uint access)
        => ((dev << 16) | (access << 14) | (func << 2) | method);

    static readonly uint IOCTL_GET_MODE = CTL_CODE(FILE_DEVICE_MOUFILTR, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS);
    static readonly uint IOCTL_SET_MODE = CTL_CODE(FILE_DEVICE_MOUFILTR, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS);
    static readonly uint IOCTL_SHUTDOWN = CTL_CODE(FILE_DEVICE_MOUFILTR, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS);

    public const int MF_MODE_NONE      = 0;
    public const int MF_MODE_INVERT_XY = 1;
    public const int MF_MODE_GAIN_X2   = 2;
    public const int MF_MODE_GAIN_X4   = 3;
    public const int MF_MODE_DEADZONE  = 4;


    const uint GENERIC_READ_WRITE = 0x80000000u | 0x40000000u; // GENERIC_READ|GENERIC_WRITE
    const uint OPEN_EXISTING = 3;

    [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern SafeFileHandle CreateFile(
        string lpFileName, uint dwDesiredAccess, uint dwShareMode,
        IntPtr lpSecurityAttributes, uint dwCreationDisposition,
        uint dwFlagsAndAttributes, IntPtr hTemplateFile);

    [DllImport("kernel32", SetLastError = true)]
    static extern bool DeviceIoControl(
        SafeFileHandle hDevice, uint dwIoControlCode,
        IntPtr lpInBuffer, int nInBufferSize,
        IntPtr lpOutBuffer, int nOutBufferSize,
        out int lpBytesReturned, IntPtr lpOverlapped);

    static SafeFileHandle Open()
    {
        var h = CreateFile(@"\\.\MouFiltrCtl", GENERIC_READ_WRITE, 0, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
        if (h.IsInvalid)
            throw new Win32Exception(Marshal.GetLastWin32Error());
        return h;
    }

    public static int GetMode()
    {
        using var h = Open();
        int bytes;
        IntPtr outBuf = Marshal.AllocHGlobal(sizeof(int));
        try
        {
            if (!DeviceIoControl(h, IOCTL_GET_MODE, IntPtr.Zero, 0, outBuf, sizeof(int), out bytes, IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            return Marshal.ReadInt32(outBuf);
        }
        finally { Marshal.FreeHGlobal(outBuf); }
    }

    public static void SetMode(int val)
    {
        using var h = Open();
        int bytes;
        IntPtr inBuf = Marshal.AllocHGlobal(sizeof(int));
        try
        {
            Marshal.WriteInt32(inBuf, val);
            if (!DeviceIoControl(h, IOCTL_SET_MODE, inBuf, sizeof(int), IntPtr.Zero, 0, out bytes, IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error());
        }
        finally { Marshal.FreeHGlobal(inBuf); }
    }

    public static void Shutdown()
    {
        using var h = Open();
        int bytes;
        if (!DeviceIoControl(h, IOCTL_SHUTDOWN, IntPtr.Zero, 0, IntPtr.Zero, 0, out bytes, IntPtr.Zero))
            throw new Win32Exception(Marshal.GetLastWin32Error());
    }
}

class Program
{
    // when true, the session has performed a Shutdown: do NOT auto-retry until next run
    static bool _shutdownThisSession = false;

    static int Usage()
    {
        Console.WriteLine("Usage:");
        Console.WriteLine("  moumode.exe get");
        Console.WriteLine("  moumode.exe set <0..4>  (0=NONE, 1=INVERT_XY, 2=GAIN_X2, 3=GAIN_X4, 4=DEADZONE)");
        Console.WriteLine("  moumode.exe shutdown");
        Console.WriteLine("  moumode.exe              (interactive menu)");
        return 1;
    }


    static int Main(string[] args)
    {
        try
        {
            // On startup only: try to ensure service is running (no harm if already running)
            TryStartServiceOnceAtLaunch();

            if (args.Length > 0)
            {
                switch (args[0].ToLowerInvariant())
                {
                    case "get":
                        Console.WriteLine("mode: " + SafeGetMode(showErrors: true));
                        return 0;

                    case "set":
                        if (args.Length != 2) return Usage();
                        if (!int.TryParse(args[1], out int m) || m < 0 || m > 4) {
                            Console.Error.WriteLine("Invalid value. Use 0..4 (0=NONE, 1=INVERT_XY, 2=GAIN_X2, 3=GAIN_X4, 4=DEADZONE).");
                            return 2;
                        }
                        if (_shutdownThisSession) { Console.WriteLine("Control device was shut down. Reopen this app after restarting the mouse stack."); return 3; }

                        if (SafeSetMode(m))
                            Console.WriteLine("Mode set to " + m);
                        return 0;

                    case "shutdown":
                        SafeShutdown();
                        return 0;

                    default:
                        return Usage();
                }
            }

            // --- Interactive Menu ---
            while (true)
            {
                int cur = SafeGetMode(showErrors: false);
                Console.WriteLine();
                Console.WriteLine("=== MouFiltr console ===");
                Console.WriteLine($"Current mode: {cur}");
                Console.WriteLine("[0] NONE");
                Console.WriteLine("[1] INVERT_XY");
                Console.WriteLine("[2] GAIN_X2");
                Console.WriteLine("[3] GAIN_X4");
                Console.WriteLine("[4] DEADZONE");
                Console.WriteLine("[S] Shutdown control device");
                Console.WriteLine("[Q] Quit");
                Console.Write("Select: ");
                var key = Console.ReadKey();
                Console.WriteLine();

                if (key.Key == ConsoleKey.Q)
                    return 0;

                if (key.Key == ConsoleKey.S)
                {
                    SafeShutdown();
                    continue;
                }

                if (key.KeyChar >= '0' && key.KeyChar <= '4')
                {
                    int m = key.KeyChar - '0';
                    if (_shutdownThisSession)
                    {
                        Console.WriteLine("Control device was shut down. Close this app and restart the mouse (disable/enable or replug), then run the app again.");
                        continue;
                    }

                    if (SafeSetMode(m))
                        Console.WriteLine($"Mode set to {m}");
                }
            }
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("Fatal error: " + ex.Message);
            Console.WriteLine("Press any key to exit...");
            Console.ReadKey(true);
            return 5;
        }
    }

    // ---------- Helpers ----------

    // Startup helper: try to start the service once (does not recreate control device if stack not rebuilt)
    static void TryStartServiceOnceAtLaunch()
    {
        try
        {
            var psi = new ProcessStartInfo
            {
                FileName = "sc.exe",
                Arguments = "start moufiltr",
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            };
            using var p = Process.Start(psi);
            p.WaitForExit(2000);
        }
        catch { /* ignore */ }
    }

    // Reads mode; if control device missing returns -1 (no auto-retry after Shutdown in this session)
    static int SafeGetMode(bool showErrors)
    {
        if (_shutdownThisSession) {
            if (showErrors) Console.WriteLine("Control device was shut down. Reopen this app after restarting the mouse stack.");
            return -1;
        }

        try {
            return MouFiltrCtl.GetMode();
        }
        catch (Win32Exception wex) {
            if (wex.NativeErrorCode == 2) { // ERROR_FILE_NOT_FOUND
                if (showErrors) Console.WriteLine("Control device not found. Unplug and plug back the mouse (or disable/enable) to rebuild the driver stack.");
                return -1;
            }
            if (showErrors) Console.WriteLine($"[GetMode] 0x{wex.NativeErrorCode:X}");
            return -1;
        }
        catch {
            return -1;
        }
    }

    static bool SafeSetMode(int m)
    {
        try
        {
            MouFiltrCtl.SetMode(m);
            return true; // successo
        }
        catch (Win32Exception wex)
        {
            if (wex.NativeErrorCode == 2) // ERROR_FILE_NOT_FOUND
            {
                Console.WriteLine("Control device not found. Unplug and plug back the mouse (or disable/enable) to rebuild the driver stack.");
            }
            else if (wex.NativeErrorCode == 5) // ACCESS_DENIED
            {
                Console.WriteLine("SetMode failed: access denied. Run this app as Administrator.");
            }
            else
            {
                Console.WriteLine($"SetMode failed (0x{wex.NativeErrorCode:X}).");
            }
            return false;
        }
        catch
        {
            Console.WriteLine("SetMode failed.");
            return false;
        }
    }

    // Shutdown: set mode=0, then delete control device. Do NOT try to reconnect in this session.
    static void SafeShutdown()
    {
        if (_shutdownThisSession)
        {
            Console.WriteLine("Already shut down in this session.");
            return;
        }

        // Prova prima a portare il driver in NONE; se il device non c'è, mostra subito il suggerimento e termina
        try
        {
            MouFiltrCtl.SetMode(MouFiltrCtl.MF_MODE_NONE);
        }
        catch (Win32Exception wex)
        {
            if (wex.NativeErrorCode == 2) // FILE_NOT_FOUND
                Console.WriteLine("Control device not found. Unplug and plug back the mouse (or disable/enable) to rebuild the driver stack.");
            else if (wex.NativeErrorCode == 5)
                Console.WriteLine("Shutdown failed: access denied. Run this app as Administrator.");
            else
                Console.WriteLine($"SetMode(0) before shutdown failed (0x{wex.NativeErrorCode:X}).");
            return;
        }
        catch
        {
            Console.WriteLine("SetMode(0) before shutdown failed.");
            return;
        }

        // Ora prova lo shutdown vero e proprio
        try
        {
            MouFiltrCtl.Shutdown();
            _shutdownThisSession = true;
            Console.WriteLine("Shutdown sent. Now you can 'sc stop moufiltr' and replace the .sys if needed.");
            Console.WriteLine("To use the console again, restart the mouse stack (disable/enable or replug), then reopen this app.");
        }
        catch (Win32Exception wex)
        {
            if (wex.NativeErrorCode == 2)
                Console.WriteLine("Control device not found. Unplug and plug back the mouse (or disable/enable) to rebuild the driver stack.");
            else
                Console.WriteLine($"Shutdown failed (0x{wex.NativeErrorCode:X}).");
        }
        catch
        {
            Console.WriteLine("Shutdown failed.");
        }
    }

}
