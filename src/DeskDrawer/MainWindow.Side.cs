using System.Runtime.InteropServices;
using System.Windows;

namespace DeskDrawer;

/// Side by side: this window takes the left half of its screen and a second DeskDrawer window opens on the right half with the same folder,
/// ready to drag files across. Two real windows, so everything (tabs, search, drag and drop) works in both without a second code path.
public partial class MainWindow
{
    [StructLayout(LayoutKind.Sequential)] private struct SRect { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] private struct SMon { public int Size; public SRect Monitor, Work; public uint Flags; }
    [DllImport("user32.dll", EntryPoint = "MonitorFromWindow")] private static extern IntPtr SideMonitor(IntPtr h, uint flags);
    [DllImport("user32.dll", EntryPoint = "GetMonitorInfoW")] private static extern bool SideMonitorInfo(IntPtr m, ref SMon mi);
    [DllImport("user32.dll", EntryPoint = "SetWindowPos")] private static extern bool SidePos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);

    /// The two halves of a work area, in screen pixels: (left, right) as "x,y,w,h".
    internal static (string Left, string Right) Halves(int l, int t, int r, int b)
    {
        int half = (r - l) / 2;
        return ($"{l},{t},{half},{b - t}", $"{l + half},{t},{r - l - half},{b - t}");
    }

    private void SideBySide()
    {
        var mi = new SMon { Size = Marshal.SizeOf<SMon>() };
        if (!SideMonitorInfo(SideMonitor(Hwnd, 2), ref mi)) { OpenNewWindow(); return; }
        var (left, right) = Halves(mi.Work.L, mi.Work.T, mi.Work.R, mi.Work.B);
        PlaceAt(left); OpenNewWindow(null, "--bounds " + right);
    }

    /// Moves the window to "x,y,w,h" in screen pixels (the right-hand window of Side by side gets this on its command line).
    internal void PlaceAt(string bounds)
    {
        var p = bounds.Split(','); if (p.Length != 4 || !p.All(s => int.TryParse(s, out _))) return;
        if (WindowState != WindowState.Normal) WindowState = WindowState.Normal;
        SidePos(Hwnd, IntPtr.Zero, int.Parse(p[0]), int.Parse(p[1]), int.Parse(p[2]), int.Parse(p[3]), 0x0004 | 0x0010);   // keep the z-order, don't activate
    }
}
