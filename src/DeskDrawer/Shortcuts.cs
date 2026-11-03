using System.IO;

namespace DeskDrawer;

/// Start Menu and Desktop shortcuts to this exe (DeskBar's Start search finds a Start Menu shortcut by name). Made on first run, or with DeskDrawer.exe --shortcuts.
public static class Shortcuts
{
    public static void Make()
    {
        string? exe = Environment.ProcessPath; if (exe == null) return;
        foreach (var dir in new[] { Environment.GetFolderPath(Environment.SpecialFolder.Programs), Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory) })
        {
            try { File.Delete(Path.Combine(dir, "Dexplorer.lnk")); } catch { }   // the old name
            try
            {
                dynamic sh = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell")!)!;
                dynamic lnk = sh.CreateShortcut(Path.Combine(dir, "DeskDrawer.lnk"));
                lnk.TargetPath = exe; lnk.WorkingDirectory = Path.GetDirectoryName(exe); lnk.Description = "DeskDrawer, a file manager"; lnk.IconLocation = exe + ",0"; lnk.Save();
            }
            catch { }
        }
    }
}
