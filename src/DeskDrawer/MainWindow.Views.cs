namespace DeskDrawer;

/// Per-folder views, like File Explorer: each folder keeps the sort, grouping and layout you gave it. Folders you never changed use the defaults,
/// except Downloads, which starts grouped by date, newest first ("Today", "Yesterday", "Last week"...).
public partial class MainWindow
{
    private bool _applyingView;

    private static string ViewKey(string p) => p.TrimEnd('\\').ToLowerInvariant();

    private FolderView ViewFor(string? path)
    {
        FolderView v;
        if (path != null && !IsVirtual(path) && _settings.Folders.TryGetValue(ViewKey(path), out var saved)) v = saved;
        else if (path != null && ViewKey(path) == ViewKey(KnownDownloads())) v = new FolderView { Sort = "Date modified", Asc = false, Group = "Date modified", Mode = _settings.ViewMode, Icon = _settings.IconSize };
        else v = new FolderView { Sort = _settings.SortKey, Asc = _settings.SortAscending, Group = _settings.GroupBy, Mode = _settings.ViewMode, Icon = _settings.IconSize };
        return App.ForcedView == null ? v : new FolderView { Sort = v.Sort, Asc = v.Asc, Group = v.Group, Mode = App.ForcedView, Icon = v.Icon };
    }

    private string ViewSig() => $"{_sortKey}|{_sortAsc}|{_groupBy}";

    /// Puts the open folder's own view in place (called whenever a tab is shown or navigates).
    private void ApplyFolderView()
    {
        if (_tab == null) return;
        var v = ViewFor(_tab.Current);
        _applyingView = true;
        _sortKey = v.Sort; _sortAsc = v.Asc; _groupBy = v.Group; UpdateSortHeaders();
        if (_viewMode != v.Mode || _iconSize != v.Icon) SetViewMode(v.Mode, v.Icon);
        _applyingView = false;
        if (_tab.View != null && _tab.ViewSig != ViewSig()) ApplyGrouping(_tab);   // arranged for another folder's view (loaded in the background)
    }

    /// Saves what you just chose (sort, group, layout) for the open folder only.
    private void RememberView()
    {
        if (_applyingView || _tab?.Current is not { } p || IsVirtual(p)) return;
        _settings.Folders[ViewKey(p)] = new FolderView { Sort = _sortKey, Asc = _sortAsc, Group = _groupBy, Mode = _viewMode, Icon = _iconSize };
    }
}
