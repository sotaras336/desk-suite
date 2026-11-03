using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;

namespace DeskDrawer;

/// The right-click menu on empty space (View, Sort by, Group by, New ▸ every type Windows knows, Open in Terminal...), the pieces it shares with the
/// command bar, grouping, pinning to Home, and ZIP files.
public partial class MainWindow
{
    private string _groupBy = "";
    private TreeViewItem? _quick;
    private readonly HashSet<string> _pinned = new(StringComparer.OrdinalIgnoreCase);

    private void ShowBackgroundMenu()
    {
        if (IsVirtual(_tab.Current) || _tab.Current is not { } dir) return;
        var m = new ContextMenu(); _lastMenu = m; var it = m.Items;
        var view = MI("View", "", () => { }); AddViewModeItems(view.Items); it.Add(view);
        var sort = MI("Sort by", "", () => { }); AddSortItems(sort.Items); it.Add(sort);
        var group = MI("Group by", "", () => { }); AddGroupItems(group.Items); it.Add(group);
        it.Add(new Separator());
        if (_undo.Count > 0) it.Add(MI("Undo " + _undo[^1].Label, "", Undo, "Ctrl+Z"));
        it.Add(MI("Refresh", "", ReloadActive, "F5"));
        it.Add(MI("Paste", "", () => Paste(dir), "Ctrl+V", enabled: ShellOps.ReadClipboard() != null));
        it.Add(new Separator());
        var nw = MI("New", "", () => { }); AddNewItems(nw.Items); it.Add(nw);
        it.Add(new Separator());
        bool pinned = _pinned.Contains(dir);
        it.Add(MI(pinned ? "Unpin from Home" : "Pin to Home", "", () => TogglePin(dir, !pinned)));
        it.Add(MI("Open in Terminal", "", () => OpenTerminal(dir)));
        it.Add(MI("Copy folder path", "", () => { try { Clipboard.SetText("\"" + dir + "\""); } catch { } }));
        it.Add(new Separator());
        it.Add(MI("Properties", "", ShowProperties, "Alt+Enter"));
        m.PlacementTarget = List; m.Placement = System.Windows.Controls.Primitives.PlacementMode.MousePoint; m.IsOpen = true;
    }

    /// Extra entries on a selection's menu: pin a folder to Home, open a terminal there, zip / unzip.
    private void AddItemExtras(ItemCollection items, List<FileEntry> sel)
    {
        items.Add(new Separator());
        if (sel.Count == 1 && sel[0].IsDir)
        {
            string p = sel[0].FullPath; bool pinned = _pinned.Contains(p);
            items.Add(MI(pinned ? "Unpin from Home" : "Pin to Home", "", () => TogglePin(p, !pinned)));
            items.Add(MI("Open in Terminal", "", () => OpenTerminal(p)));
        }
        var paths = sel.Select(x => x.FullPath).ToList();
        items.Add(MI("Compress to ZIP file", "", () => Zip(paths)));
        if (sel.Count == 1 && sel[0].Ext == ".zip") items.Add(MI("Extract all", "", () => Unzip(paths[0])));
    }

    // ---------- shared menu pieces (right-click menu and command bar) ----------

    private void AddViewModeItems(ItemCollection items)
    {
        foreach (var v in Views)
        {
            bool on = _viewMode == v.Mode && (v.Mode != "Icons" || _iconSize == v.Size);
            var (mode, size) = (v.Mode, v.Size); items.Add(MI(v.Label, on ? Check : "", () => SetViewMode(mode, size), v.Keys));
        }
    }

    private void AddSortItems(ItemCollection items)
    {
        foreach (var k in ColumnNames) { var key = k; items.Add(MI(k, _sortKey == k ? Check : "", () => SetSort(key, _sortAsc))); }
        items.Add(new Separator());
        items.Add(MI("Ascending", _sortAsc ? Check : "", () => SetSort(_sortKey, true)));
        items.Add(MI("Descending", !_sortAsc ? Check : "", () => SetSort(_sortKey, false)));
    }

    private void AddGroupItems(ItemCollection items)
    {
        foreach (var (label, key) in new[] { ("None", ""), ("Name", "Name"), ("Date modified", "Date modified"), ("Type", "Type"), ("Size", "Size") })
        { var k = key; items.Add(MI(label, _groupBy == key ? Check : "", () => SetGroup(k))); }
    }

    /// Folder, Shortcut, then every "New" type this PC has (Word, Excel, ZIP folder, bitmap...), each with its own icon.
    private void AddNewItems(ItemCollection items)
    {
        items.Add(MI("Folder", "", () => NewItem(true), "Ctrl+Shift+N"));
        items.Add(MI("Shortcut", "", NewShortcut));
        items.Add(new Separator());
        var kinds = ShellNew.Kinds;
        if (kinds == null) { items.Add(MI("Text Document", "", () => NewItem(false))); return; }   // the list is still being read (first seconds after start)
        foreach (var k in kinds)
        {
            var kind = k; var mi = MI(k.Name, "", () => CreateAndRename(d => ShellNew.Create(kind, d)));
            if (k.Icon != null) mi.Icon = new Image { Source = k.Icon, Width = 16, Height = 16 };
            items.Add(mi);
        }
    }

    private void CreateAndRename(Func<string, string> make)
    {
        if (_tab.Current == null || IsVirtual(_tab.Current)) return;
        try
        {
            string p = make(_tab.Current); UndoNew(p);
            _tab.SelectAfterLoad = new HashSet<string>(new[] { p }, StringComparer.OrdinalIgnoreCase); _tab.RenameAfterLoad = p; LoadTab(_tab);
        }
        catch (Exception ex) { MessageBox.Show(this, ex.Message, "DeskDrawer", MessageBoxButton.OK, MessageBoxImage.Warning); }
    }

    /// Windows' own "Create shortcut" wizard, started the way File Explorer starts it: on an empty .lnk it then fills in.
    private void NewShortcut()
    {
        if (_tab.Current == null || IsVirtual(_tab.Current)) return;
        try
        {
            string p = ShellOps.Unique(_tab.Current, "New shortcut", ".lnk"); File.WriteAllBytes(p, Array.Empty<byte>());
            Process.Start(new ProcessStartInfo("rundll32.exe", "appwiz.cpl,NewLinkHere " + p) { UseShellExecute = false });
        }
        catch (Exception ex) { MessageBox.Show(this, ex.Message, "DeskDrawer", MessageBoxButton.OK, MessageBoxImage.Warning); }
    }

    // ---------- group by ----------

    private void SetGroup(string key)
    {
        _groupBy = key; ApplyGrouping(_tab); RememberView();
    }

    private void ApplyGrouping(TabState t)
    {
        if (t.View == null || t.Entries == null) return;
        t.ViewSig = ViewSig();
        if (_groupBy.Length > 0) foreach (var e in t.Entries) (e.Group, e.GroupRank) = GroupOf(e, _groupBy);
        using (t.View.DeferRefresh())
        {
            t.View.GroupDescriptions.Clear();
            if (_groupBy.Length > 0) t.View.GroupDescriptions.Add(new PropertyGroupDescription(nameof(FileEntry.Group)));
            ApplySort(t.View);
        }
    }

    /// The heading an item goes under, and where that heading sits (Today before Yesterday, Folders first...).
    private static (string, int) GroupOf(FileEntry e, string key)
    {
        switch (key)
        {
            case "Name":
                char c = e.Name.Length > 0 ? char.ToUpper(e.Name[0]) : '#';
                return char.IsLetter(c) ? (c.ToString(), 1) : char.IsDigit(c) ? ("0–9", 0) : ("#", 0);
            case "Type":
                return e.IsDir ? ("Folders", 0) : (e.Ext.Length > 1 ? e.Ext.TrimStart('.').ToUpperInvariant() + " files" : "Other files", 1);
            case "Size":
                if (e.IsDir) return ("Folders", 0);
                long b = e.Size;
                return b == 0 ? ("Empty", 1) : b < 16 << 10 ? ("Tiny (under 16 KB)", 2) : b < 1 << 20 ? ("Small (16 KB – 1 MB)", 3)
                     : b < 128 << 20 ? ("Medium (1 – 128 MB)", 4) : b < 1L << 30 ? ("Large (128 MB – 1 GB)", 5) : ("Huge (over 1 GB)", 6);
            default:   // Date modified
                var d = e.Modified.Date; var today = DateTime.Today; var week = today.AddDays(-(((int)today.DayOfWeek + 6) % 7)); var month = new DateTime(today.Year, today.Month, 1);
                if (d >= today) return ("Today", 0);
                if (d == today.AddDays(-1)) return ("Yesterday", 1);
                if (d >= week) return ("Earlier this week", 2);
                if (d >= week.AddDays(-7)) return ("Last week", 3);
                if (d >= month) return ("Earlier this month", 4);
                if (d >= month.AddMonths(-1)) return ("Last month", 5);
                return d.Year == today.Year ? ("Earlier this year", 6) : ("A long time ago", 7);
        }
    }

    // ---------- pins (shared with File Explorer's Quick access) ----------

    private void RefreshPins(bool reloadHome = false)
    {
        Pinned.Read(list =>
        {
            _pinned.Clear(); foreach (var (_, p) in list) _pinned.Add(p);
            if (list.Count > 0 && _quick != null)
            {
                _quick.Items.Clear();
                foreach (var (name, path) in list) { var n = Node(name, path, "", lazy: true); _quick.Items.Add(n); ExactIcon(n); }
            }
            if (reloadHome) foreach (var t in _tabs) if (t.Current is HomeTag or QuickTag) LoadTab(t);
        });
    }

    private void TogglePin(string path, bool pin) => Pinned.SetPinned(path, pin, () => { RefreshPins(reloadHome: true); Flash(pin ? "Pinned to Home" : "Unpinned from Home"); });

    private void OpenTerminal(string dir)
    {
        try { Process.Start(new ProcessStartInfo("wt.exe", $"-d \"{dir.TrimEnd('\\')}\\.\"") { UseShellExecute = true }); }
        catch
        {
            try { Process.Start(new ProcessStartInfo("powershell.exe") { WorkingDirectory = dir, UseShellExecute = true }); }
            catch (Exception ex) { MessageBox.Show(this, ex.Message, "DeskDrawer", MessageBoxButton.OK, MessageBoxImage.Warning); }
        }
    }

    // ---------- ZIP ----------

    private void Zip(List<string> paths)
    {
        if (paths.Count == 0 || Path.GetDirectoryName(paths[0].TrimEnd('\\')) is not { } dir) return;
        string stem = paths.Count == 1 ? Path.GetFileNameWithoutExtension(paths[0].TrimEnd('\\')) : Path.GetFileName(dir.TrimEnd('\\'));
        string target = ShellOps.Unique(dir, string.IsNullOrEmpty(stem) ? "Archive" : stem, ".zip");
        Status.Text = "Compressing…";
        RunZipJob(() =>
        {
            using var z = ZipFile.Open(target, ZipArchiveMode.Create);
            foreach (var p in paths)
            {
                if (!Directory.Exists(p)) { z.CreateEntryFromFile(p, Path.GetFileName(p), CompressionLevel.Optimal); continue; }
                string baseDir = Path.GetDirectoryName(p.TrimEnd('\\'))!;
                foreach (var f in Directory.EnumerateFiles(p, "*", SearchOption.AllDirectories)) z.CreateEntryFromFile(f, Path.GetRelativePath(baseDir, f).Replace('\\', '/'), CompressionLevel.Optimal);
                foreach (var d in Directory.EnumerateDirectories(p, "*", SearchOption.AllDirectories).Prepend(p))   // keep empty folders too
                    if (!Directory.EnumerateFileSystemEntries(d).Any()) z.CreateEntry(Path.GetRelativePath(baseDir, d).Replace('\\', '/') + "/");
            }
        }, dir, target, "Compressed to ");
    }

    private void Unzip(string zip)
    {
        if (Path.GetDirectoryName(zip) is not { } dir) return;
        string target = ShellOps.Unique(dir, Path.GetFileNameWithoutExtension(zip), "");
        Status.Text = "Extracting…";
        RunZipJob(() => ZipFile.ExtractToDirectory(zip, target), dir, target, "Extracted to ");
    }

    /// Runs a zip / unzip off the UI thread; on failure removes the half-made result, on success shows and selects it.
    private void RunZipJob(Action job, string dir, string target, string done)
    {
        Task.Run(job).ContinueWith(t => Dispatcher.BeginInvoke(() =>
        {
            if (t.Exception != null)
            {
                try { if (File.Exists(target)) File.Delete(target); else if (Directory.Exists(target)) Directory.Delete(target, true); } catch { }
                MessageBox.Show(this, t.Exception.InnerException?.Message ?? "It didn't work.", "DeskDrawer", MessageBoxButton.OK, MessageBoxImage.Warning); UpdateStatus(); return;
            }
            foreach (var tab in _tabs)
                if (string.Equals(tab.Current, dir, StringComparison.OrdinalIgnoreCase)) { tab.SelectAfterLoad = new HashSet<string>(new[] { target }, StringComparer.OrdinalIgnoreCase); LoadTab(tab); }
            Flash(done + Path.GetFileName(target));
        }));
    }
}
