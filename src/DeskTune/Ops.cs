using System.Diagnostics;
using System.Text.Json.Nodes;
using Microsoft.Win32;

namespace DeskTune;

/// One small, exactly reversible change. Apply() returns what was there before (as JSON, so it survives a restart in the undo log);
/// Restore() puts it back. IsApplied tells whether the change is in effect right now (whoever made it).
public abstract class Op
{
    public bool Admin { get; init; }
    public abstract bool IsApplied { get; }
    public abstract JsonNode Apply();
    public abstract void Restore(JsonNode before);
    public abstract string Describe();

    public static RegistryKey Base(RegistryHive h) => RegistryKey.OpenBaseKey(h, RegistryView.Registry64);

    /// Runs a console tool hidden and waits (sc.exe, powershell). Returns its exit code.
    public static int Run(string exe, string args, int timeoutMs = 60000)
    {
        using var p = Process.Start(new ProcessStartInfo(exe, args) { CreateNoWindow = true, UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true })!;
        p.StandardOutput.ReadToEndAsync(); p.StandardError.ReadToEndAsync();
        if (!p.WaitForExit(timeoutMs)) { try { p.Kill(); } catch { } return -1; }
        return p.ExitCode;
    }
}

/// A registry value set to a fixed value (DWORD or binary). Undo writes the old value back, or deletes it if there was none.
public sealed class RegOp : Op
{
    public RegistryHive Hive { get; }
    public string Key { get; }
    public string Name { get; }
    public object Value { get; }
    public RegistryValueKind Kind { get; }

    public RegOp(RegistryHive hive, string key, string name, object value, RegistryValueKind kind = RegistryValueKind.DWord)
    { Hive = hive; Key = key; Name = name; Value = value; Kind = kind; Admin = hive == RegistryHive.LocalMachine; }

    public object? Current { get { using var k = Base(Hive).OpenSubKey(Key); return k?.GetValue(Name, null, RegistryValueOptions.DoNotExpandEnvironmentNames); } }

    public override bool IsApplied => Current is { } c && (c is byte[] a && Value is byte[] b ? a.AsSpan().SequenceEqual(b) : Convert.ToString(c) == Convert.ToString(Value));

    public override JsonNode Apply()
    {
        using var k = Base(Hive).CreateSubKey(Key, true);
        var before = Snapshot(k, Name);
        k.SetValue(Name, Value, Kind);
        return before;
    }

    public override void Restore(JsonNode before)
    {
        using var k = Base(Hive).CreateSubKey(Key, true);
        Put(k, Name, before);
    }

    /// {"exists":false} or {"exists":true,"kind":"DWord","value":...}: enough to recreate the value exactly.
    public static JsonNode Snapshot(RegistryKey k, string name)
    {
        var v = k.GetValue(name, null, RegistryValueOptions.DoNotExpandEnvironmentNames);
        if (v == null) return new JsonObject { ["exists"] = false };
        var kind = k.GetValueKind(name);
        JsonNode val = v switch { byte[] b => Convert.ToBase64String(b), string[] m => new JsonArray(m.Select(x => (JsonNode?)x).ToArray()), int i => i, long l => l, _ => v.ToString()! };
        return new JsonObject { ["exists"] = true, ["kind"] = kind.ToString(), ["value"] = val };
    }

    public static void Put(RegistryKey k, string name, JsonNode before)
    {
        if (!(bool)before["exists"]!) { k.DeleteValue(name, false); return; }
        var kind = Enum.Parse<RegistryValueKind>((string)before["kind"]!);
        var node = before["value"]!;
        object v = kind switch
        {
            RegistryValueKind.Binary => Convert.FromBase64String((string)node!),
            RegistryValueKind.DWord => (int)node,
            RegistryValueKind.QWord => (long)node,
            RegistryValueKind.MultiString => node.AsArray().Select(x => (string)x!).ToArray(),
            _ => (string)node!,
        };
        k.SetValue(name, v, kind);
    }

    public override string Describe() => $"{(Hive == RegistryHive.LocalMachine ? "HKLM" : "HKCU")}\\{Key}\\{Name} = {(Value is byte[] b ? Convert.ToHexString(b) : Value)}";
}

/// A Windows service set to Disabled and stopped. Undo restores its start type, and starts it again if it was automatic.
public sealed class ServiceOp : Op
{
    public string Name { get; }
    private string Key => $@"SYSTEM\CurrentControlSet\Services\{Name}";

    public ServiceOp(string name) { Name = name; Admin = true; }

    public bool Exists { get { using var k = Base(RegistryHive.LocalMachine).OpenSubKey(Key); return k?.GetValue("Start") != null; } }
    private int Start { get { using var k = Base(RegistryHive.LocalMachine).OpenSubKey(Key); return k?.GetValue("Start") is int s ? s : -1; } }

    public override bool IsApplied => Start == 4;

    public override JsonNode Apply()
    {
        int before = Start;
        if (Run("sc.exe", $"config \"{Name}\" start= disabled") != 0) throw new InvalidOperationException($"Windows refused to change the service {Name}.");
        Run("sc.exe", $"stop \"{Name}\"", 20000);
        return new JsonObject { ["start"] = before };
    }

    public override void Restore(JsonNode before)
    {
        int s = (int)before["start"]!;
        string type = s switch { 2 => "auto", 3 => "demand", 4 => "disabled", 0 => "boot", 1 => "system", _ => "demand" };
        Run("sc.exe", $"config \"{Name}\" start= {type}");
        if (s == 2) Run("sc.exe", $"start \"{Name}\"", 20000);
    }

    public override string Describe() => $"service {Name} -> disabled";
}

/// A program that starts with Windows, switched off the way Task Manager's Startup tab does it (the StartupApproved flag): the entry itself stays,
/// so Task Manager shows it as Disabled and can turn it back on too.
public sealed class StartupOp : Op
{
    public RegistryHive Hive { get; }
    public string ApprovedKey { get; }   // ...\Explorer\StartupApproved\Run | Run32 | StartupFolder
    public string Name { get; }

    public StartupOp(RegistryHive hive, string approvedKey, string name) { Hive = hive; ApprovedKey = approvedKey; Name = name; Admin = hive == RegistryHive.LocalMachine; }

    public override bool IsApplied { get { using var k = Base(Hive).OpenSubKey(ApprovedKey); return k?.GetValue(Name) is byte[] { Length: > 0 } b && (b[0] & 1) == 1; } }   // odd first byte = disabled

    public override JsonNode Apply()
    {
        using var k = Base(Hive).CreateSubKey(ApprovedKey, true);
        var before = RegOp.Snapshot(k, Name);
        var v = new byte[12]; v[0] = 3; BitConverter.GetBytes(DateTime.UtcNow.ToFileTimeUtc()).CopyTo(v, 4);   // 03 00 00 00 + when it was disabled
        k.SetValue(Name, v, RegistryValueKind.Binary);
        return before;
    }

    public override void Restore(JsonNode before) { using var k = Base(Hive).CreateSubKey(ApprovedKey, true); RegOp.Put(k, Name, before); }
    public override string Describe() => $"startup '{Name}' -> disabled";
}

/// A Store app that starts with Windows (Teams, Phone Link...): its startup task's State value, 2 = on, 1 = off by the user.
public sealed class AppStartupOp : Op
{
    public string Key { get; }
    public AppStartupOp(string key) { Key = key; }
    public override bool IsApplied { get { using var k = Base(RegistryHive.CurrentUser).OpenSubKey(Key); return k?.GetValue("State") is int s && s == 1; } }
    public override JsonNode Apply() { using var k = Base(RegistryHive.CurrentUser).CreateSubKey(Key, true); var b = RegOp.Snapshot(k, "State"); k.SetValue("State", 1, RegistryValueKind.DWord); return b; }
    public override void Restore(JsonNode before) { using var k = Base(RegistryHive.CurrentUser).CreateSubKey(Key, true); RegOp.Put(k, "State", before); }
    public override string Describe() => $"app startup task {Key} -> off";
}

/// A preinstalled Store app removed for this user. Undo can't reinstall silently: it opens the app's Microsoft Store page.
public sealed class AppxOp : Op
{
    public string Package { get; }   // the package Name, e.g. Clipchamp.Clipchamp
    public static HashSet<string> Installed = new(StringComparer.OrdinalIgnoreCase);   // filled by the scan
    public static Dictionary<string, string> Families = new(StringComparer.OrdinalIgnoreCase);

    public AppxOp(string package) { Package = package; }
    public bool Present => Installed.Contains(Package);
    public override bool IsApplied => !Present;

    public override JsonNode Apply()
    {
        string family = Families.TryGetValue(Package, out var f) ? f : "";
        if (Run("powershell.exe", $"-NoProfile -Command \"Get-AppxPackage -Name '{Package}' | Remove-AppxPackage\"") != 0) throw new InvalidOperationException($"Windows refused to remove {Package}.");
        Installed.Remove(Package);
        return new JsonObject { ["family"] = family };
    }

    public override void Restore(JsonNode before)
    {
        string family = (string?)before["family"] ?? "";
        Process.Start(new ProcessStartInfo(family.Length > 0 ? "ms-windows-store://pdp/?PFN=" + family : "ms-windows-store://search/?query=" + Uri.EscapeDataString(Package)) { UseShellExecute = true });
    }

    public override string Describe() => $"remove app {Package}";

    /// One PowerShell call lists every installed package (name and family) for the current user.
    public static void Scan()
    {
        try
        {
            var psi = new ProcessStartInfo("powershell.exe", "-NoProfile -Command \"Get-AppxPackage | ForEach-Object { $_.Name + '|' + $_.PackageFamilyName }\"") { CreateNoWindow = true, UseShellExecute = false, RedirectStandardOutput = true };
            using var p = Process.Start(psi)!; string text = p.StandardOutput.ReadToEnd(); p.WaitForExit(30000);
            var inst = new HashSet<string>(StringComparer.OrdinalIgnoreCase); var fam = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var line in text.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            { var parts = line.Split('|'); if (parts.Length == 2) { inst.Add(parts[0]); fam[parts[0]] = parts[1]; } }
            Installed = inst; Families = fam;
        }
        catch { }
    }
}
