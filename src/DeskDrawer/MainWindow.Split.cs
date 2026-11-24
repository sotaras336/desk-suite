using System.IO;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Threading;

namespace DeskDrawer;

/// Split view (Ctrl+\): two folders next to each other in one window, each one a tab, ready to drag files across.
/// The full list (every view, menu, key and the toolbar) always shows the side you are working on; the other side is a lighter list (PeerList)
/// of the tab marked with a line in the tab strip. Click the other side's top bar, press F6, or use a key there (Delete, F2, Ctrl+C...) and it becomes
/// the side you work on: the full list moves over to it, so each folder stays on its own side. Picker windows (Open / Save) never split.
/// The older Side by side (Ctrl+Shift+S, MainWindow.Side.cs) still opens two separate windows.
public partial class MainWindow
{
    private bool _splitOn, _activeRight;
    private TabState? _partner;
    private FileSystemWatcher? _peerWatch;
    private readonly DispatcherTimer _peerReload = new() { Interval = TimeSpan.FromMilliseconds(400) };
    private Point _peerPress; private FileEntry? _peerGrab; private bool _peerHooked;

    internal bool SplitOn => _splitOn && _partner != null;

    /// Turns split view on (with that tab on the other side, or the neighbouring tab, or a new tab of the same folder) or off.
    internal void SetSplit(bool on, TabState? other = null)
    {
        if (_pick != null || _tab == null || !_tabs.Contains(_tab)) on = false;   // a picker answers one dialog from one list
        HookPeer();
        if (on)
        {
            if (other == null || other == _tab || !_tabs.Contains(other))
            {
                int p = TabMath.PartnerFor(_tabs.Count, _tabs.IndexOf(_tab));
                if (p >= 0) other = _tabs[p];
                else
                {
                    var t = new TabState(); _tabs.Insert(_tabs.IndexOf(_tab) + 1, t);
                    NavigateTab(t, _tab.Current ?? Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), push: false); other = t;
                }
            }
            if (!_splitOn) _activeRight = false;   // a fresh split: the folder you were in stays on the left
            _partner = other; _splitOn = true;
        }
        else { _splitOn = false; _partner = null; }
        foreach (var x in _tabs) x.IsPartner = _splitOn && x == _partner;
        LayoutPanes(); BindPeer();
    }

    private void ToggleSplit() { SetSplit(!SplitOn); Flash(SplitOn ? "Split view: F6 switches sides, drag files across" : "Split view off"); }

    /// Shows a folder on the other side: in the split if it is on, else in a new tab that opens next to this one.
    private void OpenOnOtherSide(string path)
    {
        if (_pick != null) { Navigate(path); return; }
        if (SplitOn) { NavigateTab(_partner!, path, push: true); return; }
        var t = new TabState(); _tabs.Insert(_tabs.IndexOf(_tab) + 1, t); NavigateTab(t, path, push: false); SetSplit(true, t);
    }

    /// The full list goes to the side the active tab is on; the other side shows the partner tab.
    private void LayoutPanes()
    {
        bool on = SplitOn;
        if (!on && Grid.GetColumn(MainPane) == 0 && PeerPane.Visibility != Visibility.Visible && PaneBCol.Width.Value == 0) return;   // already one side: don't reset widths the window just restored
        PeerPane.Visibility = PaneSplit.Visibility = ActiveMark.Visibility = on ? Visibility.Visible : Visibility.Collapsed;
        int main = on && _activeRight ? 2 : 0;
        Grid.SetColumn(MainPane, main); Grid.SetColumn(PeerPane, 2 - main);
        double share = TabMath.Share(_settings.SplitShare, 1 - _settings.SplitShare);
        if (on && PaneACol.ActualWidth > 0 && PaneBCol.ActualWidth > 0) share = TabMath.Share(PaneACol.ActualWidth, PaneBCol.ActualWidth);   // keep where the divider was dragged to
        PaneACol.Width = new GridLength(on ? share : 1, GridUnitType.Star); PaneBCol.Width = new GridLength(on ? 1 - share : 0, GridUnitType.Star);
        PaneACol.MinWidth = PaneBCol.MinWidth = on ? 200 : 0; PaneSplitCol.Width = new GridLength(on ? 6 : 0);
        AutomationProperties.SetName(List, on ? "Files, the side you are working on" : "Files");
    }

    /// Fills the other side from the partner tab (again whenever that tab loads or changes folder).
    private void BindPeer()
    {
        var t = SplitOn ? _partner : null;
        ResetPeerWatcher();
        if (t == null) { PeerList.ItemsSource = null; return; }
        PeerList.ItemsSource = t.View;
        PeerTitle.Text = t.Title; PeerPath.Text = IsVirtual(t.Current) ? "" : t.Current ?? "";
        PeerIcon.Source = t.Icon; PeerGlyph.Text = t.Glyph; PeerGlyph.Visibility = t.Icon == null ? Visibility.Visible : Visibility.Collapsed;
        PeerBackBtn.IsEnabled = t.Back.Count > 0;
        PeerUpBtn.IsEnabled = t.Current != null && !IsVirtual(t.Current) && Directory.GetParent(t.Current) != null;
        string? msg = t.Loading ? "Loading…" : t.Error ?? (t.Entries is { Count: 0 } ? (t.Current is RecentTag or ThisPcTag ? t.Title + " shows on the side you work on.\nClick the bar above to work on this side." : "This folder is empty") : null);
        PeerMessage.Text = msg ?? ""; PeerMessage.Visibility = msg == null ? Visibility.Collapsed : Visibility.Visible;
        AutomationProperties.SetName(PeerList, "Files on the other side: " + t.Title);
        AutomationProperties.SetName(PeerHead, "Work on the other side: " + t.Title);
        if (t.Entries != null && !t.IconsRequested) { t.IconsRequested = true; IconCache.Request(t.Entries); }
    }

    /// A tab loaded, changed folder or was pruned: whichever side shows it is redrawn.
    private void Shown(TabState tab)
    {
        if (tab == _tab) BindTab();
        else if (SplitOn && tab == _partner) BindPeer();
    }

    /// The other side becomes the side you work on, keeping what was selected there.
    internal void WorkOnPeer()
    {
        if (!SplitOn) return;
        var t = _partner!;
        foreach (var en in PeerList.SelectedItems.Cast<FileEntry>()) t.SelectAfterLoad.Add(en.FullPath);
        Activate(t);   // Activate sees the partner and swaps the sides' roles
        if (!InHome) List.Focus();
        Dispatcher.BeginInvoke(() => { if (List.SelectedItem != null && List.ItemContainerGenerator.ContainerFromItem(List.SelectedItem) is ListViewItem li) li.Focus(); }, DispatcherPriority.Loaded);
    }

    /// The two folders change places (the side you work on stays the one you work on).
    internal void SwapSides() { if (!SplitOn) return; _activeRight = !_activeRight; LayoutPanes(); }

    private void HookPeer()
    {
        if (_peerHooked) return; _peerHooked = true;
        _peerReload.Tick += (_, _) => { _peerReload.Stop(); RefreshPeer(); };
        PeerList.Loaded += (_, _) => { if (FindScroll(PeerList) is { } sv) Wheel.Attach(sv); };   // a touchpad scrolls by its own movement here too
        if (FindScroll(PeerList) is { } sv0) Wheel.Attach(sv0);
        PeerHead.Activate = WorkOnPeer;   // Enter or Space on the bar, and screen readers can press it
        PaneSplit.DragCompleted += (_, _) => _settings.SplitShare = TabMath.Share(PaneACol.ActualWidth, PaneBCol.ActualWidth);
    }

    // ---------- keeping the other side current ----------

    private void ResetPeerWatcher()
    {
        var old = _peerWatch; _peerWatch = null; old?.Dispose(); _peerReload.Stop();
        string? path = SplitOn ? _partner!.Current : null;
        if (path == null || IsVirtual(path)) return;
        try
        {
            var w = new FileSystemWatcher(path) { NotifyFilter = NotifyFilters.FileName | NotifyFilters.DirectoryName | NotifyFilters.LastWrite | NotifyFilters.Size, InternalBufferSize = 64 * 1024 };
            FileSystemEventHandler h = (_, _) => Dispatcher.BeginInvoke(() => { if (_peerWatch == w && !_peerReload.IsEnabled) _peerReload.Start(); });
            w.Created += h; w.Deleted += h; w.Changed += h; w.Renamed += (_, _) => Dispatcher.BeginInvoke(() => { if (_peerWatch == w && !_peerReload.IsEnabled) _peerReload.Start(); });
            w.Error += (_, _) => Dispatcher.BeginInvoke(() => { if (_peerWatch == w) { ResetPeerWatcher(); _peerReload.Start(); } });
            w.EnableRaisingEvents = true; _peerWatch = w;
        }
        catch { }
    }

    /// Reads the other side's folder again and merges the changes in (rows, selection and scroll position stay), like RefreshActive does for this side.
    private void RefreshPeer()
    {
        if (!SplitOn) return;
        var tab = _partner!; string? path = tab.Current;
        if (path == null || IsVirtual(path) || tab.Loading || tab.Entries == null || tab.View == null) return;
        bool showHidden = _settings.ShowHidden; var cts = tab.Cts;
        Task.Run(() =>
        {
            List<FileEntry>? fresh = new();
            try { foreach (var fi in new DirectoryInfo(path).EnumerateFileSystemInfos()) { if (!showHidden && (fi.Attributes & (FileAttributes.Hidden | FileAttributes.System)) != 0) continue; fresh.Add(new FileEntry(fi)); } }
            catch { fresh = null; }
            Dispatcher.BeginInvoke(() =>
            {
                bool same = tab == _partner && tab.Cts == cts && cts is { IsCancellationRequested: false } && tab.Current == path && !tab.Loading && tab.Entries != null && tab.View != null;
                if (same && fresh == null) LoadTab(tab);
                else if (same) { MergeListing(tab, fresh!); BindPeer(); }
            });
        });
    }

    // ---------- the other side's mouse, touch and keys ----------

    private static FileEntry? EntryIn(ItemsControl list, object? src) =>
        AsVisual(src as DependencyObject) is { } d && ItemsControl.ContainerFromElement(list, d) is ListViewItem lvi ? lvi.Content as FileEntry : null;
    private static bool OnScrollBar(object? src) { var d = AsVisual(src as DependencyObject); while (d != null && d is not ScrollBar && d is not ListViewItem) d = System.Windows.Media.VisualTreeHelper.GetParent(d); return d is ScrollBar; }

    private void PeerHead_Up(object s, MouseButtonEventArgs e) { if (e.OriginalSource is DependencyObject d && FindParent<ButtonBase>(d) != null) return; WorkOnPeer(); e.Handled = true; }
    private static T? FindParent<T>(DependencyObject? d) where T : DependencyObject { d = AsVisual(d); while (d != null && d is not T) d = System.Windows.Media.VisualTreeHelper.GetParent(d); return d as T; }

    private void PeerBack_Click(object s, RoutedEventArgs e)
    {
        if (!SplitOn || _partner!.Back.Count == 0 || _partner.Current == null) return;
        _partner.Fwd.Push(_partner.Current); NavigateTab(_partner, _partner.Back.Pop(), push: false);
    }
    private void PeerUp_Click(object s, RoutedEventArgs e) => PeerUp();
    private void PeerUp() { if (SplitOn && _partner!.Current is { } c && !IsVirtual(c) && Directory.GetParent(c) is { } p) NavigateTab(_partner, p.FullName, push: true); }
    private void PeerSwap_Click(object s, RoutedEventArgs e) => SwapSides();
    private void PeerClose_Click(object s, RoutedEventArgs e) => SetSplit(false);

    /// Double-click (or Enter): a folder opens on the other side itself, a file opens in its app.
    private void PeerList_DoubleClick(object s, MouseButtonEventArgs e) { if (e.ChangedButton == MouseButton.Left && EntryIn(PeerList, e.OriginalSource) is { } en) PeerOpen(en); }
    private void PeerOpen(FileEntry en)
    {
        if (!SplitOn) return;
        string? dir = en.IsDir ? en.FullPath : FolderBehind(en.FullPath);
        if (dir != null) NavigateTab(_partner!, dir, push: true); else OpenFile(en.FullPath);
    }

    private void PeerList_KeyDown(object s, KeyEventArgs e)
    {
        if (Keyboard.Modifiers != ModifierKeys.None) return;
        if (e.Key == Key.Enter && PeerList.SelectedItem is FileEntry en) { PeerOpen(en); e.Handled = true; }
        else if (e.Key == Key.Back) { PeerUp(); e.Handled = true; }
    }

    private void PeerList_PreviewDown(object s, MouseButtonEventArgs e) { _peerPress = e.GetPosition(null); _peerGrab = EntryIn(PeerList, e.OriginalSource); }

    /// A press on empty space on the other side (not on a file, not on the scroll bar) makes it the side you work on.
    private void PeerList_PreviewUp(object s, MouseButtonEventArgs e)
    {
        bool blank = _peerGrab == null && EntryIn(PeerList, e.OriginalSource) == null && !OnScrollBar(e.OriginalSource);
        _peerGrab = null;
        if (blank) { PeerList.UnselectAll(); Dispatcher.BeginInvoke(WorkOnPeer); }
    }

    /// Dragging files out of the other side: into this side, a tab, the sidebar or another app.
    private void PeerList_PreviewMove(object s, MouseEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed || _peerGrab == null) return;
        var p = e.GetPosition(null);
        if (Math.Abs(p.X - _peerPress.X) < SystemParameters.MinimumHorizontalDragDistance && Math.Abs(p.Y - _peerPress.Y) < SystemParameters.MinimumVerticalDragDistance) return;
        if (!PeerList.SelectedItems.Contains(_peerGrab)) { PeerList.SelectedItems.Clear(); PeerList.SelectedItems.Add(_peerGrab); }
        var paths = PeerList.SelectedItems.Cast<FileEntry>().Select(x => x.FullPath).ToArray(); _peerGrab = null;
        PeerList.ReleaseMouseCapture(); Mouse.Capture(null);
        if (!ShellMenu.Drag(paths, Hwnd, 1 | 2 | 4, out _))
            DragDrop.DoDragDrop(PeerList, new DataObject(DataFormats.FileDrop, paths), DragDropEffects.Copy | DragDropEffects.Move | DragDropEffects.Link);
        e.Handled = true;
    }

    /// Right-click on the other side: it becomes the side you work on, then the usual menu.
    private void PeerList_RightUp(object s, MouseButtonEventArgs e)
    {
        var en = EntryIn(PeerList, e.OriginalSource);
        if (en == null) PeerList.UnselectAll(); else if (!PeerList.SelectedItems.Contains(en)) { PeerList.SelectedItems.Clear(); PeerList.SelectedItems.Add(en); }
        _menuScreen = PointToScreen(e.GetPosition(this));
        WorkOnPeer(); if (en == null) List.UnselectAll();
        ShowMenu(); e.Handled = true;
    }

    /// Where a drop on the other side goes: the folder under the pointer, else the other side's own folder. null = nowhere (Home, search results).
    private string? PeerDropTarget(DragEventArgs e) =>
        !SplitOn ? null : EntryIn(PeerList, e.OriginalSource) is { IsDir: true } d ? d.FullPath : IsVirtual(_partner!.Current) ? null : _partner.Current;

    private void Peer_DragOver(object s, DragEventArgs e)
    {
        e.Effects = PeerDropTarget(e) is { } dest && e.Data.GetData(DataFormats.FileDrop) is string[] { Length: > 0 } src ? EffectFor(src, dest, e.KeyStates) : DragDropEffects.None;
        e.Handled = true;
    }
    private void Peer_Drop(object s, DragEventArgs e) { if (PeerDropTarget(e) is { } dest) DoDrop(e, dest); e.Handled = true; }

    // ---------- the tab strip: keyboard and screen readers ----------

    /// Tab reaches each tab, Enter or Space opens it, and a screen reader hears its name and "open" / "on the other side" (A11y.TapBorder).
    private void Tab_Loaded(object s, RoutedEventArgs e) { if (s is TapBorder tb && tb.Activate == null) tb.Activate = () => { if (tb.DataContext is TabState t && _tabs.Contains(t)) Activate(t); }; }

    /// On a focused tab: Delete closes it, the menu key opens its menu.
    private void Tab_KeyDown(object s, KeyEventArgs e)
    {
        if (TabOf(s) is not { } t || Keyboard.Modifiers != ModifierKeys.None) return;
        if (e.Key == Key.Delete) { CloseTab(t); e.Handled = true; }
        else if (e.Key == Key.Apps) { Tab_RightUp(s, new MouseButtonEventArgs(Mouse.PrimaryDevice, Environment.TickCount, MouseButton.Right) { RoutedEvent = UIElement.MouseRightButtonUpEvent }); e.Handled = true; }
    }

    /// The tab and split view keys (TabMath.KeyFor). True = done.
    private bool TabKey(Key key, bool ctrl, bool shift, bool alt)
    {
        var what = TabMath.KeyFor(key, ctrl, shift, alt);
        if (what == null || _pick != null) return false;
        int i = _tabs.IndexOf(_tab), n = _tabs.Count;
        switch (what)
        {
            case "split": ToggleSplit(); return true;
            case "other side": if (!SplitOn) return false; WorkOnPeer(); return true;
            case "next": Activate(_tabs[TabMath.Cycle(i, 1, n)]); return true;
            case "previous": Activate(_tabs[TabMath.Cycle(i, -1, n)]); return true;
            case "move left": case "move right":
                int to = TabMath.Step(i, what == "move left" ? -1 : 1, n); if (to != i && i >= 0) _tabs.Move(i, to); return true;
            default:
                int k = TabMath.ByNumber(what[^1] - '0', n); if (k >= 0) Activate(_tabs[k]); return true;
        }
    }

    // ---------- remembered between sessions ----------

    /// Saved with the tabs: which tab was on the other side, which side you were working on, and where the divider was.
    private void SaveSplit()
    {
        _settings.SplitWith = SplitOn ? _tabs.IndexOf(_partner!) : -1;
        _settings.SplitRight = SplitOn && _activeRight;
        if (SplitOn && PaneACol.ActualWidth > 0 && PaneBCol.ActualWidth > 0) _settings.SplitShare = TabMath.Share(PaneACol.ActualWidth, PaneBCol.ActualWidth);   // the left side's share of the width
    }

    /// The split from last time, once the saved tabs are back. partner = its tab's number among them (-1: off or gone).
    private void RestoreSplit(int partner)
    {
        int p = TabMath.RestorePartner(partner, _tabs.IndexOf(_tab), _tabs.Count);
        if (p < 0) { if (SplitOn) SetSplit(false); return; }
        SetSplit(true, _tabs[p]); _activeRight = _settings.SplitRight; LayoutPanes();
    }
}
