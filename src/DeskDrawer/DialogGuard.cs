using System.Diagnostics;
using System.Runtime.InteropServices;

namespace DeskDrawer;

/// DeskDrawer.exe --dialog-guard [pid] [dialog]: a tiny watcher, started each time DeskDrawer stands in for another app's Save / Open dialog.
/// It has no window and loads no WPF. It waits, and if DeskDrawer (process pid) goes away while the dialog is still hidden, it shows the dialog again with its
/// colours back, so the app that asked doesn't sit there looking frozen. DeskDrawer handles a normal exit and its own errors itself; this covers the cases it
/// can't: killed from Task Manager, a hard crash. It ends by itself as soon as the dialog is answered (gone) or shown again by DeskDrawer, or after its job.
internal static class DialogGuard
{
    public static int Run(string[] args)
    {
        if (args.Length < 3 || !int.TryParse(args[1], out int pid) || !long.TryParse(args[2], out long h64)) return 2;
        var dlg = new IntPtr(h64);
        Process? parent = null;
        try { parent = Process.GetProcessById(pid); } catch { }   // already gone: put the dialog back at once
        try
        {
            int shown = 0;
            while (IsWindow(dlg))
            {
                if (parent == null || parent.WaitForExit(200)) { Restore(dlg); return 0; }
                // DeskDrawer showed it again itself (the app had something to ask, or the picker failed): nothing left to guard. Seen three times in a row,
                // because an app that shows its own dialog again (Word) has it hidden again by DeskDrawer within a quarter of a second.
                shown = IsWindowVisible(dlg) && !SeeThrough(dlg) ? shown + 1 : 0;
                if (shown >= 3) return 0;
            }
            return 0;   // answered: the dialog is gone
        }
        finally { parent?.Dispose(); }
    }

    private static bool SeeThrough(IntPtr h) => (GetWindowLong(h, -20) & 0x80000) != 0 && GetLayeredWindowAttributes(h, out _, out byte a, out uint f) && (f & 2) != 0 && a < 255;

    /// Colours back (only if it is see-through: a window its own app made layered is left as it is), on screen, and in front if Windows allows it.
    internal static void Restore(IntPtr dlg)
    {
        if (!IsWindow(dlg)) return;
        if (SeeThrough(dlg)) { SetLayeredWindowAttributes(dlg, 0, 255, 2); SetWindowLong(dlg, -20, GetWindowLong(dlg, -20) & ~0x80000); }
        ShowWindowAsync(dlg, 5 /*SW_SHOW*/);
        SetForegroundWindow(dlg);
    }

    [DllImport("user32.dll")] private static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] private static extern int GetWindowLong(IntPtr h, int i);
    [DllImport("user32.dll")] private static extern int SetWindowLong(IntPtr h, int i, int v);
    [DllImport("user32.dll")] private static extern bool GetLayeredWindowAttributes(IntPtr h, out uint key, out byte alpha, out uint flags);
    [DllImport("user32.dll")] private static extern bool SetLayeredWindowAttributes(IntPtr h, uint key, byte alpha, uint flags);
    [DllImport("user32.dll")] private static extern bool ShowWindowAsync(IntPtr h, int cmd);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr h);
}
