using System.Collections.Concurrent;
using System.IO;
using System.Windows;
using System.Windows.Threading;

namespace DeskDrawer;

/// Adds up the size of every folder in the open list on background threads and fills the Size column as each one finishes.
/// A size that was worked out before is remembered for as long as DeskDrawer runs and shown AT ONCE when the folder is listed again; if that figure is
/// older than 30 s it is counted again quietly and the cell corrects itself. (It used to forget after 30 s and show nothing until the recount finished,
/// which on a big folder like C:\Users meant a blank column and a busy disk on every visit.)
public static class FolderSizes
{
    private static readonly ConcurrentDictionary<string, (long Bytes, long Ticks)> Cache = new(StringComparer.OrdinalIgnoreCase);
    private const long Fresh = 30_000; private const int MaxRemembered = 50_000;

    /// recount: these folders are known to have changed, so count them again whatever the cache says (the old figure still shows meanwhile).
    public static void Start(IReadOnlyList<FileEntry> entries, CancellationToken ct, Action done, bool recount = false)
    {
        var todo = new List<FileEntry>(); long now = Environment.TickCount64;
        foreach (var e in entries)
        {
            if (!e.IsDir) continue;
            bool known = Cache.TryGetValue(e.FullPath, out var c);
            if (known) e.SetFolderSize(c.Bytes);
            if (!known || recount || now - c.Ticks >= Fresh) todo.Add(e);
        }
        if (todo.Count == 0) return;
        if (Cache.Count > MaxRemembered) Cache.Clear();   // a very long session: start over rather than grow without end
        Task.Run(() =>
        {
            try
            {
                // two at a time, below normal priority: this is background arithmetic and must not make opening the next folder wait for the disk
                Parallel.ForEach(todo, new ParallelOptions { MaxDegreeOfParallelism = 2, CancellationToken = ct }, d =>
                {
                    var t = Thread.CurrentThread; var keep = t.Priority; t.Priority = ThreadPriority.BelowNormal;
                    try
                    {
                        long n = Measure(d.FullPath, ct); if (ct.IsCancellationRequested) return;
                        Cache[d.FullPath] = (n, Environment.TickCount64);
                        Application.Current?.Dispatcher.BeginInvoke(() => d.SetFolderSize(n), DispatcherPriority.Background);
                    }
                    finally { t.Priority = keep; }
                });
                if (!ct.IsCancellationRequested) Application.Current?.Dispatcher.BeginInvoke(done, DispatcherPriority.Background);
            }
            catch (OperationCanceledException) { }
        });
    }

    internal static long Measure(string path, CancellationToken ct)
    {
        long total = 0;
        try
        {
            var opt = new EnumerationOptions { RecurseSubdirectories = true, IgnoreInaccessible = true, AttributesToSkip = FileAttributes.ReparsePoint };   // hidden files count; links are not followed
            foreach (var f in new DirectoryInfo(path).EnumerateFiles("*", opt)) { if (ct.IsCancellationRequested) break; try { total += f.Length; } catch { } }
        }
        catch { }
        return total;
    }
}
