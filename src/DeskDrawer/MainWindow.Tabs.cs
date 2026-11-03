using System.IO;
using System.Windows.Controls;
using System.Windows.Input;

namespace DeskDrawer;

/// Tab extras: right-click menu (duplicate, close others / to the right, reopen closed), Ctrl+Shift+T, and dragging a tab sideways to reorder.
public partial class MainWindow
{
    private readonly Stack<string> _closed = new();
    private TabState? _dragTab;

    private void ReopenClosed()
    {
        while (_closed.Count > 0) { var p = _closed.Pop(); if (IsVirtual(p) || Directory.Exists(p)) { NewTab(p); return; } }   // skip folders deleted since
        Flash("No closed tabs to reopen");
    }

    private void Tab_RightUp(object s, MouseButtonEventArgs e)
    {
        if (TabOf(s) is not { } t) return;
        int i = _tabs.IndexOf(t);
        var m = new ContextMenu();
        m.Items.Add(MI("New tab", "", () => NewTab(t.Current), "Ctrl+T"));
        m.Items.Add(MI("Duplicate tab", "", () => { if (t.Current != null) { AddTab(t.Current); _tabs.Move(_tabs.Count - 1, _tabs.IndexOf(t) + 1); Activate(_tabs[_tabs.IndexOf(t) + 1]); } }));
        m.Items.Add(MI("Open in new window", "", () => { Activate(t); OpenNewWindow(); }, "Ctrl+N"));
        m.Items.Add(new Separator());
        m.Items.Add(MI("Close tab", "", () => CloseTab(t), "Ctrl+W"));
        m.Items.Add(MI("Close other tabs", "", () => { foreach (var o in _tabs.Where(x => x != t).ToList()) CloseTab(o); }, enabled: _tabs.Count > 1));
        m.Items.Add(MI("Close tabs to the right", "", () => { foreach (var o in _tabs.Skip(_tabs.IndexOf(t) + 1).ToList()) CloseTab(o); }, enabled: i < _tabs.Count - 1));
        m.Items.Add(new Separator());
        m.Items.Add(MI("Reopen closed tab", "", ReopenClosed, "Ctrl+Shift+T", enabled: _closed.Count > 0));
        m.IsOpen = true; e.Handled = true;
    }

    /// Right-click on the sidebar: the same useful things as on a folder in the list, plus pin / unpin.
    private void Tree_RightUp(object s, MouseButtonEventArgs e)
    {
        if (TreeItemOf(e.OriginalSource) is not { Tag: string path } || path == Placeholder) return;
        _menuScreen = PointToScreen(e.GetPosition(this));
        var m = new ContextMenu();
        m.Items.Add(MI("Open", "", () => Navigate(path), bold: true));
        m.Items.Add(MI("Open in new tab", "", () => NewTab(path)));
        m.Items.Add(MI("Open in new window", "", () => OpenNewWindow(path)));
        if (!IsVirtual(path))
        {
            bool pinned = _pinned.Contains(path);
            m.Items.Add(new Separator());
            m.Items.Add(MI(pinned ? "Unpin from Home" : "Pin to Home", "", () => TogglePin(path, !pinned)));
            m.Items.Add(MI("Open in Terminal", "", () => OpenTerminal(path)));
            m.Items.Add(MI("Copy as path", "", () => { try { System.Windows.Clipboard.SetText("\"" + path + "\""); } catch { } }));
            m.Items.Add(MI("Properties", "", () => ShellOps.Properties(path, Hwnd)));
            m.Items.Add(new Separator());
            m.Items.Add(MI("Show more options", "", () => Dispatcher.BeginInvoke(() => ShowMoreOptions(new List<string> { path }))));
        }
        m.IsOpen = true; e.Handled = true;
    }

    /// Holding a tab and sliding over its neighbour swaps them, like a browser.
    private void Tab_Move(object s, MouseEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed) { _dragTab = null; return; }
        if (_dragTab == null || TabOf(s) is not { } over || over == _dragTab) return;
        int from = _tabs.IndexOf(_dragTab), to = _tabs.IndexOf(over);
        if (from >= 0 && to >= 0) _tabs.Move(from, to);
    }
}
