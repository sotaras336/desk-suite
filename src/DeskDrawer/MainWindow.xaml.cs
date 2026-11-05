using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using static DeskDrawer.Native;

namespace DeskDrawer;

public partial class MainWindow : Window
{
    private readonly ObservableCollection<TabState> _tabs = new();
    private TabState _tab = null!;
    private readonly Settings _settings = Settings.Load();
    private FileSystemWatcher? _watch;
    private readonly DispatcherTimer _reload = new() { Interval = TimeSpan.FromMilliseconds(350) };
    private bool _binding;
    private Point _dragStart; private FileEntry? _dragItem, _pendingSingle;
    private System.Windows.Point _menuScreen;
    private ContextMenu? _lastMenu;
    private const string Placeholder = "<loading>";
    private readonly DispatcherTimer _previewTimer = new() { Interval = TimeSpan.FromMilliseconds(120) };
    private bool _previewOpen;
    private string _sortKey = "Name"; private bool _sortAsc = true;
    private static readonly string[] ColumnNames = { "Name", "Date modified", "Type", "Size" };

    public MainWindow()
    {
        InitializeComponent();
        BuildCommandBar(); Activated += (_, _) => UpdateCommands();   // the clipboard may have changed while we were in the background
        _grid = (GridView)List.View; _rowsPanel = List.ItemsPanel;
        _iconSize = _settings.IconSize; _zoom = 1; _groupBy = _settings.GroupBy;   // zoom now steps through the views instead of scaling
        SetViewMode(App.ForcedView ?? _settings.ViewMode); ApplyZoom(); 
        List.PreviewMouseWheel += List_Wheel;
        if (App.SnapshotFile == null)
        {
            var wa = SystemParameters.WorkArea;   // layout units shrink as scaling grows (a 1080p screen at 150% is ~1280 x 670): never open larger than the screen
            Width = Math.Min(_settings.Width, wa.Width - 24); Height = Math.Min(_settings.Height, wa.Height - 24); if (_settings.Maximized) WindowState = WindowState.Maximized;
        }
        TabStrip.ItemsSource = _tabs;
        _sortKey = _settings.SortKey; _sortAsc = _settings.SortAscending; UpdateSortHeaders();
        List.AddHandler(GridViewColumnHeader.ClickEvent, new RoutedEventHandler(Header_Click));
        _previewTimer.Tick += (_, _) => { _previewTimer.Stop(); UpdatePreview(); };
        SetPreview(App.SnapshotFile != null);   // the preview pane starts closed; Alt+P or View opens it
        SourceInitialized += (_, _) => { if (App.Background) Cloak(true); ApplyChrome(); HwndSource.FromHwnd(Hwnd)!.AddHook(CaptionHook); OnStateChanged(); };
        if (App.Background) { ShowActivated = false; _maxAfterWarmUp = WindowState == WindowState.Maximized; WindowState = WindowState.Normal; ContentRendered += WarmedUp; }   // WPF refuses a maximised window that doesn't take the focus
        StateChanged += (_, _) => OnStateChanged();
        MouseMove += (_, _) => MaxBtn.Tag = null;   // back in the client area: no longer over the (hit-test) maximise button
        Loaded += (_, _) => { Start(); ShellNew.Preload(); };
        Closing += (_, e) => { SaveSession(); if (App.Resident && _settings.KeepReady && !App.Quitting) { e.Cancel = true; GoToBackground(); } };
        PreviewKeyDown += Window_PreviewKeyDown;
        PreviewMouseDown += (_, e) => { if (e.ChangedButton == MouseButton.XButton1) { GoBack(); e.Handled = true; } else if (e.ChangedButton == MouseButton.XButton2) { GoForward(); e.Handled = true; } };   // the mouse's back / forward buttons
        _reload.Tick += (_, _) => { _reload.Stop(); ReloadActive(); };
        BuildTree();
    }

    // Cached: the shell operations ask for the owner window from a worker thread, and WPF refuses WindowInteropHelper.Handle off the UI thread
    // (that exception was swallowed by the task, so Delete silently did nothing). Set on the UI thread once the window exists.
    private IntPtr _hwnd;
    private IntPtr Hwnd => _hwnd != IntPtr.Zero ? _hwnd : (_hwnd = new WindowInteropHelper(this).Handle);

    // ---------- window chrome: title bar in the theme colour, glass backdrop ----------

    private static System.Windows.Media.Color Lighter(System.Windows.Media.Color c) => System.Windows.Media.Color.FromRgb((byte)(c.R + (255 - c.R) * 0.28), (byte)(c.G + (255 - c.G) * 0.28), (byte)(c.B + (255 - c.B) * 0.28));
    private static int ColorRef(System.Windows.Media.Color c) => c.R | (c.G << 8) | (c.B << 16);

    private void ApplyChrome()
    {
        var h = Hwnd;
        int round = 2; DwmSetWindowAttribute(h, 33, ref round, 4);   // DWMWA_WINDOW_CORNER_PREFERENCE = round: a borderless window gets square corners unless asked
        int dark = Theme.IsLight ? 0 : 1; DwmSetWindowAttribute(h, 20, ref dark, 4);
        if (Theme.IsGlass)
        {
            HwndSource.FromHwnd(h)!.CompositionTarget.BackgroundColor = Colors.Transparent;
            System.Windows.Shell.WindowChrome.GetWindowChrome(this).GlassFrameThickness = new Thickness(-1);   // keep the acrylic behind the whole window
            var m = new MARGINS { Left = -1, Right = -1, Top = -1, Bottom = -1 }; DwmExtendFrameIntoClientArea(h, ref m);
            int acrylic = 3; DwmSetWindowAttribute(h, 38, ref acrylic, 4); _glassOn = true;
        }
        else
        {
            if (_glassOn)   // switched away from glass: give the window its solid frame back
            {
                System.Windows.Shell.WindowChrome.GetWindowChrome(this).GlassFrameThickness = new Thickness(0, 0, 0, 1);   // the 1 px keeps the window shadow
                var none = new MARGINS(); DwmExtendFrameIntoClientArea(h, ref none);
                int off = 1; DwmSetWindowAttribute(h, 38, ref off, 4); HwndSource.FromHwnd(h)!.CompositionTarget.BackgroundColor = Colors.Black; _glassOn = false;
            }
            int cap = ColorRef(((SolidColorBrush)FindResource("StripBg")).Color), txt = ColorRef(((SolidColorBrush)FindResource("Fg")).Color);
            DwmSetWindowAttribute(h, 35, ref cap, 4); DwmSetWindowAttribute(h, 36, ref txt, 4); int edge = ColorRef(Lighter(((SolidColorBrush)FindResource("StripBg")).Color)); DwmSetWindowAttribute(h, 34, ref edge, 4);   // a visible outline: same colour as the strip made the window edge vanish into dark backgrounds
        }
    }

    private bool _glassOn;

    // ---------- our own title bar: the tab strip, plus minimise / maximise / close ----------

    private void Min_Click(object s, RoutedEventArgs e) => WindowState = WindowState.Minimized;
    private void Max_Click(object s, RoutedEventArgs e) => ToggleMax();
    private void Close_Click(object s, RoutedEventArgs e) => Close();
    private void ToggleMax() => WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;

    /// A maximised borderless window hangs over the screen edge by its resize frame: pad it back in. Also swaps the maximise / restore glyph.
    private void OnStateChanged()
    {
        bool max = WindowState == WindowState.Maximized;
        double k = VisualTreeHelper.GetDpi(this).DpiScaleX, f = (GetSystemMetrics(32) + GetSystemMetrics(92)) / k;   // SM_CXSIZEFRAME + SM_CXPADDEDBORDER
        Root.Margin = max ? new Thickness(f) : new Thickness(0);
        MaxBtn.Content = max ? "" : ""; MaxBtn.ToolTip = max ? "Restore" : "Maximise";
    }

    [DllImport("user32.dll")] private static extern int GetSystemMetrics(int i);

    /// Windows 11 shows its snap layouts when the pointer rests on a window's maximise button, which it recognises by hit-test: so we answer HTMAXBUTTON there
    /// and do the click ourselves.
    private IntPtr CaptionHook(IntPtr hwnd, int msg, IntPtr w, IntPtr l, ref bool handled)
    {
        const int WM_NCHITTEST = 0x84, WM_NCLBUTTONDOWN = 0xA1, WM_NCLBUTTONUP = 0xA2, HTMAXBUTTON = 9;
        if (msg == WM_NCHITTEST)
        {
            bool over = OverMax(l); MaxBtn.Tag = over ? "hover" : null;
            if (over) { handled = true; return (IntPtr)HTMAXBUTTON; }
        }
        else if ((msg == WM_NCLBUTTONDOWN || msg == WM_NCLBUTTONUP) && w.ToInt32() == HTMAXBUTTON)
        {
            handled = true; if (msg == WM_NCLBUTTONUP) { MaxBtn.Tag = null; ToggleMax(); }
        }
        return IntPtr.Zero;
    }

    private bool OverMax(IntPtr l)
    {
        if (!MaxBtn.IsVisible) return false;
        long v = l.ToInt64(); int x = (short)(v & 0xFFFF), y = (short)((v >> 16) & 0xFFFF);
        var a = MaxBtn.PointToScreen(new Point(0, 0)); var b = MaxBtn.PointToScreen(new Point(MaxBtn.ActualWidth, MaxBtn.ActualHeight));
        return x >= a.X && x < b.X && y >= a.Y && y < b.Y;
    }

    // ---------- start / session ----------

    private void Start()
    {
        OpenTabs(App.StartPaths);
        if (App.SnapshotFile != null)
        {
            var t = new DispatcherTimer { Interval = TimeSpan.FromSeconds(3) };
            t.Tick += (_, _) => { t.Stop();
                if (App.SelectName != null && _tab.Entries?.FirstOrDefault(x => x.Name == App.SelectName) is { } sn) { List.SelectedItems.Clear(); List.SelectedItems.Add(sn); }
                if (App.ShowMore && _tab.Entries is { Count: > 0 }) { List.SelectedItems.Clear(); List.SelectedItems.Add(_tab.Entries.First(x => !x.IsDir)); _menuScreen = PointToScreen(new Point(300, 200)); ShowMoreOptions(Sel().Select(x => x.FullPath).ToList()); }
                else if (App.ShowMenu) { if (_tab.Entries is { Count: > 0 } && !App.BgMenu) { List.SelectedItems.Clear(); List.SelectedItems.Add(_tab.Entries[0]); } ShowMenu(); } var t2 = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(700) }; t2.Tick += (_, _) => { t2.Stop(); Snapshot(App.SnapshotFile); Application.Current.Shutdown(); }; t2.Start(); };
            t.Start();
        }
    }

    /// The folders asked for, or else the tabs open last time (This PC, Home... included; searches aren't re-run), or else Documents.
    private void OpenTabs(IEnumerable<string> asked)
    {
        var paths = asked.Where(p => IsVirtual(p) || Directory.Exists(p)).ToList(); int active = paths.Count - 1;
        if (paths.Count == 0 && App.SnapshotFile == null) { paths = _settings.Tabs.Where(p => IsPage(p) || Directory.Exists(p)).ToList(); active = _settings.ActiveTab; }
        if (paths.Count == 0) paths.Add(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments));
        foreach (var p in paths) AddTab(p);
        Activate(_tabs[Math.Clamp(active, 0, _tabs.Count - 1)]);
    }

    // ---------- kept ready in the background ----------

    /// Closing the window keeps DeskDrawer running out of sight, like File Explorer, so the next folder opens in about half a second instead of
    /// a ~2.5 s cold start. The tabs and their lists are dropped and the memory handed back while it waits.
    private void GoToBackground()
    {
        Hide();
        foreach (var t in _tabs) t.Cts?.Cancel();
        SharedIndex.Release();   // the map of DeskBar's file list goes back to the system while we sleep
        _tabs.Clear(); _tab = new TabState();
        List.ItemsSource = null; HomeContent.Children.Clear();
        _watch?.Dispose(); _watch = null; _reload.Stop(); _previewTimer.Stop();
        Dispatcher.BeginInvoke(() => { GC.Collect(2, GCCollectionMode.Aggressive, true, true); SetProcessWorkingSetSize(Process.GetCurrentProcess().Handle, -1, -1); }, DispatcherPriority.ApplicationIdle);
    }

    /// Sign-in start: the window draws once while cloaked (so WPF and our code are loaded and compiled), lets the file list draw too, then waits hidden.
    private void WarmedUp(object? s, EventArgs e)
    {
        ContentRendered -= WarmedUp;
        _warm = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) }; _warm.Tick += (_, _) => EndWarmUp(); _warm.Start();
    }

    private DispatcherTimer? _warm; private bool _maxAfterWarmUp;

    /// Shows the hidden window again with these folders (none = the last session).
    private void Wake(IEnumerable<string> paths)
    {
        OpenTabs(paths);
        if (_maxAfterWarmUp) { _maxAfterWarmUp = false; WindowState = WindowState.Maximized; }
        Show();
    }

    private void EndWarmUp()
    {
        ContentRendered -= WarmedUp; _warm?.Stop(); _warm = null;
        GoToBackground(); Cloak(false); ShowActivated = true; App.Background = false;
    }

    private void Cloak(bool on) { int v = on ? 1 : 0; DwmSetWindowAttribute(Hwnd, 13 /*DWMWA_CLOAK*/, ref v, 4); }

    [DllImport("kernel32.dll")] private static extern bool SetProcessWorkingSetSize(IntPtr process, nint min, nint max);

    private void SaveSession()
    {
        if (App.SnapshotFile != null || App.UiTest != null) return;
        _settings.Tabs = _tabs.Select(t => t.Current).Where(p => p != null).Select(p => p!).ToList(); _settings.ActiveTab = Math.Max(0, _tabs.IndexOf(_tab));
        _settings.Maximized = WindowState == WindowState.Maximized;
        _settings.PreviewOpen = _previewOpen; if (_previewOpen && PreviewCol.ActualWidth > 120) _settings.PreviewWidth = PreviewCol.ActualWidth;
        _settings.Theme = Theme.Current.Id;   // sort and view are saved per folder (Folders)
        var b = WindowState == WindowState.Normal ? new Rect(Left, Top, Width, Height) : RestoreBounds; _settings.Width = b.Width; _settings.Height = b.Height;
        _settings.Save();
    }

    private void Snapshot(string file)
    {
        var root = (FrameworkElement)Content; var dpi = VisualTreeHelper.GetDpi(root);
        var rtb = new RenderTargetBitmap((int)(root.ActualWidth * dpi.DpiScaleX), (int)(root.ActualHeight * dpi.DpiScaleY), 96 * dpi.DpiScaleX, 96 * dpi.DpiScaleY, PixelFormats.Pbgra32);
        var back = new DrawingVisual();   // the window's own background is not part of its content: paint it first
        using (var dc = back.RenderOpen()) dc.DrawRectangle((Brush)FindResource("WinBg"), null, new Rect(0, 0, root.ActualWidth, root.ActualHeight));
        rtb.Render(back); rtb.Render(root);
        Save(rtb, file);
        if (_lastMenu is { IsOpen: true } m && m.ActualWidth > 0)
        {
            var mb = new RenderTargetBitmap((int)(m.ActualWidth * dpi.DpiScaleX), (int)(m.ActualHeight * dpi.DpiScaleY), 96 * dpi.DpiScaleX, 96 * dpi.DpiScaleY, PixelFormats.Pbgra32);
            var mbg = new DrawingVisual(); using (var dc = mbg.RenderOpen()) dc.DrawRectangle((Brush)FindResource("WinBg"), null, new Rect(0, 0, m.ActualWidth, m.ActualHeight));
            mb.Render(mbg); mb.Render(m); Save(mb, file + ".menu.png");
        }
        File.WriteAllText(file + ".txt", $"theme={Theme.Current.Id} tabs={_tabs.Count} folder={_tab.Current} items={_tab.View?.Count} pins={_quick?.Items.Count} nav={string.Join("|", Tree.Items.OfType<TreeViewItem>().Select(t => ((t.Header as StackPanel)?.Children[1] as TextBlock)?.Text))} pinErr={Pinned.LastError} size={root.ActualWidth}x{root.ActualHeight}");
    }

    private static void Save(BitmapSource bs, string file)
    {
        var enc = new PngBitmapEncoder(); enc.Frames.Add(BitmapFrame.Create(bs));
        using var fs = File.Create(file); enc.Save(fs);
    }

    // ---------- tabs ----------

    private void AddTab(string path)
    {
        var t = new TabState(); _tabs.Add(t); NavigateTab(t, path, push: false);
    }

    private void Activate(TabState t)
    {
        foreach (var x in _tabs) x.IsActive = x == t;
        _tab = t; BindTab();
    }

    private void CloseTab(TabState t)
    {
        if (_tabs.Count == 1) { Close(); return; }
        int i = _tabs.IndexOf(t); t.Cts?.Cancel(); _tabs.Remove(t); if (t.Current != null) _closed.Push(t.Current);
        if (t == _tab) Activate(_tabs[Math.Min(i, _tabs.Count - 1)]);
    }

    private static TabState? TabOf(object sender) => (sender as FrameworkElement)?.DataContext as TabState;
    private void Tab_Down(object s, MouseButtonEventArgs e) { if (TabOf(s) is { } t) { if (t != _tab) Activate(t); _dragTab = t; } }
    private void Tab_MiddleDown(object s, MouseButtonEventArgs e) { if (e.ChangedButton == MouseButton.Middle && TabOf(s) is { } t) { CloseTab(t); e.Handled = true; } }
    // dragging files over a tab opens it (so you can aim at a folder inside); dropping on the tab itself moves / copies into its folder
    private void Tab_DragEnter(object s, DragEventArgs e) { if (TabOf(s) is { } t && t != _tab) Activate(t); }
    private void Tab_DragOver(object s, DragEventArgs e)
    {
        e.Effects = TabOf(s) is { Current: { } dir } && !IsVirtual(dir) && e.Data.GetData(DataFormats.FileDrop) is string[] { Length: > 0 } src ? EffectFor(src, dir, e.KeyStates) : DragDropEffects.None;
        e.Handled = true;
    }
    private void Tab_Drop(object s, DragEventArgs e) { if (TabOf(s) is { Current: { } dir } && !IsVirtual(dir)) DoDrop(e, dir); e.Handled = true; }

    private void TabClose_Click(object s, RoutedEventArgs e) { if (TabOf(s) is { } t) CloseTab(t); e.Handled = true; }
    private void NewTab_Click(object s, RoutedEventArgs e) => NewTab(_tab.Current);

    private void NewTab(string? path)
    {
        path ??= Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
        AddTab(path); Activate(_tabs[^1]);
    }

    /// Shows the active tab: its path, its filter, its list, and what is selected afterwards (after a paste, a new folder...).
    private void BindTab()
    {
        ApplyFolderView();
        _binding = true;
        Address.Text = _tab.Current ?? ""; BuildCrumbs(); ShowFolderColumn(InSearch); Search.Text = _tab.Filter; SearchHint.Visibility = Search.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
        List.ItemsSource = _tab.View; _binding = false;
        UpdateNavButtons(); UpdateCommands();
        Title = _tab.Title + " - DeskDrawer";
        Message.Visibility = Visibility.Collapsed;
        if (_tab.Loading) Status.Text = "Loading…";
        else
        {
            if (_tab.Error != null) { Message.Text = _tab.Error; Message.Visibility = Visibility.Visible; }
            else if (_tab.Entries is { Count: 0 }) { Message.Text = "This folder is empty"; Message.Visibility = Visibility.Visible; }
            UpdateStatus();
        }
        SelectionStatus.Text = ""; PreviewSoon();
        if (_tab.Entries != null)
        {
            if (!_tab.IconsRequested) { _tab.IconsRequested = true; IconCache.Request(_tab.Entries); }
            RequestThumbs();
            if (_tab.SelectAfterLoad.Count > 0)
            {
                List.SelectedItems.Clear(); FileEntry? first = null;
                foreach (var en in _tab.Entries) if (_tab.SelectAfterLoad.Contains(en.FullPath)) { List.SelectedItems.Add(en); first ??= en; }
                _tab.SelectAfterLoad.Clear(); if (first != null) List.ScrollIntoView(first);
            }
            if (_tab.RenameAfterLoad is { } rp) { _tab.RenameAfterLoad = null; if (_tab.Entries.FirstOrDefault(x => string.Equals(x.FullPath, rp, StringComparison.OrdinalIgnoreCase)) is { } re) BeginRename(re); }
        }
        ResetWatcher(); ShowSearchBar();
        bool home = InHome;
        HomePane.Visibility = home ? Visibility.Visible : Visibility.Collapsed; List.Visibility = home ? Visibility.Collapsed : Visibility.Visible;
        if (_previewOpen)   // nothing to preview on Home: give the cards the room, keep the setting
        {
            PreviewCol.Width = new GridLength(home ? 0 : _settings.PreviewWidth); PreviewSplitCol.Width = new GridLength(home ? 0 : 6);
            Preview.Visibility = PreviewSplit.Visibility = home ? Visibility.Collapsed : Visibility.Visible;
        }
        if (home) { Message.Visibility = Visibility.Collapsed; if (!_tab.Loading) ShowHome(); }
    }

    // ---------- navigation ----------

    private void Navigate(string path, bool push = true) => NavigateTab(_tab, path, push);

    /// "Home" is not a real folder: it lists the folders pinned in Quick access.
    private const string HomeTag = "shell:Home";
    private const string QuickTag = "shell:QuickAccess", ThisPcTag = "shell:ThisPC", RecentTag = "shell:Recent";
    private static readonly Dictionary<string, (string Title, string Glyph)> Pages = new()
    {
        [HomeTag] = ("Home", "\uE80F"), [QuickTag] = ("Quick access", "\uE734"), [ThisPcTag] = ("This PC", "\uE7F8"), [RecentTag] = ("Recent", "\uE81C"),
    };
    /// Home, Quick access, This PC, Recent: pages of cards instead of a file list.
    private static bool IsPage(string? p) => p != null && Pages.ContainsKey(p);
    private bool InHome => IsPage(_tab.Current);
    /// "search:<folder>?<words>": everything under <folder> whose name contains <words> (Enter in the filter box). '?' can't be in a path, so it splits safely.
    private const string SearchTag = "search:";
    private static bool IsSearch(string? p) => p?.StartsWith(SearchTag, StringComparison.Ordinal) == true;
    private static (string Root, string Query) SearchParts(string p) { int q = p.IndexOf('?'); return (p[SearchTag.Length..q], p[(q + 1)..]); }
    private bool InSearch => IsSearch(_tab.Current);
    /// Home and search results: not a real folder, so nothing can be pasted, created or dropped "here".
    private static bool IsVirtual(string? p) => IsPage(p) || IsSearch(p);

    private void NavigateTab(TabState tab, string path, bool push)
    {
        if (!IsVirtual(path))
        {
            try { path = Path.GetFullPath(Environment.ExpandEnvironmentVariables(path.Trim().Trim('"'))); } catch { if (tab == _tab) FlashAddressError(); return; }
            if (!Directory.Exists(path)) { if (tab == _tab) FlashAddressError(); return; }
        }
        if (push && tab.Current != null && !string.Equals(tab.Current, path, StringComparison.OrdinalIgnoreCase)) { tab.Back.Push(tab.Current); tab.Fwd.Clear(); }
        tab.Current = path; tab.Filter = ""; tab.Kind = ""; tab.View = null; tab.Entries = null; tab.IconsRequested = false;
        tab.Title = IsPage(path) ? Pages[path].Title : IsSearch(path) ? "Search: " + SearchParts(path).Query : Path.GetFileName(path.TrimEnd('\\')) is { Length: > 0 } leaf ? leaf : path;
        string iconOf = IsSearch(path) ? SearchParts(path).Root : path;
        tab.Glyph = IsPage(path) ? Pages[path].Glyph : "\uE8B7";
        tab.Icon = null; if (!IsPage(path)) IconCache.PathIcon(iconOf, true, ic => { if (tab.Current == path) tab.Icon = ic; });
        LoadTab(tab);
        if (tab == _tab) BindTab();
    }

    private void UpdateNavButtons()
    {
        BackBtn.IsEnabled = _tab.Back.Count > 0; ForwardBtn.IsEnabled = _tab.Fwd.Count > 0;
        UpBtn.IsEnabled = _tab.Current != null && !IsVirtual(_tab.Current) && Directory.GetParent(_tab.Current) != null;
    }

    private void GoBack() { if (_tab.Back.Count == 0 || _tab.Current == null) return; _tab.Fwd.Push(_tab.Current); Navigate(_tab.Back.Pop(), push: false); }
    private void GoForward() { if (_tab.Fwd.Count == 0 || _tab.Current == null) return; _tab.Back.Push(_tab.Current); Navigate(_tab.Fwd.Pop(), push: false); }
    private void GoUp() { if (_tab.Current != null && !IsVirtual(_tab.Current) && Directory.GetParent(_tab.Current) is { } parent) Navigate(parent.FullName); }

    private void Back_Click(object s, RoutedEventArgs e) => GoBack();
    private void Forward_Click(object s, RoutedEventArgs e) => GoForward();
    private void Up_Click(object s, RoutedEventArgs e) => GoUp();
    private void Refresh_Click(object s, RoutedEventArgs e) => ReloadActive();

    // ---------- keyboard ----------

    private void Window_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        bool alt = (Keyboard.Modifiers & ModifierKeys.Alt) != 0, ctrl = (Keyboard.Modifiers & ModifierKeys.Control) != 0, shift = (Keyboard.Modifiers & ModifierKeys.Shift) != 0;
        var key = e.Key == Key.System ? e.SystemKey : e.Key;
        bool done = true;
        if (alt && key == Key.Left) GoBack();
        else if (alt && key == Key.Right) GoForward();
        else if (alt && key == Key.Up) GoUp();
        else if (key == Key.F5) ReloadActive();
        else if (alt && key == Key.P) SetPreview(!_previewOpen);
        else if ((ctrl && key == Key.L) || key == Key.F4 || (alt && key == Key.D)) EditAddress();
        else if ((ctrl && key == Key.E) || key == Key.F3) Search.Focus();
        else if (ctrl && shift && key == Key.C && Keyboard.FocusedElement is not TextBox) CopyPaths();
        else if (ctrl && key == Key.H) ToggleHidden();
        else if (ctrl && key == Key.N && !shift) OpenNewWindow();
        else if (ctrl && key == Key.Z && Keyboard.FocusedElement is not TextBox) Undo();
        else if (ctrl && key is Key.OemPlus or Key.Add) Zoom(1);
        else if (ctrl && key is Key.OemMinus or Key.Subtract) Zoom(-1);
        else if (ctrl && !shift && key is Key.D0 or Key.NumPad0) Zoom(0);
        else if (ctrl && shift && key is >= Key.D1 and <= Key.D7) { var v = Views[key - Key.D1]; SetViewMode(v.Mode, v.Size); }
        else if (ctrl && key == Key.F) Search.Focus();
        else if (ctrl && shift && key == Key.T) ReopenClosed();
        else if (ctrl && key == Key.T) NewTab(_tab.Current);
        else if (ctrl && key == Key.W) CloseTab(_tab);
        else if (ctrl && key == Key.Tab) Activate(_tabs[(_tabs.IndexOf(_tab) + (shift ? _tabs.Count - 1 : 1)) % _tabs.Count]);
        else if (Keyboard.FocusedElement is TextBox) done = false;   // typing in the address, filter or rename box: leave the keys alone
        else if (!List.IsKeyboardFocusWithin) done = false;
        else if (ctrl && key == Key.C) CopySelection(false);
        else if (ctrl && key == Key.X) CopySelection(true);
        else if (ctrl && key == Key.V) Paste(PasteTarget());
        else if (key == Key.Delete) DeleteSelection(shift);
        else if (key == Key.F2) RenameSelection();
        else if (ctrl && shift && key == Key.N) NewItem(folder: true);
        else if (alt && key == Key.Enter) ShowProperties();
        else done = false;
        if (done) e.Handled = true;
    }

    // ---------- address bar and filter ----------

    private void Address_GotFocus(object s, KeyboardFocusChangedEventArgs e) => Address.SelectAll();

    /// The path bar is clickable crumbs (each with a ▸ that lists its sub-folders); Ctrl+L or a click on empty space swaps it for a text box.
    private void EditAddress() { Address.Text = _tab.Current ?? ""; Address.Visibility = Visibility.Visible; CrumbBar.Visibility = Visibility.Hidden; Address.Focus(); }
    private void Address_Lost(object s, KeyboardFocusChangedEventArgs e) { Address.Visibility = Visibility.Hidden; CrumbBar.Visibility = Visibility.Visible; Address.Text = _tab.Current ?? ""; }
    private void CrumbBar_Down(object s, MouseButtonEventArgs e) { EditAddress(); e.Handled = true; }

    private void BuildCrumbs()
    {
        Crumbs.Children.Clear();
        if (_tab.Current is not { } p) return;
        if (IsPage(p)) { Crumbs.Children.Add(new Button { Content = Pages[p].Title, Style = (Style)FindResource("Crumb") }); return; }
        if (IsSearch(p))
        {
            var (sroot, q) = SearchParts(p); string leaf = Path.GetFileName(sroot.TrimEnd('\\')), where = sroot == ThisPcTag ? "this PC" : leaf.Length > 0 ? leaf : sroot;
            var back = new Button { Content = "Search results for “" + q + "” in " + where, Style = (Style)FindResource("Crumb"), ToolTip = "Back to " + sroot };
            back.Click += (_, _) => Navigate(sroot); Crumbs.Children.Add(back); return;
        }
        string root = Path.GetPathRoot(p) ?? p, acc = root;
        var parts = new List<(string Name, string Path)> { (root.TrimEnd('\\'), root) };
        foreach (var seg in p.Substring(root.Length).Split('\\', StringSplitOptions.RemoveEmptyEntries)) { acc = Path.Combine(acc, seg); parts.Add((seg, acc)); }
        foreach (var (name, path) in parts)
        {
            var b = new Button { Content = name, Style = (Style)FindResource("Crumb"), ToolTip = path };
            b.Click += (_, _) => Navigate(path);
            var a = new Button { Content = "", Style = (Style)FindResource("CrumbArrow"), ToolTip = "Folders inside " + name };
            a.Click += (_, _) => ShowSubfolders(a, path);
            Crumbs.Children.Add(b); Crumbs.Children.Add(a);
        }
        Dispatcher.BeginInvoke(() => CrumbScroll.ScrollToRightEnd(), DispatcherPriority.Loaded);
    }

    private void ShowSubfolders(FrameworkElement anchor, string path)
    {
        var m = new ContextMenu { PlacementTarget = anchor, Placement = System.Windows.Controls.Primitives.PlacementMode.Bottom };
        try
        {
            var dirs = new DirectoryInfo(path).EnumerateDirectories().Where(d => (d.Attributes & (FileAttributes.Hidden | FileAttributes.System)) == 0 || _settings.ShowHidden)
                .Select(d => d.Name).OrderBy(n => n, Comparer<string>.Create(StrCmpLogicalW)).Take(300).ToList();
            foreach (var n in dirs) { var mi = new MenuItem { Header = n.Replace("_", "__") }; string target = Path.Combine(path, n); mi.Click += (_, _) => Navigate(target); m.Items.Add(mi); }
        }
        catch { }
        if (m.Items.Count == 0) m.Items.Add(new MenuItem { Header = "No folders", IsEnabled = false });
        m.IsOpen = true;
    }

    // ---------- view menu: preview pane, hidden files, colour ----------

    private void ToggleHidden() { _settings.ShowHidden = !_settings.ShowHidden; foreach (var t in _tabs) if (t.Current != null) LoadTab(t); Flash(_settings.ShowHidden ? "Showing hidden files" : "Hiding hidden files"); }

    private void SetTheme(string id)
    {
        Theme.Load(id); Theme.Apply(); ApplyChrome(); _settings.Theme = id; _settings.Save();
    }

    // ---------- view modes and zoom ----------

    private GridView _grid = null!; private ItemsPanelTemplate _rowsPanel = null!;
    private string _viewMode = "Details"; private int _iconSize = 96; private double _zoom = 1;
    private static readonly (string Label, string Mode, int Size, string Keys)[] Views =
    {
        ("Extra large icons", "Icons", 192, "Ctrl+Shift+1"), ("Large icons", "Icons", 96, "Ctrl+Shift+2"), ("Medium icons", "Icons", 64, "Ctrl+Shift+3"),
        ("Small icons", "Icons", 40, "Ctrl+Shift+4"), ("List", "List", 0, "Ctrl+Shift+5"), ("Details", "Details", 0, "Ctrl+Shift+6"), ("Tiles", "Tiles", 0, "Ctrl+Shift+7"),
    };

    /// Details = the columns; List = small icons in columns that scroll sideways; Tiles = picture + name, type, size; Icons = thumbnails of any size.
    private void SetViewMode(string mode, int iconSize = 0)
    {
        if (mode is not ("Details" or "List" or "Tiles" or "Icons")) mode = "Details";
        _viewMode = mode; if (iconSize > 0) _iconSize = iconSize;
        RememberView();
        List.SetValue(ScrollViewer.HorizontalScrollBarVisibilityProperty, mode == "List" ? ScrollBarVisibility.Auto : ScrollBarVisibility.Disabled);
        List.SetValue(ScrollViewer.VerticalScrollBarVisibilityProperty, mode == "List" ? ScrollBarVisibility.Disabled : ScrollBarVisibility.Auto);
        if (mode == "Details") { List.View = _grid; List.ItemsPanel = _rowsPanel; List.ItemTemplate = null; List.ClearValue(ItemsControl.ItemContainerStyleProperty); }
        else
        {
            List.View = null; List.ItemContainerStyle = (Style)FindResource("PlainItem");
            List.ItemsPanel = (ItemsPanelTemplate)FindResource(mode == "List" ? "WrapV" : "WrapH");
            List.ItemTemplate = (DataTemplate)FindResource(mode switch { "List" => "ListTpl", "Tiles" => "TileTpl", _ => "IconTpl" });
        }
        Resources["IconPx"] = (double)_iconSize; Resources["IconCellW"] = (double)Math.Max(_iconSize + 30, 92);
        RequestThumbs();
    }

    private void RequestThumbs()
    {
        if (_tab?.Entries == null || _viewMode is "Details" or "List") return;
        double dpi = VisualTreeHelper.GetDpi(this).DpiScaleX; int want = _viewMode == "Tiles" ? 48 : _iconSize;
        ThumbLoader(_tab.Entries, Math.Min(256, (int)Math.Ceiling(want * dpi / 32.0) * 32));
    }

    private static void ThumbLoader(IReadOnlyList<FileEntry> entries, int px) => Thumbs.Request(entries, px);

    /// Ctrl + wheel / Ctrl +/-: bigger or smaller icons in the icon view, the whole list scaled in the others; Ctrl+0 back to normal.
    /// Ctrl + wheel / Ctrl +/- walk the views like File Explorer: Details, List, then icons from small (32) to extra large (256); Ctrl+0 = Details.
    private void Zoom(int dir)
    {
        int step = _viewMode switch { "Details" => 0, "List" or "Tiles" => 1, _ => 2 + (Math.Clamp(_iconSize, 32, 256) - 32) / 16 };
        step = dir == 0 ? 0 : Math.Clamp(step + dir, 0, 2 + (256 - 32) / 16);
        if (step == 0) SetViewMode("Details"); else if (step == 1) SetViewMode("List"); else SetViewMode("Icons", 32 + (step - 2) * 16);
        Flash(step == 0 ? "Details" : step == 1 ? "List" : $"Icons · {_iconSize} px");
    }

    private void ApplyZoom()
    {
        List.LayoutTransform = _zoom == 1 ? Transform.Identity : new ScaleTransform(_zoom, _zoom);
        TextOptions.SetTextFormattingMode(List, _zoom == 1 ? TextFormattingMode.Display : TextFormattingMode.Ideal);   // Display text goes blurry when scaled
        _settings.Zoom = _zoom;
    }

    private void List_Wheel(object s, MouseWheelEventArgs e)
    {
        if ((Keyboard.Modifiers & ModifierKeys.Control) != 0) { Zoom(e.Delta > 0 ? 1 : -1); e.Handled = true; }
        else if (_viewMode == "List" && FindScroll(List) is { } sv) { sv.ScrollToHorizontalOffset(sv.HorizontalOffset - e.Delta); e.Handled = true; }   // the list view scrolls sideways, like File Explorer's
    }

    private static ScrollViewer? FindScroll(DependencyObject d)
    {
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(d); i++) { var c = VisualTreeHelper.GetChild(d, i); if (c is ScrollViewer sv) return sv; if (FindScroll(c) is { } r) return r; }
        return null;
    }

    // ---------- command bar ----------

    private Button _new = null!, _cut = null!, _copy = null!, _paste = null!, _rename = null!, _share = null!, _delete = null!;
    private const string Check = "";

    private Button Cmd(string glyph, string label, string tip, Action<Button> click, bool chevron = false)
    {
        var sp = new StackPanel { Orientation = Orientation.Horizontal };
        var g = new TextBlock { Text = glyph, FontFamily = (FontFamily)FindResource("IconFont"), FontSize = 16, VerticalAlignment = VerticalAlignment.Center };
        g.SetResourceReference(TextBlock.ForegroundProperty, "Accent"); sp.Children.Add(g);
        if (label.Length > 0) sp.Children.Add(new TextBlock { Text = label, Margin = new Thickness(8, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center });
        if (chevron) { var c = new TextBlock { Text = "", FontFamily = (FontFamily)FindResource("IconFont"), FontSize = 9, Margin = new Thickness(7, 2, 0, 0), VerticalAlignment = VerticalAlignment.Center }; c.SetResourceReference(TextBlock.ForegroundProperty, "FgDim"); sp.Children.Add(c); }
        var b = new Button { Content = sp, Style = (Style)FindResource("Cmd"), ToolTip = tip, Margin = new Thickness(0, 0, 2, 0) };
        b.Click += (_, _) => click(b); return b;
    }

    private Border CmdLine() { var l = new Border { Width = 1, Height = 24, Margin = new Thickness(8, 0, 8, 0) }; l.SetResourceReference(Border.BackgroundProperty, "CardLine"); return l; }

    private ContextMenu DropDown(FrameworkElement anchor) => new() { PlacementTarget = anchor, Placement = System.Windows.Controls.Primitives.PlacementMode.Bottom };

    private void BuildCommandBar()
    {
        var c = CmdBar.Children;
        c.Add(_new = Cmd("", "New", "New folder or file", b => { var m = DropDown(b); AddNewItems(m.Items); m.IsOpen = true; }, chevron: true));
        c.Add(CmdLine());
        c.Add(_cut = Cmd("", "", "Cut (Ctrl+X)", _ => CopySelection(true)));
        c.Add(_copy = Cmd("", "", "Copy (Ctrl+C)", _ => CopySelection(false)));
        c.Add(_paste = Cmd("", "", "Paste (Ctrl+V)", _ => Paste(PasteTarget())));
        c.Add(_rename = Cmd("", "", "Rename (F2)", _ => RenameSelection()));
        c.Add(_share = Cmd("", "", "Share", _ => ShareSelection()));
        c.Add(_delete = Cmd("", "", "Delete (Del)", _ => DeleteSelection(false)));
        c.Add(CmdLine());
        c.Add(Cmd("", "Sort", "Sort by", ShowSortMenu, chevron: true));
        c.Add(Cmd("", "View", "Preview pane, hidden files, colour", ShowViewMenu, chevron: true));
        c.Add(Cmd("", "Filter", "Show only some kinds of item", ShowFilterMenu, chevron: true));
        c.Add(CmdLine());
        c.Add(Cmd("", "", "More", ShowMoreMenu));
    }

    /// Greys out what can't be used right now, like File Explorer's command bar.
    private void UpdateCommands()
    {
        if (_cut == null || _tab == null) return;
        int n = List.SelectedItems.Count; bool items = !InHome, here = !IsVirtual(_tab.Current);   // search results are real files; Home's pins are better left alone
        _cut.IsEnabled = _copy.IsEnabled = _share.IsEnabled = _delete.IsEnabled = n > 0 && items;
        _rename.IsEnabled = n == 1 && items; _new.IsEnabled = here;
        _paste.IsEnabled = here && ShellOps.ReadClipboard() != null;
    }

    private void ShowSortMenu(FrameworkElement anchor)
    {
        var m = DropDown(anchor);
        AddSortItems(m.Items);
        m.IsOpen = true;
    }

    private void ShowViewMenu(FrameworkElement anchor)
    {
        var m = DropDown(anchor);
        AddViewModeItems(m.Items);
        var group = MI("Group by", "", () => { }); AddGroupItems(group.Items); m.Items.Add(group);
        m.Items.Add(new Separator());
        m.Items.Add(MI("Preview pane", _previewOpen ? Check : "", () => SetPreview(!_previewOpen), "Alt+P"));
        m.Items.Add(MI("Hidden items", _settings.ShowHidden ? Check : "", ToggleHidden, "Ctrl+H"));
        m.Items.Add(new Separator());
        var color = MI("Color", "", () => { });
        foreach (var t in Theme.All) { var id = t.Id; color.Items.Add(MI(t.Name, t.Id == Theme.Current.Id ? Check : "", () => SetTheme(id))); }
        AddColourExtras(color.Items);
        m.Items.Add(color);
        m.IsOpen = true;
    }

    private void ShowFilterMenu(FrameworkElement anchor)
    {
        var m = DropDown(anchor);
        foreach (var k in new[] { "", "Folders", "Files" }.Concat(Kinds.Keys))
        {
            var kind = k; m.Items.Add(MI(k.Length == 0 ? "Everything" : k, _tab.Kind == k ? Check : "", () => { _tab.Kind = kind; ApplyFilter(_tab); UpdateStatus(); }));
            if (k.Length == 0 || k == "Files") m.Items.Add(new Separator());
        }
        m.IsOpen = true;
    }

    private void ShowMoreMenu(FrameworkElement anchor)
    {
        var m = DropDown(anchor);
        m.Items.Add(MI("Select all", "", () => List.SelectAll(), "Ctrl+A"));
        m.Items.Add(MI("Select none", "", () => List.UnselectAll()));
        m.Items.Add(MI("Invert selection", "", () => { var keep = Sel(); List.SelectAll(); foreach (var x in keep) List.SelectedItems.Remove(x); }));
        m.Items.Add(new Separator());
        m.Items.Add(MI("Copy path", "", () => { var p = Sel().Select(x => "\"" + x.FullPath + "\"").ToList(); if (p.Count == 0 && !InHome) p.Add("\"" + _tab.Current + "\""); try { Clipboard.SetText(string.Join("\r\n", p)); } catch { } }));
        m.Items.Add(MI("Open in new tab", "", () => NewTab(_tab.Current)));
        m.Items.Add(MI("Properties", "", ShowProperties, "Alt+Enter", enabled: !InHome || Sel().Count > 0));
        m.Items.Add(new Separator());
        bool def = DefaultApp.IsOn;
        m.Items.Add(MI("Open folders with DeskDrawer", def ? Check : "", () => SetDefault(!def)));
        m.Items.Add(MI("Keep ready in the background (opens faster)", _settings.KeepReady ? Check : "", () => { _settings.KeepReady = !_settings.KeepReady; _settings.Save(); Setup.StartAtSignIn(_settings.KeepReady); }));
        m.Items.Add(MI("Use graphics card (smoother, more memory)", _settings.Gpu ? Check : "", () => { _settings.Gpu = !_settings.Gpu; _settings.Save(); Flash("Restart DeskDrawer to apply"); }));
        m.IsOpen = true;
    }

    /// Windows' own share sheet, reached through the item's shell menu (its last "Share" entry; OneDrive adds a first one of its own).
    private void ShareSelection()
    {
        var paths = Sel().Select(x => x.FullPath).ToList(); if (paths.Count == 0) return;
        var session = ShellMenu.Open(Hwnd, paths, false); if (session == null) { Flash("Can't share this"); return; }
        uint? id = null;
        void Walk(List<ShellMenu.ShellEntry> list) { foreach (var e in list) { if (!e.Separator && string.Equals(e.Text, "Share", StringComparison.OrdinalIgnoreCase)) id = e.Id; if (e.Children != null) Walk(e.Children); } }
        Walk(session.Items);
        var at = PointToScreen(new Point(ActualWidth / 2, ActualHeight / 2));
        try { if (id is { } i) session.Invoke(i, (int)at.X, (int)at.Y); else Flash("Sharing isn't available for this"); }
        catch (Exception ex) { MessageBox.Show(this, ex.Message, "DeskDrawer", MessageBoxButton.OK, MessageBoxImage.Warning); }
        finally { Dispatcher.BeginInvoke(() => session.Dispose(), DispatcherPriority.Background); }
    }

    private void Address_KeyDown(object s, KeyEventArgs e)
    {
        if (e.Key == Key.Enter) { Navigate(Address.Text); List.Focus(); e.Handled = true; }
        else if (e.Key == Key.Escape) { Address.Text = _tab.Current ?? ""; List.Focus(); e.Handled = true; }
    }

    private void FlashAddressError()
    {
        Address.SetResourceReference(ForegroundProperty, "Error");
        var t = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(1200) };
        t.Tick += (_, _) => { t.Stop(); Address.SetResourceReference(ForegroundProperty, "Fg"); };
        t.Start();
    }

    private void Search_Changed(object s, TextChangedEventArgs e)
    {
        SearchHint.Visibility = Search.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
        if (_binding || _tab.View == null) return;
        _tab.Filter = Search.Text.Trim(); ApplyFilter(_tab);
        UpdateStatus();
    }

    private static readonly Dictionary<string, string[]> Kinds = new()
    {
        ["Pictures"] = new[] { ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp", ".heic", ".tif", ".tiff", ".svg", ".ico", ".raw" },
        ["Videos"] = new[] { ".mp4", ".mkv", ".mov", ".avi", ".wmv", ".webm", ".m4v" },
        ["Music"] = new[] { ".mp3", ".wav", ".flac", ".m4a", ".aac", ".ogg", ".wma" },
        ["Documents"] = new[] { ".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx", ".txt", ".md", ".rtf", ".odt", ".csv" },
        ["Archives"] = new[] { ".zip", ".7z", ".rar", ".tar", ".gz" },
    };

    /// The name typed in the filter box and the Filter menu's kind, together.
    private static void ApplyFilter(TabState t)
    {
        if (t.View == null) return;
        string q = t.Filter, k = t.Kind;
        t.View.Filter = q.Length == 0 && k.Length == 0 ? null : o =>
        {
            var e = (FileEntry)o;
            if (q.Length > 0 && !e.Name.Contains(q, StringComparison.OrdinalIgnoreCase)) return false;
            return k switch { "" => true, "Folders" => e.IsDir, "Files" => !e.IsDir, _ => !e.IsDir && Kinds[k].Contains(e.Ext) };
        };
    }

    // ---------- folder contents ----------

    private static int Compare(FileEntry a, FileEntry b) => a.IsDir != b.IsDir ? (a.IsDir ? -1 : 1) : StrCmpLogicalW(a.Name, b.Name);

    private void LoadTab(TabState tab)
    {
        tab.Cts?.Cancel(); var cts = tab.Cts = new CancellationTokenSource(); tab.Loading = true; tab.Error = null;
        string path = tab.Current!; bool showHidden = _settings.ShowHidden;
        if (path is RecentTag or ThisPcTag) { Done(new List<FileEntry>(), null); return; }   // these pages build their own content
        if (path == HomeTag || path == QuickTag) { Pinned.Read(pins => Done(pins.Select(p => new FileEntry(new DirectoryInfo(p.Path))).ToList(), null)); return; }
        if (IsSearch(path)) { RunSearch(tab, path, showHidden, cts, Done); return; }
        Task.Run(() =>
        {
            var list = new List<FileEntry>(); string? error = null;
            try
            {
                foreach (var fi in new DirectoryInfo(path).EnumerateFileSystemInfos())
                {
                    if (cts.IsCancellationRequested) return;
                    if (!showHidden && (fi.Attributes & (FileAttributes.Hidden | FileAttributes.System)) != 0) continue;   // like File Explorer's default
                    list.Add(new FileEntry(fi));
                }
                list.Sort(Compare);
            }
            catch (UnauthorizedAccessException) { error = "You don't have permission to open this folder."; }
            catch (Exception ex) { error = ex.Message; }
            if (cts.IsCancellationRequested) return;
            Dispatcher.BeginInvoke(() => Done(list, error));
        });

        void Done(List<FileEntry> list, string? error)
        {
            if (cts.IsCancellationRequested) return;
            tab.Loading = false; tab.Error = error; tab.Entries = list; tab.IconsRequested = false;
            var v = new ListCollectionView(list); tab.View = v; ApplyGrouping(tab); ApplyFilter(tab);
            if (tab == _tab) BindTab();
            if (!IsVirtual(path))   // Home holds whole drives and OneDrive, search results come from everywhere: too much to add up
                FolderSizes.Start(list, cts.Token, () => { if (_sortKey == "Size" && tab.View != null) ApplySort(tab.View); if (tab == _tab) UpdateSelectionStatus(); });
        }
    }

    /// Re-reads the open folder and keeps what was selected.
    private void ReloadActive()
    {
        if (_tab.Current == null || (_tab.Entries?.Any(x => x.IsRenaming) ?? false)) return;
        foreach (var x in List.SelectedItems.Cast<FileEntry>()) _tab.SelectAfterLoad.Add(x.FullPath);
        LoadTab(_tab);
    }

    private void ResetWatcher()
    {
        _watch?.Dispose(); _watch = null;
        if (_tab.Current == null || !Directory.Exists(_tab.Current)) return;
        try
        {
            _watch = new FileSystemWatcher(_tab.Current) { NotifyFilter = NotifyFilters.FileName | NotifyFilters.DirectoryName | NotifyFilters.LastWrite | NotifyFilters.Size, EnableRaisingEvents = true };
            FileSystemEventHandler h = (_, _) => Dispatcher.BeginInvoke(() => { _reload.Stop(); _reload.Start(); });
            _watch.Created += h; _watch.Deleted += h; _watch.Changed += h; _watch.Renamed += (_, _) => h(null!, null!);
        }
        catch { }
    }

    private void UpdateStatus()
    {
        if (Environment.TickCount64 < _flashUntil) return;   // a message ("Moved to the Recycle Bin") stays up for its three seconds, even if the folder reloads meanwhile
        int n = _tab.View?.Count ?? 0;
        Status.Text = n == 1 ? "1 item" : $"{n:N0} items";
    }

    private long _flashUntil; private DispatcherTimer? _flashTimer;
    private void Flash(string text)
    {
        Status.Text = text; _flashUntil = Environment.TickCount64 + 2900;
        if (_flashTimer == null) { _flashTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(3) }; _flashTimer.Tick += (_, _) => { _flashTimer.Stop(); _flashUntil = 0; UpdateStatus(); }; }
        _flashTimer.Stop(); _flashTimer.Start();
    }

    // ---------- preview pane ----------

    private void SetPreview(bool open)
    {
        _previewOpen = open;
        PreviewCol.Width = open ? new GridLength(_settings.PreviewWidth) : new GridLength(0); PreviewSplitCol.Width = open ? new GridLength(6) : new GridLength(0);
        Preview.Visibility = PreviewSplit.Visibility = open ? Visibility.Visible : Visibility.Collapsed;
        if (open) PreviewSoon();
    }

    private void PreviewSoon() { if (!_previewOpen) return; _previewTimer.Stop(); _previewTimer.Start(); }   // arrow-keying through a folder must not start a thumbnail for every row
    private void UpdatePreview() { if (_previewOpen && _tab != null) Preview.Show(Sel(), InHome ? Pages[_tab.Current!].Title : InSearch ? "Search results" : _tab.Current, _tab.View?.Count ?? 0); }

    // ---------- sorting (click a column header) ----------

    private sealed class EntryComparer : System.Collections.IComparer
    {
        private readonly string _key; private readonly bool _asc;
        private readonly bool _grouped;
        private readonly bool _relevance;
        public EntryComparer(string key, bool asc, bool grouped, bool relevance = false) { _key = key; _asc = asc; _grouped = grouped; _relevance = relevance; }
        public int Compare(object? x, object? y)
        {
            var a = (FileEntry)x!; var b = (FileEntry)y!;
            if (_relevance) { int r = b.Score.CompareTo(a.Score); return r != 0 ? r : StrCmpLogicalW(a.Name, b.Name); }   // search results: best match first
            if (_grouped && (a.GroupRank != b.GroupRank || a.Group != b.Group)) return a.GroupRank != b.GroupRank ? a.GroupRank.CompareTo(b.GroupRank) : string.Compare(a.Group, b.Group, StringComparison.CurrentCultureIgnoreCase);   // headings in order
            if (a.IsDir != b.IsDir && !(_grouped && _key == "Date modified")) return a.IsDir ? -1 : 1;   // folders on top, except in date groups (File Explorer mixes them in by date there)
            int c = _key switch
            {
                "Date modified" => a.Modified.CompareTo(b.Modified),
                "Type" => string.Compare(a.TypeText, b.TypeText, StringComparison.OrdinalIgnoreCase),
                "Size" => a.Size.CompareTo(b.Size),
                _ => StrCmpLogicalW(a.Name, b.Name),
            };
            if (!_asc) c = -c;
            return c != 0 ? c : StrCmpLogicalW(a.Name, b.Name);
        }
    }

    private void ApplySort(ListCollectionView v, bool relevance = false) => v.CustomSort = new EntryComparer(_sortKey, _sortAsc, _groupBy.Length > 0 && !relevance, relevance);

    private void Header_Click(object s, RoutedEventArgs e)
    {
        if (e.OriginalSource is not GridViewColumnHeader { Column: { } col, Role: not GridViewColumnHeaderRole.Padding }) return;
        int i = _grid.Columns.IndexOf(col); if (i < 0 || i >= ColumnNames.Length) return;
        SetSort(ColumnNames[i], _sortKey == ColumnNames[i] ? !_sortAsc : true);
    }

    private void SetSort(string key, bool asc)
    {
        _sortKey = key; _sortAsc = asc; _tab.ByRelevance = false;
        UpdateSortHeaders(); if (_tab.View != null) ApplyGrouping(_tab);   // this folder only: each folder keeps its own order
        RememberView();
    }

    private void UpdateSortHeaders()
    {
        var cols = _grid.Columns;
        for (int i = 0; i < cols.Count && i < ColumnNames.Length; i++)
        {
            if (ColumnNames[i] != _sortKey || (_tab != null && _tab.ByRelevance && IsSearch(_tab.Current))) { cols[i].Header = ColumnNames[i]; continue; }   // search results: best match first, no arrow
            var sp = new StackPanel { Orientation = Orientation.Horizontal };   // a small chevron, not a full-size triangle
            sp.Children.Add(new TextBlock { Text = ColumnNames[i] });
            var arrow = new TextBlock { Text = _sortAsc ? "" : "", FontFamily = (FontFamily)FindResource("IconFont"), FontSize = 9, Margin = new Thickness(8, 1, 0, 0), VerticalAlignment = VerticalAlignment.Center };
            arrow.SetResourceReference(TextBlock.ForegroundProperty, "FgDim"); sp.Children.Add(arrow);
            cols[i].Header = sp;
        }
    }

    private void List_SelectionChanged(object s, SelectionChangedEventArgs e)
    {
        if (_mqActive) return;   // the selection box updates the status once per step, not once per item
        PreviewSoon(); UpdateCommands(); UpdateSelectionStatus();
    }

    private void UpdateSelectionStatus()
    {
        var sel = Sel();
        if (sel.Count == 0) { SelectionStatus.Text = ""; return; }
        long bytes = sel.Sum(x => x.Size);   // folders count once their size is known
        SelectionStatus.Text = (sel.Count == 1 && IsVirtual(_tab.Current) ? sel[0].Folder + "   " : "") + $"{sel.Count:N0} selected" + (bytes > 0 ? "   " + FileEntry.FormatSize(bytes) : "");   // in search results, where the one item lives
    }

    private List<FileEntry> Sel() => List.SelectedItems.Cast<FileEntry>().ToList();

    private static DependencyObject? AsVisual(DependencyObject? d) { while (d != null && d is not Visual && d is not System.Windows.Media.Media3D.Visual3D) d = LogicalTreeHelper.GetParent(d); return d; }

    private FileEntry? EntryUnder(object? src) =>
        AsVisual(src as DependencyObject) is { } d && ItemsControl.ContainerFromElement(List, d) is ListViewItem lvi ? lvi.Content as FileEntry : null;

    // ---------- opening ----------

    private void List_DoubleClick(object s, MouseButtonEventArgs e) { DisarmRename(); if (e.ChangedButton == MouseButton.Left && EntryUnder(e.OriginalSource) != null) OpenSelected(); }

    private void List_KeyDown(object s, KeyEventArgs e)
    {
        if (Keyboard.FocusedElement is TextBox) return;
        if (e.Key == Key.Enter && (Keyboard.Modifiers & ModifierKeys.Alt) == 0) { OpenSelected(); e.Handled = true; }
        else if (e.Key == Key.Back) { GoUp(); e.Handled = true; }
        else if (e.Key == Key.Escape && InSearch) { Navigate(SearchParts(_tab.Current!).Root); e.Handled = true; }   // out of the results, back to the folder you searched from
    }

    // ---------- click a selected name to rename it (File Explorer's slow double click) ----------

    private FileEntry? _renameArm; private DispatcherTimer? _renameTimer;
    [DllImport("user32.dll")] private static extern uint GetDoubleClickTime();

    private void DisarmRename() { _renameArm = null; _renameTimer?.Stop(); }

    /// Called on a left-button press: arms the rename only for a plain click on the name of the one item that was already selected.
    private void ArmRename(MouseButtonEventArgs e, FileEntry? en)
    {
        DisarmRename();
        if (en == null || e.ClickCount != 1 || Keyboard.Modifiers != ModifierKeys.None || InHome || List.SelectedItems.Count != 1 || List.SelectedItems[0] != en) return;
        if (e.OriginalSource is TextBlock { Text: var t } && t == en.Name) _renameArm = en;
    }

    /// Called on release: if it was a click (no drag) and no second click follows within the double-click time, the name turns into a text box.
    private void StartRenameTimer(FileEntry? at)
    {
        if (_renameArm == null || at != _renameArm) { _renameArm = null; return; }
        _renameTimer ??= new DispatcherTimer();
        _renameTimer.Interval = TimeSpan.FromMilliseconds(GetDoubleClickTime() + 60);
        _renameTimer.Tick -= RenameTimerTick; _renameTimer.Tick += RenameTimerTick; _renameTimer.Stop(); _renameTimer.Start();
    }

    private void RenameTimerTick(object? s, EventArgs e)
    {
        _renameTimer!.Stop(); var en = _renameArm; _renameArm = null;
        if (en != null && List.SelectedItems.Count == 1 && List.SelectedItems[0] == en && !en.IsRenaming && _tab.Entries?.Contains(en) == true) BeginRename(en);
    }

    private void OpenSelected()
    {
        var sel = Sel(); if (sel.Count == 0) return;
        if (sel[0].IsDir) { Navigate(sel[0].FullPath); return; }
        foreach (var f in sel.Where(x => !x.IsDir)) OpenFile(f.FullPath);
    }

    private void OpenFile(string path)
    {
        try { Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); }
        catch (Exception ex) { MessageBox.Show(this, ex.Message, "DeskDrawer", MessageBoxButton.OK, MessageBoxImage.Warning); }
    }

    // ---------- file operations ----------

    private void CopySelection(bool cut)
    {
        var p = Sel().Select(x => x.FullPath).ToList(); if (p.Count == 0) return;
        try { ShellOps.SetClipboard(p, cut); } catch (Exception) { Flash("Another program is using the clipboard. Try again"); return; }   // WPF already retried for a second
        UpdateCommands(); Flash(p.Count == 1 ? (cut ? "1 item cut" : "1 item copied") : $"{p.Count} items {(cut ? "cut" : "copied")}");
    }

    /// The full path of what is selected (or of the open folder) onto the clipboard, quoted like File Explorer's "Copy as path".
    private void CopyPaths()
    {
        var p = Sel().Select(x => "\"" + x.FullPath + "\"").ToList(); if (p.Count == 0 && !IsVirtual(_tab.Current)) p.Add("\"" + _tab.Current + "\""); if (p.Count == 0) return;
        try { Clipboard.SetText(string.Join("\r\n", p)); Flash(p.Count == 1 ? "Path copied" : $"{p.Count} paths copied"); } catch { }
    }

    private string PasteTarget() => Sel() is { Count: 1 } s && s[0].IsDir ? s[0].FullPath : _tab.Current!;

    private void Paste(string destDir)
    {
        if (IsVirtual(destDir) || ShellOps.ReadClipboard() is not { } clip) return;
        var (paths, cut) = clip; bool sameFolder = paths.All(p => string.Equals(Path.GetDirectoryName(p), destDir, StringComparison.OrdinalIgnoreCase));
        if (cut && sameFolder) return;
        Transfer(paths, destDir, cut, sameFolder);
        if (cut) try { Clipboard.Clear(); } catch { }
    }

    private void DeleteSelection(bool permanent)
    {
        var sel = Sel(); if (sel.Count == 0 || InHome) return;
        var p = sel.Select(x => x.FullPath).ToList(); var owner = Hwnd;
        string what = sel.Count == 1 ? "“" + sel[0].Name + "”" : $"{sel.Count:N0} items", dir = _tab.Current!;
        var next = NeighbourOf(sel);   // like File Explorer: the item after (or before) the deleted ones takes the selection, so Delete can be pressed again
        RunOp(() => ShellOps.Delete(p, owner, permanent), next == null ? null : new[] { next }, dir, () =>
        {
            if (permanent) { Flash($"Deleted {what} for good"); return; }
            Flash($"Moved {what} to the Recycle Bin. Ctrl+Z takes it back");
            PushUndo("delete of " + what, _ => ShellOps.RestoreFromRecycleBin(p));
        });
    }

    private string? NeighbourOf(List<FileEntry> sel)
    {
        var all = List.Items.Cast<FileEntry>().ToList(); var at = sel.Select(x => all.IndexOf(x)).Where(i => i >= 0).ToList(); if (at.Count == 0) return null;
        for (int i = at.Max() + 1; i < all.Count; i++) if (!sel.Contains(all[i])) return all[i].FullPath;
        for (int i = at.Min() - 1; i >= 0; i--) if (!sel.Contains(all[i])) return all[i].FullPath;
        return null;
    }

    /// Runs a shell operation off the UI thread (the shell shows its own progress and conflict dialogs), then refreshes and selects the results.
    /// A failure is said out loud: it used to vanish, which is how a broken Delete went unnoticed.
    private void RunOp(Func<bool> op, IEnumerable<string>? select, string? touchedDir, Action? succeeded = null)
    {
        Status.Text = "Working…"; var tab = _tab;
        Task.Run(op).ContinueWith(t => Dispatcher.BeginInvoke(() =>
        {
            string? problem = null;
            if (t.Status == TaskStatus.RanToCompletion && t.Result) succeeded?.Invoke();
            else if (t.IsFaulted) problem = "That didn't work: " + t.Exception?.GetBaseException().Message;
            else if (t.Status == TaskStatus.RanToCompletion) problem = ShellOps.LastError;   // null when you cancelled it yourself
            if (select != null && touchedDir != null && string.Equals(touchedDir, tab.Current, StringComparison.OrdinalIgnoreCase)) tab.SelectAfterLoad = new HashSet<string>(select, StringComparer.OrdinalIgnoreCase);
            foreach (var x in _tabs) if (x.Current != null) { if (IsSearch(x.Current)) PruneSearch(x); else LoadTab(x); }   // search results: drop what is gone instead of searching all over again
            if (problem != null) Flash(problem); else UpdateStatus();
        }), TaskScheduler.Default);
    }

    /// A search tab after a delete / move: keep its results, minus the items that no longer exist.
    private void PruneSearch(TabState tab)
    {
        if (tab.Entries == null || tab.Loading) return;
        if (tab.Entries.RemoveAll(x => !Exists(x.FullPath)) == 0) return;
        tab.View = new ListCollectionView(tab.Entries); ApplyGrouping(tab); ApplyFilter(tab);
        if (tab == _tab) BindTab();
    }

    private void NewItem(bool folder)
    {
        if (_tab.Current == null || IsVirtual(_tab.Current)) return;
        try
        {
            string p = folder ? ShellOps.NewFolder(_tab.Current) : ShellOps.NewTextFile(_tab.Current); UndoNew(p);
            _tab.SelectAfterLoad = new HashSet<string>(new[] { p }, StringComparer.OrdinalIgnoreCase); _tab.RenameAfterLoad = p; LoadTab(_tab);
        }
        catch (Exception ex) { MessageBox.Show(this, ex.Message, "DeskDrawer", MessageBoxButton.OK, MessageBoxImage.Warning); }
    }

    private void ShowProperties()
    {
        var sel = Sel(); string p = sel.Count > 0 ? sel[0].FullPath : _tab.Current!;
        ShellOps.Properties(p, Hwnd);
    }

    // ---------- inline rename ----------

    private void RenameSelection() { var s = Sel(); if (s.Count == 1) BeginRename(s[0]); }

    private void BeginRename(FileEntry e)
    {
        foreach (var x in _tab.Entries ?? new()) x.IsRenaming = false;
        e.EditName = e.Name; e.IsRenaming = true;
    }

    private void RenameBox_Visible(object s, DependencyPropertyChangedEventArgs e)
    {
        if (s is not TextBox tb || e.NewValue is not true || tb.DataContext is not FileEntry en) return;
        Dispatcher.BeginInvoke(() =>
        {
            tb.Focus(); int dot = en.IsDir ? -1 : en.Name.LastIndexOf('.');
            tb.Select(0, dot > 0 ? dot : tb.Text.Length);   // the name without its extension, like File Explorer
        }, DispatcherPriority.Input);
    }

    private void RenameBox_KeyDown(object s, KeyEventArgs e)
    {
        if (s is not TextBox { DataContext: FileEntry en }) return;
        if (e.Key == Key.Enter) { CommitRename(en); List.Focus(); e.Handled = true; }
        else if (e.Key == Key.Escape) { en.IsRenaming = false; List.Focus(); e.Handled = true; }
    }

    private void RenameBox_Lost(object s, KeyboardFocusChangedEventArgs e) { if (s is TextBox { DataContext: FileEntry { IsRenaming: true } en }) CommitRename(en); }

    private void CommitRename(FileEntry en)
    {
        string name = en.EditName.Trim(); en.IsRenaming = false;
        if (name.Length == 0 || name == en.Name) return;
        if (name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0) { MessageBox.Show(this, "A name can't contain any of these characters:\n\\ / : * ? \" < > |", "DeskDrawer", MessageBoxButton.OK, MessageBoxImage.Warning); return; }
        string dir = Path.GetDirectoryName(en.FullPath)!, target = Path.Combine(dir, name);
        if (!string.Equals(target, en.FullPath, StringComparison.OrdinalIgnoreCase) && (File.Exists(target) || Directory.Exists(target)))
        { MessageBox.Show(this, $"\"{name}\" already exists here.", "DeskDrawer", MessageBoxButton.OK, MessageBoxImage.Warning); return; }
        try { if (en.IsDir) Directory.Move(en.FullPath, target); else File.Move(en.FullPath, target); }
        catch (Exception ex) { MessageBox.Show(this, ex.Message, "DeskDrawer", MessageBoxButton.OK, MessageBoxImage.Warning); return; }
        UndoRename(en.FullPath, target);
        _tab.SelectAfterLoad = new HashSet<string>(new[] { target }, StringComparer.OrdinalIgnoreCase); LoadTab(_tab);
    }

    // ---------- right-click menu ----------

    private MenuItem MI(string header, string glyph, Action act, string gesture = "", bool enabled = true, bool bold = false)
    {
        var icon = new TextBlock { Text = glyph, FontFamily = (FontFamily)FindResource("IconFont"), FontSize = 14 }; icon.SetResourceReference(TextBlock.ForegroundProperty, "Accent");
        var mi = new MenuItem { Header = header, Icon = icon, InputGestureText = gesture, IsEnabled = enabled, FontWeight = bold ? FontWeights.SemiBold : FontWeights.Normal };
        mi.Click += (_, _) => act(); return mi;
    }

    private void ShowMenu()
    {
        if (List.SelectedItems.Count == 0) { ShowBackgroundMenu(); return; }
        var sel = Sel(); var m = new ContextMenu(); _lastMenu = m;
        var items = m.Items;
        if (sel.Count > 0)
        {
            items.Add(MI("Open", "", OpenSelected, "Enter", bold: true));
            if (sel.All(x => x.IsDir)) items.Add(MI(sel.Count == 1 ? "Open in new tab" : "Open each in a new tab", "", () => { foreach (var d in sel) { AddTab(d.FullPath); } }));
            if (sel.Count == 1 && !sel[0].IsDir) items.Add(MI("Open with…", "", () => ShellOps.OpenWith(sel[0].FullPath)));
            items.Add(new Separator());
            items.Add(MI("Cut", "", () => CopySelection(true), "Ctrl+X"));
            items.Add(MI("Copy", "", () => CopySelection(false), "Ctrl+C"));
            if (sel.Count == 1 && sel[0].IsDir) items.Add(MI("Paste into folder", "", () => Paste(sel[0].FullPath), "Ctrl+V", enabled: ShellOps.ReadClipboard() != null));
            items.Add(new Separator());
            items.Add(MI("Rename", "", RenameSelection, "F2", enabled: sel.Count == 1));
            items.Add(MI("Delete", "", () => DeleteSelection(false), "Del"));
            items.Add(new Separator());
            items.Add(MI("Copy as path", "", () => { try { Clipboard.SetText(string.Join("\r\n", sel.Select(x => "\"" + x.FullPath + "\""))); } catch { } }));
            items.Add(MI("Properties", "", ShowProperties, "Alt+Enter"));
            if (IsVirtual(_tab.Current) && sel.Count == 1) items.Add(MI("Open file location", "", () => ShowInFolder(sel[0].FullPath)));   // from search results
            AddItemExtras(items, sel);
            items.Add(new Separator());
            var paths = sel.Select(x => x.FullPath).ToList();
            items.Add(MI("Show more options", "", () => Dispatcher.BeginInvoke(() => ShowMoreOptions(paths))));   // after this menu has closed
        }
        m.PlacementTarget = List; m.Placement = System.Windows.Controls.Primitives.PlacementMode.MousePoint; m.IsOpen = true;
    }

    /// "Show more options": the Windows menu for these files (7-Zip, Git, Send to, Open with...), read from the shell and drawn in DeskDrawer's own theme.
    /// Picking an entry still runs it through the shell, so every extension behaves as in File Explorer.
    private void ShowMoreOptions(List<string> paths)
    {
        var session = ShellMenu.Open(Hwnd, paths, (Keyboard.Modifiers & ModifierKeys.Shift) != 0);
        if (session == null) { ShellMenu.Show(this, paths, _menuScreen); return; }   // could not read it: fall back to Windows' own popup
        var m = new ContextMenu(); _lastMenu = m;
        int x = (int)_menuScreen.X, y = (int)_menuScreen.Y;
        void Fill(ItemCollection into, List<ShellMenu.ShellEntry> entries)
        {
            foreach (var e in entries)
            {
                if (e.Separator) { into.Add(new Separator()); continue; }
                var mi = new MenuItem { Header = e.Text, InputGestureText = e.Gesture, IsEnabled = !e.Disabled, FontWeight = e.Default ? FontWeights.SemiBold : FontWeights.Normal };
                if (e.Icon != null) mi.Icon = new Image { Source = e.Icon, Width = 16, Height = 16, Stretch = Stretch.Uniform };
                else if (e.Checked) { var c = new TextBlock { Text = "", FontFamily = (FontFamily)FindResource("IconFont"), FontSize = 13 }; c.SetResourceReference(TextBlock.ForegroundProperty, "Accent"); mi.Icon = c; }
                if (e.Children != null) Fill(mi.Items, e.Children);
                else { uint id = e.Id; mi.Click += (_, _) => { try { session.Invoke(id, x, y); } catch (Exception ex) { MessageBox.Show(this, ex.Message, "DeskDrawer", MessageBoxButton.OK, MessageBoxImage.Warning); } }; }
                into.Add(mi);
            }
        }
        Fill(m.Items, session.Items);
        m.Closed += (_, _) => Dispatcher.BeginInvoke(() => session.Dispose(), DispatcherPriority.Background);   // after the click has been handled
        m.PlacementTarget = List; m.Placement = System.Windows.Controls.Primitives.PlacementMode.MousePoint; m.IsOpen = true;
    }

    private void List_RightDown(object s, MouseButtonEventArgs e)
    {
        _menuScreen = PointToScreen(e.GetPosition(this));
        var en = EntryUnder(e.OriginalSource);
        if (en == null) List.UnselectAll();
        else if (!List.SelectedItems.Contains(en)) { List.SelectedItems.Clear(); List.SelectedItems.Add(en); }
        List.Focus();
    }

    private void List_RightUp(object s, MouseButtonEventArgs e) { ShowMenu(); e.Handled = true; }
    private void Background_RightClick(object s, MouseButtonEventArgs e) { if (!e.Handled) { _menuScreen = PointToScreen(e.GetPosition(this)); List.UnselectAll(); ShowMenu(); } }

    private void List_MiddleDown(object s, MouseButtonEventArgs e)
    {
        if (e.ChangedButton == MouseButton.Middle && EntryUnder(e.OriginalSource) is { IsDir: true } d) { AddTab(d.FullPath); e.Handled = true; }
    }

    // ---------- drag and drop (to Explorer, from Explorer, between folders) ----------

    private void List_PreviewDown(object s, MouseButtonEventArgs e)
    {
        var under = EntryUnder(e.OriginalSource); _dragStart = e.GetPosition(null); _pendingSingle = null;
        _dragItem = OnContent(e.OriginalSource as DependencyObject) ? under : null;   // only a name or icon is grabbed to drag a file; the blank part of a row starts the selection box
        ArmRename(e, _dragItem); MarqueeArm(e, under);
        // pressing on one of several selected rows must not collapse the selection: that is how you drag them all
        if (_dragItem != null && List.SelectedItems.Count > 1 && List.SelectedItems.Contains(_dragItem) && Keyboard.Modifiers == ModifierKeys.None && e.ClickCount == 1)
        { _pendingSingle = _dragItem; e.Handled = true; List.Focus(); }
    }

    private void List_PreviewMove(object s, MouseEventArgs e)
    {
        if (_mqActive) { e.Handled = true; if (Frame() is { } f0) MarqueeMoveTo(e.GetPosition(f0)); return; }   // the selection box follows the pointer
        if (_mqArmed && e.LeftButton == MouseButtonState.Pressed && Frame() is { } f1)
        {
            var q = e.GetPosition(f1);
            if (Math.Abs(q.X - _mqPress.X) >= SystemParameters.MinimumHorizontalDragDistance || Math.Abs(q.Y - _mqPress.Y) >= SystemParameters.MinimumVerticalDragDistance) { MarqueeBegin(); e.Handled = true; }
            return;
        }
        if (e.LeftButton != MouseButtonState.Pressed || _dragItem == null) { return; }
        var p = e.GetPosition(null);
        if (Math.Abs(p.X - _dragStart.X) < SystemParameters.MinimumHorizontalDragDistance && Math.Abs(p.Y - _dragStart.Y) < SystemParameters.MinimumVerticalDragDistance) return;
        if (!List.SelectedItems.Contains(_dragItem)) { List.SelectedItems.Clear(); List.SelectedItems.Add(_dragItem); }
        var paths = Sel().Select(x => x.FullPath).ToArray(); _dragItem = null; _pendingSingle = null;
        DragDrop.DoDragDrop(List, new DataObject(DataFormats.FileDrop, paths), DragDropEffects.Copy | DragDropEffects.Move | DragDropEffects.Link);
    }

    protected override void OnPreviewMouseLeftButtonUp(MouseButtonEventArgs e)
    {
        base.OnPreviewMouseLeftButtonUp(e);
        if (_pendingSingle != null) { var one = _pendingSingle; _pendingSingle = null; List.SelectedItems.Clear(); List.SelectedItems.Add(one); }   // it was a click, not a drag
        MarqueeEnd();
        StartRenameTimer(_dragItem);   // _dragItem is cleared the moment a drag starts, so a drag never renames
        _dragItem = null;
    }

    private static DragDropEffects EffectFor(string[] src, string dest, DragDropKeyStates keys)
    {
        if (IsVirtual(dest)) return DragDropEffects.None;
        if (src.Any(p => string.Equals(p, dest, StringComparison.OrdinalIgnoreCase) || dest.StartsWith(p.TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase))) return DragDropEffects.None;   // into itself
        if ((keys & DragDropKeyStates.ControlKey) != 0) return DragDropEffects.Copy;
        if ((keys & DragDropKeyStates.ShiftKey) != 0) return DragDropEffects.Move;
        return string.Equals(Path.GetPathRoot(src[0]), Path.GetPathRoot(dest), StringComparison.OrdinalIgnoreCase) ? DragDropEffects.Move : DragDropEffects.Copy;
    }

    private string ListDropTarget(DragEventArgs e) => EntryUnder(e.OriginalSource) is { IsDir: true } d ? d.FullPath : _tab.Current!;

    private void List_DragOver(object s, DragEventArgs e)
    {
        e.Effects = e.Data.GetDataPresent(DataFormats.FileDrop) && e.Data.GetData(DataFormats.FileDrop) is string[] { Length: > 0 } src ? EffectFor(src, ListDropTarget(e), e.KeyStates) : DragDropEffects.None;
        e.Handled = true;
    }

    private void List_Drop(object s, DragEventArgs e) { DoDrop(e, ListDropTarget(e)); e.Handled = true; }

    private void DoDrop(DragEventArgs e, string dest)
    {
        if (e.Data.GetData(DataFormats.FileDrop) is not string[] { Length: > 0 } src) return;
        var fx = EffectFor(src, dest, e.KeyStates); if (fx == DragDropEffects.None) return;
        bool sameFolder = src.All(p => string.Equals(Path.GetDirectoryName(p), dest, StringComparison.OrdinalIgnoreCase));
        if (fx == DragDropEffects.Move && sameFolder) return;
        Transfer(src, dest, fx == DragDropEffects.Move, sameFolder);
    }

    private static TreeViewItem? TreeItemOf(object? src) { var d = AsVisual(src as DependencyObject); while (d != null && d is not TreeViewItem) d = VisualTreeHelper.GetParent(d); return d as TreeViewItem; }

    private void Tree_DragOver(object s, DragEventArgs e)
    {
        e.Effects = TreeItemOf(e.OriginalSource) is { Tag: string path } && path != Placeholder && e.Data.GetData(DataFormats.FileDrop) is string[] { Length: > 0 } src ? EffectFor(src, path, e.KeyStates) : DragDropEffects.None;
        e.Handled = true;
    }

    private void Tree_Drop(object s, DragEventArgs e) { if (TreeItemOf(e.OriginalSource) is { Tag: string path } && path != Placeholder) DoDrop(e, path); e.Handled = true; }

    private void Tree_MouseDown(object s, MouseButtonEventArgs e)
    {
        if (e.ChangedButton == MouseButton.Middle && TreeItemOf(e.OriginalSource) is { Tag: string path } && path != Placeholder) { AddTab(path); e.Handled = true; }
    }

    // ---------- folder tree ----------

    private TreeViewItem Node(string text, string? path, string glyph, bool lazy)
    {
        var sp = new StackPanel { Orientation = Orientation.Horizontal };
        var g = new TextBlock { Text = glyph, FontFamily = (FontFamily)FindResource("IconFont"), FontSize = 14, Margin = new Thickness(0, 0, 9, 0), VerticalAlignment = VerticalAlignment.Center };
        g.SetResourceReference(TextBlock.ForegroundProperty, "Accent");
        sp.Children.Add(g); sp.Children.Add(new TextBlock { Text = text, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis });
        var item = new TreeViewItem { Header = sp, Tag = path };
        if (lazy && path != null) IconCache.PathIcon(path, false, src => { if (sp.Children[0] is TextBlock) SwapIcon(sp, src); });   // the ordinary folder picture
        if (lazy && path != null)
        {
            item.Items.Add(new TreeViewItem { Tag = Placeholder });
            item.Expanded += (_, e) => { if (e.OriginalSource == item) FillChildren(item); };
        }
        return item;
    }

    private void FillChildren(TreeViewItem item)
    {
        if (item.Items.Count != 1 || (item.Items[0] as TreeViewItem)?.Tag as string != Placeholder) return;
        string path = (string)item.Tag;
        Task.Run(() =>
        {
            var dirs = new List<string>();
            try
            {
                foreach (var d in new DirectoryInfo(path).EnumerateDirectories())
                    if ((d.Attributes & (FileAttributes.Hidden | FileAttributes.System)) == 0) dirs.Add(d.FullName);
                dirs.Sort((a, b) => StrCmpLogicalW(Path.GetFileName(a), Path.GetFileName(b)));
            }
            catch { }
            Dispatcher.BeginInvoke(() =>
            {
                item.Items.Clear();
                foreach (var d in dirs) item.Items.Add(Node(Path.GetFileName(d), d, "", lazy: true));
            });
        });
    }

    private void BuildTree()
    {
        var home = Node("Home", HomeTag, "", false); Tree.Items.Add(home);
        Tree.Items.Add(Node("Recent", RecentTag, "\uE81C", false));
        var clouds = new[] { ("OneDriveConsumer", "OneDrive - Personal"), ("OneDriveCommercial", "OneDrive - Work") }
            .Select(v => (Path: Environment.GetEnvironmentVariable(v.Item1) ?? "", Name: v.Item2)).Where(v => v.Path.Length > 0 && Directory.Exists(v.Path))
            .DistinctBy(v => v.Path.ToLowerInvariant()).ToList();
        var cloudNodes = new Dictionary<string, TreeViewItem>(StringComparer.OrdinalIgnoreCase);
        foreach (var (path, name) in clouds) { var n = Node(Path.GetFileName(path) is { Length: > 0 } f && f != "OneDrive" ? f : name, path, "", lazy: true); Tree.Items.Add(n); cloudNodes[path] = n;
            var cloud = new TextBlock { Text = "", FontFamily = (FontFamily)FindResource("IconFont"), FontSize = 15, Foreground = new SolidColorBrush(Color.FromRgb(0x4C, 0xB0, 0xF5)), VerticalAlignment = VerticalAlignment.Center };
            var sp = (StackPanel)n.Header; sp.Children.RemoveAt(0); sp.Children.Insert(0, new Border { Child = cloud, Margin = new Thickness(0, 0, 9, 0) }); }   // OneDrive's blue cloud; a Border, so the plain-folder icon never replaces it
        if (cloudNodes.Count > 0) Pinned.DisplayNames(cloudNodes.Keys.ToList(), names =>   // "Your Name - Personal", as File Explorer labels them
        {
            foreach (var (p, label) in names) if (cloudNodes.TryGetValue(p, out var n) && n.Header is StackPanel { Children.Count: > 1 } sp && sp.Children[1] is TextBlock tb) tb.Text = label;
        });

        var quick = Node("Quick access", QuickTag, "", false);
        string downloads = KnownDownloads();
        foreach (var (name, path) in new[]
        {
            ("Desktop", Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory)), ("Downloads", downloads),
            ("Documents", Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments)), ("Pictures", Environment.GetFolderPath(Environment.SpecialFolder.MyPictures)),
            ("Music", Environment.GetFolderPath(Environment.SpecialFolder.MyMusic)), ("Videos", Environment.GetFolderPath(Environment.SpecialFolder.MyVideos)),
        })
            if (path.Length > 0 && Directory.Exists(path)) quick.Items.Add(Node(name, path, "", lazy: true));
        quick.IsExpanded = true; Tree.Items.Add(quick);
        foreach (TreeViewItem q in quick.Items) ExactIcon(q);
        _quick = quick; RefreshPins();   // swap the standard six for what is pinned in File Explorer's Quick access

        var pc = Node("This PC", ThisPcTag, "", false);
        foreach (var d in DriveInfo.GetDrives())
        {
            string label = "";
            try { label = d.IsReady ? d.VolumeLabel : ""; } catch { }
            string text = (label.Length > 0 ? label : d.DriveType == DriveType.Removable ? "Removable disk" : d.DriveType == DriveType.Network ? "Network drive" : "Local disk") + " (" + d.Name.TrimEnd('\\') + ")";
            pc.Items.Add(Node(text, d.Name, "", lazy: true));
        }
        pc.IsExpanded = true; Tree.Items.Add(pc);
        foreach (TreeViewItem d in pc.Items) ExactIcon(d);
    }

    private static Image TreeIcon(ImageSource src) => new() { Source = src, Width = 16, Height = 16, Margin = new Thickness(0, 0, 9, 0), VerticalAlignment = VerticalAlignment.Center };

    /// This folder's own shell picture (Downloads' arrow, Desktop's, a drive...), instead of the yellow folder.
    private static void ExactIcon(TreeViewItem item)
    {
        if (item.Tag is not string path || item.Header is not StackPanel sp) return;
        IconCache.PathIcon(path, true, src => SwapIcon(sp, src));
    }

    private static void SwapIcon(StackPanel sp, ImageSource src) { sp.Children.RemoveAt(0); sp.Children.Insert(0, TreeIcon(src)); }

    private static string KnownDownloads()
    {
        try { if (SHGetKnownFolderPath(FolderDownloads, 0, IntPtr.Zero, out var p) == 0) { var s = Marshal.PtrToStringUni(p) ?? ""; Marshal.FreeCoTaskMem(p); return s; } } catch { }
        return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Downloads");
    }

    private void Tree_Selected(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        if (e.NewValue is TreeViewItem { Tag: string path } && path != Placeholder) Navigate(path);
    }
}
