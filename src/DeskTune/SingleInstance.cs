using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace DeskTune;

/// One DeskTune window. Opening DeskTune while it is open (Start, the desktop, Quick Settings' tile, Desk settings > Open DeskTune) used to start a
/// second window; "Put everything back" did too, and the undo ran there while the first window kept showing the old ticks. Now a second start hands
/// over to the open window (it comes to the front, and runs the undo when that was asked for) and quits.
/// An administrator start ("Unlock services") is the exception: it is meant to replace the window without rights, so it asks that one to close and opens.
public static class SingleInstance
{
    public const int Show = 0, UndoAll = 1, Close = 2;
    public static readonly uint Message = RegisterWindowMessageW("DeskTuneSingleInstance");

    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern uint RegisterWindowMessageW(string name);
    [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern bool AllowSetForegroundWindow(int pid);
    [DllImport("user32.dll")] static extern bool ChangeWindowMessageFilterEx(IntPtr h, uint m, uint action, IntPtr info);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);

    /// What a new start does, given its arguments and whether a DeskTune window is already open. Pure, for the self-test.
    /// null: open normally (nothing to hand over to, or a run that must have its own window: tests, pictures, --new-window).
    public static int? Decide(string[] args, bool otherOpen, bool iAmAdmin, bool otherIsAdmin)
    {
        if (!otherOpen || args.Any(a => a is "--snapshot" or "--settingsshot" or "--colour" or "--size" or "--theme" or "--new-window")) return null;
        if (iAmAdmin && !otherIsAdmin) return Close;   // "Unlock services": this one replaces it
        return args.Contains("--undoall") ? UndoAll : Show;
    }

    /// What an administrator window does with a message. Anyone without rights can post it (the filter below lets them), so it never closes
    /// itself or runs "Put everything back" on that say-so: it only comes forward, and the person clicks what they want. Pure, for the self-test.
    public static int? Accept(int what, bool elevated) => !elevated || what == Show ? what : what == UndoAll ? Show : null;

    /// The open DeskTune's window (another process, same exe, a real window), or zero.
    static (IntPtr h, int pid) Other()
    {
        string me = Environment.ProcessPath ?? "";
        foreach (var p in Process.GetProcessesByName("DeskTune"))
            try
            {
                if (p.Id == Environment.ProcessId || p.MainWindowHandle == IntPtr.Zero) continue;
                string? path = null; try { path = p.MainModule?.FileName; } catch { }   // an elevated copy can't be read: count it (it is the installed one, started by "Unlock services")
                if (path != null && !string.Equals(path, me, StringComparison.OrdinalIgnoreCase)) continue;   // a dev build elsewhere is its own app
                return (p.MainWindowHandle, p.Id);
            }
            catch { }
        return (IntPtr.Zero, 0);
    }
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("advapi32.dll")] static extern bool OpenProcessToken(IntPtr p, uint access, out IntPtr token);
    [DllImport("advapi32.dll")] static extern bool GetTokenInformation(IntPtr token, int cls, out int value, int size, out int got);
    /// Does the process run elevated ("as administrator")? One it can't look into runs with more rights than this one. With UAC off every process is
    /// elevated, so both sides say yes and a second start just brings the window forward.
    static bool Elevated(int pid)
    {
        IntPtr p = OpenProcess(0x1000 /*QUERY_LIMITED_INFORMATION*/, false, pid); if (p == IntPtr.Zero) return true;
        try
        {
            if (!OpenProcessToken(p, 0x0008 /*TOKEN_QUERY*/, out var t)) return true;
            try { return GetTokenInformation(t, 20 /*TokenElevation*/, out int e, 4, out _) ? e != 0 : true; } finally { CloseHandle(t); }
        }
        finally { CloseHandle(p); }
    }

    /// At start: true = handed over to the open window, quit now.
    public static bool HandOver(string[] args)
    {
        var (h, pid) = Other();
        int? what = Decide(args, h != IntPtr.Zero, Elevated(Environment.ProcessId), h != IntPtr.Zero && Elevated(pid));
        if (what == null) return false;
        AllowSetForegroundWindow(pid);
        bool sent = PostMessageW(h, Message, (IntPtr)what.Value, IntPtr.Zero);
        if (what == Close) { try { using var p = Process.GetProcessById(pid); p.WaitForExit(5000); } catch { } return false; }
        return sent;   // not delivered (it is closing right now): open normally
    }

    /// On the main window: answers the next start.
    public static void Listen(MainWindow w)
    {
        var h = new WindowInteropHelper(w).Handle; bool elevated = Elevated(Environment.ProcessId);
        ChangeWindowMessageFilterEx(h, Message, 1 /*MSGFLT_ALLOW*/, IntPtr.Zero);   // a start without rights can still reach a window that has them
        HwndSource.FromHwnd(h)?.AddHook((IntPtr hwnd, int msg, IntPtr wp, IntPtr lp, ref bool handled) =>
        {
            if ((uint)msg != Message || Message == 0) return IntPtr.Zero;
            handled = true; if (Accept((int)wp, elevated) is not int what) return IntPtr.Zero;
            w.Dispatcher.BeginInvoke(() =>
            {
                if (what == Close) { w.Close(); return; }
                if (w.WindowState == WindowState.Minimized) w.WindowState = WindowState.Normal;
                w.Show(); w.Activate(); w.Topmost = true; w.Topmost = false; SetForegroundWindow(hwnd);
                if (what == UndoAll) w.UndoAll();
            });
            return IntPtr.Zero;
        });
    }

    public static IEnumerable<(string, bool, string)> Checks()
    {
        string[] none = Array.Empty<string>(), undo = { "--undoall" };
        yield return ("one window: nothing open, a start opens its window", Decide(none, false, false, false) == null, "");
        yield return ("one window: a second start brings the open window forward", Decide(none, true, false, false) == Show, "");
        yield return ("one window: Put everything back runs in the open window", Decide(undo, true, false, false) == UndoAll && Decide(undo, false, false, false) == null, "");
        yield return ("one window: Unlock services (administrator) replaces the window without rights", Decide(none, true, true, false) == Close, "");
        yield return ("one window: a start without rights goes to the administrator window", Decide(none, true, false, true) == Show && Decide(none, true, true, true) == Show, "");
        yield return ("one window: tests and pictures get their own window", Decide(new[] { "--snapshot", "x.png" }, true, false, false) == null && Decide(new[] { "--settingsshot", "x.png" }, true, false, false) == null, "");
        yield return ("one window: an administrator window only comes forward; it never closes or undoes because someone without rights asked", Accept(Close, true) == null && Accept(UndoAll, true) == Show && Accept(Show, true) == Show && Accept(Close, false) == Close && Accept(UndoAll, false) == UndoAll, "");
        yield return ("one window: the hand-over message is registered", Message != 0, "");
    }
}
