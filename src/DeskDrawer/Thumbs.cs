using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media.Imaging;

namespace DeskDrawer;

/// Pictures for the preview pane from the Windows shell itself (IShellItemImageFactory): real thumbnails for images, videos, PDFs and Office files
/// wherever Windows has a handler, and the file's icon where it has not.
public static class Thumbs
{
    [StructLayout(LayoutKind.Sequential)] private struct SIZE { public int cx, cy; }

    [ComImport, Guid("BCC18B79-BA16-442F-80C4-8A59C30C463B"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IShellItemImageFactory { [PreserveSig] int GetImage(SIZE size, int flags, out IntPtr phbm); }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    private static extern void SHCreateItemFromParsingName(string path, IntPtr pbc, ref Guid riid, [MarshalAs(UnmanagedType.Interface)] out IShellItemImageFactory ppv);

    [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr h);

    private const int SIIGBF_BIGGERSIZEOK = 0x1;

    /// Call from an STA thread. Returns null when the shell has nothing.
    public static BitmapSource? Get(string path, int px)
    {
        try
        {
            var iid = typeof(IShellItemImageFactory).GUID;
            SHCreateItemFromParsingName(path, IntPtr.Zero, ref iid, out var f);
            if (f.GetImage(new SIZE { cx = px, cy = px }, SIIGBF_BIGGERSIZEOK, out var hbm) != 0 || hbm == IntPtr.Zero) return null;
            try { var bs = Imaging.CreateBitmapSourceFromHBitmap(hbm, IntPtr.Zero, Int32Rect.Empty, BitmapSizeOptions.FromEmptyOptions()); bs.Freeze(); return bs; }
            finally { DeleteObject(hbm); }
        }
        catch { return null; }
    }

    private static readonly Stack<(FileEntry E, int Px)> Queue = new();
    private static Thread[]? _workers;

    /// Thumbnails for the tiles and icon views, on a few STA threads (one made a 900-photo folder crawl), starting at the row `first` (what is on screen) and wrapping round;
    /// a new request replaces whatever was still waiting.
    public static void Request(IReadOnlyList<FileEntry> entries, int px, int first = 0)
    {
        int n = entries.Count; if (first < 0 || first >= n) first = 0;
        lock (Queue)
        {
            Queue.Clear();
            for (int k = n - 1; k >= 0; k--) { var e = entries[(first + k) % n]; if (e.ThumbPx < px) Queue.Push((e, px)); }   // pushed last-first so the visible row pops first
            Monitor.PulseAll(Queue);
        }
        if (_workers != null) return;
        _workers = new Thread[3];
        for (int w = 0; w < _workers.Length; w++)
        {
            _workers[w] = new Thread(() =>
            {
                for (;;)
                {
                    (FileEntry E, int Px) job;
                    lock (Queue) { while (Queue.Count == 0) Monitor.Wait(Queue); job = Queue.Pop(); }
                    if (job.E.ThumbPx >= job.Px) continue;
                    var bs = Get(job.E.FullPath, job.Px); if (bs == null) continue;
                    var e = job.E; int p = job.Px;
                    Application.Current?.Dispatcher.BeginInvoke(() => e.SetThumb(bs, p), System.Windows.Threading.DispatcherPriority.Background);
                }
            }) { IsBackground = true, Name = "DeskDrawer thumbnails", Priority = ThreadPriority.BelowNormal };
            _workers[w].SetApartmentState(ApartmentState.STA); _workers[w].Start();
        }
    }

    /// Runs a job on a fresh STA thread (the shell wants one) and hands the answer back through a callback.
    public static void OnSta(Action job) { var t = new Thread(() => { try { job(); } catch { } }) { IsBackground = true, Name = "DeskDrawer preview" }; t.SetApartmentState(ApartmentState.STA); t.Start(); }
}
