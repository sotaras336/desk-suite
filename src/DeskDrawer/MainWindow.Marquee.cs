using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using System.Windows.Threading;

namespace DeskDrawer;

/// The selection box: press on empty space (or on the blank part of a row, away from its name, icon and other text) and drag, and everything the box
/// touches is selected, like File Explorer. Hold Ctrl or Shift to add to what is selected already. Near the edge the list scrolls on its own and the box
/// keeps growing with it. Pressing on a name or icon still drags the file instead. The box lives in the list's scrolled content, so it is measured in
/// content coordinates (the list scrolls in pixels: see VirtualizingPanel.ScrollUnit in the XAML) and drawn on a layer above it.
public partial class MainWindow
{
    private bool _mqArmed, _mqActive, _mqEmptyPress, _mqDirty;
    private Point _mqPress, _mqMouse;       // in the list's viewport (the scroll content presenter): where the press was, where the pointer is now
    private Vector _mqPressOffset;          // the scroll offset at the press: with _mqPress it gives the box's fixed corner in the scrolled content
    private HashSet<FileEntry> _mqBase = new();
    private readonly Dictionary<FileEntry, Rect> _mqBounds = new();   // content-space bounds of every item seen on screen during this drag (an item scrolled out of sight can't be measured)
    private Rectangle? _mqRect; private DispatcherTimer? _mqTimer;

    /// The part of the list where the rows are (not the column header, which has a scroll presenter of its own).
    private ScrollContentPresenter? Frame()
    {
        if (FindScroll(List) is not { } sv || FindDesc<ItemsPresenter>(sv) is not { } ip) return null;
        for (DependencyObject? d = ip; d != null; d = VisualTreeHelper.GetParent(d)) if (d is ScrollContentPresenter scp) return scp;
        return null;
    }

    private static T? FindDesc<T>(DependencyObject d) where T : DependencyObject
    {
        for (int i = 0, n = VisualTreeHelper.GetChildrenCount(d); i < n; i++) { var c = VisualTreeHelper.GetChild(d, i); if (c is T t) return t; if (FindDesc<T>(c) is { } r) return r; }
        return null;
    }

    private static void Realized(DependencyObject d, List<ListViewItem> into)
    {
        for (int i = 0, n = VisualTreeHelper.GetChildrenCount(d); i < n; i++) { var c = VisualTreeHelper.GetChild(d, i); if (c is ListViewItem lvi) into.Add(lvi); else Realized(c, into); }
    }

    /// Text, icons, column headers and scroll bars are "something": a press there selects / drags / sorts / scrolls. Everything else counts as empty.
    private static bool OnContent(DependencyObject? src)
    {
        for (var d = src; d != null; d = d is Visual or System.Windows.Media.Media3D.Visual3D ? VisualTreeHelper.GetParent(d) : LogicalTreeHelper.GetParent(d))
            if (d is TextBlock or Image or TextBox or ScrollBar or GridViewColumnHeader or Thumb or ToggleButton) return true;
        return false;
    }

    /// A left press in the list: remember it; the box only starts once the pointer has moved a little (a plain click still just selects).
    private void MarqueeArm(MouseButtonEventArgs e, FileEntry? under)
    {
        _mqArmed = false;
        if (e.ChangedButton != MouseButton.Left || e.ClickCount != 1 || InHome || OnContent(e.OriginalSource as DependencyObject) || Frame() is not { } f) return;
        MarqueeArmAt(e.GetPosition(f), under == null, (Keyboard.Modifiers & (ModifierKeys.Control | ModifierKeys.Shift)) != 0);
    }

    private void MarqueeArmAt(Point pressInFrame, bool onEmptySpace, bool additive)
    {
        var sv = FindScroll(List)!; _mqArmed = true; _mqEmptyPress = onEmptySpace; _mqPress = pressInFrame; _mqPressOffset = new Vector(sv.HorizontalOffset, sv.VerticalOffset);
        _mqBase = additive ? new HashSet<FileEntry>(Sel()) : new HashSet<FileEntry>();
    }

    private void MarqueeBegin()
    {
        _mqArmed = false; _mqActive = true; _mqDirty = false; _mqBounds.Clear();
        if (_mqRect == null) { _mqRect = new Rectangle { RadiusX = 2, RadiusY = 2, StrokeThickness = 1, IsHitTestVisible = false, Visibility = Visibility.Collapsed }; MarqueeLayer.Children.Add(_mqRect); }
        _mqRect.SetResourceReference(Shape.FillProperty, "Select"); _mqRect.SetResourceReference(Shape.StrokeProperty, "SelectLine");   // exactly the colours of a selected row, so the box follows the theme (and a theme change while it is open)
        if (_mqTimer == null) { _mqTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(30) }; _mqTimer.Tick += MarqueeTick; }
        List.Focus(); List.CaptureMouse(); _mqTimer.Start();
        if (Frame() is { } f) _mqMouse = Mouse.GetPosition(f);
        MarqueeUpdate();
    }

    private void List_LostCapture(object s, MouseEventArgs e) { if (_mqActive) MarqueeEnd(); }   // something else took the mouse (a dialog, Alt+Tab)

    private void MarqueeMoveTo(Point p) { _mqMouse = p; MarqueeUpdate(); }

    /// Every 30 ms: scroll if the pointer is past the edge (faster the further out), then re-measure the box.
    private void MarqueeTick(object? s, EventArgs e)
    {
        if (!_mqActive) { _mqTimer?.Stop(); return; }
        if (Mouse.LeftButton != MouseButtonState.Pressed && App.UiTest == null) { MarqueeEnd(); return; }   // let go outside the window (the UI test has no real mouse)
        var f = Frame(); var sv = FindScroll(List); if (f == null || sv == null) return;
        _mqMouse = Mouse.GetPosition(f); double dx = 0, dy = 0;
        if (_mqMouse.Y < 0) dy = -Math.Min(48, 4 - _mqMouse.Y / 3); else if (_mqMouse.Y > f.ActualHeight) dy = Math.Min(48, 4 + (_mqMouse.Y - f.ActualHeight) / 3);
        if (_mqMouse.X < 0) dx = -Math.Min(48, 4 - _mqMouse.X / 3); else if (_mqMouse.X > f.ActualWidth) dx = Math.Min(48, 4 + (_mqMouse.X - f.ActualWidth) / 3);
        if (dy != 0 && sv.ScrollableHeight > 0) sv.ScrollToVerticalOffset(sv.VerticalOffset + dy); else dy = 0;
        if (dx != 0 && sv.ScrollableWidth > 0) sv.ScrollToHorizontalOffset(sv.HorizontalOffset + dx); else dx = 0;
        if (dx != 0 || dy != 0) List.UpdateLayout();   // so the rows that scrolled into view exist before they are measured
        MarqueeUpdate();
    }

    private void MarqueeUpdate()
    {
        var f = Frame(); var sv = FindScroll(List); if (f == null || sv == null || _mqRect == null) return;
        var off = new Vector(sv.HorizontalOffset, sv.VerticalOffset); var items = new List<ListViewItem>(); Realized(List, items);
        foreach (var lvi in items)
            if (lvi.Content is FileEntry en && lvi.ActualWidth > 0) { var b = lvi.TransformToAncestor(f).TransformBounds(new Rect(0, 0, lvi.ActualWidth, lvi.ActualHeight)); b.Offset(off); _mqBounds[en] = b; }
        var band = new Rect(_mqPress + _mqPressOffset, _mqMouse + off);   // in content coordinates; Rect(point, point) puts the corners in order
        var want = new HashSet<FileEntry>(_mqBase);
        foreach (var kv in _mqBounds) if (kv.Value.IntersectsWith(band)) want.Add(kv.Key);
        var have = List.SelectedItems.Cast<FileEntry>().ToList(); var haveSet = new HashSet<FileEntry>(have);
        foreach (var x in have) if (!want.Contains(x)) { List.SelectedItems.Remove(x); _mqDirty = true; }
        foreach (var x in want) if (!haveSet.Contains(x)) { List.SelectedItems.Add(x); _mqDirty = true; }
        if (_mqDirty) { _mqDirty = false; PreviewSoon(); UpdateCommands(); UpdateSelectionStatus(); }
        var shown = new Rect(band.X - off.X, band.Y - off.Y, band.Width, band.Height); shown.Intersect(new Rect(0, 0, f.ActualWidth, f.ActualHeight));   // only the part in view
        if (shown.IsEmpty) { _mqRect.Visibility = Visibility.Collapsed; return; }
        var origin = f.TranslatePoint(new Point(0, 0), MarqueeLayer);
        Canvas.SetLeft(_mqRect, origin.X + shown.X); Canvas.SetTop(_mqRect, origin.Y + shown.Y); _mqRect.Width = shown.Width; _mqRect.Height = shown.Height; _mqRect.Visibility = Visibility.Visible;
    }

    /// Button released (or capture lost). A box that never started was a plain click: on empty space that clears the selection.
    private void MarqueeEnd()
    {
        bool armed = _mqArmed; _mqArmed = false;
        if (_mqActive)
        {
            _mqActive = false; _mqTimer?.Stop(); if (List.IsMouseCaptured) List.ReleaseMouseCapture();
            if (_mqRect != null) _mqRect.Visibility = Visibility.Collapsed; _mqBounds.Clear();
        }
        else if (armed && _mqEmptyPress && (Keyboard.Modifiers & (ModifierKeys.Control | ModifierKeys.Shift)) == 0) List.UnselectAll();
    }
}
