using System.IO;
using System.Text.Json;

namespace DeskDrawer;

/// What DeskDrawer remembers between runs: the open tabs and the window size. %LOCALAPPDATA%\DeskDrawer\settings.json
public sealed class Settings
{
    public List<string> Tabs { get; set; } = new();
    public int ActiveTab { get; set; }
    public double Width { get; set; } = 1320;
    public double Height { get; set; } = 760;
    public bool Maximized { get; set; }
    public string Theme { get; set; } = "black";
    public bool ShowHidden { get; set; }
    public bool KeepReady { get; set; } = true;   // closing hides the window and the process stays, started at sign-in: folders open in ~0.5 s instead of ~2.5 s
    public bool Gpu { get; set; }   // graphics-card rendering: smoother on huge screens, but the driver adds ~85 MB; off = software rendering
    public string ViewMode { get; set; } = "Details";   // Details, List, Tiles, Icons
    public int IconSize { get; set; } = 96;
    public string GroupBy { get; set; } = "";   // "", Name, Type, Date modified, Size
    public double Zoom { get; set; } = 1.0;
    public bool ShortcutsMade { get; set; }
    public List<string> RecentPaths { get; set; } = new();   // the folders you were in, newest first: the address box suggests them (like File Explorer)
    /// Each folder's own view (sort, grouping, layout), like File Explorer remembers them. Keys are lower-case paths.
    public Dictionary<string, FolderView> Folders { get; set; } = new();
    public bool PreviewOpen { get; set; } = true;
    public double PreviewWidth { get; set; } = 320;
    public string SortKey { get; set; } = "Name";
    public bool SortAscending { get; set; } = true;

    private static readonly string FilePath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DeskDrawer", "settings.json");

    private static Settings? _loaded;

    /// Read once per process: App and the window share it.
    public static Settings Load() => _loaded ??= Read();

    private static Settings Read()
    {
        // DeskDrawer used to be Dexplorer: the first time, bring its settings (tabs, views, theme) over and make the shortcuts under the new name
        string old = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Dexplorer", "settings.json");
        if (!File.Exists(FilePath) && File.Exists(old))
            try { var s = JsonSerializer.Deserialize(File.ReadAllText(old), SettingsJson.Default.Settings) ?? new(); s.ShortcutsMade = false; s.Save(); return s; } catch { }
        try { return JsonSerializer.Deserialize(File.ReadAllText(FilePath), SettingsJson.Default.Settings) ?? new(); } catch { return new(); }
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            var tmp = FilePath + ".tmp"; File.WriteAllText(tmp, JsonSerializer.Serialize(this, SettingsJson.Default.Settings)); File.Move(tmp, FilePath, true);
        }
        catch { }
    }
}

/// Reader and writer generated at build time: the reflection-based default cost ~0.1 s of compiling at every launch.
[System.Text.Json.Serialization.JsonSourceGenerationOptions(WriteIndented = true)]
[System.Text.Json.Serialization.JsonSerializable(typeof(Settings))]
internal partial class SettingsJson : System.Text.Json.Serialization.JsonSerializerContext { }

public sealed class FolderView
{
    public string Sort { get; set; } = "Name";
    public bool Asc { get; set; } = true;
    public string Group { get; set; } = "";
    public string Mode { get; set; } = "Details";
    public int Icon { get; set; } = 96;
}
