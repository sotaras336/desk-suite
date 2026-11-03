using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace DeskDrawer;

/// The card pages: Home (greeting, pinned folders, drives, recent files), Quick access, This PC and Recent.
public partial class MainWindow
{
    /// Home, Quick access, This PC and Recent are pages of cards, not folder listings. Each is built from the same sections.
    private void ShowHome()
    {
        var c = HomeContent.Children; c.Clear();
        var icons = new List<(string Path, Image Img, int Px)>();
        var pins = _tab.Entries ?? new();
        switch (_tab.Current)
        {
            case QuickTag:
                PageTitle(c, "Quick access", "Folders pinned here and in File Explorer");
                PinnedSection(c, pins, icons, title: false);
                Status.Text = pins.Count == 1 ? "1 pinned folder" : $"{pins.Count} pinned folders";
                break;
            case ThisPcTag:
                int n = DriveTiles(c, icons);
                Status.Text = n == 1 ? "1 drive" : $"{n} drives";
                break;
            case RecentTag:
                PageTitle(c, "Recent", "Files you opened lately, newest first");
                RecentSection(c, 150, title: false);
                Status.Text = "Recent files";
                break;
            default:   // Home
                int h = DateTime.Now.Hour;
                PageTitle(c, h < 5 ? "Good night" : h < 12 ? "Good morning" : h < 18 ? "Good afternoon" : "Good evening", DateTime.Now.ToString("dddd, d MMMM"));
                PinnedSection(c, pins, icons, title: true);
                DrivesSection(c, icons);
                RecentSection(c, 24, title: true);
                Status.Text = pins.Count == 1 ? "1 pinned folder" : $"{pins.Count} pinned folders";
                break;
        }
        LoadIcons(icons);
    }

    private void PageTitle(UIElementCollection c, string title, string sub)
    {
        c.Add(Label(title, 26, "Fg", FontWeights.SemiBold));
        c.Add(Label(sub, 14, "FgDim", margin: new Thickness(0, 2, 0, 0)));
    }

    private void PinnedSection(UIElementCollection c, List<FileEntry> pins, List<(string, Image, int)> icons, bool title)
    {
        if (title && pins.Count > 0) c.Add(Section("", "Pinned")); else if (!title) c.Add(new Border { Height = 22 });
        if (pins.Count == 0) { if (!title) c.Add(Label("Nothing pinned yet: right-click a folder and choose Pin to Home", 13, "FgFaint", margin: new Thickness(4, 0, 0, 0))); return; }
        var wrap = new FillWrap { MinItemWidth = 250 };
        foreach (var p in pins)
        {
            string path = p.FullPath;
            var card = Card(p.Name.TrimEnd('\\'), Friendly(Path.GetDirectoryName(path.TrimEnd('\\')) ?? "This PC"), out var img, 228);
            icons.Add((path, img, 64));
            FolderCardActions(card, path, pinned: true);
            wrap.Children.Add(card);
        }
        c.Add(wrap);
    }

    private void FolderCardActions(Border card, string path, bool pinned)
    {
        card.MouseLeftButtonUp += (_, _) => Navigate(path);
        card.MouseDown += (_, e) => { if (e.ChangedButton == MouseButton.Middle) AddTab(path); };
        card.MouseRightButtonUp += (_, e) =>
        {
            var m = new ContextMenu();
            m.Items.Add(MI("Open", "", () => Navigate(path), bold: true));
            m.Items.Add(MI("Open in new tab", "", () => NewTab(path)));
            m.Items.Add(MI("Open in Terminal", "", () => OpenTerminal(path)));
            m.Items.Add(new Separator());
            m.Items.Add(MI(pinned ? "Unpin from Home" : "Pin to Home", pinned ? "" : "", () => TogglePin(path, !pinned)));
            m.Items.Add(MI("Copy as path", "", () => { try { Clipboard.SetText("\"" + path + "\""); } catch { } }));
            m.Items.Add(MI("Properties", "", () => ShellOps.Properties(path, Hwnd)));
            m.IsOpen = true; e.Handled = true;
        };
    }

    private static List<(string Root, string Name, long Free, long Total)> Drives()
    {
        var list = new List<(string, string, long, long)>();
        foreach (var d in DriveInfo.GetDrives())
        {
            long total = 0, free = 0; string label = "";
            try { if (d.IsReady) { total = d.TotalSize; free = d.AvailableFreeSpace; label = d.VolumeLabel; } } catch { }
            if (total == 0) continue;
            list.Add((d.Name, (label.Length > 0 ? label : d.DriveType == DriveType.Removable ? "USB Drive" : "Local Disk") + " (" + d.Name.TrimEnd('\\') + ")", free, total));
        }
        return list;
    }

    /// A thin bar showing how full a drive is (red past 90%, like File Explorer).
    private static Border UsageBar(long free, long total, double width)
    {
        double used = 1 - (double)free / total;
        var track = new Border { Height = 5, Width = width, CornerRadius = new CornerRadius(3), Margin = new Thickness(0, 6, 0, 0) }; track.SetResourceReference(Border.BackgroundProperty, "CardLine");
        var fill = new Border { Height = 5, Width = width * used, CornerRadius = new CornerRadius(3), HorizontalAlignment = HorizontalAlignment.Left };
        fill.SetResourceReference(Border.BackgroundProperty, used > 0.9 ? "Error" : "Accent");
        track.Child = fill; return track;
    }

    /// Home's compact drive cards. Returns how many were shown.
    private int DrivesSection(UIElementCollection c, List<(string, Image, int)> icons)
    {
        c.Add(Section("", "Drives"));
        var wrap = new FillWrap { MinItemWidth = 300 }; var drives = Drives();
        foreach (var (root, name, free, total) in drives)
        {
            var card = Card(name, $"{FileEntry.FormatSize(free)} free of {FileEntry.FormatSize(total)}", out var img, 300);
            icons.Add((root, img, 64));
            var bar = UsageBar(free, total, 0); bar.Width = double.NaN; ((Border)bar.Child).Width = double.NaN; bar.Child = null;
            var fill = new Border { Height = 5, CornerRadius = new CornerRadius(3), HorizontalAlignment = HorizontalAlignment.Left }; fill.SetResourceReference(Border.BackgroundProperty, (double)(total - free) / total > 0.9 ? "Error" : "Accent");
            bar.Child = fill; double used = (double)(total - free) / total; bar.SizeChanged += (_, _) => fill.Width = bar.ActualWidth * used;   // stretches with the card
            ((StackPanel)((Grid)card.Child).Children[1]).Children.Add(bar);
            card.MouseLeftButtonUp += (_, _) => Navigate(root);
            card.MouseDown += (_, e) => { if (e.ChangedButton == MouseButton.Middle) AddTab(root); };
            wrap.Children.Add(card);
        }
        c.Add(wrap);
        return drives.Count;
    }

    /// This PC, the way File Explorer shows it: "Devices and drives", each drive a big icon with its name, how full it is, and the free space.
    private int DriveTiles(UIElementCollection c, List<(string, Image, int)> icons)
    {
        c.Add(Section("", "Devices and drives"));
        var wrap = new WrapPanel(); var drives = Drives();
        foreach (var (root, name, free, total) in drives)
        {
            var img = new Image { Width = 96, Height = 96, HorizontalAlignment = HorizontalAlignment.Center };
            RenderOptions.SetBitmapScalingMode(img, BitmapScalingMode.HighQuality);
            icons.Add((root, img, 128));
            var sp = new StackPanel { Width = 190 };
            sp.Children.Add(img);
            var t = Label(name, 14, "Fg"); t.HorizontalAlignment = HorizontalAlignment.Center; t.Margin = new Thickness(0, 8, 0, 0); sp.Children.Add(t);
            var bar = UsageBar(free, total, 150); bar.HorizontalAlignment = HorizontalAlignment.Center; sp.Children.Add(bar);
            var f = Label($"{FileEntry.FormatSize(free)} free of {FileEntry.FormatSize(total)}", 12, "FgDim"); f.HorizontalAlignment = HorizontalAlignment.Center; f.Margin = new Thickness(0, 4, 0, 0); sp.Children.Add(f);
            var tile = new Border { Child = sp, Padding = new Thickness(10, 12, 10, 12), Margin = new Thickness(0, 0, 8, 8), CornerRadius = new CornerRadius(10), Background = Brushes.Transparent, Cursor = Cursors.Hand, ToolTip = root };
            tile.MouseEnter += (_, _) => tile.SetResourceReference(Border.BackgroundProperty, "CardHover");
            tile.MouseLeave += (_, _) => tile.Background = Brushes.Transparent;
            tile.MouseLeftButtonUp += (_, _) => Navigate(root);
            tile.MouseDown += (_, e) => { if (e.ChangedButton == MouseButton.Middle) AddTab(root); };
            tile.MouseRightButtonUp += (_, e) =>
            {
                var m = new ContextMenu();
                m.Items.Add(MI("Open", "", () => Navigate(root), bold: true));
                m.Items.Add(MI("Open in new tab", "", () => NewTab(root)));
                m.Items.Add(MI("Open in Terminal", "", () => OpenTerminal(root)));
                m.Items.Add(new Separator());
                m.Items.Add(MI("Properties", "", () => ShellOps.Properties(root, Hwnd)));
                m.Items.Add(MI("Show more options", "", () => { _menuScreen = PointToScreen(e.GetPosition(this)); Dispatcher.BeginInvoke(() => ShowMoreOptions(new List<string> { root })); }));
                m.IsOpen = true; e.Handled = true;
            };
            wrap.Children.Add(tile);
        }
        c.Add(wrap);
        return drives.Count;
    }

    private void RecentSection(UIElementCollection c, int max, bool title)
    {
        var host = new StackPanel();
        if (title) c.Add(Section("", "Recent files")); else c.Add(new Border { Height = 18 });
        c.Add(host);
        host.Children.Add(Label("Looking…", 13, "FgFaint", margin: new Thickness(4, 0, 0, 0)));
        var tab = _tab; string page = _tab.Current ?? "";
        Pinned.Recent(max, list =>
        {
            if (tab != _tab || tab.Current != page) return;   // moved on meanwhile
            host.Children.Clear();
            if (list.Count == 0) { host.Children.Add(Label("Nothing opened lately", 13, "FgFaint", margin: new Thickness(4, 0, 0, 0))); return; }
            var rowIcons = new List<(string, Image, int)>();
            foreach (var (path, when) in list) { host.Children.Add(RecentRow(path, when, out var img)); rowIcons.Add((path, img, 32)); }
            LoadIcons(rowIcons);
        });
    }

    private Border RecentRow(string path, DateTime when, out Image img)
    {
        var g = new Grid { Margin = new Thickness(10, 6, 12, 6) };
        g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(34) }); g.ColumnDefinitions.Add(new ColumnDefinition()); g.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        img = new Image { Width = 24, Height = 24, VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Left };
        var text = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
        text.Children.Add(Label(Path.GetFileName(path), 14, "Fg"));
        text.Children.Add(Label(Friendly(Path.GetDirectoryName(path) ?? ""), 12, "FgDim"));
        var t = Label(Ago(when), 12, "FgDim"); t.VerticalAlignment = VerticalAlignment.Center; t.Margin = new Thickness(16, 0, 0, 0);
        Grid.SetColumn(text, 1); Grid.SetColumn(t, 2); g.Children.Add(img); g.Children.Add(text); g.Children.Add(t);
        var row = new Border { Child = g, CornerRadius = new CornerRadius(8), Background = Brushes.Transparent, Cursor = Cursors.Hand, ToolTip = path };
        row.MouseEnter += (_, _) => row.SetResourceReference(Border.BackgroundProperty, "CardHover");
        row.MouseLeave += (_, _) => row.Background = Brushes.Transparent;
        row.MouseLeftButtonUp += (_, _) => OpenFile(path);
        row.MouseRightButtonUp += (_, e) =>
        {
            var m = new ContextMenu();
            m.Items.Add(MI("Open", "\uE8E5", () => OpenFile(path), bold: true));
            m.Items.Add(MI("Open file location", "\uE838", () => ShowInFolder(path)));
            m.Items.Add(MI("Copy as path", "\uE8C8", () => { try { Clipboard.SetText("\"" + path + "\""); } catch { } }));
            m.IsOpen = true; e.Handled = true;
        };
        return row;
    }

    private void ShowInFolder(string path)
    {
        if (Path.GetDirectoryName(path) is not { } dir) return;
        _tab.SelectAfterLoad = new HashSet<string>(new[] { path }, StringComparer.OrdinalIgnoreCase); Navigate(dir);
    }

    /// A rounded card: big icon on the left, a title and a dimmer line under it. The icon is filled in later by LoadIcons.
    private Border Card(string title, string sub, out Image img, double width)
    {
        var g = new Grid();
        g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(52) }); g.ColumnDefinitions.Add(new ColumnDefinition());
        img = new Image { Width = 40, Height = 40, VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Left };
        RenderOptions.SetBitmapScalingMode(img, BitmapScalingMode.HighQuality);
        var text = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
        var t = Label(title, 14, "Fg", FontWeights.SemiBold); t.TextTrimming = TextTrimming.CharacterEllipsis;
        var s = Label(sub, 12, "FgDim"); s.TextTrimming = TextTrimming.CharacterEllipsis; s.Margin = new Thickness(0, 2, 0, 0);
        text.Children.Add(t); text.Children.Add(s); Grid.SetColumn(text, 1);
        g.Children.Add(img); g.Children.Add(text);
        var b = new Border { Child = g, MinWidth = Math.Min(width, 200), Padding = new Thickness(14, 12, 14, 12), CornerRadius = new CornerRadius(12), BorderThickness = new Thickness(1), Cursor = Cursors.Hand, ToolTip = title };
        b.SetResourceReference(Border.BackgroundProperty, "Card"); b.SetResourceReference(Border.BorderBrushProperty, "CardLine");
        b.MouseEnter += (_, _) => b.SetResourceReference(Border.BackgroundProperty, "CardHover");
        b.MouseLeave += (_, _) => b.SetResourceReference(Border.BackgroundProperty, "Card");
        return b;
    }

    private FrameworkElement Section(string glyph, string title)
    {
        var sp = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(2, 26, 0, 12) };
        var g = new TextBlock { Text = glyph, FontFamily = (FontFamily)FindResource("IconFont"), FontSize = 15, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 10, 0) };
        g.SetResourceReference(TextBlock.ForegroundProperty, "Accent");
        sp.Children.Add(g); sp.Children.Add(Label(title, 16, "Fg", FontWeights.SemiBold));
        return sp;
    }

    private static TextBlock Label(string text, double size, string brush, FontWeight? weight = null, Thickness? margin = null)
    {
        var t = new TextBlock { Text = text, FontSize = size, FontWeight = weight ?? FontWeights.Normal, Margin = margin ?? new Thickness(0), TextTrimming = TextTrimming.CharacterEllipsis };
        t.SetResourceReference(TextBlock.ForegroundProperty, brush); return t;
    }

    /// Big shell pictures for the cards and rows, fetched on one STA thread.
    private static void LoadIcons(List<(string Path, Image Img, int Px)> jobs)
    {
        if (jobs.Count == 0) return;
        Thumbs.OnSta(() =>
        {
            foreach (var (path, img, px) in jobs)
            {
                var bs = Thumbs.Get(path, px); if (bs == null) continue;
                Application.Current?.Dispatcher.BeginInvoke(() => img.Source = bs);
            }
        });
    }

    /// "OneDrive › Desktop › notes" instead of C:\Users\you\OneDrive\Desktop\notes.
    private static string Friendly(string dir)
    {
        if (dir.Length == 0) return "";
        string profile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        string rest = dir.StartsWith(profile, StringComparison.OrdinalIgnoreCase) ? dir.Substring(profile.Length).TrimStart('\\') : dir.TrimEnd('\\');
        if (rest.Length == 0) return Path.GetFileName(profile);
        return string.Join(" › ", rest.Split('\\', StringSplitOptions.RemoveEmptyEntries));
    }

    private static string Ago(DateTime t)
    {
        var d = DateTime.Now - t;
        if (d.TotalMinutes < 1) return "Just now";
        if (d.TotalMinutes < 60) return $"{(int)d.TotalMinutes} min ago";
        if (t.Date == DateTime.Today) return "Today " + t.ToString("t");
        if (t.Date == DateTime.Today.AddDays(-1)) return "Yesterday " + t.ToString("t");
        return t.ToString(t.Year == DateTime.Now.Year ? "d MMM" : "d MMM yyyy");
    }
}
