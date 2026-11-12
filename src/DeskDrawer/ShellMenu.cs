using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace DeskDrawer;

/// The real Windows right-click menu for files and folders (every "Open with", "Send to", 7-Zip, Git... extension), through IContextMenu.
/// Used for "Show more options" in DeskDrawer's own menu.
public static class ShellMenu
{
    [ComImport, Guid("000214E6-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IShellFolder
    {
        [PreserveSig] int ParseDisplayName(IntPtr hwnd, IntPtr pbc, [MarshalAs(UnmanagedType.LPWStr)] string name, out uint eaten, out IntPtr pidl, ref uint attrs);
        [PreserveSig] int EnumObjects(IntPtr hwnd, uint flags, out IntPtr enumIdList);
        [PreserveSig] int BindToObject(IntPtr pidl, IntPtr pbc, ref Guid riid, out IntPtr ppv);
        [PreserveSig] int BindToStorage(IntPtr pidl, IntPtr pbc, ref Guid riid, out IntPtr ppv);
        [PreserveSig] int CompareIDs(IntPtr lParam, IntPtr pidl1, IntPtr pidl2);
        [PreserveSig] int CreateViewObject(IntPtr hwndOwner, ref Guid riid, out IntPtr ppv);
        [PreserveSig] int GetAttributesOf(uint cidl, [MarshalAs(UnmanagedType.LPArray)] IntPtr[] apidl, ref uint attrs);
        [PreserveSig] int GetUIObjectOf(IntPtr hwndOwner, uint cidl, [MarshalAs(UnmanagedType.LPArray)] IntPtr[] apidl, ref Guid riid, IntPtr reserved, out IntPtr ppv);
        [PreserveSig] int GetDisplayNameOf(IntPtr pidl, uint flags, IntPtr strret);
        [PreserveSig] int SetNameOf(IntPtr hwnd, IntPtr pidl, [MarshalAs(UnmanagedType.LPWStr)] string name, uint flags, out IntPtr newPidl);
    }

    [StructLayout(LayoutKind.Sequential)] internal struct POINT { public int X, Y; }

    [StructLayout(LayoutKind.Sequential)]
    internal struct CMINVOKECOMMANDINFOEX
    {
        public int cbSize; public uint fMask; public IntPtr hwnd; public IntPtr lpVerb; public IntPtr lpParameters; public IntPtr lpDirectory;
        public int nShow; public uint dwHotKey; public IntPtr hIcon; public IntPtr lpTitle; public IntPtr lpVerbW; public IntPtr lpParametersW; public IntPtr lpDirectoryW; public IntPtr lpTitleW; public POINT ptInvoke;
    }

    [ComImport, Guid("000214E4-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IContextMenu
    {
        [PreserveSig] int QueryContextMenu(IntPtr hmenu, uint indexMenu, uint idCmdFirst, uint idCmdLast, uint flags);
        [PreserveSig] int InvokeCommand(ref CMINVOKECOMMANDINFOEX info);
        [PreserveSig] int GetCommandString(UIntPtr idCmd, uint type, IntPtr reserved, IntPtr name, uint cch);
    }
    [ComImport, Guid("000214F4-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IContextMenu2 : IContextMenu { [PreserveSig] int HandleMenuMsg(uint msg, IntPtr wp, IntPtr lp); }
    [ComImport, Guid("BCFCE0A0-EC17-11D0-8D10-00A0C90F2719"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IContextMenu3 : IContextMenu2 { [PreserveSig] int HandleMenuMsg2(uint msg, IntPtr wp, IntPtr lp, out IntPtr result); }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)] private static extern int SHParseDisplayName(string name, IntPtr bc, out IntPtr pidl, uint sfgaoIn, out uint sfgaoOut);
    [DllImport("shell32.dll")] private static extern int SHBindToParent(IntPtr pidl, ref Guid riid, out IntPtr ppv, out IntPtr pidlLast);
    [DllImport("ole32.dll")] private static extern void CoTaskMemFree(IntPtr p);
    [DllImport("shell32.dll")] private static extern int SHDoDragDrop(IntPtr hwnd, IntPtr dataObject, IntPtr dropSource, uint okEffects, out uint effect);
    private static readonly Guid IID_IDataObject = new("0000010E-0000-0000-C000-000000000046");

    /// Drags files the way File Explorer does: the shell's own data object for them (every format other apps look for, not just a file list) and Windows' own
    /// drag loop and drag image. False when the shell can't make one (items from several folders, as in search results): the caller falls back to WPF's drag.
    public static bool Drag(IReadOnlyList<string> paths, IntPtr owner, uint okEffects, out uint effect)
    {
        effect = 0; var pidls = new List<IntPtr>(); var children = new List<IntPtr>(); IShellFolder? folder = null; string? parent = null;
        try
        {
            foreach (var p in paths)
            {
                string dir = System.IO.Path.GetDirectoryName(p.TrimEnd('\\')) ?? p; parent ??= dir;
                if (!string.Equals(parent, dir, StringComparison.OrdinalIgnoreCase)) return false;   // one data object = one folder
                if (SHParseDisplayName(p, IntPtr.Zero, out var pidl, 0, out _) != 0) return false; pidls.Add(pidl);
                var iid = IID_IShellFolder; if (SHBindToParent(pidl, ref iid, out var ppv, out var last) != 0) return false;
                folder ??= (IShellFolder)Marshal.GetObjectForIUnknown(ppv); Marshal.Release(ppv); children.Add(last);
            }
            if (folder == null || children.Count == 0) return false;
            var iidData = IID_IDataObject;
            if (folder.GetUIObjectOf(owner, (uint)children.Count, children.ToArray(), ref iidData, IntPtr.Zero, out var data) != 0 || data == IntPtr.Zero) return false;
            try { return SHDoDragDrop(owner, data, IntPtr.Zero, okEffects, out effect) >= 0; } finally { Marshal.Release(data); }
        }
        catch { return false; }
        finally { foreach (var p in pidls) CoTaskMemFree(p); }
    }
    [DllImport("user32.dll")] private static extern IntPtr CreatePopupMenu();
    [DllImport("user32.dll")] private static extern bool DestroyMenu(IntPtr h);
    [DllImport("user32.dll")] private static extern int GetMenuItemCount(IntPtr h);
    [DllImport("user32.dll")] private static extern uint TrackPopupMenuEx(IntPtr hmenu, uint flags, int x, int y, IntPtr hwnd, IntPtr tpm);

    private static readonly Guid IID_IShellFolder = new("000214E6-0000-0000-C000-000000000046"), IID_IContextMenu = new("000214E4-0000-0000-C000-000000000046");
    private const uint CMF_NORMAL = 0, CMF_EXTENDEDVERBS = 0x100, TPM_RETURNCMD = 0x100, TPM_RIGHTBUTTON = 0x2, CMIC_MASK_UNICODE = 0x4000, CMIC_MASK_PTINVOKE = 0x20000000;

    internal sealed class Bound : IDisposable
    {
        public IContextMenu Menu = null!; public IContextMenu2? Menu2; public IContextMenu3? Menu3;
        private readonly List<IntPtr> _pidls = new();
        public static Bound? Create(IReadOnlyList<string> paths, IntPtr owner)
        {
            if (paths.Count == 0) return null;
            var b = new Bound(); var children = new List<IntPtr>(); IShellFolder? folder = null; string? parent = null;
            try
            {
                foreach (var p in paths)
                {
                    string dir = System.IO.Path.GetDirectoryName(p.TrimEnd('\\')) ?? p; parent ??= dir; if (!string.Equals(parent, dir, StringComparison.OrdinalIgnoreCase)) continue;   // one parent folder per menu
                    if (SHParseDisplayName(p, IntPtr.Zero, out var pidl, 0, out _) != 0) continue; b._pidls.Add(pidl);
                    var iid = IID_IShellFolder;
                    if (SHBindToParent(pidl, ref iid, out var ppv, out var last) != 0) continue;
                    folder ??= (IShellFolder)Marshal.GetObjectForIUnknown(ppv); Marshal.Release(ppv); children.Add(last);
                }
                if (folder == null || children.Count == 0) { b.Dispose(); return null; }
                var cmIid = IID_IContextMenu;
                if (folder.GetUIObjectOf(owner, (uint)children.Count, children.ToArray(), ref cmIid, IntPtr.Zero, out var cm) != 0 || cm == IntPtr.Zero) { b.Dispose(); return null; }
                var obj = Marshal.GetObjectForIUnknown(cm); Marshal.Release(cm);
                b.Menu = (IContextMenu)obj; b.Menu2 = obj as IContextMenu2; b.Menu3 = obj as IContextMenu3;
                return b;
            }
            catch { b.Dispose(); return null; }
        }
        public void Dispose() { foreach (var p in _pidls) CoTaskMemFree(p); _pidls.Clear(); }
    }

    // ---------- the shell's menu as data, so DeskDrawer can draw it in its own theme ----------

    public sealed class ShellEntry
    {
        public string Text = "", Gesture = ""; public uint Id; public bool Separator, Disabled, Checked, Default;
        public System.Windows.Media.ImageSource? Icon; public List<ShellEntry>? Children;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MENUITEMINFO
    {
        public uint cbSize, fMask, fType, fState, wID; public IntPtr hSubMenu, hbmpChecked, hbmpUnchecked, dwItemData; public IntPtr dwTypeData; public uint cch; public IntPtr hbmpItem;
    }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern bool GetMenuItemInfoW(IntPtr hMenu, uint item, bool byPosition, ref MENUITEMINFO mii);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr h);

    /// An open shell menu: its items as data, and the means to run one. Keep it alive while the menu is on screen, then Dispose.
    public sealed class Session : IDisposable
    {
        internal Bound B = null!; internal IntPtr Menu, Owner; public List<ShellEntry> Items = new();
        public void Invoke(uint id, int x, int y)
        {
            var info = new CMINVOKECOMMANDINFOEX { fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE, hwnd = Owner, lpVerb = (IntPtr)(id - 1), lpVerbW = (IntPtr)(id - 1), nShow = 1, ptInvoke = new POINT { X = x, Y = y } };
            info.cbSize = Marshal.SizeOf(info); B.Menu.InvokeCommand(ref info);
        }
        public void Dispose() { if (Menu != IntPtr.Zero) { DestroyMenu(Menu); Menu = IntPtr.Zero; } B?.Dispose(); }
    }

    public static Session? Open(IntPtr owner, IReadOnlyList<string> paths, bool extended)
    {
        var b = Bound.Create(paths, owner); if (b == null) return null;
        var hmenu = CreatePopupMenu();
        try
        {
            b.Menu.QueryContextMenu(hmenu, 0, 1, 0x7FFF, CMF_NORMAL | (extended ? CMF_EXTENDEDVERBS : 0));
            var s = new Session { B = b, Menu = hmenu, Owner = owner }; s.Items = Read(b, hmenu, 0); return s;
        }
        catch { DestroyMenu(hmenu); b.Dispose(); return null; }
    }

    private static List<ShellEntry> Read(Bound b, IntPtr hmenu, int depth)
    {
        var list = new List<ShellEntry>(); int n = GetMenuItemCount(hmenu);
        for (int i = 0; i < n; i++)
        {
            var mii = new MENUITEMINFO { cbSize = (uint)Marshal.SizeOf<MENUITEMINFO>(), fMask = 0x1 | 0x2 | 0x4 | 0x40 | 0x80 | 0x100 };   // state, id, submenu, string, bitmap, type
            if (!GetMenuItemInfoW(hmenu, (uint)i, true, ref mii)) continue;
            if ((mii.fType & 0x800) != 0) { if (list.Count > 0 && !list[^1].Separator) list.Add(new ShellEntry { Separator = true }); continue; }
            string text = "";
            if (mii.cch > 0)
            {
                uint len = mii.cch; var buf = Marshal.AllocHGlobal((int)(len + 2) * 2);
                try { mii.dwTypeData = buf; mii.cch = len + 1; mii.fMask = 0x40; if (GetMenuItemInfoW(hmenu, (uint)i, true, ref mii)) text = Marshal.PtrToStringUni(buf) ?? ""; }
                finally { Marshal.FreeHGlobal(buf); }
                mii.fMask = 0x1 | 0x2 | 0x4 | 0x80 | 0x100; mii.dwTypeData = IntPtr.Zero; mii.cch = 0; GetMenuItemInfoW(hmenu, (uint)i, true, ref mii);   // read the rest again (the string call overwrote the struct)
            }
            if (text.Length == 0) continue;   // owner-drawn entries carry no text we can read
            var e = new ShellEntry { Id = mii.wID, Disabled = (mii.fState & 0x3) != 0, Checked = (mii.fState & 0x8) != 0, Default = (mii.fState & 0x1000) != 0 };
            int tab = text.IndexOf('\t'); if (tab >= 0) { e.Gesture = text[(tab + 1)..]; text = text[..tab]; }
            e.Text = text.Replace("&&", "\u0001").Replace("&", "").Replace("\u0001", "&");
            if ((long)mii.hbmpItem > 16)
            {
                try { var bs = System.Windows.Interop.Imaging.CreateBitmapSourceFromHBitmap(mii.hbmpItem, IntPtr.Zero, Int32Rect.Empty, System.Windows.Media.Imaging.BitmapSizeOptions.FromEmptyOptions()); bs.Freeze(); e.Icon = bs; } catch { }
            }
            if (mii.hSubMenu != IntPtr.Zero)
            {
                if (depth >= 3) continue;
                try { var lp = (IntPtr)(i & 0xFFFF); if (b.Menu3 != null) b.Menu3.HandleMenuMsg2(0x117, mii.hSubMenu, lp, out _); else b.Menu2?.HandleMenuMsg(0x117, mii.hSubMenu, lp); } catch { }   // WM_INITMENUPOPUP: "Send to" and "Open with" fill themselves now
                e.Children = Read(b, mii.hSubMenu, depth + 1); if (e.Children.Count == 0) continue;
            }
            list.Add(e);
        }
        while (list.Count > 0 && list[^1].Separator) list.RemoveAt(list.Count - 1);
        return list;
    }

    /// How many entries the shell would show for these files (used by the self-test; nothing is displayed).
    public static int CountItems(IReadOnlyList<string> paths, IntPtr owner)
    {
        using var b = Bound.Create(paths, owner); if (b == null) return -1;
        var hmenu = CreatePopupMenu();
        try { b.Menu.QueryContextMenu(hmenu, 0, 1, 0x7FFF, CMF_NORMAL); return GetMenuItemCount(hmenu); }
        finally { DestroyMenu(hmenu); }
    }

    /// Shows the shell menu at a screen point and runs whatever the user picks.
    public static void Show(Window owner, IReadOnlyList<string> paths, System.Windows.Point screen)
    {
        var hwnd = new WindowInteropHelper(owner).Handle;
        using var b = Bound.Create(paths, hwnd); if (b == null) return;
        var hmenu = CreatePopupMenu();
        var src = HwndSource.FromHwnd(hwnd);
        IntPtr Hook(IntPtr h, int msg, IntPtr wp, IntPtr lp, ref bool handled)
        {   // submenus ("Send to", "Open with") fill themselves and draw their own items through these messages
            if (msg == 0x117 || msg == 0x2B || msg == 0x2C || msg == 0x120)
            {
                try
                {
                    if (b.Menu3 != null) { b.Menu3.HandleMenuMsg2((uint)msg, wp, lp, out var res); handled = true; return res; }
                    if (b.Menu2 != null) { b.Menu2.HandleMenuMsg((uint)msg, wp, lp); handled = true; return IntPtr.Zero; }
                }
                catch { }
            }
            return IntPtr.Zero;
        }
        try
        {
            b.Menu.QueryContextMenu(hmenu, 0, 1, 0x7FFF, CMF_NORMAL | ((System.Windows.Input.Keyboard.Modifiers & System.Windows.Input.ModifierKeys.Shift) != 0 ? CMF_EXTENDEDVERBS : 0));
            src?.AddHook(Hook);
            uint id = TrackPopupMenuEx(hmenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, (int)screen.X, (int)screen.Y, hwnd, IntPtr.Zero);
            src?.RemoveHook(Hook);
            if (id == 0) return;
            var info = new CMINVOKECOMMANDINFOEX
            {
                fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE, hwnd = hwnd, lpVerb = (IntPtr)(id - 1), lpVerbW = (IntPtr)(id - 1), nShow = 1, ptInvoke = new POINT { X = (int)screen.X, Y = (int)screen.Y },
            };
            info.cbSize = Marshal.SizeOf(info);
            b.Menu.InvokeCommand(ref info);
        }
        finally { src?.RemoveHook(Hook); DestroyMenu(hmenu); }
    }
}
