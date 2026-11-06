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
            System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(InstalledExe)!);
            if (!string.Equals(Environment.ProcessPath, InstalledExe, StringComparison.OrdinalIgnoreCase))
                for (int i = 0; ; i++) { try { System.IO.File.Copy(Environment.ProcessPath!, InstalledExe, true); break; } catch (System.IO.IOException) when (i < 30) { System.Threading.Thread.Sleep(200); } }   // an open DeskTune that is updating itself is still closing
            foreach (var dir in new[] { Programs, Desktop }) Link(System.IO.Path.Combine(dir, "DeskTune.lnk"), "", "DeskTune: make Windows lighter");
            Link(System.IO.Path.Combine(Programs, "Uninstall DeskTune.lnk"), "--uninstall", "Remove DeskTune");
            return true;
        }
        catch (Exception ex) { MessageBox.Show("DeskTune couldn't install: " + ex.Message, "DeskTune"); return false; }
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
        System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("cmd.exe", $"/c ping 127.0.0.1 -n 3 > nul & rmdir /s /q \"{System.IO.Path.GetDirectoryName(InstalledExe)}\"") { CreateNoWindow = true, UseShellExecute = false });
        return 0;
    }

    /// --size 1280x670: open at exactly this size (to photograph how it looks on a small screen).
    public static Size? ForcedSize;

    /// DeskTune.exe  [--install]  [--selftest report.txt]  [--snapshot out.png]  [--makeicon file.ico]  [--size WxH]
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
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
            if (ok && setupName) System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(InstalledExe) { UseShellExecute = false });
            Shutdown(ok ? 0 : 1); return;
        }
        Theme.Load("black"); Theme.Apply();
        var w = new MainWindow(); w.Show();
        if (!a.Contains("--snapshot") || Environment.GetEnvironmentVariable("DESK_UPDATE_BASE") != null) Updater.Start();   // a snapshot run never asks GitHub
        for (int i = 0; i + 1 < a.Length; i++) if (a[i] == "--snapshot") SelfTest.SnapshotLater(w, a[i + 1]);
    }
}
