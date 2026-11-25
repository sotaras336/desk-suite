using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace DeskTune;

/// Keeps a window inside the screen it opened on. WPF only knows the MAIN screen's work area (SystemParameters.WorkArea), so a window sized for a big main
/// screen could open hanging off a smaller second screen, or one with higher scaling (a 1280 x 800 tablet at 150% is only ~850 x 500 layout units).
/// Same file in DeskDrawer and DeskTune.
internal static class ScreenFit
{
    internal readonly record struct Px(int L, int T, int R, int B) { public int W => R - L; public int H => B - T; }

    /// Where the window goes, in screen pixels: unchanged when it already lies inside the work area; otherwise shrunk (only if it must) and moved to lie
    /// inside it, margin pixels from its edges. Works the same on a screen left of or above the main one (negative coordinates).
    internal static Px Fit(Px win, Px work, int margin)
    {
        if (win.L >= work.L && win.T >= work.T && win.R <= work.R && win.B <= work.B) return win;
        int w = Math.Max(1, Math.Min(win.W, work.W - 2 * margin)), h = Math.Max(1, Math.Min(win.H, work.H - 2 * margin));
        int x = Math.Clamp(win.L, work.L + margin, Math.Max(work.L + margin, work.R - margin - w)), y = Math.Clamp(win.T, work.T + margin, Math.Max(work.T + margin, work.B - margin - h));
        return new Px(x, y, x + w, y + h);
    }

    /// Fits a normal (not maximized) window to its own screen. A window that is on no screen at all was put off screen on purpose (the self-tests) and is
    /// left alone. True when it had to move or shrink it.
    internal static bool Apply(Window win)
    {
        if (win.WindowState != WindowState.Normal) return false;
        var h = new WindowInteropHelper(win).Handle; if (h == IntPtr.Zero) return false;
        var mon = MonitorFromWindow(h, 0 /* MONITOR_DEFAULTTONULL */); if (mon == IntPtr.Zero) return false;
        var mi = new MonInfo { Size = Marshal.SizeOf<MonInfo>() };
        if (!GetMonitorInfo(mon, ref mi) || !GetWindowRect(h, out var r)) return false;
        int margin = (int)(12 * Math.Max(96u, GetDpiForWindow(h)) / 96.0);
        Px cur = new(r.L, r.T, r.R, r.B), f = Fit(cur, new Px(mi.Work.L, mi.Work.T, mi.Work.R, mi.Work.B), margin);
        if (f == cur) return false;
        SetWindowPos(h, IntPtr.Zero, f.L, f.T, f.W, f.H, 0x0004 | 0x0010);   // keep the z-order, don't activate
        return true;
    }

    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] private struct MonInfo { public int Size; public Rect Monitor, Work; public uint Flags; }
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromWindow(IntPtr h, uint flags);
    [DllImport("user32.dll", EntryPoint = "GetMonitorInfoW")] private static extern bool GetMonitorInfo(IntPtr m, ref MonInfo mi);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);

    /// The self-test's pretend screens: the test machine has one screen at 100%.
    internal static IEnumerable<(string Name, bool Ok, string Detail)> Checks()
    {
        static bool Inside(Px a, Px b) => a.L >= b.L && a.T >= b.T && a.R <= b.R && a.B <= b.B;
        // a 1280 x 800 tablet at 150% LEFT of the main screen (negative coordinates), its bar 72 px tall
        Px tablet = new(-1280, 0, 0, 728), big = new(-1300, 40, -1300 + 1470, 40 + 1230);
        var a = Fit(big, tablet, 18);
        yield return ("screens: a window too big for a small second screen is shrunk to fit inside it", Inside(a, tablet) && a.W == 1280 - 36 && a.H == 728 - 36, $"{a}");
        Px off = new(-400, 100, -400 + 900, 100 + 600); var b = Fit(off, tablet, 18);
        yield return ("screens: a window hanging off the second screen's edge is moved in, same size", Inside(b, tablet) && b.W == 900 && b.H == 600 && b.R == -18, $"{b}");
        Px ok = new(-1200, 50, -300, 650);
        yield return ("screens: a window already inside its screen is left exactly where it is", Fit(ok, tablet, 18) == ok, "");
        Px above = new(0, -1032, 1920, 0); var c = Fit(new Px(1500, -900, 2400, -100), above, 12);
        yield return ("screens: a screen above the main one", Inside(c, above) && c.W == 900 && c.R == 1908, $"{c}");
    }
}
