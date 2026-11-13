using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Shapes;

namespace DeskDrawer;

/// The Settings window (⋯ > Settings, Ctrl+,), in DeskMode's look like the rest of the suite: a rail of pages on the left, cards of switches on the right.
/// Always dark (DeskMode's 0x202226 with its two soft glows), whatever theme the file window wears. Everything applies the moment it changes.
public partial class MainWindow
{
    internal Window? _settingsWin;

    static SolidColorBrush Rgb(uint rgb, byte a = 255) { var b = new SolidColorBrush(Theme.Ui(rgb, a)); b.Freeze(); return b; }   // DeskMode's colours, worn in the app's theme
    static Brush Glow() { if (Theme.Current.Soft) return Theme.Duo(); var g = new LinearGradientBrush(Theme.Ui(0x1167C4), Theme.Ui(0x1FA7B9), 45); g.Freeze(); return g; }
    static Brush DmBg => Rgb(0x202226); static Brush DmCard => Rgb(0xFFFFFF, 0x0C); static Brush DmLine => Rgb(0xFFFFFF, 0x14); static Brush DmHover => Rgb(0xFFFFFF, 0x08); static Brush DmText => Rgb(0xFFFFFF); static Brush DmDim => Rgb(0xFFFFFF, 0x9C); static Brush DmSoft => Rgb(0x7FD3FF); static Brush DmBlue => Theme.Duo();   // properties, not fields: the theme can change while the app runs

    TextBlock T(string s, double size, Brush fg, FontWeight? w = null, Thickness? m = null) =>
        new() { Text = s, FontSize = size, Foreground = fg, FontWeight = w ?? FontWeights.Normal, Margin = m ?? new Thickness(0), TextWrapping = TextWrapping.Wrap };
    TextBlock G(string glyph, double size, Brush fg) => new() { Text = glyph, FontFamily = (FontFamily)FindResource("IconFont"), FontSize = size, Foreground = fg, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };

    /// DeskMode's switch: a pill that fills blue, the knob slides across.
    static Border MakeToggle(bool on, out Action<bool> paint)
    {
        var knob = new Ellipse { Width = 12, Height = 12, HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Center };
        var tt = new TranslateTransform(); knob.RenderTransform = tt;
        var pill = new Border { Width = 42, Height = 22, CornerRadius = new CornerRadius(11), BorderThickness = new Thickness(1), Padding = new Thickness(4, 0, 4, 0), Child = knob, VerticalAlignment = VerticalAlignment.Center };
        bool first = true;
        paint = v =>
        {
            pill.Background = v ? DmBlue : Brushes.Transparent; pill.BorderBrush = v ? DmBlue : Rgb(0xFFFFFF, 0xB0); knob.Fill = v ? Brushes.Black : Rgb(0xFFFFFF, 0xD0);
            double to = v ? 20 : 0; if (first) tt.X = to; else tt.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(to, TimeSpan.FromMilliseconds(130)) { EasingFunction = new QuadraticEase() });
            first = false;
        };
        paint(on); return pill;
    }

    internal void ShowSettings()
    {
        if (_settingsWin != null) { _settingsWin.Activate(); return; }
        var win = new Window { Title = "DeskDrawer settings", Owner = this, Width = 820, Height = Math.Min(600, SystemParameters.WorkArea.Height - 80), WindowStartupLocation = WindowStartupLocation.CenterOwner, ResizeMode = ResizeMode.CanMinimize, ShowInTaskbar = false, Background = DmBg, Foreground = DmText, FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI") };
        win.SourceInitialized += (_, _) => { var h = new WindowInteropHelper(win).Handle; int dark = 1; Native.DwmSetWindowAttribute(h, 20, ref dark, 4); int cap = 0x262220; Native.DwmSetWindowAttribute(h, 35, ref cap, 4); };   // the title bar in the page's colour (COLORREF is BGR)
        win.Closed += (_, _) => _settingsWin = null;
        win.PreviewKeyDown += (_, e) => { if (e.Key == Key.Escape) win.Close(); };

        // ---- builders
        StackPanel? page = null;
        StackPanel Card(string title)
        {
            page!.Children.Add(T(title, 13.5, Rgb(0xFFFFFF, 0xD8), m: new Thickness(4, 10, 0, 8)));
            var sp = new StackPanel();
            page.Children.Add(new Border { Background = DmCard, BorderBrush = DmLine, BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(14), Padding = new Thickness(6), Margin = new Thickness(0, 0, 0, 6), Child = sp });
            return sp;
        }
        void Sep(StackPanel card) { if (card.Children.Count > 0) card.Children.Add(new Border { Height = 1, Background = Rgb(0xFFFFFF, 0x0E), Margin = new Thickness(58, 0, 10, 0) }); }
        Border RowBase(StackPanel card, string glyph, string title, string what, UIElement? right)
        {
            Sep(card);
            var disc = new Border { Width = 34, Height = 34, CornerRadius = new CornerRadius(10), Background = Rgb(0xFFFFFF, 0x10), Child = G(glyph, 16, DmSoft), VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 14, 0) };
            var text = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
            text.Children.Add(T(title, 14, DmText)); if (what.Length > 0) text.Children.Add(T(what, 12.5, DmDim, m: new Thickness(0, 2, 0, 0)));
            var row = new DockPanel(); DockPanel.SetDock(disc, Dock.Left); row.Children.Add(disc);
            if (right != null) { DockPanel.SetDock(right, Dock.Right); row.Children.Add(right); ((FrameworkElement)right).Margin = new Thickness(14, 0, 0, 0); }
            row.Children.Add(text);
            var hit = new Border { Child = row, Padding = new Thickness(10, 10, 12, 10), CornerRadius = new CornerRadius(10), Background = Brushes.Transparent };
            card.Children.Add(hit); return hit;
        }
        void Switch(StackPanel card, string glyph, string title, string what, bool on, Action<bool> set)
        {
            var tg = MakeToggle(on, out var paint);
            var hit = RowBase(card, glyph, title, what, tg); hit.Cursor = Cursors.Hand;
            hit.MouseEnter += (_, _) => hit.Background = DmHover; hit.MouseLeave += (_, _) => hit.Background = Brushes.Transparent;
            hit.MouseLeftButtonUp += (_, _) => { on = !on; paint(on); set(on); };
        }
        Border Btn(string text, string glyph, bool primary, Action act)
        {
            var sp = new StackPanel { Orientation = Orientation.Horizontal };
            if (glyph.Length > 0) sp.Children.Add(new TextBlock { Text = glyph, FontFamily = (FontFamily)FindResource("IconFont"), FontSize = 13, Foreground = DmText, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 8, 0) });
            sp.Children.Add(T(text, 13.5, DmText, primary ? FontWeights.SemiBold : FontWeights.Normal));
            var idle = primary ? Glow() : Rgb(0xFFFFFF, 0x10); var over = primary ? Glow() : Rgb(0xFFFFFF, 0x1C);
            var b = new Border { Child = sp, CornerRadius = new CornerRadius(10), Padding = new Thickness(16, 8, 16, 9), Background = idle, BorderBrush = primary ? null : DmLine, BorderThickness = new Thickness(primary ? 0 : 1), Cursor = Cursors.Hand, Margin = new Thickness(0, 0, 8, 0), VerticalAlignment = VerticalAlignment.Center };
            b.MouseEnter += (_, _) => b.Background = over; b.MouseLeave += (_, _) => b.Background = idle; b.MouseLeftButtonUp += (_, _) => act();
            return b;
        }
        void Head(string glyph, string title, string sub)
        {
            var tile = new Border { Width = 48, Height = 48, CornerRadius = new CornerRadius(14), Background = Glow(), Child = G(glyph, 22, DmText), Margin = new Thickness(0, 0, 16, 0) };
            var tx = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
            tx.Children.Add(T(title, 24, DmText, FontWeights.SemiBold)); tx.Children.Add(T(sub, 13, DmDim, m: new Thickness(0, 2, 0, 0)));
            var h = new DockPanel { Margin = new Thickness(0, 0, 0, 12) }; DockPanel.SetDock(tile, Dock.Left); h.Children.Add(tile); h.Children.Add(tx);
            page!.Children.Add(h);
        }

        // ---- the pages
        void PageFiles()
        {
            Head("", "Folders and files", "Where DeskDrawer stands in for File Explorer.");
            var a = Card("Instead of File Explorer");
            Switch(a, "", "Open folders with DeskDrawer", "Folders, drives and Win+E open here. Off gives them back to File Explorer.", DefaultApp.IsOn, v => SetDefault(v));
            Switch(a, "", "Answer other apps' Open and Save", "When Word, Chrome or any app asks where to open or save, DeskDrawer answers instead of Windows' window.", _settings.TakeOverDialogs,
                v => { _settings.TakeOverDialogs = v; DialogSwitch.TakeOver = v; _settings.Save(); });
            var c = Card("What you see");
            Switch(c, "", "Show hidden items", "Hidden and system files are listed, dimmed. Ctrl+H flips it from any folder.", _settings.ShowHidden, v => { if (v != _settings.ShowHidden) ToggleHidden(); });
        }
        void PageSpeed()
        {
            Head("", "Speed", "How quickly DeskDrawer opens and scrolls.");
            var b = Card("Opening");
            Switch(b, "", "Keep ready in the background", "Closing the window keeps DeskDrawer out of sight, so the next folder opens in about half a second. Uses some memory.", _settings.KeepReady,
                v => { _settings.KeepReady = v; _settings.Save(); Setup.StartAtSignIn(v); });
            var sc = Card("Scrolling");
            RowBase(sc, "", "Scrolling speed", "How far a turn of the wheel or a touchpad swipe moves the folder.", null);
            double[] speeds = { 0.4, 0.7, 1, 1.5 }; string[] names = { "Slower", "Slow", "Normal", "Fast" };
            var strip = new System.Windows.Controls.Primitives.UniformGrid { Rows = 1, Margin = new Thickness(58, 0, 12, 12) };
            var segs = new List<Border>();
            void Mark() { for (int k = 0; k < segs.Count; k++) { bool on = Math.Abs(speeds[k] - _settings.ScrollSpeed) < 0.01; segs[k].Background = on ? Glow() : Brushes.Transparent; ((TextBlock)segs[k].Child).FontWeight = on ? FontWeights.SemiBold : FontWeights.Normal; } }
            for (int k = 0; k < speeds.Length; k++)
            {
                int n = k; var seg = new Border { CornerRadius = new CornerRadius(8), Padding = new Thickness(6, 8, 6, 8), Margin = new Thickness(1), Cursor = Cursors.Hand, Child = new TextBlock { Text = names[k], FontSize = 13, Foreground = DmText, HorizontalAlignment = HorizontalAlignment.Center } };
                seg.MouseLeftButtonUp += (_, _) => { _settings.ScrollSpeed = speeds[n]; Wheel.Speed = speeds[n]; _settings.Save(); Mark(); };
                segs.Add(seg); strip.Children.Add(seg);
            }
            Mark();
            sc.Children.Add(new Border { Background = Rgb(0xFFFFFF, 0x0A), CornerRadius = new CornerRadius(10), Padding = new Thickness(3), Margin = new Thickness(58, 0, 12, 12), Child = strip });
            strip.Margin = new Thickness(0);
            var g = Card("Drawing");
            Switch(g, "", "Use the graphics card", "Smoother on very large screens, about 85 MB more memory. From the next start.", _settings.Gpu, v => { _settings.Gpu = v; _settings.Save(); });
        }
        void PageUpdates()
        {
            Head("", "Updates", "Keeping DeskDrawer current.");
            var d = Card("Updates");
            Switch(d, "", "Look for updates once a day", "Only looks. Nothing installs until you press Update.", Updater.Enabled, v => Updater.Enabled = v);
            Sep(d);
            var st = T(Updater.Status.Length > 0 ? Updater.Status : "Not checked yet.", 13, DmDim); st.VerticalAlignment = VerticalAlignment.Center;
            var btns = new StackPanel { Orientation = Orientation.Horizontal };
            btns.Children.Add(Btn("Check now", "", false, () => { _userCheck = true; _ = Updater.CheckAsync(); }));
            var upd = Btn("Update now", "", true, UpdateNow); upd.Visibility = Updater.Newer ? Visibility.Visible : Visibility.Collapsed; btns.Children.Add(upd);
            var row = new DockPanel { Margin = new Thickness(12, 10, 4, 10) }; DockPanel.SetDock(btns, Dock.Right); row.Children.Add(btns); row.Children.Add(st); d.Children.Add(row);
            Action changed = () => Dispatcher.BeginInvoke(() => { st.Text = Updater.Status.Length > 0 ? Updater.Status : "Not checked yet."; upd.Visibility = Updater.Newer ? Visibility.Visible : Visibility.Collapsed; });
            Updater.Changed += changed; st.Unloaded += (_, _) => Updater.Changed -= changed;
        }
        var pages = new (string Glyph, string Name, Action Build)[] { ("", "Folders and files", PageFiles), ("", "Speed", PageSpeed), ("", "Updates", PageUpdates) };

        // ---- the frame: glows, rail, page
        var scroll = new ScrollViewer { VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        var nav = new List<(Border Item, Border Bar)>();
        void Show(int i)
        {
            for (int k = 0; k < nav.Count; k++) { nav[k].Item.Background = k == i ? Rgb(0xFFFFFF, 0x14) : Brushes.Transparent; nav[k].Item.Tag = k == i; nav[k].Bar.Background = k == i ? Glow() : Brushes.Transparent; }
            page = new StackPanel { Margin = new Thickness(32, 28, 32, 28) }; pages[i].Build(); scroll.Content = page; scroll.ScrollToTop();
        }
        var rail = new DockPanel { Width = 232, Margin = new Thickness(16, 20, 14, 18), LastChildFill = false };
        {
            var id = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(6, 0, 0, 22) };
            id.Children.Add(new Border { Width = 38, Height = 38, CornerRadius = new CornerRadius(10), Background = DmBlue, Child = G("", 19, Brushes.Black), Margin = new Thickness(0, 0, 12, 0) });
            var tx = new StackPanel { VerticalAlignment = VerticalAlignment.Center }; tx.Children.Add(T("DeskDrawer", 16, DmText, FontWeights.SemiBold)); tx.Children.Add(T("Settings", 12, DmDim)); id.Children.Add(tx);
            DockPanel.SetDock(id, Dock.Top); rail.Children.Add(id);
            for (int i = 0; i < pages.Length; i++)
            {
                int n = i;
                var bar = new Border { Width = 3, Height = 18, CornerRadius = new CornerRadius(1.5), Margin = new Thickness(0, 0, 12, 0) };
                var sp = new StackPanel { Orientation = Orientation.Horizontal };
                var g = G(pages[i].Glyph, 16, DmSoft); g.Width = 18; g.Margin = new Thickness(0, 0, 12, 0);
                sp.Children.Add(bar); sp.Children.Add(g); sp.Children.Add(T(pages[i].Name, 14, DmText));
                var item = new Border { Child = sp, CornerRadius = new CornerRadius(10), Padding = new Thickness(0, 10, 12, 10), Margin = new Thickness(0, 0, 0, 4), Background = Brushes.Transparent, Cursor = Cursors.Hand };
                item.MouseEnter += (_, _) => { if (item.Tag is not true) item.Background = Rgb(0xFFFFFF, 0x0C); };
                item.MouseLeave += (_, _) => { if (item.Tag is not true) item.Background = Brushes.Transparent; };
                item.MouseLeftButtonUp += (_, _) => Show(n);
                DockPanel.SetDock(item, Dock.Top); rail.Children.Add(item); nav.Add((item, bar));
            }
            var foot = T("Changes apply right away.", 12, DmDim, m: new Thickness(8, 0, 0, 0)); DockPanel.SetDock(foot, Dock.Bottom); rail.Children.Add(foot);
            // an update that is out shows on every page, not only on Updates (nobody looks at the window's status bar from in here)
            var railUpd = Btn("Update now", "", true, UpdateNow); railUpd.HorizontalAlignment = HorizontalAlignment.Stretch; railUpd.Margin = new Thickness(0, 0, 0, 12);
            DockPanel.SetDock(railUpd, Dock.Bottom); rail.Children.Add(railUpd);
            void ShowRailUpd() => railUpd.Visibility = Updater.Newer && !Updater.Busy ? Visibility.Visible : Visibility.Collapsed;
            ShowRailUpd(); Action railChanged = () => Dispatcher.BeginInvoke(ShowRailUpd); Updater.Changed += railChanged; win.Closed += (_, _) => Updater.Changed -= railChanged;
        }
        var main = new DockPanel(); DockPanel.SetDock(rail, Dock.Left); main.Children.Add(rail);
        var line = new Border { Width = 1, Background = Rgb(0xFFFFFF, 0x0E) }; DockPanel.SetDock(line, Dock.Left); main.Children.Add(line);
        main.Children.Add(scroll);
        var root = new Grid { Background = DmBg };
        RadialGradientBrush Soft(uint rgb, byte a, double x, double y, double rx, double ry) { var r = new RadialGradientBrush(Theme.Ui(rgb, a), Theme.Ui(rgb, 0)) { Center = new Point(x, y), GradientOrigin = new Point(x, y), RadiusX = rx, RadiusY = ry }; r.Freeze(); return r; }
        root.Children.Add(new Border { Background = Soft(0x1167C4, 0x60, 0, 0, 0.55, 0.45), IsHitTestVisible = false });   // DeskMode's two soft glows
        root.Children.Add(new Border { Background = Soft(0x1FA7B9, 0x40, 1, 1, 0.6, 0.5), IsHitTestVisible = false });
        root.Children.Add(main);
        int start = int.TryParse(Environment.GetEnvironmentVariable("DD_SETPAGE"), out var sp0) ? Math.Clamp(sp0, 0, pages.Length - 1) : 0;   // dev: which page --settingsshot shows
        Show(start);
        win.Content = root;
        _settingsWin = win; win.Show();
    }
}
