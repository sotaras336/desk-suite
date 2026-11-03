using System.Diagnostics;
using System.IO;
using Microsoft.Win32;
using static Microsoft.Win32.RegistryHive;

namespace DeskTune;

/// One switch in the list: a few Ops that together turn one thing off, what that costs you, and which processes it runs (to show live memory).
public sealed class Tweak
{
    public required string Id { get; init; }
    public required string Category { get; init; }
    public required string Title { get; init; }
    public required string Lose { get; init; }
    public required List<Op> Ops { get; init; }
    public string[] Processes { get; init; } = Array.Empty<string>();
    public string Preset { get; init; } = "";   // "safe", "recommended" or "" (only by hand)
    public Func<bool> Present { get; init; } = () => true;

    public bool Admin => Ops.Any(o => o.Admin);
    public bool IsApplied => Ops.All(o => o.IsApplied);
    public long MemoryMB(Dictionary<string, long> mb) => Processes.Sum(p => mb.TryGetValue(p.ToLowerInvariant(), out var v) ? v : 0);
}

public static class Catalogue
{
    const string CDM = @"Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager";
    const string ADV = @"Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced";
    const string Approved = @"Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved";

    static RegOp U(string key, string name, int v) => new(CurrentUser, key, name, v);
    static RegOp M(string key, string name, int v) => new(LocalMachine, key, name, v);

    public static List<Tweak> Build()
    {
        var list = new List<Tweak>();
        list.AddRange(Startup());
        list.AddRange(Extras());
        list.AddRange(Ads());
        list.AddRange(Services());
        list.AddRange(Apps());
        return list.Where(t => t.Ops.Count > 0 && t.Present()).ToList();
    }

    // ---------------------------------------------------------------- what starts with Windows

    public static IEnumerable<Tweak> Startup()
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        Tweak Make(string id, string name, string exe, Op op) => new()
        {
            Id = id, Category = "Starts with Windows", Title = $"Don't start {name} with Windows",
            Lose = $"{name} won't open by itself when you sign in. You can still open it yourself.",
            Ops = new() { op }, Processes = exe.Length > 0 ? new[] { Path.GetFileNameWithoutExtension(exe) } : Array.Empty<string>(),
        };
        foreach (var (hive, runKey, approved) in new[] {
            (CurrentUser, @"Software\Microsoft\Windows\CurrentVersion\Run", Approved + @"\Run"),
            (LocalMachine, @"Software\Microsoft\Windows\CurrentVersion\Run", Approved + @"\Run"),
            (LocalMachine, @"Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Run", Approved + @"\Run32") })
        {
            using var k = Op.Base(hive).OpenSubKey(runKey); if (k == null) continue;
            foreach (var name in k.GetValueNames())
            {
                if (name.Length == 0 || k.GetValue(name) is not string cmd) continue;
                string id = $"startup:{hive}:{runKey}:{name}"; if (!seen.Add(id)) continue;
                yield return Make(id, name, ExeOf(cmd), new StartupOp(hive, approved, name));
            }
        }
        foreach (var (hive, folder) in new[] { (CurrentUser, Environment.GetFolderPath(Environment.SpecialFolder.Startup)), (LocalMachine, Environment.GetFolderPath(Environment.SpecialFolder.CommonStartup)) })
        {
            if (!Directory.Exists(folder)) continue;
            foreach (var f in Directory.EnumerateFiles(folder))
            {
                string file = Path.GetFileName(f); if (file.Equals("desktop.ini", StringComparison.OrdinalIgnoreCase)) continue;
                yield return Make($"startup:{hive}:folder:{file}", Path.GetFileNameWithoutExtension(file), LinkTarget(f), new StartupOp(hive, Approved + @"\StartupFolder", file));
            }
        }
        // Store apps that start with Windows (Teams, Phone Link, Spotify from the Store...): their startup tasks
        const string apps = @"Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppModel\SystemAppData";
        using (var root = Op.Base(CurrentUser).OpenSubKey(apps))
            if (root != null)
                foreach (var pkg in root.GetSubKeyNames())
                {
                    using var pk = root.OpenSubKey(pkg); if (pk == null) continue;
                    foreach (var task in pk.GetSubKeyNames())
                    {
                        using var tk = pk.OpenSubKey(task); if (tk?.GetValue("State") is not int) continue;
                        string name = FriendlyPackage(pkg);
                        yield return new Tweak
                        {
                            Id = $"appstartup:{pkg}:{task}", Category = "Starts with Windows", Title = $"Don't start {name} with Windows",
                            Lose = $"{name} won't open by itself when you sign in. You can still open it yourself.", Ops = new() { new AppStartupOp($@"{apps}\{pkg}\{task}") },
                        };
                    }
                }
    }

    /// The program a Run command starts: the quoted path, or everything up to ".exe".
    public static string ExeOf(string cmd)
    {
        cmd = cmd.Trim();
        if (cmd.StartsWith('"')) { int e = cmd.IndexOf('"', 1); return e > 1 ? cmd[1..e] : ""; }
        int x = cmd.IndexOf(".exe", StringComparison.OrdinalIgnoreCase);
        return x > 0 ? cmd[..(x + 4)] : cmd.Split(' ')[0];
    }

    static string LinkTarget(string lnk)
    {
        if (!lnk.EndsWith(".lnk", StringComparison.OrdinalIgnoreCase)) return lnk;
        try { dynamic sh = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell")!)!; return (string)sh.CreateShortcut(lnk).TargetPath; } catch { return ""; }
    }

    /// "MicrosoftTeams_8wekyb3d8bbwe" -> "MicrosoftTeams", "SpotifyAB.SpotifyMusic_zpdnekdrzrea0" -> "SpotifyMusic".
    public static string FriendlyPackage(string family) { string n = family.Split('_')[0]; int d = n.LastIndexOf('.'); return d >= 0 && d < n.Length - 1 ? n[(d + 1)..] : n; }

    // ---------------------------------------------------------------- Windows extras that stay running

    static IEnumerable<Tweak> Extras()
    {
        yield return new Tweak
        {
            Id = "widgets", Category = "Windows extras", Title = "Turn off Widgets", Preset = "recommended",
            Lose = "The widgets board (news, weather, stocks) and its taskbar button.",
            Ops = new() { M(@"Software\Policies\Microsoft\Dsh", "AllowNewsAndInterests", 0), U(ADV, "TaskbarDa", 0) }, Processes = new[] { "Widgets", "WidgetService" },
        };
        yield return new Tweak
        {
            Id = "copilot", Category = "Windows extras", Title = "Turn off Copilot", Preset = "recommended",
            Lose = "The Copilot button and Win+C. The Copilot app can still be opened if installed.",
            Ops = new() { U(@"Software\Policies\Microsoft\Windows\WindowsCopilot", "TurnOffWindowsCopilot", 1), U(ADV, "ShowCopilotButton", 0) },
        };
        yield return new Tweak
        {
            Id = "gamedvr", Category = "Windows extras", Title = "Turn off Game Bar background recording", Preset = "recommended",
            Lose = "Win+Alt+G \"record what happened\" and automatic game clips.",
            Ops = new() { U(@"System\GameConfigStore", "GameDVR_Enabled", 0), U(@"Software\Microsoft\Windows\CurrentVersion\GameDVR", "AppCaptureEnabled", 0) }, Processes = new[] { "GameBar", "GameBarFTServer" },
        };
        yield return new Tweak
        {
            Id = "edge-bg", Category = "Windows extras", Title = "Stop Edge running after you close it", Preset = "recommended",
            Lose = "Edge opens a moment slower; its extensions stop when Edge is closed.",
            Ops = new() { M(@"Software\Policies\Microsoft\Edge", "StartupBoostEnabled", 0), M(@"Software\Policies\Microsoft\Edge", "BackgroundModeEnabled", 0) },
            Present = () => File.Exists(@"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe") || File.Exists(@"C:\Program Files\Microsoft\Edge\Application\msedge.exe"),
        };
        yield return new Tweak
        {
            Id = "chrome-bg", Category = "Windows extras", Title = "Stop Chrome running after you close it", Preset = "recommended",
            Lose = "Chrome apps and extensions stop when Chrome is closed.",
            Ops = new() { M(@"Software\Policies\Google\Chrome", "BackgroundModeEnabled", 0) },
            Present = () => File.Exists(@"C:\Program Files\Google\Chrome\Application\chrome.exe") || File.Exists(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), @"Google\Chrome\Application\chrome.exe")),
        };
        yield return new Tweak
        {
            Id = "phonelink", Category = "Windows extras", Title = "Turn off Phone Link",
            Lose = "Your phone's texts, calls, photos and notifications on the PC.",
            Ops = new() { M(@"Software\Policies\Microsoft\Windows\System", "EnableMmx", 0) }, Processes = new[] { "PhoneExperienceHost", "CrossDeviceService", "CrossDeviceResume" },
        };
        yield return new Tweak
        {
            Id = "bgapps", Category = "Windows extras", Title = "Stop Store apps running in the background",
            Lose = "Store apps (Mail, Phone Link, WhatsApp...) can't update or notify while closed; notifications may arrive late.",
            Ops = new() { M(@"Software\Policies\Microsoft\Windows\AppPrivacy", "LetAppsRunInBackground", 2) },
        };
    }

    // ---------------------------------------------------------------- ads, suggestions, nags

    static IEnumerable<Tweak> Ads()
    {
        Tweak A(string id, string title, string lose, params RegOp[] ops) => new() { Id = id, Category = "Ads and suggestions", Title = title, Lose = lose, Ops = ops.ToList<Op>(), Preset = "safe" };
        yield return A("ads-start", "Turn off suggestions in Start and Settings", "Start stops recommending apps and tips.",
            U(CDM, "SystemPaneSuggestionsEnabled", 0), U(CDM, "SubscribedContent-338388Enabled", 0), U(CDM, "SubscribedContent-338389Enabled", 0),
            U(CDM, "SubscribedContent-353694Enabled", 0), U(CDM, "SubscribedContent-353696Enabled", 0), U(ADV, "Start_IrisRecommendations", 0));
        yield return A("ads-lock", "Turn off tips and ads on the lock screen", "Fun facts and tips over the lock-screen picture.",
            U(CDM, "RotatingLockScreenOverlayEnabled", 0), U(CDM, "SubscribedContent-338387Enabled", 0));
        yield return A("ads-tips", "Turn off tips and \"finish setting up\" pop-ups", "Windows' welcome and tips screens after updates.",
            U(CDM, "SoftLandingEnabled", 0), U(CDM, "SubscribedContent-310093Enabled", 0), U(@"Software\Microsoft\Windows\CurrentVersion\UserProfileEngagement", "ScoobeSystemSettingEnabled", 0));
        yield return A("ads-install", "Stop Windows installing suggested apps by itself", "Nothing you use: only apps Windows would add on its own.",
            U(CDM, "SilentInstalledAppsEnabled", 0), U(CDM, "OemPreInstalledAppsEnabled", 0), U(CDM, "PreInstalledAppsEnabled", 0));
        yield return A("ads-id", "Turn off the advertising ID", "Apps can no longer show ads based on your activity.",
            U(@"Software\Microsoft\Windows\CurrentVersion\AdvertisingInfo", "Enabled", 0));
        yield return A("ads-explorer", "Turn off OneDrive and Microsoft 365 ads in File Explorer", "Nothing you use.", U(ADV, "ShowSyncProviderNotifications", 0));
        yield return A("ads-tailored", "Turn off tailored experiences", "Tips chosen from your diagnostic data.",
            U(@"Software\Microsoft\Windows\CurrentVersion\Privacy", "TailoredExperiencesWithDiagnosticDataEnabled", 0));
    }

    // ---------------------------------------------------------------- background services (administrator)

    static IEnumerable<Tweak> Services()
    {
        Tweak S(string id, string title, string lose, string preset, string[] procs, params string[] names)
        {
            var ops = names.Select(n => new ServiceOp(n)).Where(o => o.Exists).ToList<Op>();
            return new() { Id = id, Category = "Background services", Title = title, Lose = lose, Ops = ops, Preset = preset, Processes = procs };
        }
        yield return S("svc-telemetry", "Stop sending usage data to Microsoft", "Nothing you use. Microsoft gets less diagnostic data.", "recommended", Array.Empty<string>(), "DiagTrack", "dmwappushservice");
        yield return S("svc-wer", "Turn off Windows Error Reporting", "Crash reports are no longer sent to Microsoft.", "recommended", new[] { "WerFault" }, "WerSvc");
        yield return S("svc-fax", "Turn off Fax", "Faxing from this PC.", "recommended", Array.Empty<string>(), "Fax");
        yield return S("svc-demo", "Turn off Retail Demo", "Nothing: it's for PCs on display in shops.", "recommended", Array.Empty<string>(), "RetailDemo");
        yield return S("svc-maps", "Turn off offline maps updates", "Downloaded offline maps stop updating.", "recommended", Array.Empty<string>(), "MapsBroker");
        yield return S("svc-xbox", "Turn off Xbox services", "Xbox app sign-in, cloud game saves and Xbox Live in PC games.", "", Array.Empty<string>(), "XblAuthManager", "XblGameSave", "XboxNetApiSvc");
        yield return S("svc-location", "Turn off location", "Apps can't find where you are (Weather, Maps, Find my device).", "", Array.Empty<string>(), "lfsvc");
        if (!HasRealPrinter()) yield return S("svc-print", "Turn off printing (no printer is set up)", "Printing, until you add a printer and switch this back on.", "recommended", new[] { "spoolsv" }, "Spooler");
    }

    static bool HasRealPrinter()
    {
        using var k = Op.Base(LocalMachine).OpenSubKey(@"SYSTEM\CurrentControlSet\Control\Print\Printers");
        return k?.GetSubKeyNames().Any(n => !n.StartsWith("Microsoft Print to PDF") && !n.StartsWith("Microsoft XPS") && !n.StartsWith("OneNote") && !n.Equals("Fax", StringComparison.OrdinalIgnoreCase)) ?? false;
    }

    // ---------------------------------------------------------------- preinstalled apps (removed for this user; reinstall from the Store)

    static IEnumerable<Tweak> Apps()
    {
        foreach (var (pkg, name, preset) in new[] {
            ("Microsoft.BingNews", "Microsoft News", "recommended"), ("Microsoft.MicrosoftSolitaireCollection", "Solitaire", "recommended"), ("Microsoft.GetHelp", "Get Help", "recommended"),
            ("Microsoft.Getstarted", "Tips", "recommended"), ("Microsoft.WindowsFeedbackHub", "Feedback Hub", "recommended"), ("Microsoft.People", "People", "recommended"),
            ("Microsoft.PowerAutomateDesktop", "Power Automate", "recommended"), ("MicrosoftTeams", "Teams (personal chat)", "recommended"), ("Microsoft.549981C3F5F10", "Cortana", "recommended"),
            ("Microsoft.MixedReality.Portal", "Mixed Reality Portal", "recommended"), ("Microsoft.SkypeApp", "Skype", "recommended"),
            ("king.com.CandyCrushSaga", "Candy Crush Saga", "recommended"), ("king.com.CandyCrushSodaSaga", "Candy Crush Soda Saga", "recommended"),
            ("Clipchamp.Clipchamp", "Clipchamp", ""), ("Microsoft.BingWeather", "Weather", ""), ("Microsoft.MicrosoftOfficeHub", "Microsoft 365 (Office) hub", ""),
            ("Microsoft.WindowsMaps", "Maps", ""), ("Microsoft.Windows.DevHome", "Dev Home", ""), ("Microsoft.OutlookForWindows", "Outlook (new)", ""), ("Microsoft.Todos", "To Do", "") })
        {
            var op = new AppxOp(pkg);
            yield return new Tweak
            {
                Id = "app:" + pkg, Category = "Preinstalled apps", Title = $"Remove {name}", Preset = preset,
                Lose = $"The {name} app, for your account only. Reinstall it from the Microsoft Store any time.", Ops = new() { op },
                Present = () => op.Present || Log.Touched("app:" + pkg),
            };
        }
    }

    /// Private memory (MB) per process name right now, for the "uses N MB" badges.
    public static Dictionary<string, long> MeasureMemory()
    {
        var mb = new Dictionary<string, long>();
        foreach (var p in Process.GetProcesses())
        {
            try { string n = p.ProcessName.ToLowerInvariant(); mb[n] = mb.GetValueOrDefault(n) + p.PrivateMemorySize64 / (1024 * 1024); } catch { }
            finally { p.Dispose(); }
        }
        return mb;
    }
}
