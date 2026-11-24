using System.Windows;

namespace DeskTune;

public partial class App : Application
{
    /// Where DeskBar looks for DeskTune (its Quick Settings tile appears when this file exists).
    public static string InstalledExe => System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "DeskTune", "DeskTune.exe");

    /// --install: copies this exe to %LOCALAPPDATA%\Programs\DeskTune and adds Start menu and Desktop shortcuts.
    private static bool Install()
    {
        try
        {
            var dir = System.IO.Path.GetDirectoryName(InstalledExe)!; System.IO.Directory.CreateDirectory(dir);
            CloseInstalledCopies();
            try { System.IO.File.Delete(System.IO.Path.Combine(dir, UninstallMark)); } catch { }   // an uninstall a moment ago: its delayed folder removal must not take this copy
            if (!string.Equals(Environment.ProcessPath, InstalledExe, StringComparison.OrdinalIgnoreCase)) CopyIn(Environment.ProcessPath!, dir);
            if (!System.IO.File.Exists(InstalledExe)) { MessageBox.Show("DeskTune couldn't install: the program file didn't arrive in " + dir, "DeskTune"); return false; }
            foreach (var place in new[] { Programs, Desktop }) Link(System.IO.Path.Combine(place, "DeskTune.lnk"), "", "DeskTune: make Windows lighter");
            Link(System.IO.Path.Combine(Programs, "Uninstall DeskTune.lnk"), "--uninstall", "Remove DeskTune");
            return true;
        }
        catch (Exception ex) { MessageBox.Show("DeskTune couldn't install: " + ex.Message, "DeskTune"); return false; }
    }

    /// An open DeskTune holds its exe, and nothing told it to close: an update from Desk settings > Desk apps (or Desk-Setup.exe) while DeskTune was open
    /// waited 6 s for the file and then said "couldn't install". Its own "Update now" closes itself, the others don't. So the installer asks the installed
    /// copy to close (as its window's X would) and only ends it if it doesn't within 5 s. A copy run from anywhere else (a dev build) is left alone.
    internal static int CloseInstalledCopies()
    {
        int n = 0;
        foreach (var p in System.Diagnostics.Process.GetProcessesByName("DeskTune"))
            try
            {
                if (p.Id == Environment.ProcessId) continue;
                string? path = null; try { path = p.MainModule?.FileName; } catch { }   // an elevated copy can't be read from here: its path is unknown, leave it
                if (!string.Equals(path, InstalledExe, StringComparison.OrdinalIgnoreCase)) continue;
                n++; if (p.CloseMainWindow() && p.WaitForExit(5000)) continue;
                p.Kill(); p.WaitForExit(3000);
            }
            catch { }
        return n;
    }

    const string UninstallMark = "uninstalling";

    /// Each file is copied beside its place, then swapped in: a failed copy leaves the old file, never an empty folder. A "dotnet build" copy (exe + dll +
    /// two .json files) installs whole, a published single exe on its own (DeskDrawer's Setup.CopyIn does the same).
    internal static List<string> CopyIn(string fromExe, string toDir)
    {
        string src = System.IO.Path.GetDirectoryName(fromExe)!, me = System.IO.Path.GetFileNameWithoutExtension(fromExe);
        var pairs = new List<(string from, string to)> { (fromExe, System.IO.Path.Combine(toDir, "DeskTune.exe")) };
        foreach (var ext in new[] { ".dll", ".deps.json", ".runtimeconfig.json" })
        {
            var f = System.IO.Path.Combine(src, me + ext); var t = System.IO.Path.Combine(toDir, "DeskTune" + ext);
            if (System.IO.File.Exists(f)) pairs.Add((f, t)); else if (System.IO.File.Exists(t)) try { System.IO.File.Delete(t); } catch { }
        }
        var copied = new List<string>();
        foreach (var (from, to) in pairs)
            for (int i = 0; ; i++)
            {
                try { System.IO.File.Copy(from, to + ".new", true); System.IO.File.Move(to + ".new", to, true); copied.Add(to); break; }
                catch (Exception) when (i < 30) { System.Threading.Thread.Sleep(200); }   // an open DeskTune that is updating itself is still closing
            }
        return copied;
    }

    private static string Programs => Environment.GetFolderPath(Environment.SpecialFolder.Programs);
    private static string Desktop => Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);

    private static void Link(string lnk, string args, string description)
    {
        dynamic sh = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell")!)!;
        dynamic l = sh.CreateShortcut(lnk); l.TargetPath = InstalledExe; l.Arguments = args; l.WorkingDirectory = System.IO.Path.GetDirectoryName(InstalledExe); l.Description = description; l.IconLocation = InstalledExe + ",0"; l.Save();
    }

    /// --uninstall: removes the program and its shortcuts. The changes you made stay as they are (they're Windows settings, not part of DeskTune);
    /// "Undo everything" first if you want Windows back exactly as before. The change log stays too, so a reinstall can still undo them.
    private static int Uninstall()
    {
        var answer = MessageBox.Show("Remove DeskTune?\n\nThe changes you applied stay. To put Windows back as it was, open DeskTune and use \"Undo all changes\" first.", "DeskTune", MessageBoxButton.YesNo, MessageBoxImage.Question);
        if (answer != MessageBoxResult.Yes) return 1;
        foreach (var l in new[] { System.IO.Path.Combine(Programs, "DeskTune.lnk"), System.IO.Path.Combine(Desktop, "DeskTune.lnk"), System.IO.Path.Combine(Programs, "Uninstall DeskTune.lnk") })
            try { System.IO.File.Delete(l); } catch { }
        var dir = System.IO.Path.GetDirectoryName(InstalledExe)!; var mark = System.IO.Path.Combine(dir, UninstallMark);
        try { System.IO.File.WriteAllText(mark, ""); } catch { }   // an install in the next 3 s deletes this, and the folder stays
        System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("cmd.exe", $"/c ping 127.0.0.1 -n 3 > nul & if exist \"{mark}\" rmdir /s /q \"{dir}\"") { CreateNoWindow = true, UseShellExecute = false });
        return 0;
    }

    /// --size 1280x670: open at exactly this size (to photograph how it looks on a small screen).
    public static Size? ForcedSize;

    /// DeskTune.exe  [--install]  [--selftest report.txt]  [--snapshot out.png]  [--makeicon file.ico]  [--size WxH]
    /// One-off errors used to close the app without a trace. Now they are written to crash.log; an error in the window itself no longer closes it
    /// (after 20 in one run it is let through: something is really broken).
    private void CrashNet(string app)
    {
        string dir = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), app); int n = 0;
        void W(string kind, object? ex) { try { System.IO.Directory.CreateDirectory(dir); System.IO.File.AppendAllText(System.IO.Path.Combine(dir, "crash.log"), $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} {kind}\r\n{ex}\r\n\r\n"); } catch { } }
        DispatcherUnhandledException += (_, a) => { W("window", a.Exception); if (++n <= 20) a.Handled = true; };
        AppDomain.CurrentDomain.UnhandledException += (_, a) => W("fatal", a.ExceptionObject);
        System.Threading.Tasks.TaskScheduler.UnobservedTaskException += (_, a) => { W("task", a.Exception); a.SetObserved(); };
    }

    private static System.IO.FileSystemWatcher? _themeWatch;
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        A11y.Init();   // keyboard, screen readers, high contrast, text size (A11y.cs)
        System.Windows.Media.RenderOptions.ProcessRenderMode = System.Windows.Interop.RenderMode.SoftwareOnly;   // a tune-up tool shouldn't spend 85 MB on the GPU driver
        var a = e.Args;
        for (int i = 0; i + 1 < a.Length; i++)
        {
            if (a[i] == "--selftest") { Shutdown(SelfTest.Run(a[i + 1])); return; }
            if (a[i] == "--makeicon") { IconMaker.Make(a[i + 1]); Shutdown(0); return; }
            if (a[i] == "--size" && a[i + 1].Split('x') is [var sw, var sh] && double.TryParse(sw, out var fw) && double.TryParse(sh, out var fh)) ForcedSize = new Size(fw, fh);
        }
        if (a.Contains("--uninstall")) { Shutdown(Uninstall()); return; }
        bool setupName = System.IO.Path.GetFileName(Environment.ProcessPath ?? "").Contains("setup", StringComparison.OrdinalIgnoreCase);
        if (a.Contains("--install") || setupName)   // "DeskTune-Setup.exe" from the website: install, then start the installed copy
        {
            bool ok = Install();
            if (ok && setupName && !a.Contains("--nostart")) System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(InstalledExe) { UseShellExecute = false });
            Shutdown(ok ? 0 : 1); return;
        }
        string? forced = null; for (int i = 0; i + 1 < a.Length; i++) if (a[i] == "--theme") forced = a[i + 1];   // dev: --theme glass (pictures without touching the suite's theme)
        Theme.Load(forced ?? SharedTheme.Read() ?? "black"); Theme.Apply();   // the suite's one theme (DeskBar / DeskDrawer pick it); Black when there is none
        CrashNet("DeskTune");
        var w = new MainWindow(); w.Show();
        if (forced == null) try
        {   // a colour picked in another Desk app (or Desk settings > Colours) while DeskTune is open: wear it at once
            var fw = new System.IO.FileSystemWatcher(SharedTheme.Dir, "theme*.txt") { NotifyFilter = System.IO.NotifyFilters.LastWrite | System.IO.NotifyFilters.FileName, EnableRaisingEvents = true };
            void Changed() => w.Dispatcher.BeginInvoke(async () => { await Task.Delay(150); if (SharedTheme.Read() is { } id && id != Theme.FullId) { Theme.Load(id); Theme.Apply(); } });
            fw.Changed += (_, _) => Changed(); fw.Created += (_, _) => Changed(); fw.Deleted += (_, _) => Changed(); fw.Renamed += (_, _) => Changed(); _themeWatch = fw;   // Renamed: DeskBar writes theme.txt as a .tmp renamed over it (WriteAtomic), so that is the only event its colour changes raise
        } catch { }
        if (a.Contains("--colour")) w.Dispatcher.BeginInvoke(() => ColourWindow.Show(w), System.Windows.Threading.DispatcherPriority.ApplicationIdle);   // dev: a picture of the colour window
        if (a.Contains("--undoall")) w.Dispatcher.BeginInvoke(w.UndoAll, System.Windows.Threading.DispatcherPriority.ApplicationIdle);   // "Put everything back" in the Desk settings window
        if (!a.Contains("--snapshot") && !a.Contains("--settingsshot") || Environment.GetEnvironmentVariable("DESK_UPDATE_BASE") != null) Updater.Start();   // a snapshot run never asks GitHub
        for (int i = 0; i + 1 < a.Length; i++) if (a[i] == "--snapshot") SelfTest.SnapshotLater(w, a[i + 1]);
        for (int i = 0; i + 1 < a.Length; i++) if (a[i] == "--settingsshot") { w.OpenSettings(); SelfTest.SnapshotLater(w.Settings!, a[i + 1]); break; }   // dev: a picture of Settings (DT_SETPAGE picks the page)
    }
}
