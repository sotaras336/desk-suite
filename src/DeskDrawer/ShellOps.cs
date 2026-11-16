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
    /// Why the last operation failed, for the status bar; null when it worked or you cancelled it yourself.
    public static string? LastError;

    // IFileOperation: the copy engine File Explorer itself has used since Vista. Unlike the old SHFileOperation it is not limited to 260-character paths, it reports
    // a real error instead of a private code, and its copies, moves and deletes land in Explorer's own undo list.
    [ComImport, Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)] private interface IShellItem { }
    [ComImport, Guid("947AAB5F-0A5C-4C13-B4D6-4BF7836FC9F8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IFileOperation   // every method declared, in vtable order: a missing one would shift the rest onto the wrong call
    {
        [PreserveSig] int Advise(IntPtr sink, out uint cookie);
        [PreserveSig] int Unadvise(uint cookie);
        [PreserveSig] int SetOperationFlags(uint flags);
        [PreserveSig] int SetProgressMessage([MarshalAs(UnmanagedType.LPWStr)] string message);
        [PreserveSig] int SetProgressDialog(IntPtr dialog);
        [PreserveSig] int SetProperties(IntPtr props);
        [PreserveSig] int SetOwnerWindow(IntPtr hwnd);
        [PreserveSig] int ApplyPropertiesToItem(IShellItem item);
        [PreserveSig] int ApplyPropertiesToItems(IntPtr items);
        [PreserveSig] int RenameItem(IShellItem item, [MarshalAs(UnmanagedType.LPWStr)] string newName, IntPtr sink);
        [PreserveSig] int RenameItems(IntPtr items, [MarshalAs(UnmanagedType.LPWStr)] string newName);
        [PreserveSig] int MoveItem(IShellItem item, IShellItem destFolder, [MarshalAs(UnmanagedType.LPWStr)] string? newName, IntPtr sink);
        [PreserveSig] int MoveItems(IntPtr items, IShellItem destFolder);
        [PreserveSig] int CopyItem(IShellItem item, IShellItem destFolder, [MarshalAs(UnmanagedType.LPWStr)] string? copyName, IntPtr sink);
        [PreserveSig] int CopyItems(IntPtr items, IShellItem destFolder);
        [PreserveSig] int DeleteItem(IShellItem item, IntPtr sink);
        [PreserveSig] int DeleteItems(IntPtr items);
        [PreserveSig] int NewItem(IShellItem destFolder, uint attributes, [MarshalAs(UnmanagedType.LPWStr)] string name, [MarshalAs(UnmanagedType.LPWStr)] string? template, IntPtr sink);
        [PreserveSig] int PerformOperations();
        [PreserveSig] int GetAnyOperationsAborted([MarshalAs(UnmanagedType.Bool)] out bool aborted);
    }
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)] private static extern int SHCreateItemFromParsingName(string path, IntPtr bc, ref Guid iid, out IShellItem item);
    private static readonly Guid ClsidFileOperation = new("3AD05575-8857-4850-9277-11B85BDB8E09");
    private const uint FOFX_ADDUNDORECORD = 0x20000000, FOFX_RECYCLEONDELETE = 0x00080000, FOFX_SHOWELEVATIONPROMPT = 0x00040000, FOF_NOERRORUI = 0x400;
    private const int Cancelled = unchecked((int)0x800704C7), CancelledCopy = unchecked((int)0x80270000);   // ERROR_CANCELLED, COPYENGINE_E_USER_CANCELLED

    private static bool Run(uint func, IEnumerable<string> from, string? to, ushort flags, IntPtr owner)
    {
        var list = from.ToList(); if (list.Count == 0) return true;
        uint f = flags | FOFX_ADDUNDORECORD | FOFX_SHOWELEVATIONPROMPT;
        if (func == FO_DELETE && (flags & FOF_ALLOWUNDO) != 0) f |= FOFX_RECYCLEONDELETE;
        if (Silent) f |= FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI;
        int hr = 0; bool aborted = false; string? missing = null;
        void Work()
        {
            IFileOperation? op = null;
            try
            {
                op = (IFileOperation)Activator.CreateInstance(Type.GetTypeFromCLSID(ClsidFileOperation)!)!;
                var iid = typeof(IShellItem).GUID; IShellItem? dest = null;
                if ((hr = op.SetOperationFlags(f)) < 0) return; if (owner != IntPtr.Zero) op.SetOwnerWindow(owner);
                if (to != null && (hr = SHCreateItemFromParsingName(to, IntPtr.Zero, ref iid, out dest)) < 0) { missing = to; return; }
                foreach (var p in list)
                {
                    if ((hr = SHCreateItemFromParsingName(p, IntPtr.Zero, ref iid, out var item)) < 0) { missing = p; return; }   // nothing has been done yet: the whole job is refused, not half of it
                    hr = func == FO_DELETE ? op.DeleteItem(item, IntPtr.Zero) : func == FO_MOVE ? op.MoveItem(item, dest!, null, IntPtr.Zero) : op.CopyItem(item, dest!, null, IntPtr.Zero);
                    if (hr < 0) return;
                }
                hr = op.PerformOperations(); op.GetAnyOperationsAborted(out aborted);
            }
            catch (Exception ex) { hr = ex.HResult < 0 ? ex.HResult : unchecked((int)0x80004005); }
            finally { if (op != null) Marshal.FinalReleaseComObject(op); }
        }
        // the shell's file engine wants a single-threaded apartment; callers are on a pool thread (RunOp) or the window's own thread
        if (Thread.CurrentThread.GetApartmentState() == ApartmentState.STA) Work();
        else { var t = new Thread(Work) { IsBackground = true, Name = "DeskDrawer file operation" }; t.SetApartmentState(ApartmentState.STA); t.Start(); t.Join(); }
        bool cancelled = aborted || hr == Cancelled || hr == CancelledCopy;
        LastError = hr >= 0 || cancelled ? null : missing != null ? $"Can't find “{Path.GetFileName(missing.TrimEnd('\\'))}”. It may have been moved or deleted." : "Windows couldn't finish that: " + Explain(hr);
        return hr >= 0 && !aborted;
    }

    /// Windows' own sentence for an error ("Access is denied.", "The file name is too long."); the bare number only when it has none.
    internal static string Explain(int hr)
    {
        string? m = null; try { m = Marshal.GetExceptionForHR(hr)?.Message; } catch { }
        return string.IsNullOrWhiteSpace(m) || m.Contains("HRESULT", StringComparison.OrdinalIgnoreCase) ? $"error 0x{hr:X8}" : m.Trim();
    }

    /// Undo for a delete: puts the items back where they were, from the Recycle Bin. Takes the newest copy of each path.
    public static bool RestoreFromRecycleBin(IEnumerable<string> paths)
    {
        var want = new HashSet<string>(paths, StringComparer.OrdinalIgnoreCase); int back = 0;
        var t = new Thread(() =>
        {
            try
            {
                dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("Shell.Application")!)!, bin = shell.NameSpace(10);   // 10 = the Recycle Bin
                var found = new Dictionary<string, (DateTime When, dynamic Item)>(StringComparer.OrdinalIgnoreCase);
                foreach (dynamic item in bin.Items())
                {
                    string from = Path.Combine((string)bin.GetDetailsOf(item, 1), (string)item.ExtendedProperty("System.FileName"));   // column 1 = where it came from
                    if (!want.Contains(from)) continue;
                    DateTime when = item.ExtendedProperty("System.Recycle.DateDeleted") is DateTime d ? d : DateTime.MinValue;
                    if (!found.TryGetValue(from, out var old) || when >= old.When) found[from] = (when, item);
                }
                foreach (var f in found.Values) { f.Item.InvokeVerb("undelete"); back++; }
            }
            catch { }
        });
        t.SetApartmentState(ApartmentState.STA); t.Start(); t.Join();
        return back > 0;
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
