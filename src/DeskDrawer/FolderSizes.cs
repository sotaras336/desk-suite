using System.Collections.Concurrent;
using System.IO;
using System.Windows;
using System.Windows.Threading;

namespace DeskDrawer;

/// Adds up the size of every folder in the open list on background threads, newest folder list first, and fills the Size column as each one finishes.
/// Results are kept for 30 s so stepping back and forth, or a refresh after a small change, does not recount.
public static class FolderSizes
{
    private static readonly ConcurrentDictionary<string, (long Bytes, long Ticks)> Cache = new(StringComparer.OrdinalIgnoreCase);
    private const long Ttl = 30_000;

    public static void Start(IReadOnlyList<FileEntry> entries, CancellationToken ct, Action done)
    {
        var todo = new List<FileEntry>(); long now = Environment.TickCount64;
        foreach (var e in entries)
        {
            if (!e.IsDir) continue;
            if (Cache.TryGetValue(e.FullPath, out var c) && now - c.Ticks < Ttl) e.SetFolderSize(c.Bytes); else todo.Add(e);
        }
        if (todo.Count == 0) return;
        Task.Run(() =>
        {
            try
            {
                Parallel.ForEach(todo, new ParallelOptions { MaxDegreeOfParallelism = 3, CancellationToken = ct }, d =>
                {
                    long n = Measure(d.FullPath, ct); if (ct.IsCancellationRequested) return;
                    Cache[d.FullPath] = (n, Environment.TickCount64);
                    Application.Current?.Dispatcher.BeginInvoke(() => d.SetFolderSize(n), DispatcherPriority.Background);
                });
                if (!ct.IsCancellationRequested) Application.Current?.Dispatcher.BeginInvoke(done, DispatcherPriority.Background);
            }
            catch (OperationCanceledException) { }
        });
    }

    private static long Measure(string path, CancellationToken ct)
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
