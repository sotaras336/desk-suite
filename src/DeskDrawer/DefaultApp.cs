using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace DeskDrawer;

/// "Open folders with DeskDrawer": per-user registry entries (HKCU only, nothing of Windows is changed) that make a folder or drive's default action
/// DeskDrawer, and point Win+E (the "This PC" item's new-window action) at it. Switching it off removes exactly those entries, and File Explorer is back.
public static class DefaultApp
{
    private const string Verb = "deskdrawer", OldVerb = "dexplorer";   // DeskDrawer was called Dexplorer: its old entries are moved over once
    private const string WinE = @"Software\Classes\CLSID\{52205fd8-5dfb-447d-801a-d0b52f2e83e1}\shell\opennewwindow";
    private static readonly string[] Classes = { "Directory", "Drive" };

    public static bool IsOn => Registry.CurrentUser.OpenSubKey(@"Software\Classes\Directory\shell")?.GetValue("") as string == Verb;

    public static void Set(bool on, string exe) => Set(on, exe, Verb);

    /// Dexplorer's "open folders" switch, if it was on, now points at DeskDrawer.
    public static void MigrateFromDexplorer(string exe)
    {
        if (Registry.CurrentUser.OpenSubKey(@"Software\Classes\Directory\shell")?.GetValue("") as string != OldVerb) return;
        Set(false, exe, OldVerb); Set(true, exe, Verb);
    }

    private static void Set(bool on, string exe, string verb)
    {
        var cu = Registry.CurrentUser;
        foreach (var cls in Classes)
        {
            string shell = $@"Software\Classes\{cls}\shell";
            if (on)
            {
                using (var k = cu.CreateSubKey(shell)) k.SetValue("", verb);
                using (var k = cu.CreateSubKey($@"{shell}\{verb}")) k.SetValue("", "Open in DeskDrawer");
                using (var k = cu.CreateSubKey($@"{shell}\{verb}\command")) k.SetValue("", $"\"{exe}\" \"%1\"");
            }
            else
            {
                using (var k = cu.OpenSubKey(shell, true)) if (k?.GetValue("") as string == verb) k.DeleteValue("", false);
                cu.DeleteSubKeyTree($@"{shell}\{verb}", false);
            }
        }
        if (on) { using var k = cu.CreateSubKey(WinE + @"\command"); k.SetValue("", $"\"{exe}\" shell:Home"); k.SetValue("DelegateExecute", ""); }
        else cu.DeleteSubKeyTree(WinE, false);
        SHChangeNotify(0x08000000, 0, IntPtr.Zero, IntPtr.Zero);   // SHCNE_ASSOCCHANGED: tell the shell right away
    }

    [DllImport("shell32.dll")] private static extern void SHChangeNotify(int eventId, uint flags, IntPtr a, IntPtr b);
}
