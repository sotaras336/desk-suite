using System.Runtime.InteropServices;
using System.Text;

namespace DeskMode;

/// Watches the foreground window for a full-screen game (a window that covers a whole screen and has no title bar).
/// While one is up DeskMode stops touching display modes (see Controller), and it can turn windowed games into
/// borderless full-screen windows, which never switch the monitor's mode and so never blank the screen.
public sealed class GameWatch : IDisposable
{
    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] private struct MONITORINFO { public int cb; public RECT Mon, Work; public int Flags; }

    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] private static extern bool IsZoomed(IntPtr h);
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromWindow(IntPtr h, int flags);
    [DllImport("user32.dll")] private static extern bool GetMonitorInfo(IntPtr m, ref MONITORINFO mi);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] private static extern IntPtr GetWindowLongPtr(IntPtr h, int i);
    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")] private static extern IntPtr SetWindowLongPtr(IntPtr h, int i, IntPtr v);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] private static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc cb, IntPtr p);
    private delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("kernel32.dll")] private static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern bool QueryFullProcessImageName(IntPtr h, int flags, StringBuilder s, ref int n);

    private const int GWL_STYLE = -16;
    private const long WS_CAPTION = 0x00C00000, WS_THICKFRAME = 0x00040000, WS_SYSMENU = 0x00080000, WS_MINIMIZEBOX = 0x00020000, WS_MAXIMIZEBOX = 0x00010000;
    private const uint SWP_NOZORDER = 0x4, SWP_FRAMECHANGED = 0x20, SWP_NOACTIVATE = 0x10;

    private readonly Settings _settings;
    private readonly System.Threading.Timer _timer;
    private readonly object _lock = new();
    private readonly Dictionary<IntPtr, (long Style, RECT Rect, bool Zoomed)> _borderless = new();
    private readonly Dictionary<IntPtr, DateTime> _seenWindowed = new();

    /// True while a full-screen game is in front. Read from any thread.
    public bool Active { get; private set; }
    public string Who { get; private set; } = "";
    /// Raised (on a worker thread) when Active flips.
    public event Action<bool>? Changed;

    /// Set while the panel is open: only then is the listed-game scan (which walks every window) worth running.
    public bool NeedListed { get; set; }
    private int _polling;

    public GameWatch(Settings settings)
    {
        _settings = settings;
        _timer = new System.Threading.Timer(_ => Poll(), null, 1000, 1000);
    }

    private static string Class(IntPtr h) { var sb = new StringBuilder(128); GetClassName(h, sb, 128); return sb.ToString(); }
    private static string Title(IntPtr h) { var sb = new StringBuilder(160); GetWindowText(h, sb, 160); return sb.ToString(); }
    private static string Exe(IntPtr h)
    {
        GetWindowThreadProcessId(h, out uint pid);
        var p = OpenProcess(0x1000, false, pid); if (p == IntPtr.Zero) return "";
        try { var sb = new StringBuilder(520); int n = sb.Capacity; return QueryFullProcessImageName(p, 0, sb, ref n) ? System.IO.Path.GetFileName(sb.ToString()) : ""; }
        finally { CloseHandle(p); }
    }

    private static bool Shell(string cls) => cls is "Progman" or "WorkerW" or "Shell_TrayWnd" or "Shell_SecondaryTrayWnd" or "DB3.Wnd" or "DB3.Standin" || cls.StartsWith("DB3.");

    /// A full-screen window: covers its whole monitor (a few pixels of slack) and has no title bar. A maximized normal window keeps its title bar and stops above the taskbar, so it never counts.
    private static bool CoversMonitor(IntPtr h, out MONITORINFO mi)
    {
        mi = new MONITORINFO { cb = Marshal.SizeOf<MONITORINFO>() };
        if (!GetWindowRect(h, out var r)) return false;
        var m = MonitorFromWindow(h, 2); if (m == IntPtr.Zero || !GetMonitorInfo(m, ref mi)) return false;
        return r.L <= mi.Mon.L + 2 && r.T <= mi.Mon.T + 2 && r.R >= mi.Mon.R - 2 && r.B >= mi.Mon.B - 2 && r.L >= mi.Mon.L - 16 && r.T >= mi.Mon.T - 16 && r.R <= mi.Mon.R + 16 && r.B <= mi.Mon.B + 16;
    }

    private void Poll()
    {
        // A slow pass (EnumWindows over every window) must not overlap the next tick or a CheckNow.
        if (Interlocked.Exchange(ref _polling, 1) == 1) return;
        try
        {
            var h = GetForegroundWindow();
            bool game = false; string who = "";
            if (h != IntPtr.Zero && IsWindowVisible(h) && !IsIconic(h))
            {
                var cls = Class(h);
                if (!Shell(cls) && (((long)GetWindowLongPtr(h, GWL_STYLE)) & WS_CAPTION) != WS_CAPTION && CoversMonitor(h, out _))
                { game = true; who = $"{Exe(h)} \"{Title(h)}\""; }
            }
            bool flipped;
            lock (_lock) { flipped = game != Active; Active = game; Who = who; }
            if (flipped) Changed?.Invoke(game);
            // Nothing reads ListedRunning while the panel is shut, so skip the window walk unless it is needed.
            if (_settings.AutoBorderless || NeedListed) ScanListed(_settings.AutoBorderless);
            else if (ListedRunning) { ListedRunning = false; ListedName = ""; _listedWindow = IntPtr.Zero; }
        }
        catch { }
        finally { Interlocked.Exchange(ref _polling, 0); }
    }

    /// Re-checks right now (used when a display change arrives, so the answer is not up to a second old).
    public void CheckNow() => Poll();

    // ---------- borderless full-screen ----------

    /// Windowed windows whose class or program is on the list (default: GLFW30 = Minecraft and other GLFW games) become borderless once they have sat still for 2 seconds.
    /// A listed game (class or program on the list) has a window open, windowed or full-screen. Shows the panel's Gaming card.
    public bool ListedRunning { get; private set; }
    public string ListedName { get; private set; } = "";
    private IntPtr _listedWindow;

    private void ScanListed(bool convert)
    {
        var wanted = _settings.BorderlessMatch;
        // Hoisted out of the callback: this runs for every window on the system.
        bool anyExe = wanted.Any(w => w.EndsWith(".exe", StringComparison.OrdinalIgnoreCase));
        var live = new HashSet<IntPtr>(); IntPtr first = IntPtr.Zero; string name = "";
        lock (_lock)
        {
        if (wanted.Count > 0)
        EnumWindows((h, _) =>
        {
            if (!IsWindowVisible(h)) return true;
            var cls = Class(h);
            bool listed = wanted.Contains(cls, StringComparer.OrdinalIgnoreCase) || (anyExe && wanted.Contains(Exe(h), StringComparer.OrdinalIgnoreCase));
            if (!listed) return true;
            live.Add(h); if (first == IntPtr.Zero) { first = h; name = Title(h); }
            if (!convert || IsIconic(h)) return true;
            if (_borderless.ContainsKey(h)) return true;
            if ((((long)GetWindowLongPtr(h, GWL_STYLE)) & WS_CAPTION) != WS_CAPTION) return true;   // already full-screen or frameless
            if (!_seenWindowed.TryGetValue(h, out var since)) { _seenWindowed[h] = DateTime.UtcNow; return true; }
            if (DateTime.UtcNow - since > TimeSpan.FromSeconds(2)) { MakeBorderless(h); _seenWindowed.Remove(h); }
            return true;
        }, IntPtr.Zero);
        _listedWindow = first; ListedRunning = first != IntPtr.Zero; ListedName = name;
        foreach (var k in _seenWindowed.Keys.Where(k => !live.Contains(k)).ToList()) _seenWindowed.Remove(k);
        foreach (var k in _borderless.Keys.Where(k => !IsWindow(k)).ToList()) _borderless.Remove(k);
        }
    }

    /// Panel button: toggles borderless on the running listed game without needing it to be in front.
    public string ToggleListed()
    {
        lock (_lock)
        {
        var h = _listedWindow;
        if (h == IntPtr.Zero || !IsWindow(h)) return "No listed game is running";
        if (_borderless.ContainsKey(h)) { Restore(h); return "Window back to normal"; }
        if ((((long)GetWindowLongPtr(h, GWL_STYLE)) & WS_CAPTION) != WS_CAPTION) return "That game is in full-screen mode. Set it to windowed first (Minecraft: F11)";
        MakeBorderless(h); return "Borderless full-screen. Press again to undo";
        }
    }

    /// Hotkey (Ctrl+Alt+F): toggles the window in front between its normal frame and borderless full-screen. Returns a short message for a balloon.
    public string ToggleForeground()
    {
        var h = GetForegroundWindow();
        if (h == IntPtr.Zero || Shell(Class(h))) return "No window to change";
        lock (_lock)
        {
            if (_borderless.ContainsKey(h)) { Restore(h); return "Window back to normal"; }
            if ((((long)GetWindowLongPtr(h, GWL_STYLE)) & WS_CAPTION) != WS_CAPTION) return "Already full-screen (that is the mode that blanks the screen: set the game to windowed, then press this)";
            MakeBorderless(h); return "Borderless full-screen. Press again to undo";
        }
    }

    private void MakeBorderless(IntPtr h)
    {
        var m = MonitorFromWindow(h, 2); var mi = new MONITORINFO { cb = Marshal.SizeOf<MONITORINFO>() };
        if (m == IntPtr.Zero || !GetMonitorInfo(m, ref mi)) return;
        bool zoomed = IsZoomed(h); if (zoomed) ShowWindow(h, 9);   // SW_RESTORE first, so the saved size is the real one
        GetWindowRect(h, out var rect);
        long style = (long)GetWindowLongPtr(h, GWL_STYLE);
        _borderless[h] = (style, rect, zoomed);
        SetWindowLongPtr(h, GWL_STYLE, (IntPtr)(style & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)));
        // One pixel short at the bottom: a window that covers the screen exactly is promoted by Windows to "full-screen" (direct flip), and that is what flickers the display on focus changes.
        var area = _settings.BorderlessKeepBar ? mi.Work : mi.Mon;   // above the taskbar, or the whole screen
        SetWindowPos(h, IntPtr.Zero, area.L, area.T, area.R - area.L, area.B - area.T - (_settings.BorderlessKeepBar ? 0 : 1), SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }

    private void Restore(IntPtr h)
    {
        if (!_borderless.Remove(h, out var s) || !IsWindow(h)) return;
        SetWindowLongPtr(h, GWL_STYLE, (IntPtr)s.Style);
        SetWindowPos(h, IntPtr.Zero, s.Rect.L, s.Rect.T, s.Rect.R - s.Rect.L, s.Rect.B - s.Rect.T, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        if (s.Zoomed) ShowWindow(h, 3);
    }

    public void Dispose()
    {
        _timer.Dispose();
        lock (_lock) foreach (var h in _borderless.Keys.ToList()) Restore(h);   // never leave a game window without its frame
    }
}
