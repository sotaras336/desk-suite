using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;

namespace DeskMode;

/// Install / uninstall without an installer, like the other Desk apps: the downloaded "DeskMode-Setup.exe" (any name containing "setup") copies itself to
/// %LOCALAPPDATA%\Programs\DeskMode, adds a Start menu entry plus "Uninstall DeskMode", starts with Windows, and opens. No administrator rights needed.
public static class Setup
{
    public const string Name = "DeskMode";
    public static string Dir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", Name);
    public static string Exe => Path.Combine(Dir, Name + ".exe");
    private static string StartMenu => Environment.GetFolderPath(Environment.SpecialFolder.Programs);
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";

    public static bool IsSetupName() => Path.GetFileName(Environment.ProcessPath ?? "").Contains("setup", StringComparison.OrdinalIgnoreCase);

    private static void AskRunningCopyToLeave()
    {
        try { if (EventWaitHandle.TryOpenExisting("DeskMode.Quit", out var q)) { q.Set(); q.Dispose(); } } catch { }
    }

    public static int Install()
    {
        try
        {
            Directory.CreateDirectory(Dir);
            AskRunningCopyToLeave();   // a copy that is running holds the exe open
            if (!string.Equals(Environment.ProcessPath, Exe, StringComparison.OrdinalIgnoreCase))
                for (int i = 0; ; i++) { try { File.Copy(Environment.ProcessPath!, Exe, true); break; } catch (IOException) when (i < 30) { Thread.Sleep(200); } }
            Link(Path.Combine(StartMenu, Name + ".lnk"), Exe, "", "DeskMode: refresh rate, power mode and brightness for your screens");
            Link(Path.Combine(StartMenu, "Uninstall " + Name + ".lnk"), Exe, "--uninstall", "Remove DeskMode");
            using (var k = Microsoft.Win32.Registry.CurrentUser.CreateSubKey(RunKey)) k.SetValue(Name, $"\"{Exe}\" --background");   // lives in the tray from sign-in on (switch off in Settings)
            Process.Start(new ProcessStartInfo(Exe) { UseShellExecute = false });
            return 0;
        }
        catch (Exception ex) { Message("DeskMode couldn't install:\n" + ex.Message); return 1; }
    }

    /// Removes the shortcuts, the start-with-Windows entry and the program folder (once this process has exited). Your settings stay, so a reinstall picks up where you left off.
    public static int Uninstall()
    {
        if (MessageBoxW(IntPtr.Zero, "Remove DeskMode? Your settings are kept.", "DeskMode", 0x24 /*YESNO | QUESTION*/) != 6) return 1;
        AskRunningCopyToLeave();
        try { using var k = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(RunKey, true); k?.DeleteValue(Name, false); } catch { }
        foreach (var l in new[] { Path.Combine(StartMenu, Name + ".lnk"), Path.Combine(StartMenu, "Uninstall " + Name + ".lnk") })
            try { File.Delete(l); } catch { }
        Process.Start(new ProcessStartInfo("cmd.exe", $"/c ping 127.0.0.1 -n 4 > nul & rmdir /s /q \"{Dir}\"") { CreateNoWindow = true, UseShellExecute = false });
        return 0;
    }

    private static void Link(string lnk, string target, string args, string description)
    {
        dynamic sh = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell")!)!;
        dynamic l = sh.CreateShortcut(lnk); l.TargetPath = target; l.Arguments = args; l.WorkingDirectory = Path.GetDirectoryName(target); l.Description = description; l.IconLocation = target + ",0"; l.Save();
    }

    private static void Message(string text) => MessageBoxW(IntPtr.Zero, text, "DeskMode", 0x10);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int MessageBoxW(IntPtr h, string text, string caption, uint type);
}
