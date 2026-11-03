using System.Collections.Specialized;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;

namespace DeskDrawer;

/// File operations done by the Windows shell itself (SHFileOperation): the same progress, conflict ("replace / skip / keep both") and
/// Recycle Bin behaviour as File Explorer, and the same clipboard format, so copy in DeskDrawer / paste in Explorer (and back) just works.
public static class ShellOps
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct SHFILEOPSTRUCT
    {
        public IntPtr hwnd; public uint wFunc;
        [MarshalAs(UnmanagedType.LPWStr)] public string pFrom;
        [MarshalAs(UnmanagedType.LPWStr)] public string? pTo;
        public ushort fFlags; public int fAnyOperationsAborted; public IntPtr hNameMappings;
        [MarshalAs(UnmanagedType.LPWStr)] public string? lpszProgressTitle;
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)] private static extern int SHFileOperation(ref SHFILEOPSTRUCT op);

    private const uint FO_MOVE = 1, FO_COPY = 2, FO_DELETE = 3;
    private const ushort FOF_SILENT = 0x4, FOF_RENAMEONCOLLISION = 0x8, FOF_NOCONFIRMATION = 0x10, FOF_ALLOWUNDO = 0x40, FOF_NOCONFIRMMKDIR = 0x200, FOF_WANTNUKEWARNING = 0x4000;

    /// Tests set this: no dialogs, no confirmations.
    public static bool Silent;

    private static bool Run(uint func, IEnumerable<string> from, string? to, ushort flags, IntPtr owner)
    {
        var list = from.ToList(); if (list.Count == 0) return true;
        if (Silent) flags |= FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR;
        var op = new SHFILEOPSTRUCT { hwnd = owner, wFunc = func, pFrom = string.Join("\0", list) + "\0", pTo = to == null ? null : to + "\0", fFlags = flags };
        int rc = SHFileOperation(ref op);
        return rc == 0 && op.fAnyOperationsAborted == 0;
    }

    public static bool Copy(IEnumerable<string> src, string destDir, IntPtr owner, bool keepBothOnCollision = false) =>
        Run(FO_COPY, src, destDir, (ushort)(FOF_ALLOWUNDO | (keepBothOnCollision ? FOF_RENAMEONCOLLISION : 0)), owner);

    public static bool Move(IEnumerable<string> src, string destDir, IntPtr owner) => Run(FO_MOVE, src, destDir, FOF_ALLOWUNDO, owner);

    /// To the Recycle Bin (undoable). permanent = delete for good, with the shell's warning.
    public static bool Delete(IEnumerable<string> paths, IntPtr owner, bool permanent = false) =>
        Run(FO_DELETE, paths, null, permanent ? FOF_WANTNUKEWARNING : FOF_ALLOWUNDO, owner);

    // ---------- new items ----------

    internal static string Unique(string dir, string name, string ext)
    {
        string p = Path.Combine(dir, name + ext);
        for (int i = 2; File.Exists(p) || Directory.Exists(p); i++) p = Path.Combine(dir, $"{name} ({i}){ext}");
        return p;
    }

    public static string NewFolder(string dir) { string p = Unique(dir, "New folder", ""); Directory.CreateDirectory(p); return p; }
    public static string NewTextFile(string dir) { string p = Unique(dir, "New Text Document", ".txt"); File.WriteAllBytes(p, Array.Empty<byte>()); return p; }

    // ---------- clipboard (File Explorer's format: a file drop list plus "Preferred DropEffect") ----------

    public static void SetClipboard(IEnumerable<string> paths, bool cut)
    {
        var sc = new StringCollection(); foreach (var p in paths) sc.Add(p);
        if (sc.Count == 0) return;
        var data = new DataObject(); data.SetFileDropList(sc);
        data.SetData("Preferred DropEffect", new MemoryStream(BitConverter.GetBytes(cut ? 2 : 5)));   // 2 = move, 5 = copy
        Clipboard.SetDataObject(data, true);
    }

    public static (List<string> Paths, bool Cut)? ReadClipboard()
    {
        try
        {
            var d = Clipboard.GetDataObject();
            if (d == null || !d.GetDataPresent(DataFormats.FileDrop)) return null;
            var paths = (d.GetData(DataFormats.FileDrop) as string[])?.ToList(); if (paths == null || paths.Count == 0) return null;
            bool cut = false;
            if (d.GetData("Preferred DropEffect") is MemoryStream ms && ms.Length >= 4) { var b = new byte[4]; ms.Position = 0; ms.Read(b, 0, 4); cut = (BitConverter.ToInt32(b) & 2) != 0; }
            return (paths, cut);
        }
        catch { return null; }
    }

    // ---------- shell verbs ----------

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct SHELLEXECUTEINFO
    {
        public int cbSize; public uint fMask; public IntPtr hwnd; public string lpVerb; public string lpFile; public string? lpParameters; public string? lpDirectory;
        public int nShow; public IntPtr hInstApp; public IntPtr lpIDList; public string? lpClass; public IntPtr hkeyClass; public uint dwHotKey; public IntPtr hIcon; public IntPtr hProcess;
    }
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)] private static extern bool ShellExecuteEx(ref SHELLEXECUTEINFO info);

    public static void Properties(string path, IntPtr owner)
    {
        var i = new SHELLEXECUTEINFO { fMask = 0x0C /* SEE_MASK_INVOKEIDLIST */, hwnd = owner, lpVerb = "properties", lpFile = path, nShow = 1 };
        i.cbSize = Marshal.SizeOf(i); ShellExecuteEx(ref i);
    }

    public static void OpenWith(string path) => System.Diagnostics.Process.Start("rundll32.exe", $"shell32.dll,OpenAs_RunDLL {path}");
}
