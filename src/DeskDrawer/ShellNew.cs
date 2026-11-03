using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using Microsoft.Win32;
using static DeskDrawer.Native;

namespace DeskDrawer;

/// The "New ▸" list Windows offers on this PC (Word document, ZIP folder, bitmap...), read from the registry's ShellNew entries, and the code that makes one.
public static class ShellNew
{
    public sealed record Kind(string Ext, string Name, ImageSource? Icon, string Mode, object? Value);   // Mode: null (empty file), file (copy a template), data (write these bytes)

    private static List<Kind>? _kinds; private static bool _loading;

    /// The list, or null while it is still being read (the first time takes a moment); call Preload at start-up so it is ready by the first right-click.
    public static List<Kind>? Kinds { get { Preload(); return _kinds; } }

    public static void Preload()
    {
        if (_kinds != null || _loading) return; _loading = true;
        Thumbs.OnSta(() => { var k = Read(); Application.Current?.Dispatcher.BeginInvoke(() => _kinds = k); });
    }

    /// The same list, read right now on the calling thread (self-test).
    public static List<Kind> ReadNow() => Read();

    private static List<Kind> Read()
    {
        var list = new List<Kind>(); var root = Registry.ClassesRoot;
        // File Explorer's own short-list of what to offer (it hides some registered types, like .contact); use it when it is there
        var shown = (Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Explorer\Discardable\PostSetup\ShellNew")?.GetValue("Classes") as string[])
                    ?.ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (var ext in root.GetSubKeyNames())
        {
            if (shown is { Count: > 0 } && !shown.Contains(ext)) continue;
            if (ext.Length < 2 || ext[0] != '.' || ext is ".lnk" or ".library-ms") continue;   // shortcuts have their own wizard; libraries aren't files you make here
            try
            {
                using var ek = root.OpenSubKey(ext); if (ek == null) continue;
                var progId = ek.GetValue("") as string;
                using var sn = ek.OpenSubKey("ShellNew") ?? (progId != null ? ek.OpenSubKey(progId + "\\ShellNew") : null);
                if (sn == null) continue;
                string mode; object? value = null;
                if (sn.GetValue("Command") is string cmd) { mode = "command"; value = cmd; }   // Google Docs / Sheets / Slides: their app makes the file
                else if (sn.GetValue("NullFile") != null) mode = "null";
                else if (sn.GetValue("FileName") is string fn) { mode = "file"; value = fn; }
                else if (sn.GetValue("Data") is { } d) { mode = "data"; value = d; }
                else continue;
                var (name, icon) = Describe(ext);
                if (!name.Contains(' ') && progId != null && root.OpenSubKey(progId)?.GetValue("") is string friendly && friendly.Length > 0) name = friendly;   // "LibreOffice.DrawDocument.1" -> "OpenDocument Drawing"
                if (name.Length == 0) continue;
                list.Add(new Kind(ext, name, icon, mode, value));
            }
            catch { }
        }
        // On Windows 11 the Notepad and Paint apps add these through their app packages, not the registry: add them ourselves
        foreach (var (ext, label) in new[] { (".txt", "Text Document"), (".bmp", "Bitmap image") })
            if (!list.Any(k => k.Ext == ext)) list.Add(new Kind(ext, label, Describe(ext).Item2, "null", null));
        return list.GroupBy(k => k.Name, StringComparer.OrdinalIgnoreCase).Select(g => g.First()).OrderBy(k => k.Name, StringComparer.CurrentCultureIgnoreCase).ToList();
    }

    /// The type's friendly name ("Microsoft Word Document") and small icon, as File Explorer shows them.
    private static (string, ImageSource?) Describe(string ext)
    {
        var fi = new SHFILEINFO();
        SHGetFileInfo("x" + ext, 0x80, ref fi, (uint)Marshal.SizeOf<SHFILEINFO>(), SHGFI_TYPENAME | SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
        ImageSource? icon = null;
        if (fi.hIcon != IntPtr.Zero) try { var bs = Imaging.CreateBitmapSourceFromHIcon(fi.hIcon, Int32Rect.Empty, System.Windows.Media.Imaging.BitmapSizeOptions.FromEmptyOptions()); bs.Freeze(); icon = bs; } finally { DestroyIcon(fi.hIcon); }
        return (fi.szTypeName ?? "", icon);
    }

    /// Makes "New <type>.<ext>" (numbered if taken) in dir and returns its path.
    public static string Create(Kind k, string dir)
    {
        string p = Path.Combine(dir, "New " + k.Name + k.Ext);
        for (int i = 2; File.Exists(p) || Directory.Exists(p); i++) p = Path.Combine(dir, $"New {k.Name} ({i}){k.Ext}");
        switch (k.Mode)
        {
            case "command":   // "app.exe --new_file="%1"": the app creates (and usually opens) the file itself
                string cmd = ((string)k.Value!).Replace("%1", p);
                int split = cmd.StartsWith('"') ? cmd.IndexOf('"', 1) + 1 : cmd.IndexOf(' ');
                string exe = split > 0 ? cmd[..split].Trim('"') : cmd, args = split > 0 ? cmd[split..].Trim() : "";
                System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(Environment.ExpandEnvironmentVariables(exe), args) { UseShellExecute = false });
                break;
            case "file" when Template((string)k.Value!) is { } t: File.Copy(t, p); break;
            case "data" when k.Value is byte[] b: File.WriteAllBytes(p, b); break;
            case "data" when k.Value is string s: File.WriteAllText(p, s); break;
            default: File.WriteAllBytes(p, Array.Empty<byte>()); break;
        }
        return p;
    }

    /// Where Windows keeps "FileName" templates when the value is not a full path.
    private static string? Template(string name)
    {
        name = Environment.ExpandEnvironmentVariables(name);
        if (Path.IsPathRooted(name)) return File.Exists(name) ? name : null;
        foreach (var dir in new[] { Environment.GetFolderPath(Environment.SpecialFolder.Templates), Environment.GetFolderPath(Environment.SpecialFolder.CommonTemplates),
                                    Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "ShellNew") })
            if (dir.Length > 0 && File.Exists(Path.Combine(dir, name))) return Path.Combine(dir, name);
        return null;
    }
}
