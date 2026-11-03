using System.IO;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;

namespace DeskDrawer;

/// Search: typing in the filter box narrows the open folder at once; Enter looks through every folder inside it as well and opens the results
/// as their own place (Back returns to the folder), with a Folder column saying where each one is.
public partial class MainWindow
{
    private const int MaxHits = 5000;   // shortcut: stops at 5,000 matches; page or stream results if people need more
    private GridViewColumn? _folderCol;

    private void Search_KeyDown(object s, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            string q = Search.Text.Trim(); if (q.Length == 0 || _tab.Current is not { } cur || cur == HomeTag) return;
            Navigate(SearchTag + (IsSearch(cur) ? SearchParts(cur).Root : cur) + "?" + q); List.Focus(); e.Handled = true;
        }
        else if (e.Key == Key.Escape) { Search.Text = ""; List.Focus(); e.Handled = true; }
    }

    private void RunSearch(TabState tab, string path, bool showHidden, CancellationTokenSource cts, Action<List<FileEntry>, string?> done)
    {
        var (root, q) = SearchParts(path);
        Task.Run(() =>
        {
            var list = new List<FileEntry>(); string? error = null; long shown = Environment.TickCount64;
            try
            {
                var opt = new EnumerationOptions { RecurseSubdirectories = true, IgnoreInaccessible = true, MaxRecursionDepth = 64, AttributesToSkip = showHidden ? 0 : FileAttributes.Hidden | FileAttributes.System };
                foreach (var fi in new DirectoryInfo(root).EnumerateFileSystemInfos("*", opt))
                {
                    if (cts.IsCancellationRequested) return;
                    if (fi.Name.Contains(q, StringComparison.OrdinalIgnoreCase)) { list.Add(new FileEntry(fi)); if (list.Count >= MaxHits) break; }
                    if (Environment.TickCount64 - shown > 400)
                    {
                        shown = Environment.TickCount64; int n = list.Count;
                        Dispatcher.BeginInvoke(() => { if (tab == _tab && !cts.IsCancellationRequested) Status.Text = $"Searching… {n:N0} found"; });
                    }
                }
            }
            catch (Exception ex) { error = ex.Message; }
            if (cts.IsCancellationRequested) return;
            if (list.Count == 0) error ??= $"Nothing called “{q}” in here";
            Dispatcher.BeginInvoke(() => { done(list, error); if (list.Count >= MaxHits && tab == _tab) Flash($"Showing the first {MaxHits:N0} matches"); });
        });
    }

    /// Search results come from many folders: show where each one lives.
    private void ShowFolderColumn(bool on)
    {
        _folderCol ??= new GridViewColumn { Header = "Folder", Width = 320, DisplayMemberBinding = new Binding(nameof(FileEntry.Folder)) };
        bool has = _grid.Columns.Contains(_folderCol);
        if (on && !has) _grid.Columns.Add(_folderCol); else if (!on && has) _grid.Columns.Remove(_folderCol);   // last, so the sort columns keep their places
    }
}
