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

    sealed class State { public bool Check { get; set; } = true; public DateTime Last { get; set; } public string? Known { get; set; } }
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
            var want = HashFor(await Http.GetStringAsync(Base + "SHA256SUMS.txt"), Asset);
            var s = Load(); s.Last = DateTime.UtcNow; s.Known = want; Save(s);
            Newer = want != null && want != SelfHash();
            Status = want == null ? "Couldn't read the release list." : Newer ? "A new version is available." : "You have the latest version.";
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
            var want = HashFor(await Http.GetStringAsync(Base + "SHA256SUMS.txt"), Asset);
            var st = Load(); st.Last = DateTime.UtcNow; st.Known = want; Save(st);
            Newer = want != null && want != SelfHash();
            if (!Newer) { Status = want == null ? "Couldn't read the release list." : "You already have the latest version."; return false; }
            Status = "Downloading…"; Changed?.Invoke();
            var data = await Http.GetByteArrayAsync(Base + Asset);
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
}
