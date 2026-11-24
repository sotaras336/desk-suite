using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text.Json;

namespace DeskTune;

/// Self-update with no service and no framework. Once a day (and when asked) the app reads SHA256SUMS.txt from the newest release of the Desk suite on GitHub.
/// If the checksum listed for this app's setup file differs from the checksum of the running exe, a different build is out. "Update now" downloads that setup file,
/// checks it against the listed checksum and runs it: the setup already knows how to replace a running copy and start the new one.
/// Only a plain download request leaves the PC: no id, no version, no PC details. Switch off in the app's settings.
public static class Updater
{
    public const string Product = "DeskTune", Asset = "DeskTune-Setup.exe";
    const string Latest = "https://github.com/sotaras336/desk-suite/releases/latest/download/";

    /// Tests point this at a local server (DESK_UPDATE_BASE=http://127.0.0.1:port/); anything but loopback is ignored.
    static string Base { get { var b = Environment.GetEnvironmentVariable("DESK_UPDATE_BASE"); return b != null && (b.StartsWith("http://127.0.0.1") || b.StartsWith("http://localhost")) ? b : Latest; } }
    static bool Testing => Base != Latest;

    /// Where the on/off choice and the last check live.
    public static string StateDir = Testing && Environment.GetEnvironmentVariable("DESK_UPDATE_STATE") is { Length: > 0 } t ? t : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), Product);

    sealed class State { public bool Check { get; set; } = true; public DateTime Last { get; set; } public string? Known { get; set; } public string? Built { get; set; } }   // Built: when the installed copy was built (DeskBar's Desk apps page reads it)
    static string StateFile => Path.Combine(StateDir, "update.json");
    static State Load() { try { return JsonSerializer.Deserialize<State>(File.ReadAllText(StateFile)) ?? new(); } catch { return new(); } }
    static void Save(State s) { try { Directory.CreateDirectory(StateDir); File.WriteAllText(StateFile, JsonSerializer.Serialize(s)); } catch { } }

    public static bool Enabled { get => Load().Check; set { var s = Load(); s.Check = value; Save(s); Changed?.Invoke(); } }

    /// True when the newest release holds a different build of this app than the one running.
    public static bool Newer { get; private set; }
    public static string Status { get; private set; } = "";
    public static bool Busy { get; private set; }
    /// Raised from any thread when Newer, Status or Enabled changed.
    public static event Action? Changed;

    static string? _self;
    static string SelfHash() => _self ??= Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Environment.ProcessPath!))).ToLowerInvariant();

    static readonly HttpClient Http = MakeClient();
    static HttpClient MakeClient() { var c = new HttpClient { Timeout = TimeSpan.FromSeconds(60) }; c.DefaultRequestHeaders.UserAgent.ParseAdd("Desk-updater"); return c; }

    /// When this exe was built, "yyyyMMddTHHmmssZ" (UTC), from the DeskBuilt metadata the project file stamps in; null for a build without it.
    public static readonly string? SelfBuilt = typeof(Updater).Assembly.GetCustomAttributes(typeof(System.Reflection.AssemblyMetadataAttribute), false)
        .OfType<System.Reflection.AssemblyMetadataAttribute>().FirstOrDefault(a => a.Key == "DeskBuilt")?.Value is { Length: 16 } b ? b : null;

    /// When the release list says a file was built ("# built name yyyyMMddTHHmmssZ"), or null. Older release lists have no such line.
    public static string? BuiltFor(string sums, string file)
    {
        foreach (var raw in sums.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            var line = raw.Trim(); if (!line.StartsWith("# built ", StringComparison.OrdinalIgnoreCase)) continue;
            var rest = line[8..].Trim(); int sp = rest.LastIndexOf(' '); if (sp < 0) continue;
            var stamp = rest[(sp + 1)..];
            if (stamp.Length == 16 && stamp[8] == 'T' && stamp[15] == 'Z' && rest[..sp].Trim().Equals(file, StringComparison.OrdinalIgnoreCase)) return stamp;
        }
        return null;
    }

    /// "Different" is not "newer": a build made after the release (a fix not published yet) was offered the older release, and Update now put the old build back.
    /// A different file only counts as newer when the release says it was built later than this exe. Without the dates, different means newer, as before.
    static bool IsNewer(string sums, string? want, out bool ahead)
    {
        ahead = false;
        if (want == null || want == SelfHash()) return false;
        var built = BuiltFor(sums, Asset);
        if (built != null && SelfBuilt != null && string.CompareOrdinal(built, SelfBuilt) <= 0) { ahead = true; return false; }
        return true;
    }

    /// The checksum SHA256SUMS.txt lists for a file ("hash *name" per line), or null.
    public static string? HashFor(string sums, string file)
    {
        foreach (var line in sums.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            var p = line.Trim().Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
            if (p.Length == 2 && p[0].Length == 64 && p[1].TrimStart('*').Trim().Equals(file, StringComparison.OrdinalIgnoreCase)) return p[0].ToLowerInvariant();
        }
        return null;
    }

    /// Reads the release list and sets Newer. Never throws; offline just says so.
    public static async Task<bool> CheckAsync()   // true: the release list was read
    {
        if (Busy) return false;
        Busy = true; Status = "Checking…"; Changed?.Invoke();
        try
        {
            var sums = await Http.GetStringAsync(Base + "SHA256SUMS.txt"); var want = HashFor(sums, Asset);
            var s = Load(); s.Last = DateTime.UtcNow; s.Known = want; Save(s);
            Newer = IsNewer(sums, want, out bool ahead);
            Status = want == null ? "Couldn't read the release list." : Newer ? "A new version is available." : ahead ? "You have a newer build than the latest release." : "You have the latest version.";
            return want != null;
        }
        catch { Status = "Couldn't reach GitHub. Try again later."; return false; }
        finally { Busy = false; Changed?.Invoke(); }
    }

    /// Start once the app is up: remembers what the last check found, then checks quietly about once a day (every 6 hours it looks at the clock; resident apps stay up for days).
    public static void Start()
    {
        // The last check saw a different build than this one: a newer release, or this IS the newer build (installed since that check). Only a fresh look can tell,
        // so look again soon instead of saying "a new version is available" (it said so wrongly right after every update).
        bool recheck = Load().Known is { } known && known != SelfHash();
        try { NoteBuilt(); } catch { }
        _ = Task.Run(async () =>
        {
            await Task.Delay(Testing ? TimeSpan.FromMilliseconds(500) : TimeSpan.FromSeconds(20));
            while (true)
            {
                var s = Load();
                if (s.Check && (Testing || recheck || DateTime.UtcNow - s.Last >= TimeSpan.FromHours(20))) recheck = !await CheckAsync();   // a failed look is retried soon
                if (Testing) return;
                await Task.Delay(TimeSpan.FromMinutes(10));
            }
        });
    }

    /// Downloads the newest setup file, checks it against the listed checksum and starts it. True = the setup is running (the app may need to end so it can be replaced).
    /// A file that doesn't match is deleted and never run.
    public static async Task<bool> UpdateNowAsync()
    {
        if (Busy) return false;
        Busy = true; Status = "Checking…"; Changed?.Invoke();
        try
        {
            // look again first: the checksum kept from an earlier check can belong to an older release than the one "latest" serves now
            var sums = await Http.GetStringAsync(Base + "SHA256SUMS.txt"); var want = HashFor(sums, Asset);
            var st = Load(); st.Last = DateTime.UtcNow; st.Known = want; Save(st);
            Newer = IsNewer(sums, want, out bool ahead);
            if (!Newer) { Status = want == null ? "Couldn't read the release list." : ahead ? "You have a newer build than the latest release." : "You already have the latest version."; return false; }
            Status = "Downloading…"; Changed?.Invoke();
            var data = await Download(Base + Asset);
            if (Convert.ToHexString(SHA256.HashData(data)).ToLowerInvariant() != want) { Status = "The download didn't match its checksum, so it was not run. Try again later."; return false; }
            var dir = Path.Combine(Path.GetTempPath(), "DeskUpdate-" + Product); Directory.CreateDirectory(dir);
            var path = Path.Combine(dir, Asset);   // "setup" in the name makes the exe install itself
            File.WriteAllBytes(path, data);
            LastDownload = path;
            if (!Testing) Process.Start(new ProcessStartInfo(path) { UseShellExecute = false });
            Status = "Updating…"; return true;
        }
        catch { Status = "The download failed. Try again later."; return false; }
        finally { Busy = false; Changed?.Invoke(); }
    }
    public static string? LastDownload { get; private set; }

    /// The setup file, capped at 400 MB so a wrong answer from the network can't fill the memory (the same cap DeskBar uses for these files).
    static async Task<byte[]> Download(string url)
    {
        const long Cap = 400L << 20;
        using var r = await Http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead); r.EnsureSuccessStatusCode();
        if (r.Content.Headers.ContentLength > Cap) throw new IOException("too big");
        await using var src = await r.Content.ReadAsStreamAsync(); using var ms = new MemoryStream(); var buf = new byte[81920]; int n;
        while ((n = await src.ReadAsync(buf)) > 0) { ms.Write(buf, 0, n); if (ms.Length > Cap) throw new IOException("too big"); }
        return ms.ToArray();
    }

    /// The installed copy writes when it was built into update.json, so DeskBar's Desk apps page can tell "newer than the release" from "an update is out".
    static void NoteBuilt()
    {
        var installed = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", Product, Product + ".exe");
        if (Testing || SelfBuilt == null || !string.Equals(Environment.ProcessPath, installed, StringComparison.OrdinalIgnoreCase)) return;
        var s = Load(); if (s.Built != SelfBuilt) { s.Built = SelfBuilt; Save(s); }
    }
}
