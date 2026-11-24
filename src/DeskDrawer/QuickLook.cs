using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace DeskDrawer;

/// Space on a file: a big look at it over the window, without opening anything. Pictures at full quality, text files as text, everything else as
/// Windows' own large preview (the first page of a PDF, a video frame...) with its details. Arrows step to the next file, Enter opens it,
/// Space or Esc closes. Folders and several selected items keep the side preview pane (Alt+P).
public sealed class QuickLook : Window
{
    private static QuickLook? _open;
    private readonly Func<int, FileEntry?> _step;   // move the selection by n and return the newly selected file
    private readonly Action<string> _openFile;
    private readonly TextBlock _title = new() { FontSize = 15, FontWeight = FontWeights.SemiBold, TextTrimming = TextTrimming.CharacterEllipsis, VerticalAlignment = VerticalAlignment.Center };
    private readonly TextBlock _sub = new() { FontSize = 12, Margin = new Thickness(0, 2, 0, 0) };
    private readonly Border _body = new() { Margin = new Thickness(16, 4, 16, 16), CornerRadius = new CornerRadius(8), ClipToBounds = true };
    private FileEntry? _cur; private int _version; private bool _closing;

    public static bool IsOpen => _open != null;
    internal static string? ShownName => _open?._cur?.Name;                                     // for the UI test
    internal static string? ShownText => (_open?._body.Child as TextBox)?.Text;
    internal static bool ShowsPicture => _open?._body.Child is Image { Source: not null };
    internal static void Press(Key k) => _open?.RaiseEvent(new KeyEventArgs(Keyboard.PrimaryDevice, PresentationSource.FromVisual(_open), 0, k) { RoutedEvent = PreviewKeyDownEvent });

    /// Opens a quick look of f over owner, or closes the one that is open.
    public static void Toggle(Window owner, FileEntry f, Func<int, FileEntry?> step, Action<string> openFile)
    {
        if (_open != null) { _open.Close(); return; }
        _open = new QuickLook(owner, step, openFile); _open.Show(f); _open.Show(); _open.Activate();
    }

    private QuickLook(Window owner, Func<int, FileEntry?> step, Action<string> openFile)
    {
        _step = step; _openFile = openFile; Owner = owner;
        WindowStyle = WindowStyle.None; AllowsTransparency = true; Background = Brushes.Transparent; ShowInTaskbar = false; ResizeMode = ResizeMode.NoResize;
        // 82% of the window it opens from, centred on it
        Width = Math.Max(480, owner.ActualWidth * 0.82); Height = Math.Max(360, owner.ActualHeight * 0.82);
        Left = owner.Left + (owner.ActualWidth - Width) / 2; Top = owner.Top + (owner.ActualHeight - Height) / 2;
        if (owner.WindowState == WindowState.Maximized && PresentationSource.FromVisual(owner) is { } ps)
        {   // a maximized owner's Left/Top are its restored place: centre on the screen it fills instead
            var wa = SystemParameters.WorkArea; Left = wa.Left + (wa.Width - Width) / 2; Top = wa.Top + (wa.Height - Height) / 2;
        }

        var card = new Border { CornerRadius = new CornerRadius(12), BorderThickness = new Thickness(1) };
        card.SetResourceReference(Border.BackgroundProperty, "MenuBg"); card.SetResourceReference(Border.BorderBrushProperty, "CardLine");
        card.Effect = new System.Windows.Media.Effects.DropShadowEffect { BlurRadius = 24, ShadowDepth = 6, Opacity = 0.35 };
        var grid = new Grid { Margin = new Thickness(0) };
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto }); grid.RowDefinitions.Add(new RowDefinition());
        var head = new DockPanel { Margin = new Thickness(16, 12, 12, 8) };
        var openBtn = Btn("Open", () => { if (_cur != null) { var p = _cur.FullPath; Close(); _openFile(p); } });
        var closeBtn = Btn("", Close, glyph: true);
        DockPanel.SetDock(closeBtn, Dock.Right); DockPanel.SetDock(openBtn, Dock.Right); head.Children.Add(closeBtn); head.Children.Add(openBtn);
        var titles = new StackPanel(); titles.Children.Add(_title); titles.Children.Add(_sub); head.Children.Add(titles);
        _title.SetResourceReference(TextBlock.ForegroundProperty, "Fg"); _sub.SetResourceReference(TextBlock.ForegroundProperty, "FgDim");
        _body.SetResourceReference(Border.BackgroundProperty, "PaneBg");
        Grid.SetRow(_body, 1); grid.Children.Add(head); grid.Children.Add(_body); card.Child = grid;
        Content = new Border { Padding = new Thickness(14), Child = card };   // room for the shadow

        PreviewKeyDown += (_, e) =>
        {
            if (e.Key is Key.Space or Key.Escape) { Close(); e.Handled = true; }
            else if (e.Key == Key.Enter) { if (_cur != null) { var p = _cur.FullPath; Close(); _openFile(p); } e.Handled = true; }
            else if (e.Key is Key.Right or Key.Down or Key.Left or Key.Up) { if (_step(e.Key is Key.Right or Key.Down ? 1 : -1) is { } n) Show(n); e.Handled = true; }
        };
        Closing += (_, _) => _closing = true;
        Deactivated += (_, _) => { if (IsVisible && !_closing) Close(); };   // clicking anywhere else puts it away (not while closing: Space, Esc or Open close it, the close deactivates it, and a second Close() inside the first throws)
        Closed += (_, _) => { _open = null; Owner?.Activate(); };
    }

    private Button Btn(string text, Action click, bool glyph = false)
    {
        var b = new Button { Content = text, Margin = new Thickness(6, 0, 0, 0), Padding = new Thickness(glyph ? 10 : 14, 5, glyph ? 10 : 14, 6), FontSize = glyph ? 11 : 13, Cursor = Cursors.Hand };
        if (glyph) b.FontFamily = new FontFamily("Segoe Fluent Icons, Segoe MDL2 Assets");
        b.Click += (_, _) => click(); return b;
    }

    private void Show(FileEntry f)
    {
        _cur = f; int v = ++_version;
        _title.Text = f.Name; _sub.Text = string.Join("  ·  ", new[] { f.TypeText, f.SizeText, f.ModifiedText }.Where(s => !string.IsNullOrEmpty(s)));
        _body.Child = Hint("Loading…");
        string path = f.FullPath, ext = f.Ext;
        if (!f.IsDir && PreviewPane.IsText(ext.Length > 0 ? ext : f.Name))
        {
            Task.Run(() => PreviewPane.ReadText(path)).ContinueWith(t => Dispatcher.BeginInvoke(() =>
            {
                if (v != _version) return;
                var tb = new TextBox { Text = t.Result, IsReadOnly = true, BorderThickness = new Thickness(0), Background = Brushes.Transparent, FontFamily = new FontFamily("Cascadia Mono, Consolas"), FontSize = 13,
                    Padding = new Thickness(14, 10, 14, 10), TextWrapping = TextWrapping.NoWrap, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
                tb.SetResourceReference(TextBox.ForegroundProperty, "Fg"); _body.Child = tb;
            }));
            return;
        }
        // the picture: an image file decoded at screen size (sharp), anything else Windows' own large preview; off the UI thread either way
        double scale = VisualTreeHelper.GetDpi(this).DpiScaleX; int px = (int)Math.Min(4096, Math.Max(Width, Height) * scale);
        bool image = !f.IsDir && PreviewPane.IsImage(ext);
        Thumbs.OnSta(() =>
        {
            BitmapSource? bs = null;
            if (image) { try { var bi = new BitmapImage(); bi.BeginInit(); bi.CacheOption = BitmapCacheOption.OnLoad; bi.CreateOptions = BitmapCreateOptions.IgnoreColorProfile; bi.UriSource = new Uri(path); bi.DecodePixelWidth = px; bi.EndInit(); bi.Freeze(); bs = bi; } catch { } }
            bs ??= Thumbs.Get(path, Math.Min(px, 1024));
            Dispatcher.BeginInvoke(() =>
            {
                if (v != _version) return;
                if (bs == null) { _body.Child = Hint("No preview for this file. Press Enter to open it."); return; }
                var img = new Image { Source = bs, Stretch = Stretch.Uniform, Margin = new Thickness(12), StretchDirection = image ? StretchDirection.Both : StretchDirection.DownOnly };
                RenderOptions.SetBitmapScalingMode(img, BitmapScalingMode.HighQuality); _body.Child = img;
            });
        });
    }

    private static TextBlock Hint(string s)
    {
        var t = new TextBlock { Text = s, FontSize = 13, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(24) };
        t.SetResourceReference(TextBlock.ForegroundProperty, "FgDim"); return t;
    }
}
