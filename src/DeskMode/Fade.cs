using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace DeskMode;

/// Windows blanks a screen for a moment while its refresh rate changes. To make that a smooth fade through black instead of a harsh flash, a black, click-through window
/// fades in over each screen that is about to change, the change happens behind it, and it fades out once the new mode has settled. It is only ever up for a few seconds:
/// a timer closes it on its own if anything goes wrong.
internal static class ScreenFade
{
    private static readonly List<Window> Wins = new();
    private static System.Windows.Threading.DispatcherTimer? _failsafe;

    private const int GWL_EXSTYLE = -20, WS_EX_TRANSPARENT = 0x20, WS_EX_TOOLWINDOW = 0x80, WS_EX_NOACTIVATE = 0x08000000;
    private static readonly IntPtr HWND_TOPMOST = new(-1);
    [DllImport("user32.dll")] private static extern int GetWindowLong(IntPtr h, int i);
    [DllImport("user32.dll")] private static extern int SetWindowLong(IntPtr h, int i, int v);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);

    /// Fades the given screens (device names such as \\.\DISPLAY1; null = all) to black. Call from any thread; returns when they are black.
    public static void Begin(IEnumerable<string>? devices, bool instant = false)
    {
        var d = Application.Current.Dispatcher;
        d.Invoke(() =>
        {
            CloseNow();
            foreach (var s in System.Windows.Forms.Screen.AllScreens)
            {
                if (devices != null && !devices.Contains(s.DeviceName, StringComparer.OrdinalIgnoreCase)) continue;
                var w = new Window { WindowStyle = WindowStyle.None, ResizeMode = ResizeMode.NoResize, AllowsTransparency = true, Background = Brushes.Black, ShowInTaskbar = false, Topmost = true, ShowActivated = false, Opacity = 0 };
                var b = s.Bounds;
                w.Show();
                var h = new WindowInteropHelper(w).Handle;
                SetWindowLong(h, GWL_EXSTYLE, GetWindowLong(h, GWL_EXSTYLE) | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);   // clicks go straight through; it never takes focus
                SetWindowPos(h, HWND_TOPMOST, b.X, b.Y, b.Width, b.Height, 0x10);   // exact pixels (SWP_NOACTIVATE); WPF's own units drift on a mixed-DPI setup
                d.Invoke(() => { }, System.Windows.Threading.DispatcherPriority.Render);
                SetWindowPos(h, HWND_TOPMOST, b.X, b.Y, b.Width, b.Height, 0x10);   // again: a move onto another screen can make WPF rescale the window
                if (!instant) w.BeginAnimation(UIElement.OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(140)));
                Wins.Add(w);
            }
            _failsafe?.Stop();
            _failsafe = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(6) };
            _failsafe.Tick += (_, _) => CloseNow();
            _failsafe.Start();
        });
        if (!instant) Thread.Sleep(170);
    }

    /// Lets the new mode settle, then fades the screens back in.
    public static void End()
    {
        Thread.Sleep(280);
        Application.Current.Dispatcher.Invoke(() =>
        {
            foreach (var w in Wins.ToList())
            {
                var a = new DoubleAnimation(w.Opacity, 0, TimeSpan.FromMilliseconds(320));
                var win = w; a.Completed += (_, _) => { try { win.Close(); } catch { } Wins.Remove(win); };
                w.BeginAnimation(UIElement.OpacityProperty, a);
            }
            _failsafe?.Stop();
        });
    }

    public static int Count => Wins.Count;

    /// The pixel rectangles of the windows that are up (for the self-test).
    public static List<System.Drawing.Rectangle> Rects() => Wins.Select(w => { var h = new WindowInteropHelper(w).Handle; GetWindowRect(h, out var r); return System.Drawing.Rectangle.FromLTRB(r.L, r.T, r.R, r.B); }).ToList();
    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr h, out RECT r);

    public static void CloseNow()
    {
        _failsafe?.Stop();
        foreach (var w in Wins.ToList()) { try { w.BeginAnimation(UIElement.OpacityProperty, null); w.Close(); } catch { } }
        Wins.Clear();
    }
}
