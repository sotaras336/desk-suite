using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace DeskMode;

public partial class PanelWindow : Window
{
    private readonly Controller _c;
    private readonly System.Windows.Threading.DispatcherTimer _poll;
    private bool _updating;
    public DateTime HiddenAt { get; private set; }

    private static readonly int[] PreferredRates = { 30, 48, 50, 60, 75, 90, 100, 120, 144, 165, 180, 240, 360 };

    public PanelWindow(Controller controller)
    {
        _c = controller;
        InitializeComponent();
        Deactivated += (_, _) => HidePanel();
        // A finger is not a mouse: no arrow or hand under it while touching; the first real mouse move (no stylus device behind it) brings the cursor back.
        PreviewTouchDown += (_, _) => System.Windows.Input.Mouse.OverrideCursor = System.Windows.Input.Cursors.None;
        PreviewMouseMove += (_, e) => { if (e.StylusDevice == null && System.Windows.Input.Mouse.OverrideCursor != null) System.Windows.Input.Mouse.OverrideCursor = null; };
        PreviewKeyDown += (_, e) => { if (e.Key == System.Windows.Input.Key.Escape) { if (_settingsOpen) ShowSettings(false); else HidePanel(); } };
        SizeChanged += (_, _) => { if (IsVisible) Anchor(); };
        DpiChanged += (_, _) => Dispatcher.BeginInvoke(Anchor);
        _c.StateChanged += () => Dispatcher.BeginInvoke(Refresh);
        SourceInitialized += (_, _) => SetHotkeys(_c.Settings.Hotkeys);

        // While the panel is open, re-read the state so changes made elsewhere (Settings, Quick Settings) show up.
        _poll = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _poll.Tick += (_, _) => { Refresh(); Task.Run(() => _c.RefreshEnergySaver()); };
        IsVisibleChanged += (_, _) =>
        {
            _c.Games.NeedListed = IsVisible;   // the Gaming card is the only reader of the listed-game scan
            if (IsVisible) { _poll.Start(); _c.Games.CheckNow(); } else _poll.Stop();
        };
        AddWheelSupport(BothSlider, v => _c.SetBothBrightness(v));
        AddWheelSupport(LaptopSlider, v => _c.SetLaptopBrightness(v));
        AddWheelSupport(MonitorSlider, v => _c.SetMonitorBrightness(v));
    }

    // ---------- global hotkeys ----------

    [DllImport("user32.dll")] private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint modifiers, uint vk);
    [DllImport("user32.dll")] private static extern bool UnregisterHotKey(IntPtr hWnd, int id);
    private const uint ModAlt = 0x1, ModControl = 0x2;
    private const int HotkeyPanel = 1, HotkeyMax = 2, HotkeySaver = 3, HotkeyBorderless = 4;
    private HwndSource? _hotkeySource;

    /// Ctrl+Alt+D opens the panel, Ctrl+Alt+M is Go max, Ctrl+Alt+B is Go easy, Ctrl+Alt+F is borderless for the window in front.
    /// Off in a test copy (they would collide with the real DeskMode's) and when switched off in Settings.
    public void SetHotkeys(bool on)
    {
        var hwnd = new WindowInteropHelper(this).EnsureHandle();
        foreach (var id in new[] { HotkeyPanel, HotkeyMax, HotkeySaver, HotkeyBorderless }) UnregisterHotKey(hwnd, id);
        HotkeyHint.Visibility = on && !App.TestInstance ? Visibility.Visible : Visibility.Collapsed;
        if (!on || App.TestInstance) return;
        RegisterHotKey(hwnd, HotkeyPanel, ModControl | ModAlt, 0x44);  // D
        RegisterHotKey(hwnd, HotkeyMax, ModControl | ModAlt, 0x4D);    // M
        RegisterHotKey(hwnd, HotkeySaver, ModControl | ModAlt, 0x42);  // B
        RegisterHotKey(hwnd, HotkeyBorderless, ModControl | ModAlt, 0x46);  // F
        if (_hotkeySource != null) return;
        _hotkeySource = HwndSource.FromHwnd(hwnd);
        _hotkeySource?.AddHook((IntPtr h, int msg, IntPtr w, IntPtr l, ref bool handled) =>
        {
            if (msg != 0x0312) return IntPtr.Zero; // WM_HOTKEY
            switch (w.ToInt32())
            {
                case HotkeyPanel: if (IsVisible) HidePanel(); else ShowPanel(); break;
                case HotkeyMax: Task.Run(_c.ApplyMax); break;
                case HotkeySaver: Task.Run(_c.ApplySaver); break;
                case HotkeyBorderless: Task.Run(() => _c.Toast(_c.Games.ToggleForeground())); break;
            }
            handled = true;
            return IntPtr.Zero;
        });
    }

    /// Scroll wheel over a brightness slider nudges it by 5%.
    private void AddWheelSupport(Slider slider, Action<int> apply)
    {
        slider.PreviewMouseWheel += (_, e) =>
        {
            if (!slider.IsEnabled) return;
            int v = (int)Math.Clamp(Math.Round(slider.Value) + (e.Delta > 0 ? 5 : -5), 0, 100);
            _updating = true;
            try { slider.Value = v; } finally { _updating = false; }
            apply(v);
            Dispatcher.BeginInvoke(RefreshBrightness);
            e.Handled = true;
        };
    }

    // ---------- show / hide ----------

    // ---------- Settings live in the panel itself: the gear swaps the page, Back (or Esc) swaps it back ----------

    private bool _settingsOpen;
    public bool SettingsOpen => _settingsOpen;
    private SettingsPage? _settingsPage;
    public SettingsPage? SettingsPageForSnapshot => _settingsPage;

    public void ShowSettings(bool on)
    {
        _settingsOpen = on;
        if (on)
        {
            _settingsPage = new SettingsPage(_c, () => ShowSettings(false)) { MaxHeight = Math.Clamp(SystemParameters.WorkArea.Height - 110, 380, 780) };
            SettingsHost.Child = _settingsPage;
        }
        else SettingsHost.Child = null;
        MainPage.Visibility = on ? Visibility.Collapsed : Visibility.Visible;
        SettingsHost.Visibility = on ? Visibility.Visible : Visibility.Collapsed;
        if (!on) Refresh();
        if (IsVisible) Dispatcher.BeginInvoke(Anchor, System.Windows.Threading.DispatcherPriority.Loaded);
    }

    public void ShowPanel(bool settings = false)
    {
        Theme.Apply();
        if (settings != _settingsOpen) ShowSettings(settings);
        Opacity = 0;
        Refresh();
        Show();
        Anchor();
        Activate();

        Slide.Y = 16;
        BeginAnimation(OpacityProperty, new DoubleAnimation(1, TimeSpan.FromMilliseconds(140)));
        Slide.BeginAnimation(TranslateTransform.YProperty,
            new DoubleAnimation(0, TimeSpan.FromMilliseconds(220)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } });

        // Pull fresh brightness from the screens (someone may have used the monitor's own buttons).
        Task.Run(() =>
        {
            _c.RefreshEnergySaver();
            _c.LaptopBrightness?.Refresh();
            _c.MonitorBrightness?.Refresh();
            Dispatcher.BeginInvoke(RefreshBrightness);
        });
    }

    public void HidePanel()
    {
        if (!IsVisible) return;
        if (_settingsOpen) ShowSettings(false);   // it opens on the main page next time
        HiddenAt = DateTime.UtcNow;
        Hide();
        Native.TrimWorkingSet();   // the panel's pages are not needed until it opens again
    }

    [DllImport("user32.dll")] private static extern bool GetCursorPos(out Native.RECT pt);
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromPoint(long pt, int flags);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr h, out Native.RECT r);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, int flags);

    /// Bottom-right corner of the screen the taskbar click happened on, in physical pixels.
    private void Anchor()
    {
        var hwnd = new WindowInteropHelper(this).Handle;
        if (hwnd == IntPtr.Zero) return;
        GetCursorPos(out var pt);
        var mon = MonitorFromPoint(((long)pt.Top << 32) | (uint)pt.Left, 2);
        var mi = new Native.MONITORINFOEX { cbSize = Marshal.SizeOf<Native.MONITORINFOEX>() };
        if (!Native.GetMonitorInfo(mon, ref mi)) return;
        GetWindowRect(hwnd, out var r);
        int w = r.Right - r.Left, h = r.Bottom - r.Top;
        SetWindowPos(hwnd, IntPtr.Zero, mi.rcWork.Right - w, mi.rcWork.Bottom - h, 0, 0, 0x0001 | 0x0004 | 0x0010);
    }

    // ---------- state -> UI ----------

    private void Refresh()
    {
        if (!IsVisible && Opacity != 0) return;
        _updating = true;
        try
        {
            var displays = _c.Displays();
            var laptop = DisplayService.Internal(displays);
            var monitor = DisplayService.External(displays, _c.Settings.MonitorMatch);
            var s = _c.Settings;
            bool plugged = PowerService.IsPluggedIn, hasBattery = PowerService.HasBattery;
            int pct = PowerService.BatteryPercent;
            string mname = _c.MonitorLabel(monitor);

            MonitorStatus.Text = monitor != null ? $"{monitor.FriendlyName} connected" : s.MonitorMatch.Length > 0 && s.MonitorName.Length > 0 ? $"{s.MonitorName} not connected" : "No external monitor";
            MonitorDot.Fill = monitor != null ? (Brush)FindResource("Accent") : (Brush)FindResource("FgDim");
            PowerIcon.Text = plugged ? "" : "";
            PowerStatus.Text = !hasBattery ? "Mains power" : plugged ? $"Plugged in{(pct >= 0 ? $" · {pct}%" : "")}" : $"On battery{(pct >= 0 ? $" · {pct}%" : "")}";

            // presets: say what they will really do on this PC
            int lMax = laptop != null ? DisplayService.BestRate(laptop, s.LaptopMaxHz) : 0, mMax = monitor != null ? DisplayService.BestRate(monitor, s.MonitorMaxHz) : 0;
            int lEasy = laptop != null ? DisplayService.BestRate(laptop, s.EasyHz) : 0;
            MaxSub.Text = Rates(lMax, mMax, "Max power");
            FirstTitle.Text = $"{(monitor?.Brand ?? "Monitor")} first";
            FirstSub.Text = Rates(lEasy, mMax, "");
            bool both = laptop != null && monitor != null;   // "monitor first" needs both screens; with two presets left, they share the width
            MaxLgButton.Visibility = both ? Visibility.Visible : Visibility.Collapsed;
            GapA.Width = new GridLength(both ? 6 : 0); ColB.Width = both ? new GridLength(1, GridUnitType.Star) : new GridLength(0);
            EasySub.Text = $"{s.EasyHz}Hz{(s.EasyEnergySaverOn ? " · saver" : "")}";

            bool powerOk = PowerService.IsSupported;
            PowerTitle.Visibility = PowerCard.Visibility = powerOk ? Visibility.Visible : Visibility.Collapsed;
            var mode = PowerService.GetPowerMode();
            FillModes(mode);
            if (hasBattery)
            {
                var other = PowerService.GetPowerMode(!plugged);
                ModeSource.Text = plugged ? $"Applies while plugged in · on battery: {PowerService.ModeName(other)}" : $"Applies while on battery · plugged in: {PowerService.ModeName(other)}";
            }
            else ModeSource.Text = "";
            ModeSource.Visibility = ModeSource.Text.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
            EnergySaverSwitch.IsEnabled = _c.EnergySaverOn != null;
            EnergySaverSwitch.IsChecked = _c.EnergySaverOn == true;

            LaptopRatesLabel.Visibility = LaptopRatesHost.Visibility = laptop != null ? Visibility.Visible : Visibility.Collapsed;
            MonitorRatesLabel.Text = monitor?.Brand ?? "Monitor";
            FillRates(LaptopRates, laptop, hz => _c.SetLaptopHz(hz));
            FillRates(MonitorRates, monitor, hz => _c.SetMonitorHz(hz));

            LinkKeys.IsChecked = s.LinkBrightness;
            LinkKeysText.Text = $"Brightness keys also change {mname}";
            GameTitle.Visibility = GameCard.Visibility = _c.Games.Active || _c.Games.ListedRunning ? Visibility.Visible : Visibility.Collapsed;   // only when a game is around (full-screen, or a listed game such as Minecraft open); the two switches also live in the tray menu and Settings
            GameLockSwitch.IsChecked = s.GameLock;
            AutoBorderlessSwitch.IsChecked = s.AutoBorderless;
            GameStatus.Text = _c.Games.Active ? $"Full-screen game: {_c.Games.Who}. Screen modes are being left alone."
                : _c.Games.ListedRunning ? $"{_c.Games.ListedName} is open (windowed). Borderless keeps its screen mode steady." : "No game running.";
        }
        finally { _updating = false; }
        RefreshBrightness();
    }

    private static string Rates(int a, int b, string none) =>
        a > 0 && b > 0 ? $"{a} + {b}Hz" : a > 0 ? $"{a}Hz" : b > 0 ? $"{b}Hz" : none;

    private void RefreshBrightness()
    {
        _updating = true;
        try
        {
            var l = _c.LaptopBrightness;
            var m = _c.MonitorBrightness;
            bool hasMonitor = m is { Available: true };
            // With no monitor attached "Both" would just duplicate the built-in slider; with no built-in screen (a desktop) there is nothing to pair it with.
            var both = hasMonitor && l is { Available: true } ? Visibility.Visible : Visibility.Collapsed;
            BothLabel.Visibility = BothSlider.Visibility = BothValue.Visibility = BothSeparator.Visibility = both;
            var lap = l == null || l.Available ? Visibility.Visible : Visibility.Collapsed;   // not read yet: keep the row; read and no brightness control: gone
            LaptopLabel.Visibility = LaptopSlider.Visibility = LaptopValue.Visibility = lap;
            var mon = _c.Monitor != null || hasMonitor ? Visibility.Visible : Visibility.Collapsed;
            MonitorLabel.Visibility = MonitorSlider.Visibility = MonitorValue.Visibility = LinkKeys.Visibility = mon;
            MonitorLabel.Text = _c.Monitor?.Brand ?? "Monitor";

            SetSlider(LaptopSlider, LaptopValue, l is { Value: >= 0 } ? l.Value : -1);
            SetSlider(MonitorSlider, MonitorValue, m is { Value: >= 0 } ? m.Value : -1);
            SetSlider(BothSlider, BothValue, _c.BothValue);
        }
        finally { _updating = false; }
    }

    private static void SetSlider(Slider s, TextBlock label, int v)
    {
        s.IsEnabled = v >= 0;
        label.Text = v >= 0 ? $"{v}%" : "–";
        if (v >= 0 && !s.IsMouseCaptureWithin) s.Value = Math.Clamp(v, 0, 100);
    }

    private readonly Dictionary<string, string> _rateSignatures = new();

    private void FillRates(UniformGrid host, DisplayInfo? d, Action<int> apply)
    {
        // Rebuilding the buttons on every poll would flicker, so only do it when something changed.
        var signature = d == null ? "none" : $"{d.RefreshRate}|{string.Join(',', d.AvailableRates)}";
        if (_rateSignatures.TryGetValue(host.Name, out var previous) && previous == signature && host.Children.Count > 0) return;
        _rateSignatures[host.Name] = signature;

        host.Children.Clear();
        if (d == null)
        {
            host.Children.Add(new RadioButton { Style = (Style)FindResource("Segment"), Content = "Not connected", IsEnabled = false });
            return;
        }
        var shown = d.AvailableRates.Where(r => PreferredRates.Contains(r)).ToList();
        if (shown.Count == 0) shown = d.AvailableRates.ToList();
        if (!shown.Contains(d.RefreshRate)) shown.Add(d.RefreshRate);
        shown.Sort();
        if (shown.Count > 7) shown = shown.Where((r, i) => r == d.RefreshRate || i % 2 == 0 || r == shown[^1]).ToList();   // a crowded row: thin it out, keep the current and the highest
        string group = host.Name;
        foreach (var hz in shown)
        {
            var rb = new RadioButton
            {
                Style = (Style)FindResource("Segment"),
                GroupName = group,
                Content = $"{hz}",
                IsChecked = hz == d.RefreshRate,
                ToolTip = $"{hz} Hz",
            };
            rb.Checked += (_, _) => { if (!_updating) Task.Run(() => apply(hz)); };
            host.Children.Add(rb);
        }
    }

    // ---------- UI -> actions ----------

    private void Max_Click(object sender, RoutedEventArgs e) => Task.Run(_c.ApplyMax);
    private void MaxLg_Click(object sender, RoutedEventArgs e) => Task.Run(_c.ApplyMonitorFirst);
    private void Saver_Click(object sender, RoutedEventArgs e) => Task.Run(_c.ApplySaver);
    private void Settings_Click(object sender, RoutedEventArgs e) => ShowSettings(true);

    private string _modeSignature = "";

    /// The three power modes this PC offers (see PowerService.Choices), as buttons; rebuilt only when that set changes.
    private void FillModes(PowerMode current)
    {
        var choices = PowerService.Choices(current); string sig = string.Join("|", choices.Select(x => x.Label));
        if (sig != _modeSignature || ModeGrid.Children.Count != choices.Count)
        {
            _modeSignature = sig; ModeGrid.Children.Clear();
            foreach (var (label, mode) in choices)
            {
                var rb = new RadioButton { Style = (Style)FindResource("Segment"), GroupName = "mode", Content = label, Tag = mode };
                rb.Checked += (_, _) => { if (!_updating) _c.SetPowerMode(mode); };
                ModeGrid.Children.Add(rb);
            }
        }
        foreach (RadioButton rb in ModeGrid.Children) rb.IsChecked = (PowerMode)rb.Tag == current;
    }

    private void EnergySaver_Click(object sender, RoutedEventArgs e)
    {
        bool on = EnergySaverSwitch.IsChecked == true;
        Task.Run(() => _c.SetEnergySaver(on));
    }

    private void Both_Changed(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (_updating) return;
        int v = (int)Math.Round(e.NewValue);
        BothValue.Text = $"{v}%";
        _c.SetBothBrightness(v);
        _updating = true;
        try
        {
            if (_c.LaptopBrightness is { } l) { LaptopSlider.Value = l.Value; LaptopValue.Text = $"{l.Value}%"; }
            if (_c.MonitorBrightness is { } m) { MonitorSlider.Value = m.Value; MonitorValue.Text = $"{m.Value}%"; }
        }
        finally { _updating = false; }
    }

    private void Laptop_Changed(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (_updating) return;
        int v = (int)Math.Round(e.NewValue);
        LaptopValue.Text = $"{v}%";
        _c.SetLaptopBrightness(v);
        UpdateBoth();
    }

    private void Monitor_Changed(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (_updating) return;
        int v = (int)Math.Round(e.NewValue);
        MonitorValue.Text = $"{v}%";
        _c.SetMonitorBrightness(v);
        UpdateBoth();
    }

    private void UpdateBoth()
    {
        _updating = true;
        try { var b = _c.BothValue; if (b >= 0) { BothSlider.Value = b; BothValue.Text = $"{b}%"; } }
        finally { _updating = false; }
    }

    private void LinkKeys_Click(object sender, RoutedEventArgs e)
    {
        _c.Settings.LinkBrightness = LinkKeys.IsChecked == true;
        _c.Settings.Save();
    }

    private void BorderlessNow_Click(object sender, RoutedEventArgs e) => Task.Run(() => _c.Toast(_c.Games.ToggleListed()));

    private void GameLock_Click(object sender, RoutedEventArgs e)
    {
        _c.Settings.GameLock = GameLockSwitch.IsChecked == true;
        _c.Settings.Save();
    }

    private void AutoBorderless_Click(object sender, RoutedEventArgs e)
    {
        _c.Settings.AutoBorderless = AutoBorderlessSwitch.IsChecked == true;
        _c.Settings.Save();
    }
}
