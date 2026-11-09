using System.IO;
using System.Reflection;
using System.Text.Json;
using Microsoft.Win32;

namespace DeskMode;

/// What things looked like right before the monitor was connected, so we can put them back.
public sealed class Snapshot
{
    public int LaptopHz { get; set; }
    public int MonitorHz { get; set; }
    public bool? EnergySaverOn { get; set; }
    public PowerMode AcPowerMode { get; set; }
}

/// Everything you can change in the Settings window. The defaults suit any PC: any external monitor, each screen's own highest refresh rate.
public sealed class Settings
{
    // ---- which monitor ----
    /// Hardware id prefix of the monitor that switches DeskMode on (e.g. GSM5C8B). Empty = any external monitor.
    public string MonitorMatch { get; set; } = "";
    /// The name that monitor had when it was last seen (shown while it is unplugged).
    public string MonitorName { get; set; } = "";

    // ---- "Go max" ----
    /// Refresh rate of the built-in screen / the monitor for "Go max". 0 = the highest rate the screen offers.
    public int LaptopMaxHz { get; set; }
    public int MonitorMaxHz { get; set; }
    public PowerMode MaxPowerMode { get; set; } = PowerMode.BestPerformance;
    public bool MaxEnergySaverOff { get; set; } = true;

    // ---- "Go easy" ----
    /// Refresh rate of both screens for "Go easy" (and of the built-in screen for "{monitor} first").
    public int EasyHz { get; set; } = 60;
    public PowerMode EasyPowerMode { get; set; } = PowerMode.Recommended;
    public bool EasyEnergySaverOn { get; set; } = true;

    // ---- automatic behaviour ----
    public bool AutoMaxWhenPlugged { get; set; } = true;
    /// On battery, or when the monitor goes away: put refresh rates, Energy saver and the power mode back as they were before.
    public bool RestoreOnBattery { get; set; } = true;
    public bool ShowPanelOnBattery { get; set; } = true;
    public bool Notifications { get; set; } = true;
    /// Which max preset was used last: true = "{monitor} first" (the built-in screen stays at the easy rate). "Go easy" does not change it.
    public bool PreferLaptop60 { get; set; }

    // ---- brightness ----
    /// When on, the built-in screen's brightness keys also move the monitor (keeping the gap).
    public bool LinkBrightness { get; set; } = true;
    public int BrightnessOffset { get; set; }

    // ---- games ----
    /// While a full-screen game is in front, DeskMode does not change refresh rates or power modes by itself (each change is another mode switch).
    public bool GameLock { get; set; } = true;
    /// Turn windowed games on the list below into borderless full-screen automatically (a borderless window never switches the monitor's mode, so no black screens). Ctrl+Alt+F does it by hand for any window.
    public bool AutoBorderless { get; set; }
    /// Borderless windows stop above the taskbar (it stays visible). A window that does not cover the whole screen is never promoted to "full-screen" by Windows, so no flicker on alt-tab. Off = cover the whole screen (the taskbar hides).
    public bool BorderlessKeepBar { get; set; } = true;
    /// Window classes or program names (e.g. "javaw.exe"). GLFW30 is the class of Minecraft and other GLFW games.
    public List<string> BorderlessMatch { get; set; } = new() { "GLFW30" };

    // ---- general ----
    /// Ctrl+Alt+D opens the panel, M = max, B = easy, F = borderless on the window in front.
    public bool Hotkeys { get; set; } = true;
    /// Which three power modes this PC offers: 0 = decide by the PC's maker, 1 = Recommended / Better / Best (Surface), 2 = Efficient / Balanced / Best.
    public int PowerModeStyle { get; set; }

    /// Appearance (Settings > Appearance): the accent colour id ("windows" = follow Windows) and dark/light ("auto" = follow Windows).
    public string Accent { get; set; } = "windows";
    public string Look { get; set; } = "auto";

    public Snapshot? Snapshot { get; set; }

    /// Where settings live. A test copy points DESKMODE_DATA somewhere else so it never touches the real file.
    private static string Dir => Environment.GetEnvironmentVariable("DESKMODE_DATA") is { Length: > 0 } d ? d : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DeskMode");
    private static string FilePath => Path.Combine(Dir, "settings.json");
    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true };

    public static Settings Load()
    {
        try { return JsonSerializer.Deserialize<Settings>(File.ReadAllText(FilePath)) ?? new(); }
        catch { return new(); }
    }

    public void Save()
    {
        try { Directory.CreateDirectory(Dir); File.WriteAllText(FilePath, JsonSerializer.Serialize(this, Json)); }
        catch { }
    }

    /// Back to the defaults for everything you can change in the Settings window (the remembered "before" state stays).
    public void ResetToDefaults()
    {
        var d = new Settings();
        foreach (var p in typeof(Settings).GetProperties(BindingFlags.Public | BindingFlags.Instance))
            if (p.Name != nameof(Snapshot) && p.CanWrite) p.SetValue(this, p.GetValue(d));
    }

    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";

    public static bool StartWithWindows
    {
        get { using var k = Registry.CurrentUser.OpenSubKey(RunKey); return k?.GetValue("DeskMode") != null; }
        set
        {
            using var k = Registry.CurrentUser.CreateSubKey(RunKey);
            if (value) k.SetValue("DeskMode", $"\"{Environment.ProcessPath}\" --background");
            else k.DeleteValue("DeskMode", false);
        }
    }
}
