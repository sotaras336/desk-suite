using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.Win32;

namespace DeskTune;

/// Every change DeskTune makes, with what was there before, in %LOCALAPPDATA%\DeskTune\changes.json: so any switch, or everything,
/// can be put back later, even after a restart or a new version. Only changes DeskTune made itself can be undone by it.
public static class Log
{
    public static string File = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DeskTune", "changes.json");
    private static JsonArray? _entries;
    private static JsonArray Entries => _entries ??= Load();

    private static JsonArray Load()
    {
        try { if (System.IO.File.Exists(File)) return JsonNode.Parse(System.IO.File.ReadAllText(File)) as JsonArray ?? new JsonArray(); } catch { }
        return new JsonArray();
    }

    private static void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(File)!);
        string tmp = File + ".tmp"; System.IO.File.WriteAllText(tmp, Entries.ToJsonString(new JsonSerializerOptions { WriteIndented = true })); System.IO.File.Move(tmp, File, true);
    }

    /// True if DeskTune has changed this switch and not undone it yet.
    public static bool Touched(string id) => Entries.OfType<JsonObject>().Any(e => (string?)e["id"] == id && !(bool)(e["undone"] ?? false));
    public static int Count => Entries.OfType<JsonObject>().Count(e => !(bool)(e["undone"] ?? false));

    /// Applies the switch's changes that aren't in effect yet, recording each one before it happens.
    public static void Apply(Tweak t)
    {
        var steps = new JsonArray();
        var entry = new JsonObject { ["id"] = t.Id, ["title"] = t.Title, ["when"] = DateTime.Now.ToString("s"), ["steps"] = steps };
        try
        {
            foreach (var op in t.Ops.Where(o => !o.IsApplied)) { var before = op.Apply(); steps.Add(new JsonObject { ["op"] = Ref(op), ["before"] = before }); }
        }
        finally { if (steps.Count > 0) { Entries.Add(entry); Save(); } }   // whatever got done is recorded, even if a later step failed
    }

    /// Puts back the most recent changes made for this switch.
    public static void Undo(string id)
    {
        foreach (var e in Entries.OfType<JsonObject>().Where(e => (string?)e["id"] == id && !(bool)(e["undone"] ?? false)).Reverse().ToList()) UndoEntry(e);
        Save();
    }

    /// Everything DeskTune ever changed, newest first.
    public static void UndoAll()
    {
        foreach (var e in Entries.OfType<JsonObject>().Where(e => !(bool)(e["undone"] ?? false)).Reverse().ToList()) UndoEntry(e);
        Save();
    }

    private static void UndoEntry(JsonObject e)
    {
        foreach (var s in e["steps"]!.AsArray().OfType<JsonObject>().Reverse()) FromRef(s["op"]!.AsObject()).Restore(s["before"]!);
        e["undone"] = true; e["undoneWhen"] = DateTime.Now.ToString("s");
    }

    // ops are stored by what they touch, not by their place in the list, so undo still works if the list changes between versions
    private static JsonObject Ref(Op op) => op switch
    {
        RegOp r => new() { ["type"] = "reg", ["hive"] = r.Hive.ToString(), ["key"] = r.Key, ["name"] = r.Name },
        ServiceOp s => new() { ["type"] = "svc", ["name"] = s.Name },
        StartupOp s => new() { ["type"] = "startup", ["hive"] = s.Hive.ToString(), ["key"] = s.ApprovedKey, ["name"] = s.Name },
        AppStartupOp a => new() { ["type"] = "appstartup", ["key"] = a.Key },
        AppxOp a => new() { ["type"] = "appx", ["package"] = a.Package },
        _ => throw new NotSupportedException(op.GetType().Name),
    };

    private static Op FromRef(JsonObject r) => (string)r["type"]! switch
    {
        "reg" => new RegOp(Enum.Parse<RegistryHive>((string)r["hive"]!), (string)r["key"]!, (string)r["name"]!, 0),
        "svc" => new ServiceOp((string)r["name"]!),
        "startup" => new StartupOp(Enum.Parse<RegistryHive>((string)r["hive"]!), (string)r["key"]!, (string)r["name"]!),
        "appstartup" => new AppStartupOp((string)r["key"]!),
        "appx" => new AppxOp((string)r["package"]!),
        var t => throw new NotSupportedException(t),
    };
}
