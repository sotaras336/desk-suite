using System.Collections.Concurrent;
using System.Management;
using static DeskMode.Native;

namespace DeskMode;

public sealed record DisplayInfo(
    string DeviceName,      // \\.\DISPLAY1
    string HardwareId,      // GSM5C8B / LGD077A
    string FriendlyName,    // "LG ULTRAGEAR", or "Built-in screen"
    bool IsInternal,
    int Width, int Height, int RefreshRate,
    IReadOnlyList<int> AvailableRates)
{
    /// A name short enough for a label next to a row of buttons.
    public string ShortName => FriendlyName.Length <= 14 ? FriendlyName : FriendlyName[..13].TrimEnd() + "…";
    /// The short name used in buttons and rows: the maker ("LG" for "LG ULTRAGEAR"), or "Monitor" when the name says nothing.
    public string Brand
    {
        get
        {
            if (IsInternal) return "Built-in";
            var first = FriendlyName.Split(' ', StringSplitOptions.RemoveEmptyEntries).FirstOrDefault() ?? "";
            return first.Length is >= 2 and <= 8 && !first.Equals("Generic", StringComparison.OrdinalIgnoreCase) && !first.Equals("External", StringComparison.OrdinalIgnoreCase) && !first.Equals("Monitor", StringComparison.OrdinalIgnoreCase) ? first : "Monitor";
        }
    }
    public int HighestRate => AvailableRates.Count > 0 ? AvailableRates.Max() : RefreshRate;
}

public static class DisplayService
{
    private static string? _internalHwId;
    private static readonly ConcurrentDictionary<string, string> Names = new(StringComparer.OrdinalIgnoreCase);
    private static DateTime _namesAt;

    /// Hardware id of the built-in panel, found via the WMI brightness class (only internal panels expose it). Null on a desktop.
    private static string? InternalHardwareId()
    {
        if (_internalHwId != null) return _internalHwId;
        try
        {
            using var s = new ManagementObjectSearcher(@"root\wmi", "SELECT InstanceName FROM WmiMonitorBrightness");
            foreach (ManagementObject o in s.Get())
            {
                var parts = ((string)o["InstanceName"]).Split('\\');
                if (parts.Length > 1) return _internalHwId = parts[1];
            }
        }
        catch { }
        return null;
    }

    /// The names monitors report about themselves (EDID "monitor name", e.g. "LG ULTRAGEAR"), by hardware id. Read again when a new id shows up.
    private static void LoadNames()
    {
        _namesAt = DateTime.UtcNow;
        try
        {
            using var s = new ManagementObjectSearcher(@"root\wmi", "SELECT InstanceName, UserFriendlyName, ManufacturerName FROM WmiMonitorID");
            foreach (ManagementObject o in s.Get())
            {
                var parts = ((string)o["InstanceName"]).Split('\\');
                if (parts.Length < 2) continue;
                string name = Chars(o["UserFriendlyName"]);
                if (name.Length == 0) name = Chars(o["ManufacturerName"]);
                if (name.Length > 0) Names[parts[1]] = name;
            }
        }
        catch { }
    }

    private static string Chars(object? v) => v is ushort[] a ? new string(a.TakeWhile(c => c != 0).Select(c => (char)c).ToArray()).Trim() : "";

    public static string NameFor(string hwId, string deviceString)
    {
        if (!Names.ContainsKey(hwId) && DateTime.UtcNow - _namesAt > TimeSpan.FromSeconds(20)) LoadNames();
        if (Names.TryGetValue(hwId, out var n)) return n;
        return deviceString.Length > 0 && !deviceString.Contains("Generic", StringComparison.OrdinalIgnoreCase) && !deviceString.Contains("PnP", StringComparison.OrdinalIgnoreCase) ? deviceString : "External monitor";
    }

    public static List<DisplayInfo> GetDisplays()
    {
        var result = new List<DisplayInfo>();
        var internalId = InternalHardwareId();

        for (int i = 0; ; i++)
        {
            var adapter = DISPLAY_DEVICE.Create();
            if (!EnumDisplayDevices(null, i, ref adapter, 0)) break;
            if ((adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0) continue;

            var mon = DISPLAY_DEVICE.Create();
            EnumDisplayDevices(adapter.DeviceName, 0, ref mon, 0);
            var idParts = (mon.DeviceID ?? "").Split('\\');
            var hwId = idParts.Length > 1 ? idParts[1] : "";

            var cur = DEVMODE.Create();
            if (!EnumDisplaySettings(adapter.DeviceName, ENUM_CURRENT_SETTINGS, ref cur)) continue;

            var rates = new SortedSet<int>();
            for (int m = 0; ; m++)
            {
                var dm = DEVMODE.Create();
                if (!EnumDisplaySettings(adapter.DeviceName, m, ref dm)) break;
                if (dm.dmPelsWidth == cur.dmPelsWidth && dm.dmPelsHeight == cur.dmPelsHeight && dm.dmDisplayFrequency > 1)
                    rates.Add(dm.dmDisplayFrequency);
            }

            bool isInternal = internalId != null
                ? hwId.Equals(internalId, StringComparison.OrdinalIgnoreCase)
                : mon.DeviceString.Contains("Panel", StringComparison.OrdinalIgnoreCase);

            result.Add(new DisplayInfo(adapter.DeviceName, hwId,
                isInternal ? "Built-in screen" : NameFor(hwId, mon.DeviceString),
                isInternal, cur.dmPelsWidth, cur.dmPelsHeight, cur.dmDisplayFrequency, rates.ToList()));
        }
        return result;
    }

    public static DisplayInfo? Internal(List<DisplayInfo> all) => all.FirstOrDefault(d => d.IsInternal);

    public static List<DisplayInfo> Externals(List<DisplayInfo> all) => all.Where(d => !d.IsInternal).ToList();

    /// The monitor DeskMode reacts to: the one whose hardware id starts with `match`, or the first external one when `match` is empty.
    public static DisplayInfo? External(List<DisplayInfo> all, string match) =>
        all.FirstOrDefault(d => !d.IsInternal && (match.Length == 0 || d.HardwareId.StartsWith(match, StringComparison.OrdinalIgnoreCase)));

    public static bool SetRefreshRate(DisplayInfo d, int hz)
    {
        if (d.RefreshRate == hz) return true;
        if (!d.AvailableRates.Contains(hz)) return false;
        var dm = DEVMODE.Create();
        if (!EnumDisplaySettings(d.DeviceName, ENUM_CURRENT_SETTINGS, ref dm)) return false;
        dm.dmDisplayFrequency = hz;
        dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
        return ChangeDisplaySettingsEx(d.DeviceName, ref dm, IntPtr.Zero, CDS_UPDATEREGISTRY, IntPtr.Zero) == DISP_CHANGE_SUCCESSFUL;
    }

    /// Several screens at once. Windows blanks a screen for a moment whenever its mode changes; changing each screen on its own blanks them one after the other,
    /// so the changes are queued (CDS_NORESET) and applied with a single commit: one blink, all screens together. Screens already at the wanted rate are not touched.
    public static bool SetRefreshRates(IEnumerable<(DisplayInfo D, int Hz)> changes)
    {
        var todo = changes.Where(c => c.D.RefreshRate != c.Hz && c.D.AvailableRates.Contains(c.Hz)).ToList();
        if (todo.Count == 0) return true;
        if (todo.Count == 1) return SetRefreshRate(todo[0].D, todo[0].Hz);
        bool ok = true;
        foreach (var (disp, hz) in todo)
        {
            var dm = DEVMODE.Create();
            if (!EnumDisplaySettings(disp.DeviceName, ENUM_CURRENT_SETTINGS, ref dm)) { ok = false; continue; }
            dm.dmDisplayFrequency = hz; dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
            ok &= ChangeDisplaySettingsEx(disp.DeviceName, ref dm, IntPtr.Zero, CDS_UPDATEREGISTRY | CDS_NORESET, IntPtr.Zero) == DISP_CHANGE_SUCCESSFUL;
        }
        return ChangeDisplaySettingsExCommit(null, IntPtr.Zero, IntPtr.Zero, 0, IntPtr.Zero) == DISP_CHANGE_SUCCESSFUL && ok;
    }

    /// 0 = the highest rate the screen offers. Otherwise the requested rate if offered, else the highest rate not above it (or the lowest there is).
    public static int BestRate(DisplayInfo d, int wanted)
    {
        if (d.AvailableRates.Count == 0) return d.RefreshRate;
        if (wanted <= 0) return d.AvailableRates.Max();
        if (d.AvailableRates.Contains(wanted)) return wanted;
        var below = d.AvailableRates.Where(r => r <= wanted).ToList();
        return below.Count > 0 ? below.Max() : d.AvailableRates.Min();
    }
}
