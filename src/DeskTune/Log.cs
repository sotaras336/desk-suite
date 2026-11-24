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

    /// Guards the list: the changes run on a worker thread while the window reads Count and Touched.
    private static readonly object Gate = new();

    private static JsonArray Load()
    {
        if (!System.IO.File.Exists(File)) return new JsonArray();
        string? text = null;
        for (int i = 0; text == null && i < 5; i++) try { text = System.IO.File.ReadAllText(File); } catch (IOException) { Thread.Sleep(100); } catch { break; }   // a moment ago another DeskTune (the one that restarted as administrator) was still writing it
        try { if (text != null && JsonNode.Parse(text) is JsonArray a) return a; } catch { }
        // Unreadable (cut short by a crash, a full disk...): it used to be replaced by an empty list at the next change, and every undo was lost for good.
        // Kept aside instead, so the old changes can still be recovered by hand.
        try { System.IO.File.Copy(File, File + ".unreadable-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".json", true); } catch { }
        return new JsonArray();
    }

    private static void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(File)!);
        string tmp = File + ".tmp"; System.IO.File.WriteAllText(tmp, Entries.ToJsonString(new JsonSerializerOptions { WriteIndented = true })); System.IO.File.Move(tmp, File, true);
    }

    /// True if DeskTune has changed this switch and not undone it yet.
    public static bool Touched(string id) { lock (Gate) return Entries.OfType<JsonObject>().Any(e => (string?)e["id"] == id && !(bool)(e["undone"] ?? false)); }
    public static int Count { get { lock (Gate) return Entries.OfType<JsonObject>().Count(e => !(bool)(e["undone"] ?? false)); } }

    /// Applies the switch's changes that aren't in effect yet, recording each one before it happens.
    public static void Apply(Tweak t)
    {
        var steps = new JsonArray();
        var entry = new JsonObject { ["id"] = t.Id, ["title"] = t.Title, ["when"] = DateTime.Now.ToString("s"), ["steps"] = steps };
        try
        {
            foreach (var op in t.Ops.Where(o => !o.IsApplied)) { var before = op.Apply(); steps.Add(new JsonObject { ["op"] = Ref(op), ["before"] = before }); }
        }
        finally { if (steps.Count > 0) lock (Gate) { Entries.Add(entry); Save(); } }   // whatever got done is recorded, even if a later step failed
    }

    /// Puts back the most recent changes made for this switch. Throws, after trying all of them, if any couldn't be put back.
    public static void Undo(string id)
    {
        var failed = UndoEntries(e => (string?)e["id"] == id);
        if (failed.Count > 0) throw new InvalidOperationException(string.Join("; ", failed.Select(f => f.Why)));
    }

    /// Everything DeskTune ever changed, newest first. Returns what couldn't be put back ("title: reason"); the rest is put back anyway.
    public static List<string> UndoAll() => UndoEntries(_ => true).Select(f => $"{f.Title} ({f.Why})").ToList();

    /// One change that fails (a machine-wide one without administrator rights, a key Windows locked) used to stop the whole undo, and what had already been
    /// put back was never written down, so it showed as still changed. Now every entry gets its turn and the log is always saved.
    private static List<(string Title, string Why)> UndoEntries(Func<JsonObject, bool> which)
    {
        List<JsonObject> todo; lock (Gate) todo = Entries.OfType<JsonObject>().Where(e => which(e) && !(bool)(e["undone"] ?? false)).Reverse().ToList();
        var failed = new List<(string, string)>();
        try
        {
            foreach (var e in todo)
                try { UndoEntry(e); }
                catch (Exception ex) { failed.Add(((string?)e["title"] ?? (string?)e["id"] ?? "?", ex.Message)); }
        }
        finally { lock (Gate) Save(); }
        return failed;
    }

    private static void UndoEntry(JsonObject e)
    {
        List<JsonObject> steps; lock (Gate) steps = e["steps"]!.AsArray().OfType<JsonObject>().Reverse().ToList();
        foreach (var s in steps) FromRef(s["op"]!.AsObject()).Restore(s["before"]!);
        lock (Gate) { e["undone"] = true; e["undoneWhen"] = DateTime.Now.ToString("s"); }
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
