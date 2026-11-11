using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;

namespace DeskDrawer;

/// Install / uninstall without an installer, like DeskBar: the downloaded "DeskDrawer-Setup.exe" (any name containing "setup") copies itself to
/// %LOCALAPPDATA%\Programs\DeskDrawer, adds Start menu and Desktop shortcuts plus "Uninstall DeskDrawer", and starts. No administrator rights needed.
public static class Setup
{
    public const string Name = "DeskDrawer";
    public static string Dir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", Name);
    public static string Exe => Path.Combine(Dir, Name + ".exe");
    static string StartMenu => Environment.GetFolderPath(Environment.SpecialFolder.Programs);
    static string Desktop => Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);

    public static bool IsSetupName() => Path.GetFileName(Environment.ProcessPath ?? "").Contains("setup", StringComparison.OrdinalIgnoreCase);

    public static int Install()
    {
        try
        {
            Directory.CreateDirectory(Dir);
            Instance.HandOff(new[] { "--quit" });   // a copy kept ready in the background holds the exe open
            if (!string.Equals(Environment.ProcessPath, Exe, StringComparison.OrdinalIgnoreCase))
                for (int i = 0; ; i++) { try { File.Copy(Environment.ProcessPath!, Exe, true); break; } catch (IOException) when (i < 25) { Thread.Sleep(200); } }
            Link(Path.Combine(StartMenu, Name + ".lnk"), Exe, "", "DeskDrawer, a file manager");
            Link(Path.Combine(Desktop, Name + ".lnk"), Exe, "", "DeskDrawer, a file manager");
            Link(Path.Combine(StartMenu, "Uninstall " + Name + ".lnk"), Exe, "--uninstall", "Remove DeskDrawer");
            Process.Start(new ProcessStartInfo(Exe) { UseShellExecute = false });
            return 0;
        }
        catch (Exception ex) { Message("DeskDrawer couldn't install:\n" + ex.Message); return 1; }
    }

    /// Gives folders and Win+E back to File Explorer first, removes the shortcuts, then deletes the program folder once this process has exited.
    /// Your settings (%LOCALAPPDATA%\DeskDrawer) stay, so a reinstall picks up where you left off.
    public static int Uninstall()
    {
        if (MessageBoxW(IntPtr.Zero, "Remove DeskDrawer? Folders and Win+E will open in File Explorer again.", "DeskDrawer", 0x24 /*YESNO | QUESTION*/) != 6) return 1;
        try { DefaultApp.Set(false, Exe); } catch { }
        Instance.HandOff(new[] { "--quit" }); StartAtSignIn(false);
        foreach (var l in new[] { Path.Combine(StartMenu, Name + ".lnk"), Path.Combine(Desktop, Name + ".lnk"), Path.Combine(StartMenu, "Uninstall " + Name + ".lnk"), Path.Combine(Desktop, "Dexplorer.lnk"), Path.Combine(StartMenu, "Dexplorer.lnk") })
            try { File.Delete(l); } catch { }
        Process.Start(new ProcessStartInfo("cmd.exe", $"/c ping 127.0.0.1 -n 3 > nul & rmdir /s /q \"{Dir}\"") { CreateNoWindow = true, UseShellExecute = false });
        return 0;
    }

    /// "Keep ready": start hidden at sign-in (HKCU Run, so no administrator rights), the way File Explorer is always there.
    /// If it was switched off in Task Manager's Startup apps, that choice stays: Windows keeps it apart from this value.
    public static void StartAtSignIn(bool on)
    {
        try
        {
            using var k = Microsoft.Win32.Registry.CurrentUser.CreateSubKey(@"Software\Microsoft\Windows\CurrentVersion\Run");
            if (on) k.SetValue(Name, $"\"{Environment.ProcessPath}\" --background"); else k.DeleteValue(Name, false);
        }
        catch { }
        SignInTask(on);
    }

    /// The Run key alone is not enough: Windows starts Run-key programs late (ten minutes after sign-in was measured, and sometimes not at all), so the first folder of the day
    /// opened cold, in seconds instead of half a second. A sign-in task starts the hidden copy right away. Only the installed copy owns the task; it is made once, in the background.
    private static void SignInTask(bool on)
    {
        if (!string.Equals(Environment.ProcessPath, Exe, StringComparison.OrdinalIgnoreCase)) return;
        string script = on
            ? "$t = Get-ScheduledTask -TaskName 'DeskDrawer' -ErrorAction SilentlyContinue; if (-not $t -or $t.Actions[0].Execute -ne '" + Exe.Replace("'", "''") + "') { " +
              "$a = New-ScheduledTaskAction -Execute '" + Exe.Replace("'", "''") + "' -Argument '--background'; $g = New-ScheduledTaskTrigger -AtLogOn -User ([Security.Principal.WindowsIdentity]::GetCurrent().Name); " +
              "$s = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero); Register-ScheduledTask -TaskName 'DeskDrawer' -Action $a -Trigger $g -Settings $s -Force | Out-Null }"
            : "Unregister-ScheduledTask -TaskName 'DeskDrawer' -Confirm:$false -ErrorAction SilentlyContinue";
        new Thread(() =>
        {
            try { Thread.Sleep(on ? 20000 : 0); Process.Start(new ProcessStartInfo("powershell.exe", "-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"" + script + "\"") { UseShellExecute = false, CreateNoWindow = true })?.WaitForExit(30000); } catch { }
        }) { IsBackground = !on, Priority = ThreadPriority.BelowNormal }.Start();
    }

    static void Link(string lnk, string target, string args, string description)
    {
        dynamic sh = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell")!)!;
        dynamic l = sh.CreateShortcut(lnk); l.TargetPath = target; l.Arguments = args; l.WorkingDirectory = Path.GetDirectoryName(target); l.Description = description; l.IconLocation = target + ",0"; l.Save();
    }

    static void Message(string text) => MessageBoxW(IntPtr.Zero, text, "DeskDrawer", 0x10);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int MessageBoxW(IntPtr h, string text, string caption, uint type);
}
