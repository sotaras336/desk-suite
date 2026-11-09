using Microsoft.Win32;

namespace DeskMode;

/// Watches for the monitor and the charger and applies the rules you set in the Settings window:
///   monitor + plugged in  -> "Go max" (your max power mode, each screen at its chosen refresh rate)
///   monitor + on battery  -> back to how it was before connecting, and show the panel
///   monitor unplugged     -> back to how it was before connecting
public sealed class Controller : IDisposable
{
    public Settings Settings { get; }
    public InternalBrightness? LaptopBrightness { get; private set; }
    public DdcBrightness? MonitorBrightness { get; private set; }

    public bool Docked { get; private set; }
    public bool PluggedIn { get; private set; }

    /// A test copy only watches: it never changes a refresh rate, a power mode or Energy saver.
    public static bool ReadOnly { get; set; }

    public event Action? StateChanged;
    public event Action<string>? Notify;
    public event Action? RequestPanel;

    private readonly System.Threading.Timer _debounce;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private bool _started;

    public GameWatch Games { get; }

    public Controller()
    {
        Settings = Settings.Load(); PowerService.StylePref = Settings.PowerModeStyle;
        Games = new GameWatch(Settings);
        Games.Changed += active => { if (!active) Kick(2500); StateChanged?.Invoke(); };   // game over: catch up on what was held back
        _debounce = new System.Threading.Timer(_ => _ = EvaluateAsync(), null, Timeout.Infinite, Timeout.Infinite);
        SystemEvents.DisplaySettingsChanged += (_, _) => { Games.CheckNow(); Kick(1500); };
        SystemEvents.PowerModeChanged += (_, e) => { if (e.Mode == PowerModes.StatusChange) Kick(800); };
        SystemEvents.PowerModeChanged += (_, e) => { if (e.Mode == PowerModes.Resume) Kick(3000); };
    }

    public void Start()
    {
        PowerService.EffectiveModeChanged += () => Task.Run(RefreshEnergySaver);
        PowerService.WatchEffectiveMode();
        Task.Run(() =>
        {
            LaptopBrightness = new InternalBrightness();
            LaptopBrightness.ChangedExternally += OnLaptopBrightnessChangedExternally;
            StateChanged?.Invoke();
        });
        Task.Run(RefreshEnergySaver);
        Kick(0);
    }

    /// The Settings window changed something that decides what DeskMode does: look at the situation again soon.
    public void SettingsChanged() { PowerService.StylePref = Settings.PowerModeStyle; Settings.Save(); Kick(400); StateChanged?.Invoke(); }

    /// Last known Energy Saver state (null = unknown). Read in the background; the UI only reads this field.
    public bool? EnergySaverOn { get; private set; }

    public void RefreshEnergySaver()
    {
        EnergySaverOn = EnergySaver.IsOn();
        StateChanged?.Invoke();
    }

    /// Turns Energy Saver on/off right away (same switch as Quick Settings).
    public void SetEnergySaver(bool on)
    {
        if (ReadOnly) return;
        EnergySaver.Set(on);
        RefreshEnergySaver();
    }

    private void Kick(int ms) => _debounce.Change(ms, Timeout.Infinite);

    public void Toast(string message) => Notify?.Invoke(message);
    private void Say(string message) { if (Settings.Notifications) Notify?.Invoke(message); }

    public List<DisplayInfo> Displays() => DisplayService.GetDisplays();

    /// One-line summary for the tray tooltip.
    public string StatusLine()
    {
        var displays = Displays();
        var laptop = DisplayService.Internal(displays);
        var monitor = DisplayService.External(displays, Settings.MonitorMatch);
        var parts = new List<string>();
        if (monitor != null) parts.Add($"{monitor.ShortName} {monitor.RefreshRate}Hz");
        if (laptop != null) parts.Add($"Built-in {laptop.RefreshRate}Hz");
        if (PowerService.IsSupported) parts.Add(PowerService.ModeName(PowerService.GetPowerMode()));
        if (EnergySaverOn == true) parts.Add("Energy saver");
        int pct = PowerService.BatteryPercent;
        parts.Add(PowerService.IsPluggedIn ? (pct >= 0 ? $"charging {pct}%" : "plugged in") : $"battery {pct}%");
        return "DeskMode\n" + string.Join(" · ", parts);
    }
    public DisplayInfo? Laptop => DisplayService.Internal(Displays());
    public DisplayInfo? Monitor => DisplayService.External(Displays(), Settings.MonitorMatch);

    /// What the monitor is called in messages and labels (the connected one, else the last one seen, else a plain word).
    public string MonitorLabel(DisplayInfo? connected) => connected?.FriendlyName is { Length: > 0 } n ? n : Settings.MonitorName.Length > 0 ? Settings.MonitorName : "the monitor";

    private async Task EvaluateAsync()
    {
        Games.CheckNow();
        if (Settings.GameLock && Games.Active) return;   // a full-screen game is up: every change here would be one more mode switch. Games.Changed kicks us again when it ends
        await _gate.WaitAsync();
        try
        {
            var displays = Displays();
            var monitor = DisplayService.External(displays, Settings.MonitorMatch);
            bool docked = monitor != null;
            bool plugged = PowerService.IsPluggedIn;
            bool firstRun = !_started;
            _started = true;
            if (monitor != null && Settings.MonitorName != monitor.FriendlyName) { Settings.MonitorName = monitor.FriendlyName; Settings.Save(); }
            string name = MonitorLabel(monitor);

            bool dockChanged = docked != Docked || firstRun;
            bool plugChanged = plugged != PluggedIn || firstRun;
            Docked = docked;
            PluggedIn = plugged;

            if (docked && (MonitorBrightness == null || dockChanged))
            {
                MonitorBrightness?.Dispose();
                MonitorBrightness = new DdcBrightness(monitor!.DeviceName);
            }
            else if (!docked && MonitorBrightness != null)
            {
                MonitorBrightness.Dispose();
                MonitorBrightness = null;
            }

            if (ReadOnly) return;   // a test copy: look, never touch

            if (!docked)
            {
                if (Settings.Snapshot != null) { if (Settings.RestoreOnBattery) Restore(displays, forDisconnect: true); Settings.Snapshot = null; }
                Settings.Save();
            }
            else if (dockChanged || plugChanged)
            {
                if (Settings.Snapshot == null)
                    Settings.Snapshot = Capture(displays, monitor!);

                if (plugged && Settings.AutoMaxWhenPlugged)
                {
                    ApplyRememberedMax();
                    if (!firstRun) Say($"Plugged in: max performance for {name}");
                }
                else if (!plugged && Settings.RestoreOnBattery)
                {
                    Restore(Displays(), forDisconnect: false);
                    if (!firstRun)
                    {
                        Say($"On battery: back to how it was before {name}");
                        if (Settings.ShowPanelOnBattery) RequestPanel?.Invoke();
                    }
                }
                Settings.Save();
            }
        }
        catch (Exception ex)
        {
            Notify?.Invoke("Something went wrong: " + ex.Message);
        }
        finally
        {
            _gate.Release();
            StateChanged?.Invoke();
        }
    }

    private static Snapshot Capture(List<DisplayInfo> displays, DisplayInfo monitor)
    {
        var laptop = DisplayService.Internal(displays);
        return new Snapshot
        {
            LaptopHz = laptop?.RefreshRate ?? 60,
            MonitorHz = monitor.RefreshRate,
            EnergySaverOn = EnergySaver.IsOn(),
            AcPowerMode = PowerService.GetPowerMode(ac: true),
        };
    }

    /// "Go max": your max power mode, each screen at its chosen rate (0 = its highest).
    public void ApplyMax() => ApplyMax(laptopEasy: false);

    /// "{monitor} first": like Go max, but the built-in screen stays at the easy rate so it costs less GPU work.
    public void ApplyMonitorFirst() => ApplyMax(laptopEasy: true);

    /// Re-applies whichever max preset was used last, for docking back in.
    private void ApplyRememberedMax() => ApplyMax(Settings.PreferLaptop60);

    private void ApplyMax(bool laptopEasy)
    {
        if (ReadOnly) return;
        Settings.PreferLaptop60 = laptopEasy;
        SaveSoon();
        var displays = Displays();
        ApplyPower(Settings.MaxPowerMode, Settings.MaxEnergySaverOff ? false : null);

        ApplyRates(laptopEasy ? Settings.EasyHz : Settings.LaptopMaxHz, Settings.MonitorMaxHz);
        RefreshEnergySaver();
    }

    /// "Go easy": your easy power mode and rate on every screen, Energy saver on (if you left that ticked).
    public void ApplySaver()
    {
        if (ReadOnly) return;
        var displays = Displays();
        ApplyPower(Settings.EasyPowerMode, Settings.EasyEnergySaverOn ? true : null);
        ApplyRates(Settings.EasyHz, Settings.EasyHz);
        RefreshEnergySaver();
    }

    /// The actual mode change, for every screen that is not at its wanted rate yet. Windows blanks those screens while it happens, so (unless switched off in Settings)
    /// they are faded to black first and faded back in after: one smooth dip instead of a flash.
    private void ChangeRates(List<(DisplayInfo D, int Hz)> changes)
    {
        var need = changes.Where(t => t.D.RefreshRate != t.Hz).ToList();
        if (need.Count == 0 || ReadOnly) return;
        bool fade = Settings.SmoothSwitch && System.Windows.Application.Current != null;
        if (fade) try { ScreenFade.Begin(need.Select(t => t.D.DeviceName)); } catch { fade = false; }
        try { DisplayService.SetRefreshRates(need); }
        finally { if (fade) try { ScreenFade.End(); } catch { } }
    }

    /// Sets each screen to its wanted rate (0 = its highest), then looks again and retries a few times: Windows reshuffles the display while the power mode and Energy saver change,
    /// and a single call right after that can be ignored (Go easy straight after Go max stayed at 120 Hz).
    private void ApplyRates(int laptopHz, int monitorHz)
    {
        for (int attempt = 0; attempt < 5; attempt++)
        {
            var displays = Displays(); var todo = new List<(DisplayInfo D, int Hz)>();
            if (DisplayService.Internal(displays) is { } l) todo.Add((l, DisplayService.BestRate(l, laptopHz)));
            if (DisplayService.External(displays, Settings.MonitorMatch) is { } m) todo.Add((m, DisplayService.BestRate(m, monitorHz)));
            if (todo.All(t => t.D.RefreshRate == t.Hz)) return;
            ChangeRates(todo);   // one commit for every screen that has to change: one blink, now a fade
            Thread.Sleep(500);
        }
    }

    /// Windows drops Energy Saver whenever the power mode changes, so set the mode first and the
    /// saver last, and skip either call when it is already in the wanted state (null = leave Energy saver alone).
    private static void ApplyPower(PowerMode mode, bool? saverOn)
    {
        if (PowerService.IsSupported && PowerService.GetPowerMode() != mode) PowerService.SetPowerMode(mode);
        if (saverOn is { } on && EnergySaver.IsOn() != on) EnergySaver.Set(on);
    }

    /// Puts refresh rates, Energy Saver and the plugged-in power mode back to the pre-connection snapshot.
    private void Restore(List<DisplayInfo> displays, bool forDisconnect)
    {
        var s = Settings.Snapshot;
        if (s == null) return;

        if (!PowerService.IsPluggedIn && s.EnergySaverOn is { } wasOn) EnergySaver.Set(wasOn);
        if (DisplayService.Internal(displays) is { } l) DisplayService.SetRefreshRate(l, DisplayService.BestRate(l, s.LaptopHz));
        if (!forDisconnect && DisplayService.External(displays, Settings.MonitorMatch) is { } m)
            DisplayService.SetRefreshRate(m, DisplayService.BestRate(m, s.MonitorHz));

        if (PowerService.IsSupported) PowerService.SetPowerMode(s.AcPowerMode, ac: true);
    }

    // ----- manual controls from the panel -----

    /// Changing the power mode makes Windows drop Energy Saver, so turn it back on if it was on.
    public void SetPowerMode(PowerMode m) => Task.Run(() =>
    {
        if (ReadOnly) return;
        bool wasOn = EnergySaver.IsOn() == true;
        PowerService.SetPowerMode(m);
        if (wasOn)
        {
            // Windows switches Energy Saver off a moment after the mode change; wait for that, then re-enable and confirm.
            for (int i = 0; i < 20 && EnergySaver.IsOn() == true; i++) Thread.Sleep(150);
            for (int attempt = 0; attempt < 5; attempt++)
            {
                Thread.Sleep(700);
                EnergySaver.Set(true);
                Thread.Sleep(1000);
                if (EnergySaver.IsOn() == true) break;
            }
        }
        RefreshEnergySaver();
    });

    public void SetLaptopHz(int hz) { if (!ReadOnly && Laptop is { } l) ChangeRates(new() { (l, hz) }); StateChanged?.Invoke(); }
    public void SetMonitorHz(int hz) { if (!ReadOnly && Monitor is { } m) ChangeRates(new() { (m, hz) }); StateChanged?.Invoke(); }

    // Brightness: each screen keeps its own level. "Both" and the brightness keys move them
    // together while keeping the gap between them (Settings.BrightnessOffset = monitor - built-in).

    public void SetLaptopBrightness(int v)
    {
        LaptopBrightness?.Set(v);
        if (MonitorBrightness is { Value: >= 0 } m) { Settings.BrightnessOffset = m.Value - v; SaveSoon(); }
    }

    public void SetMonitorBrightness(int v)
    {
        MonitorBrightness?.Set(v);
        if (LaptopBrightness is { Value: >= 0 } l) { Settings.BrightnessOffset = v - l.Value; SaveSoon(); }
    }

    /// Slider position for "Both": the midpoint between the two screens.
    public int BothValue
    {
        get
        {
            int l = LaptopBrightness?.Value ?? -1, m = MonitorBrightness?.Value ?? -1;
            if (l < 0) return m; if (m < 0) return l;
            return (int)Math.Round(l + Settings.BrightnessOffset / 2.0);
        }
    }

    public void SetBothBrightness(int v)
    {
        double half = Settings.BrightnessOffset / 2.0;
        LaptopBrightness?.Set((int)Math.Round(v - half));
        MonitorBrightness?.Set((int)Math.Round(v + half));
    }

    /// The built-in screen's brightness keys / Quick Settings slider moved -> the monitor follows, keeping the gap.
    private void OnLaptopBrightnessChangedExternally(int v)
    {
        if (!Settings.LinkBrightness || MonitorBrightness == null) return;
        MonitorBrightness.Set(v + Settings.BrightnessOffset);
        StateChanged?.Invoke();
    }

    private System.Threading.Timer? _saveTimer;
    private void SaveSoon()
    {
        _saveTimer ??= new System.Threading.Timer(_ => Settings.Save());
        _saveTimer.Change(1500, Timeout.Infinite);
    }
    public void Dispose()
    {
        _debounce.Dispose();
        Games.Dispose();
        LaptopBrightness?.Dispose();
        MonitorBrightness?.Dispose();
    }
}
