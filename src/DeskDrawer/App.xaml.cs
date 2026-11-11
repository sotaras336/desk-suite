using System.Windows;

namespace DeskDrawer;

public partial class App : Application
{
    /// DeskDrawer.exe [folder ...]  [--theme id]  [--snapshot out.png]  [--selftest report.txt]
    public static string? ForcedTheme, SnapshotFile, UiTest;   // --uitest report.txt: drive the real window (MainWindow.Test.cs)
    public static string? ForcedView;   // --view Details|List|Tiles|Icons, for snapshots
    public static string? SelectName;   // --select name: with --snapshot, select that item first (to photograph the preview pane)
    public static bool ShowMenu, ShowMore, BgMenu;   // --bgmenu: photograph the menu of empty space   // --showmore: photograph the themed "more options" menu of the first file   // --showmenu: with --snapshot, also open the right-click menu on the first item and photograph it
    public static readonly List<string> StartPaths = new();
    /// Background: started at sign-in (--background), drawn once out of sight and hidden. Resident: this is the window that stays ready
    /// when closed (not a Ctrl+N window or a test run). Quitting: really exit (Windows shutting down, Setup replacing this copy).
    public static bool Background, Resident, Quitting;

    /// Windows passes a drive as "C:\" in quotes, which arrives as C:" (the backslash escapes the quote).
    internal static string CleanPath(string a) { a = a.Trim('"'); return a.Length == 2 && a[1] == ':' ? a + "\\" : a; }

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e); 
        string? selfTest = null; bool newWindow = false;
        for (int i = 0; i < e.Args.Length; i++)
        {
            if (e.Args[i] == "--theme" && i + 1 < e.Args.Length) ForcedTheme = e.Args[++i];
            else if (e.Args[i] == "--snapshot" && i + 1 < e.Args.Length) SnapshotFile = e.Args[++i];
            else if (e.Args[i] == "--selftest" && i + 1 < e.Args.Length) selfTest = e.Args[++i];
            else if (e.Args[i] == "--showmenu") ShowMenu = true;
            else if (e.Args[i] == "--showmore") ShowMore = true;
            else if (e.Args[i] == "--bgmenu") { ShowMenu = true; BgMenu = true; }
            else if (e.Args[i] == "--select" && i + 1 < e.Args.Length) SelectName = e.Args[++i];
            else if (e.Args[i] == "--view" && i + 1 < e.Args.Length) ForcedView = e.Args[++i];
            else if (e.Args[i] == "--new-window") newWindow = true;
            else if (e.Args[i] == "--uitest" && i + 1 < e.Args.Length) { UiTest = e.Args[++i]; newWindow = true; }
            else if (e.Args[i] == "--background") Background = true;
            else if (e.Args[i] == "--default" && i + 1 < e.Args.Length) { DefaultApp.Set(e.Args[++i] == "on", Environment.ProcessPath!); Shutdown(0); return; }
            else if (!e.Args[i].StartsWith("--")) StartPaths.Add(CleanPath(e.Args[i]));
        }
        if (selfTest != null) { Shutdown(SelfTest.Run(selfTest)); return; }
        for (int i = 0; i + 1 < e.Args.Length; i++) if (e.Args[i] == "--dialogtest") { Shutdown(SelfTest.DialogTest(e.Args[i + 1])); return; } else if (e.Args[i] == "--pickshot") { Theme.Load(Settings.Load().Theme); Theme.Apply(); Shutdown(SelfTest.PickShot(e.Args[i + 1])); return; } else if (e.Args[i] == "--settingsshot") { App.SnapshotFile = e.Args[i + 1]; Theme.Load(Settings.Load().Theme); Theme.Apply(); Shutdown(SelfTest.SettingsShot(e.Args[i + 1])); return; } else if (e.Args[i] == "--picktest") { Theme.Load(Settings.Load().Theme); Theme.Apply(); Shutdown(SelfTest.PickTest(e.Args[i + 1])); return; }
        for (int i = 0; i + 1 < e.Args.Length; i++) if (e.Args[i] == "--makeicon") { IconMaker.Make(e.Args[i + 1]); Shutdown(0); return; }
        if (e.Args.Contains("--shortcuts")) { Shortcuts.Make(); Shutdown(0); return; }
        var st = Settings.Load();
        if (Background && !st.KeepReady) { Shutdown(0); return; }
        if (!st.Gpu) System.Windows.Media.RenderOptions.ProcessRenderMode = System.Windows.Interop.RenderMode.SoftwareOnly;   // measured: 200 MB with the GPU, 115 MB without
        Theme.Load(ForcedTheme ?? st.Theme);
        Theme.Apply();
        if (SnapshotFile == null) try { DefaultApp.MigrateFromDexplorer(Environment.ProcessPath!); } catch { }
        if (!st.ShortcutsMade && SnapshotFile == null) { Shortcuts.Make(); st.ShortcutsMade = true; st.Save(); }   // first run: a Start Menu and a Desktop entry, so it is one click away
        bool test = SnapshotFile != null;
        if (!test && !newWindow && !Background && Instance.HandOff(StartPaths)) { Shutdown(0); return; }   // already open: the folder becomes a tab there
        Resident = !test && !newWindow;
        SessionEnding += (_, _) => Quitting = true;
        var w = new MainWindow(); w.Show();
        if (UiTest != null) { w.RunUiTest(UiTest); return; }
        if (!test) { if (Resident) Instance.Listen(w.OpenFromOutside);   /* only the resident copy takes folder hand-offs: a --new-window copy used to grab them when the resident was not running, then crashed once its window was closed */ DialogSwitch.TakeOver = st.TakeOverDialogs; DialogSwitch.Start(w.FoldersToOffer); }
        if (Resident) Setup.StartAtSignIn(st.KeepReady);   // points at whichever copy is running
        if (!test || Environment.GetEnvironmentVariable("DESK_UPDATE_BASE") != null) Updater.Start();   // a snapshot run never asks GitHub
    }
}
