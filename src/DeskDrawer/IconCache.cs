using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using static DeskDrawer.Native;

namespace DeskDrawer;

/// Shell icons and type names, fetched on ONE background STA thread (the shell wants that) and cached: by extension for ordinary files,
/// by full path for programs, shortcuts and icons (each has its own picture). Newest request first, so the folder you are looking at wins.
public static class IconCache
{
    private sealed record Hit(ImageSource? Icon, string Type);
    private static readonly ConcurrentDictionary<string, Hit> Cache = new();
    private static readonly Stack<(FileEntry E, int Gen)> Queue = new();
    private static readonly object Gate = new();
    private static readonly AutoResetEvent Wake = new(false);
    private static int _gen;
    private static Thread? _worker;

    private static ImageSource? _genericFolder; private static bool _genericAsked; private static readonly List<Action<ImageSource>> GenericWaiting = new();
    private static readonly ConcurrentDictionary<string, ImageSource> PathIcons = new();

    private static ImageSource? Grab(string path, bool generic)
    {
        var fi = new SHFILEINFO(); uint flags = SHGFI_ICON | SHGFI_SMALLICON; uint attrs = 0;
        if (generic) { flags |= SHGFI_USEFILEATTRIBUTES; attrs = 0x10; path = "folder"; }
        SHGetFileInfo(path, attrs, ref fi, (uint)Marshal.SizeOf<SHFILEINFO>(), flags);
        if (fi.hIcon == IntPtr.Zero) return null;
        try { var bs = Imaging.CreateBitmapSourceFromHIcon(fi.hIcon, Int32Rect.Empty, BitmapSizeOptions.FromEmptyOptions()); bs.Freeze(); return bs; } finally { DestroyIcon(fi.hIcon); }
    }

    /// The tree's icons. exact = this very folder's own picture (Desktop, Downloads, a drive...); otherwise the ordinary yellow folder, fetched once and shared by every plain folder.
    public static void PathIcon(string path, bool exact, Action<ImageSource> done)
    {
        if (!exact)
        {
            if (_genericFolder != null) { done(_genericFolder); return; }
            lock (GenericWaiting) { GenericWaiting.Add(done); if (_genericAsked) return; _genericAsked = true; }
            Thumbs.OnSta(() =>
            {
                var ic = Grab(path, true); _genericFolder = ic;
                Application.Current?.Dispatcher.BeginInvoke(() => { List<Action<ImageSource>> w; lock (GenericWaiting) { w = GenericWaiting.ToList(); GenericWaiting.Clear(); } if (ic != null) foreach (var a in w) a(ic); });
            });
            return;
        }
        if (PathIcons.TryGetValue(path, out var c)) { done(c); return; }
        Thumbs.OnSta(() =>
        {
            var ic = Grab(path, false); if (ic == null) return; PathIcons[path] = ic;
            Application.Current?.Dispatcher.BeginInvoke(() => done(ic));
        });
    }

    /// Starts fetching the icons for a freshly loaded folder; anything still queued for the previous folder is dropped.
    public static void Request(IReadOnlyList<FileEntry> entries)
    {
        lock (Gate)
        {
            Queue.Clear(); _gen++;
            for (int i = entries.Count - 1; i >= 0; i--) Queue.Push((entries[i], _gen));   // top of the stack = first row
        }
        if (_worker == null)
        {
            _worker = new Thread(Run) { IsBackground = true, Name = "DeskDrawer icons", Priority = ThreadPriority.BelowNormal };
            _worker.SetApartmentState(ApartmentState.STA); _worker.Start();
        }
        Wake.Set();
    }

    private static void Run()
    {
        for (;;)
        {
            (FileEntry E, int Gen) job;
            lock (Gate) { if (Queue.Count == 0) job = default; else job = Queue.Pop(); }
            if (job.E == null) { Wake.WaitOne(); continue; }
            Hit hit;
            try { hit = Fetch(job.E); } catch { hit = new Hit(null, job.E.TypeText); }
            var e = job.E;
            Application.Current?.Dispatcher.BeginInvoke(() => { if (hit.Icon != null) e.Icon = hit.Icon; if (hit.Type.Length > 0) e.TypeText = hit.Type; }, System.Windows.Threading.DispatcherPriority.Background);
        }
    }

    private static Hit Fetch(FileEntry e)
    {
        bool own = e.Ext is ".exe" or ".lnk" or ".ico" or ".url" or ".msi" or ".appref-ms" or ".cur" or ".ani";   // these carry their own picture
        string key = e.IsDir ? "<dir>" : own ? e.FullPath : e.Ext.Length > 0 ? e.Ext : "<none>";
        if (Cache.TryGetValue(key, out var cached)) return cached;
        var fi = new SHFILEINFO(); uint flags = SHGFI_ICON | SHGFI_SMALLICON | SHGFI_TYPENAME; uint attrs = 0; string path = e.FullPath;
        if (!own) { flags |= SHGFI_USEFILEATTRIBUTES; attrs = e.IsDir ? 0x10u : 0x80u; path = e.IsDir ? "folder" : (e.Ext.Length > 0 ? "x" + e.Ext : "x"); }   // answered from the registry, no disk access
        SHGetFileInfo(path, attrs, ref fi, (uint)Marshal.SizeOf<SHFILEINFO>(), flags);
        ImageSource? icon = null;
        if (fi.hIcon != IntPtr.Zero)
        {
            try { var bs = Imaging.CreateBitmapSourceFromHIcon(fi.hIcon, Int32Rect.Empty, BitmapSizeOptions.FromEmptyOptions()); bs.Freeze(); icon = bs; }
            finally { DestroyIcon(fi.hIcon); }
        }
        var hit = new Hit(icon, fi.szTypeName ?? "");
        Cache[key] = hit; return hit;
    }
}
