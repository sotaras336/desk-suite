using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.IO.Enumeration;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Threading;

namespace DeskDrawer;

/// Search: typing in the filter box narrows the open folder at once. Enter looks through everything inside it (from Home or This PC: through the whole PC)
/// and opens the results as their own place (Esc or Back returns). Results stream in while the search runs, best match first, under a banner that says
/// what is being searched and how far it has got. Several words must all match (in the name, or in a folder above it); case and accents don't matter.
/// The whole-PC search reads DeskBar's index of every file when DeskBar has made one (tens of milliseconds), and walks the disks itself when not.
public partial class MainWindow
{
    private const int MaxHits = 20000;   // shortcut: stops at 20,000 results; the banner says so and the words can be narrowed
    private GridViewColumn? _folderCol;

    private void Search_KeyDown(object s, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            string q = Search.Text.Trim(); if (q.Length == 0 || _tab.Current is not { } cur) return;
            string root = IsSearch(cur) ? SearchParts(cur).Root : IsPage(cur) ? ThisPcTag : cur;   // from Home, This PC, Recent...: everywhere
            _tab.ForceLive = false; Navigate(SearchTag + root + "?" + q); List.Focus(); e.Handled = true;
        }
        else if (e.Key == Key.Escape) { Search.Text = ""; List.Focus(); e.Handled = true; }
    }

    private void SearchScope_Click(object s, RoutedEventArgs e) { if (_tab.Current is { } cur && IsSearch(cur)) Navigate(SearchTag + ThisPcTag + "?" + SearchParts(cur).Query); }
    private void SearchStop_Click(object s, RoutedEventArgs e) => _tab.SearchStop?.Cancel();
    private void SearchLive_Click(object s, RoutedEventArgs e) { _tab.ForceLive = true; LoadTab(_tab); BindTab(); }

    /// Shows or hides the banner above the results, and makes room for it.
    private void ShowSearchBar()
    {
        bool on = InSearch; UpdateSortHeaders(); SearchBar.Visibility = on ? Visibility.Visible : Visibility.Collapsed; List.Margin = new Thickness(0, on ? SearchBar.Height : 0, 0, 0);
        if (!on) return;
        SearchTitle.Text = _tab.SearchTitle; SearchDetail.Text = _tab.SearchDetail;
        SearchStopBtn.Visibility = _tab.Searching ? Visibility.Visible : Visibility.Collapsed;
        SearchLiveBtn.Visibility = _tab.UsedIndex && !_tab.Searching ? Visibility.Visible : Visibility.Collapsed;
        SearchScopeBtn.Visibility = SearchParts(_tab.Current!).Root == ThisPcTag ? Visibility.Collapsed : Visibility.Visible;
    }

    private sealed class Progress { public long Scanned; public volatile string Where = ""; public int Found; public volatile bool Finished, Capped; public volatile string How = ""; public volatile string? Failure; }

    private void RunSearch(TabState tab, string path, bool showHidden, CancellationTokenSource cts, Action<List<FileEntry>, string?> done)
    {
        var (root, q) = SearchParts(path); var m = new Matcher(q); bool everywhere = root == ThisPcTag; string scope = everywhere ? "this PC" : root;
        var list = new List<FileEntry>(); var pending = new ConcurrentQueue<FileEntry>(); var prog = new Progress { Where = root };
        var stop = tab.SearchStop = CancellationTokenSource.CreateLinkedTokenSource(cts.Token); var clock = Stopwatch.StartNew(); bool bound = false;
        tab.Glyph = ""; tab.ByRelevance = true; tab.Searching = true; tab.SearchTitle = $"Searching {scope} for “{q}”"; tab.SearchDetail = "Starting…";
        if (m.Tokens.Length == 0) { tab.Searching = false; done(list, "Type a word to look for"); return; }

        Task.Run(() =>
        {
            try
            {
                var ix = everywhere && !tab.ForceLive ? SharedIndex.Acquire() : null; tab.UsedIndex = ix != null;
                if (ix != null)
                {
                    try
                    {
                        prog.How = $"From DeskBar's list of all {ix.Count:N0} files and folders (updated {ix.Built:d MMM HH:mm}).";
                        var hits = ix.Find(m, 6000, stop.Token); prog.Scanned = ix.Count;
                        Parallel.ForEach(hits, new ParallelOptions { CancellationToken = stop.Token, MaxDegreeOfParallelism = 4 }, h =>
                        {
                            string p = ix.Path(h.Id); FileSystemInfo fi = ix.IsDir(h.Id) ? new DirectoryInfo(p) : new FileInfo(p);
                            if (!fi.Exists) return;   // gone since DeskBar's last scan
                            pending.Enqueue(new FileEntry(fi) { Score = h.Score }); Interlocked.Increment(ref prog.Found);
                        });
                    }
                    finally { ix.Done(); }
                }
                else
                {
                    if (everywhere) prog.How = "DeskBar's list of files isn't there yet, so this looks through every disk itself and takes a while.";
                    WalkLive(root, m, pending, prog, stop);
                }
            }
            catch (OperationCanceledException) { }
            catch (Exception ex) { prog.Failure = ex is AggregateException a ? a.GetBaseException().Message : ex.Message; }
            finally { prog.Finished = true; }
        });

        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(250) };
        void Pump()
        {
            if (cts.IsCancellationRequested) { timer.Stop(); return; }   // you went somewhere else
            int before = list.Count; while (pending.TryDequeue(out var fe)) if (list.Count < MaxHits + 200) list.Add(fe);
            bool end = prog.Finished && pending.IsEmpty;
            if (list.Count > before)
            {
                var fresh = list.GetRange(before, list.Count - before);
                if (!bound) { bound = true; done(list, null); }   // the first results: the list appears
                else { tab.View?.Refresh(); IconCache.Request(fresh); }
            }
            int n = list.Count; double secs = clock.Elapsed.TotalSeconds;
            if (end)
            {
                timer.Stop(); tab.Searching = false; bool byUser = stop.IsCancellationRequested && !prog.Capped;
                if (!bound) done(list, prog.Failure ?? (byUser ? "Stopped before anything was found" : $"Nothing called “{q}” in {scope}"));
                tab.SearchTitle = prog.Failure != null ? $"The search didn't finish: {prog.Failure}" : byUser ? $"Stopped. {n:N0} found for “{q}” in {scope}" : n == 0 ? $"Nothing found for “{q}” in {scope}" : $"{n:N0} {(n == 1 ? "result" : "results")} for “{q}” in {scope}";
                string took = (secs < 10 ? secs.ToString("0.0#") : ((int)secs).ToString()) + " s", how = prog.How.Length > 0 ? prog.How + " " : "";
                tab.SearchDetail = how + (tab.UsedIndex ? $"Took {took}. " : $"Looked at {prog.Scanned:N0} items in {took}. ") + (prog.Capped ? $"Showing the first {MaxHits:N0}: add a word to narrow it down. " : "") + (n > 1 ? "Best first; click a column to sort." : "");
            }
            else
            {
                tab.SearchTitle = $"Searching {scope} for “{q}”"; tab.SearchDetail = (prog.How.Length > 0 ? prog.How + " " : "") + $"{n:N0} found · {Interlocked.Read(ref prog.Scanned):N0} items looked at · now in {prog.Where}";
            }
            if (tab == _tab) { ShowSearchBar(); if (end) UpdateStatus(); else Status.Text = $"Searching… {n:N0} found"; }
        }
        timer.Tick += (_, _) => Pump(); timer.Start();
    }

    // ---------- the live walk ----------

    private static bool NeverLook(ReadOnlySpan<char> name) => name.Equals("$Recycle.Bin", StringComparison.OrdinalIgnoreCase) || name.Equals("System Volume Information", StringComparison.OrdinalIgnoreCase);
    private static bool IsLink(string dir) { try { return new DirectoryInfo(dir).LinkTarget != null; } catch { return true; } }   // junctions and symlinks list things twice (and loop); OneDrive's online-only folders are not links

    /// Walks the folder (or every disk) on several threads at once: each top-level folder is its own job.
    private static void WalkLive(string root, Matcher m, ConcurrentQueue<FileEntry> pending, Progress prog, CancellationTokenSource stop)
    {
        var starts = new List<string>();
        if (root == ThisPcTag) { foreach (var d in DriveInfo.GetDrives()) { try { if (d.IsReady && (d.DriveType == DriveType.Fixed || d.DriveType == DriveType.Removable)) starts.Add(d.RootDirectory.FullName); } catch { } } }
        else starts.Add(root);
        var units = new List<(string Dir, bool Recurse, string Start)>(); var opt = new EnumerationOptions { IgnoreInaccessible = true, AttributesToSkip = 0 };
        foreach (var s in starts)
        {
            units.Add((s, false, s));   // what sits directly in it
            try { foreach (var sub in Directory.EnumerateDirectories(s, "*", opt)) if (!NeverLook(Path.GetFileName(sub)) && !IsLink(sub)) units.Add((sub, true, s)); } catch { }
        }
        Parallel.ForEach(units, new ParallelOptions { CancellationToken = stop.Token, MaxDegreeOfParallelism = Math.Max(2, Environment.ProcessorCount - 1) }, u => Scan(u.Dir, u.Recurse, u.Start, m, pending, prog, stop));
    }

    private static void Scan(string dir, bool recurse, string start, Matcher m, ConcurrentQueue<FileEntry> pending, Progress prog, CancellationTokenSource stop)
    {
        var ct = stop.Token; int rootLen = start.TrimEnd('\\').Length + 1; long n = 0, lastTick = 0;
        var opt = new EnumerationOptions { RecurseSubdirectories = recurse, IgnoreInaccessible = true, AttributesToSkip = 0, ReturnSpecialDirectories = false, MaxRecursionDepth = 120 };   // hidden and system items included: this is a search
        var items = new FileSystemEnumerable<FileEntry?>(dir, (ref FileSystemEntry e) =>
        {
            int depth = 1; foreach (char c in e.Directory) if (c == '\\') depth++;   // the number of backslashes in the full path: the same measure the index uses, so both rank alike
            var fe = new FileEntry(e.ToFileSystemInfo()); fe.Score = m.Score(fe.Name, fe.IsDir, depth, e.Directory.ToString(), false); return fe;
        }, opt)
        {
            ShouldIncludePredicate = (ref FileSystemEntry e) =>
            {
                if ((++n & 0x1FFF) == 0) { Interlocked.Add(ref prog.Scanned, 0x2000); ct.ThrowIfCancellationRequested(); }
                return m.IsMatch(e.FileName, e.Directory.Length > rootLen ? e.Directory[rootLen..] : default);
            },
            ShouldRecursePredicate = (ref FileSystemEntry e) =>
            {
                if (NeverLook(e.FileName)) return false;
                if ((e.Attributes & FileAttributes.ReparsePoint) != 0 && IsLink(Path.Join(e.Directory, e.FileName))) return false;
                long now = Environment.TickCount64; if (now - lastTick > 150) { lastTick = now; prog.Where = Path.Join(e.Directory, e.FileName); }   // for the banner: where it is now
                return true;
            },
        };
        foreach (var fe in items)
        {
            if (fe == null) continue;
            pending.Enqueue(fe);
            if (Interlocked.Increment(ref prog.Found) >= MaxHits) { prog.Capped = true; stop.Cancel(); return; }
        }
        Interlocked.Add(ref prog.Scanned, n & 0x1FFF);
    }

    /// Search results come from many folders: show where each one lives. The Type column steps aside and Folder takes the rest of the width
    /// (the list can't scroll sideways, so a fixed Folder column was cut off at the edge).
    private void ShowFolderColumn(bool on)
    {
        _folderCol ??= new GridViewColumn { Header = "Folder", Width = 320, DisplayMemberBinding = new Binding(nameof(FileEntry.Folder)) };
        bool has = _grid.Columns.Contains(_folderCol); var type = _grid.Columns[2];
        if (on && !has) { _typeWidth = type.Width; type.Width = 0; _grid.Columns.Add(_folderCol); List.SizeChanged += FitFolderColumn; }   // last, so the sort columns keep their places
        else if (!on && has) { _grid.Columns.Remove(_folderCol); type.Width = _typeWidth; List.SizeChanged -= FitFolderColumn; }
        if (on) FitFolderColumn(null, null);
    }

    private double _typeWidth = 130;
    private void FitFolderColumn(object? s, SizeChangedEventArgs? e)
    {
        if (_folderCol == null || !_grid.Columns.Contains(_folderCol)) return;
        double used = 0; foreach (var c in _grid.Columns) if (c != _folderCol) used += c.ActualWidth > 0 ? c.ActualWidth : c.Width;
        _folderCol.Width = Math.Max(160, List.ActualWidth - used - 34);   // 34: the scroll bar and the row padding
    }
}
