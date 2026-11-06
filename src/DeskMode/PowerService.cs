using System.Runtime.InteropServices;
using static DeskMode.Native;

namespace DeskMode;

/// The power modes Windows 11 offers in Settings > System > Power & battery > Power mode. Surface calls the middle one "Recommended" and PCs from other
/// makers "Balanced"; "Best power efficiency" and "Better performance" only show up on some PCs. (Appended in this order so saved values keep their meaning.)
public enum PowerMode { Recommended, BetterPerformance, BestPerformance, BestEfficiency }

public static class PowerService
{
    private static readonly Guid ModeRecommended = Guid.Empty;
    private static readonly Guid ModeBetterPerformance = new("3af9b8d9-7c97-431d-ad78-34a8bfea439f");
    private static readonly Guid ModeBestPerformance = new("ded574b5-45a0-4f42-8737-46345c09c238");
    private static readonly Guid ModeBestEfficiency = new("961cc777-2547-4f9d-8174-7d86181b8a7a");

    [DllImport("powrprof.dll")] private static extern uint PowerGetUserConfiguredACPowerMode(out Guid mode);
    [DllImport("powrprof.dll")] private static extern uint PowerGetUserConfiguredDCPowerMode(out Guid mode);
    [DllImport("powrprof.dll")] private static extern uint PowerSetUserConfiguredACPowerMode(ref Guid mode);
    [DllImport("powrprof.dll")] private static extern uint PowerSetUserConfiguredDCPowerMode(ref Guid mode);
    public static bool IsPluggedIn => GetSystemPowerStatus(out var s) && s.ACLineStatus != 0;

    /// -1 when there is no battery (a desktop) or Windows doesn't say.
    public static int BatteryPercent => GetSystemPowerStatus(out var s) && s.BatteryLifePercent <= 100 ? s.BatteryLifePercent : -1;

    public static bool HasBattery => GetSystemPowerStatus(out var s) && (s.BatteryFlag & 128) == 0 && s.BatteryFlag != 255;   // 128 = no system battery

    /// False on PCs (or Windows versions) that have no power mode setting: the panel then hides that card.
    public static bool IsSupported { get { try { return PowerGetUserConfiguredACPowerMode(out _) == 0; } catch { return false; } } }

    /// Windows shows three power modes at a time, and which three depends on the PC: Surface offers Recommended / Better performance / Best performance,
    /// most others Best power efficiency / Balanced / Best performance. 0 = decide by the PC's maker, 1 = the Surface three, 2 = the usual three (Settings).
    public static int StylePref { get; set; }
    private static readonly bool MakerIsMicrosoft = ReadMaker().Contains("Microsoft", StringComparison.OrdinalIgnoreCase);
    private static string ReadMaker()
    {
        try { using var k = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(@"HARDWARE\DESCRIPTION\System\BIOS"); return k?.GetValue("SystemManufacturer") as string ?? ""; }
        catch { return ""; }
    }
    public static bool SurfaceStyle => StylePref == 1 || (StylePref == 0 && MakerIsMicrosoft);

    public static string ModeName(PowerMode m) => m switch
    {
        PowerMode.BestPerformance => "Best",
        PowerMode.BetterPerformance => "Better",
        PowerMode.BestEfficiency => "Efficient",
        _ => SurfaceStyle ? "Recommended" : "Balanced",
    };

    /// The three modes to offer, plus `also` when the PC is in a mode outside them (so the buttons never say something untrue).
    public static List<(string Label, PowerMode Mode)> Choices(PowerMode? also = null)
    {
        var l = SurfaceStyle
            ? new List<(string, PowerMode)> { (ModeName(PowerMode.Recommended), PowerMode.Recommended), ("Better", PowerMode.BetterPerformance), ("Best", PowerMode.BestPerformance) }
            : new List<(string, PowerMode)> { ("Efficient", PowerMode.BestEfficiency), ("Balanced", PowerMode.Recommended), ("Best", PowerMode.BestPerformance) };
        if (also is { } m && !l.Any(x => x.Item2 == m)) l.Insert(0, (ModeName(m), m));
        return l;
    }

    // Windows 11 reports Energy Saver through the "effective power mode" (0 = energy/battery saver).
    private delegate void EffectivePowerModeCallback(int mode, IntPtr context);
    [DllImport("powrprof.dll")]
    private static extern int PowerRegisterForEffectivePowerModeNotifications(uint version, EffectivePowerModeCallback callback, IntPtr context, out IntPtr handle);
    private static EffectivePowerModeCallback? _callback;
    private static int _effectiveMode = -1;

    /// Raised when Energy Saver or the power mode changes, from anywhere (including Quick Settings).
    public static event Action? EffectiveModeChanged;

    public static int EffectiveMode => _effectiveMode;

    public static void WatchEffectiveMode()
    {
        if (_callback != null) return;
        try
        {
            _callback = (mode, _) => { _effectiveMode = mode; EffectiveModeChanged?.Invoke(); };
            PowerRegisterForEffectivePowerModeNotifications(2, _callback, IntPtr.Zero, out _);
        }
        catch { }   // older Windows: no such notification
    }

    /// Power mode for plugged in (ac = true) or on battery, as shown in Windows Settings.
    public static PowerMode GetPowerMode(bool ac)
    {
        try
        {
            uint rc = ac ? PowerGetUserConfiguredACPowerMode(out var g) : PowerGetUserConfiguredDCPowerMode(out g);
            if (rc != 0) return PowerMode.Recommended;
            if (g == ModeBestPerformance) return PowerMode.BestPerformance;
            if (g == ModeBetterPerformance) return PowerMode.BetterPerformance;
            if (g == ModeBestEfficiency) return PowerMode.BestEfficiency;
        }
        catch { }
        return PowerMode.Recommended;
    }

    public static PowerMode GetPowerMode() => GetPowerMode(IsPluggedIn);

    /// Sets the power mode for plugged in (ac = true) or on battery. Works for either source at any time.
    public static void SetPowerMode(PowerMode mode, bool ac)
    {
        var g = mode switch
        {
            PowerMode.BestPerformance => ModeBestPerformance,
            PowerMode.BetterPerformance => ModeBetterPerformance,
            PowerMode.BestEfficiency => ModeBestEfficiency,
            _ => ModeRecommended
        };
        try
        {
            if (ac) PowerSetUserConfiguredACPowerMode(ref g);
            else PowerSetUserConfiguredDCPowerMode(ref g);
        }
        catch { }
    }

    /// Sets the power mode for whichever power source is in use right now.
    public static void SetPowerMode(PowerMode mode) => SetPowerMode(mode, IsPluggedIn);
}
