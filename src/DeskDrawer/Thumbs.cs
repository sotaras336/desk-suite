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
    private static readonly AutoResetEvent Wake = new(false);
    private static Thread? _worker;

    /// Thumbnails for the tiles and icon views, on one STA thread, first rows first; a new request replaces whatever was still waiting.
    public static void Request(IReadOnlyList<FileEntry> entries, int px)
    {
        lock (Queue) { Queue.Clear(); for (int i = entries.Count - 1; i >= 0; i--) if (entries[i].ThumbPx < px) Queue.Push((entries[i], px)); }
        if (_worker == null)
        {
            _worker = new Thread(() =>
            {
                for (;;)
                {
                    (FileEntry E, int Px) job;
                    lock (Queue) job = Queue.Count > 0 ? Queue.Pop() : default;
                    if (job.E == null) { Wake.WaitOne(); continue; }
                    if (job.E.ThumbPx >= job.Px) continue;
                    var bs = Get(job.E.FullPath, job.Px); if (bs == null) continue;
                    var e = job.E; int p = job.Px;
                    Application.Current?.Dispatcher.BeginInvoke(() => e.SetThumb(bs, p), System.Windows.Threading.DispatcherPriority.Background);
                }
            }) { IsBackground = true, Name = "DeskDrawer thumbnails", Priority = ThreadPriority.BelowNormal };
            _worker.SetApartmentState(ApartmentState.STA); _worker.Start();
        }
        Wake.Set();
    }

    /// Runs a job on a fresh STA thread (the shell wants one) and hands the answer back through a callback.
    public static void OnSta(Action job) { var t = new Thread(() => { try { job(); } catch { } }) { IsBackground = true, Name = "DeskDrawer preview" }; t.SetApartmentState(ApartmentState.STA); t.Start(); }
}
