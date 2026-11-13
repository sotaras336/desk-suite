using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Shapes;

namespace DeskTune;

/// DeskTune's settings (the gear in the footer, Ctrl+,), in DeskMode's look like the rest of the Desk suite:
/// a rail of pages on the left, cards of switches on the right, always dark with DeskMode's two soft glows. Changes apply at once.
public sealed class SettingsWindow : Window
{
    static SolidColorBrush Rgb(uint rgb, byte a = 255) { var b = new SolidColorBrush(Theme.Ui(rgb, a)); b.Freeze(); return b; }   // DeskMode's colours, worn in the app's theme
    static Brush Glow() { if (Theme.Current.Soft) return Theme.Duo(); var g = new LinearGradientBrush(Theme.Ui(0x1167C4), Theme.Ui(0x1FA7B9), 45); g.Freeze(); return g; }
    static Brush Bg => Rgb(0x202226); static Brush CardBg => Rgb(0xFFFFFF, 0x0C); static Brush Line => Rgb(0xFFFFFF, 0x14); static Brush Hover => Rgb(0xFFFFFF, 0x08); static Brush Fg => Rgb(0xFFFFFF); static Brush Dim => Rgb(0xFFFFFF, 0x9C); static Brush Soft => Rgb(0x7FD3FF); static Brush Blue => Theme.Duo();   // properties, not fields: the theme can change while the app runs

    readonly MainWindow _main;
    readonly ScrollViewer _scroll = new() { VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
    readonly List<(Border Item, Border Bar)> _nav = new();
    StackPanel _page = new();
    readonly (string Glyph, string Name, Action Build)[] _pages;

    public SettingsWindow(MainWindow main)
    {
        _main = main; Owner = main;
        Title = "DeskTune settings"; Width = 820; Height = Math.Min(560, SystemParameters.WorkArea.Height - 80); WindowStartupLocation = WindowStartupLocation.CenterOwner;
        ResizeMode = ResizeMode.CanMinimize; ShowInTaskbar = false; Background = Bg; Foreground = Fg; FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI"); UseLayoutRounding = true;
        SourceInitialized += (_, _) => { var h = new WindowInteropHelper(this).Handle; int dark = 1; MainWindow.DwmSetWindowAttribute(h, 20, ref dark, 4); int cap = 0x262220; MainWindow.DwmSetWindowAttribute(h, 35, ref cap, 4); };   // title bar in the page colour (BGR)
        PreviewKeyDown += (_, e) => { if (e.Key == Key.Escape) Close(); };
        _pages = new (string, string, Action)[] { ("", "Safety net", PageSafety), ("", "Updates", PageUpdates) };

        var rail = new DockPanel { Width = 232, Margin = new Thickness(16, 20, 14, 18), LastChildFill = false };
        var id = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(6, 0, 0, 22) };
        id.Children.Add(new Border { Width = 38, Height = 38, CornerRadius = new CornerRadius(10), Background = Blue, Child = G("", 19, Brushes.Black), Margin = new Thickness(0, 0, 12, 0) });
        var tx = new StackPanel { VerticalAlignment = VerticalAlignment.Center }; tx.Children.Add(T("DeskTune", 16, Fg, FontWeights.SemiBold)); tx.Children.Add(T("Settings", 12, Dim)); id.Children.Add(tx);
        DockPanel.SetDock(id, Dock.Top); rail.Children.Add(id);
        for (int i = 0; i < _pages.Length; i++)
        {
            int n = i;
            var bar = new Border { Width = 3, Height = 18, CornerRadius = new CornerRadius(1.5), Margin = new Thickness(0, 0, 12, 0) };
            var g = G(_pages[i].Glyph, 16, Soft); g.Width = 18; g.Margin = new Thickness(0, 0, 12, 0);
            var sp = new StackPanel { Orientation = Orientation.Horizontal }; sp.Children.Add(bar); sp.Children.Add(g); sp.Children.Add(T(_pages[i].Name, 14, Fg));
            var item = new Border { Child = sp, CornerRadius = new CornerRadius(10), Padding = new Thickness(0, 10, 12, 10), Margin = new Thickness(0, 0, 0, 4), Background = Brushes.Transparent, Cursor = Cursors.Hand };
            item.MouseEnter += (_, _) => { if (item.Tag is not true) item.Background = Rgb(0xFFFFFF, 0x0C); };
            item.MouseLeave += (_, _) => { if (item.Tag is not true) item.Background = Brushes.Transparent; };
            item.MouseLeftButtonUp += (_, _) => Show(n);
            DockPanel.SetDock(item, Dock.Top); rail.Children.Add(item); _nav.Add((item, bar));
        }
        var foot = T("Changes apply right away.", 12, Dim, m: new Thickness(8, 0, 0, 0)); DockPanel.SetDock(foot, Dock.Bottom); rail.Children.Add(foot);
        // an update that is out shows on every page, not only on Updates
        var railUpd = Btn("Update now", "", true, async () => { if (await Updater.UpdateNowAsync()) Application.Current.Shutdown(); });
        railUpd.HorizontalAlignment = HorizontalAlignment.Stretch; railUpd.Margin = new Thickness(0, 0, 0, 12); DockPanel.SetDock(railUpd, Dock.Bottom); rail.Children.Add(railUpd);
        void ShowRailUpd() => railUpd.Visibility = Updater.Newer && !Updater.Busy ? Visibility.Visible : Visibility.Collapsed;
        ShowRailUpd(); Action railChanged = () => Dispatcher.BeginInvoke(ShowRailUpd); Updater.Changed += railChanged; Closed += (_, _) => Updater.Changed -= railChanged;

        var mainPanel = new DockPanel(); DockPanel.SetDock(rail, Dock.Left); mainPanel.Children.Add(rail);
        var line = new Border { Width = 1, Background = Rgb(0xFFFFFF, 0x0E) }; DockPanel.SetDock(line, Dock.Left); mainPanel.Children.Add(line);
        mainPanel.Children.Add(_scroll);
        var root = new Grid { Background = Bg };
        root.Children.Add(new Border { Background = SoftGlow(0x1167C4, 0x60, 0, 0, 0.55, 0.45), IsHitTestVisible = false });   // DeskMode's two soft glows
        root.Children.Add(new Border { Background = SoftGlow(0x1FA7B9, 0x40, 1, 1, 0.6, 0.5), IsHitTestVisible = false });
        root.Children.Add(mainPanel);
        Content = root;
        Show(int.TryParse(Environment.GetEnvironmentVariable("DT_SETPAGE"), out var p0) ? Math.Clamp(p0, 0, _pages.Length - 1) : 0);   // dev: the page a snapshot shows
    }

    void Show(int i)
    {
        for (int k = 0; k < _nav.Count; k++) { _nav[k].Item.Background = k == i ? Rgb(0xFFFFFF, 0x14) : Brushes.Transparent; _nav[k].Item.Tag = k == i; _nav[k].Bar.Background = k == i ? Glow() : Brushes.Transparent; }
        _page = new StackPanel { Margin = new Thickness(32, 28, 32, 28) }; _pages[i].Build(); _scroll.Content = _page; _scroll.ScrollToTop();
    }

    // ---- the pages
    void PageSafety()
    {
        Head("", "Safety net", "Everything DeskTune changes is written down and can be put back.");
        var c = Card("Your changes");
        int n = Log.Count;
        Action(c, "", "Put everything back", n == 0 ? "Nothing is changed right now." : $"{n} change{(n == 1 ? "" : "s")} made by DeskTune. Undoes all of them.", n == 0 ? null : () => { Close(); _main.UndoAll(); });
        Action(c, "", "Open the change log", "The file that records every change, so it can be undone.", () =>
        { try { if (System.IO.File.Exists(Log.File)) Process.Start("explorer.exe", $"/select,\"{Log.File}\""); else Process.Start("explorer.exe", System.IO.Path.GetDirectoryName(Log.File)!); } catch { } });
        var a = Card("Administrator");
        if (Elevation.IsAdmin) Action(a, "", "Running as administrator", "Services and machine-wide switches are unlocked.", null);
        else Action(a, "", "Unlock services", "Services and machine-wide switches need administrator rights. Restarts DeskTune.", () => { if (Elevation.RestartAsAdmin()) Application.Current.Shutdown(); });
    }
    void PageUpdates()
    {
        Head("", "Updates", "Keeping DeskTune current.");
        var d = Card("Updates");
        Switch(d, "", "Look for updates once a day", "Only looks. Nothing installs until you press Update.", Updater.Enabled, v => Updater.Enabled = v);
        Sep(d);
        var st = T(Updater.Status.Length > 0 ? Updater.Status : "Not checked yet.", 13, Dim); st.VerticalAlignment = VerticalAlignment.Center;
        var btns = new StackPanel { Orientation = Orientation.Horizontal };
        btns.Children.Add(Btn("Check now", "", false, () => _ = Updater.CheckAsync()));
        var upd = Btn("Update now", "", true, async () => { if (await Updater.UpdateNowAsync()) Application.Current.Shutdown(); });
        upd.Visibility = Updater.Newer ? Visibility.Visible : Visibility.Collapsed; btns.Children.Add(upd);
        var row = new DockPanel { Margin = new Thickness(12, 10, 4, 10) }; DockPanel.SetDock(btns, Dock.Right); row.Children.Add(btns); row.Children.Add(st); d.Children.Add(row);
        Action changed = () => Dispatcher.BeginInvoke(() => { st.Text = Updater.Status.Length > 0 ? Updater.Status : "Not checked yet."; upd.Visibility = Updater.Newer ? Visibility.Visible : Visibility.Collapsed; });
        Updater.Changed += changed; st.Unloaded += (_, _) => Updater.Changed -= changed;
    }

    // ---- builders
    TextBlock T(string s, double size, Brush fg, FontWeight? w = null, Thickness? m = null) =>
        new() { Text = s, FontSize = size, Foreground = fg, FontWeight = w ?? FontWeights.Normal, Margin = m ?? new Thickness(0), TextWrapping = TextWrapping.Wrap };
    TextBlock G(string glyph, double size, Brush fg) => new() { Text = glyph, FontFamily = (FontFamily)FindResource("IconFont"), FontSize = size, Foreground = fg, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };
    static RadialGradientBrush SoftGlow(uint rgb, byte a, double x, double y, double rx, double ry)
    {
        Color c(byte al) => Theme.Ui(rgb, al);
        var r = new RadialGradientBrush(c(a), c(0)) { Center = new Point(x, y), GradientOrigin = new Point(x, y), RadiusX = rx, RadiusY = ry }; r.Freeze(); return r;
    }
    void Head(string glyph, string title, string sub)
    {
        var tile = new Border { Width = 48, Height = 48, CornerRadius = new CornerRadius(14), Background = Glow(), Child = G(glyph, 22, Fg), Margin = new Thickness(0, 0, 16, 0) };
        var tx = new StackPanel { VerticalAlignment = VerticalAlignment.Center }; tx.Children.Add(T(title, 24, Fg, FontWeights.SemiBold)); tx.Children.Add(T(sub, 13, Dim, m: new Thickness(0, 2, 0, 0)));
        var h = new DockPanel { Margin = new Thickness(0, 0, 0, 12) }; DockPanel.SetDock(tile, Dock.Left); h.Children.Add(tile); h.Children.Add(tx); _page.Children.Add(h);
    }
    StackPanel Card(string title)
    {
        _page.Children.Add(T(title, 13.5, Rgb(0xFFFFFF, 0xD8), m: new Thickness(4, 10, 0, 8)));
        var sp = new StackPanel();
        _page.Children.Add(new Border { Background = CardBg, BorderBrush = Line, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(14), Padding = new Thickness(6), Margin = new Thickness(0, 0, 0, 6), Child = sp });
        return sp;
    }
    static void Sep(StackPanel card) { if (card.Children.Count > 0) card.Children.Add(new Border { Height = 1, Background = Rgb(0xFFFFFF, 0x0E), Margin = new Thickness(58, 0, 10, 0) }); }
    Border Row(StackPanel card, string glyph, string title, string what, UIElement? right)
    {
        Sep(card);
        var disc = new Border { Width = 34, Height = 34, CornerRadius = new CornerRadius(10), Background = Rgb(0xFFFFFF, 0x10), Child = G(glyph, 16, Soft), VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 14, 0) };
        var text = new StackPanel { VerticalAlignment = VerticalAlignment.Center }; text.Children.Add(T(title, 14, Fg)); if (what.Length > 0) text.Children.Add(T(what, 12.5, Dim, m: new Thickness(0, 2, 0, 0)));
        var row = new DockPanel(); DockPanel.SetDock(disc, Dock.Left); row.Children.Add(disc);
        if (right is FrameworkElement fe) { DockPanel.SetDock(fe, Dock.Right); fe.Margin = new Thickness(14, 0, 0, 0); row.Children.Add(fe); }
        row.Children.Add(text);
        var hit = new Border { Child = row, Padding = new Thickness(10, 10, 12, 10), CornerRadius = new CornerRadius(10), Background = Brushes.Transparent };
        card.Children.Add(hit); return hit;
    }
    void Clickable(Border hit, Action act) { hit.Cursor = Cursors.Hand; hit.MouseEnter += (_, _) => hit.Background = Hover; hit.MouseLeave += (_, _) => hit.Background = Brushes.Transparent; hit.MouseLeftButtonUp += (_, _) => act(); }
    void Switch(StackPanel card, string glyph, string title, string what, bool on, Action<bool> set)
    {
        var knob = new Ellipse { Width = 12, Height = 12, HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Center };
        var tt = new TranslateTransform(); knob.RenderTransform = tt;
        var pill = new Border { Width = 42, Height = 22, CornerRadius = new CornerRadius(11), BorderThickness = new Thickness(1), Padding = new Thickness(4, 0, 4, 0), Child = knob, VerticalAlignment = VerticalAlignment.Center };
        void Paint(bool anim)
        {
            pill.Background = on ? Blue : Brushes.Transparent; pill.BorderBrush = on ? Blue : Rgb(0xFFFFFF, 0xB0); knob.Fill = on ? Brushes.Black : Rgb(0xFFFFFF, 0xD0);
            if (anim) tt.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(on ? 20 : 0, TimeSpan.FromMilliseconds(130)) { EasingFunction = new QuadraticEase() }); else tt.X = on ? 20 : 0;
        }
        Paint(false);
        Clickable(Row(card, glyph, title, what, pill), () => { on = !on; Paint(true); set(on); });
    }
    void Action(StackPanel card, string glyph, string title, string what, Action? act)
    {
        var chev = act == null ? null : G("", 12, Rgb(0xFFFFFF, 0x90));
        var hit = Row(card, glyph, title, what, chev);
        if (act != null) Clickable(hit, act);
    }
    Border Btn(string text, string glyph, bool primary, Action act)
    {
        var sp = new StackPanel { Orientation = Orientation.Horizontal };
        if (glyph.Length > 0) { var g = G(glyph, 13, Fg); g.Margin = new Thickness(0, 0, 8, 0); sp.Children.Add(g); }
        sp.Children.Add(T(text, 13.5, Fg, primary ? FontWeights.SemiBold : FontWeights.Normal));
        var idle = primary ? Glow() : Rgb(0xFFFFFF, 0x10); var over = primary ? Glow() : Rgb(0xFFFFFF, 0x1C);
        var b = new Border { Child = sp, CornerRadius = new CornerRadius(10), Padding = new Thickness(16, 8, 16, 9), Background = idle, BorderBrush = primary ? null : Line, BorderThickness = new Thickness(primary ? 0 : 1), Cursor = Cursors.Hand, Margin = new Thickness(0, 0, 8, 0), VerticalAlignment = VerticalAlignment.Center };
        b.MouseEnter += (_, _) => b.Background = over; b.MouseLeave += (_, _) => b.Background = idle; b.MouseLeftButtonUp += (_, _) => act();
        return b;
    }
}
