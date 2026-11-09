using System.IO;
using System.IO.Enumeration;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;

namespace DeskDrawer;

/// DeskDrawer's own file picker, shown in place of another app's Open / Save dialog (DialogSwitch hides that dialog, shows this, then types the answer into it).
/// Built from the same pieces as the DeskDrawer window (title strip, toolbar, file list, icons, colours). Result = the chosen full paths, or null when cancelled.
public sealed partial class PickerWindow : Window
{
    private string[] _patterns;
    private readonly string[] _types; private int _typeIndex; private readonly bool _save;
    public int TypeIndex => _typeIndex;   // which entry of the dialog's own file-type list was chosen here
    private readonly bool _hidden = Settings.Load().ShowHidden;
    private readonly (int L, int T, int R, int B) _rect;   // the dialog it stands in for, in device pixels
    private string _cur = "";
    private int _ticket;
    private bool _all;
    public List<string>? Result { get; private set; }

    public PickerWindow(string folder, string name, string button, string[] types, int typeIndex, string title, List<string> places, int l, int t, int r, int b)
    {
        InitializeComponent();
        _types = types; _typeIndex = typeIndex; _patterns = typeIndex >= 0 ? DialogSwitch.PatternsOf(types[typeIndex]) : Array.Empty<string>(); _rect = (l, t, r, b);
        _save = button.Contains("Save", StringComparison.OrdinalIgnoreCase);
        TitleText.Text = "DeskDrawer · " + (string.IsNullOrWhiteSpace(title) ? button : title);
        OkBtn.Content = string.IsNullOrWhiteSpace(button) ? "Open" : button;
        NameBox.Text = name;
        FilterBtn.Visibility = types.Length == 0 ? Visibility.Collapsed : Visibility.Visible; ShowFilter();

        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        void Place(string text, string go, string iconPath) { if (go.Length > 0 && (!Directory.Exists(go) || !seen.Add(go.TrimEnd('\\')))) return; AddPlace(text, go, iconPath); }
        var profile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        Place("Desktop", Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory));
        Place("Documents", Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments));
        Place("Downloads", Path.Combine(profile, "Downloads"), Path.Combine(profile, "Downloads"));
        Place("Pictures", Environment.GetFolderPath(Environment.SpecialFolder.MyPictures), Environment.GetFolderPath(Environment.SpecialFolder.MyPictures));
        foreach (var p0 in places.Take(8)) { var p = p0; try { p = Path.GetFullPath(p0); } catch { } Place(Path.GetFileName(p.TrimEnd('\\')) is { Length: > 0 } n ? n : p, p, p); }
        Place("This PC", "", Path.GetPathRoot(Environment.SystemDirectory) ?? "C:\\");

        SourceInitialized += (_, _) => ApplyChrome();
        PreviewKeyDown += (_, e) =>
        {
            if (e.Key == Key.Escape) { Cancel_Click(this, e); e.Handled = true; }
            else if ((e.Key == Key.Up || e.SystemKey == Key.Up) && (Keyboard.Modifiers & ModifierKeys.Alt) != 0) { Up_Click(this, e); e.Handled = true; }
        };
        // born off screen (see the XAML), moved onto the dialog's screen once the window has its real size: no flash, no jump. Twice: the first move can change the scale (another screen).
        Loaded += (_, _) => { Centre(); Centre(); Go(Directory.Exists(folder) ? folder : Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments)); NameBox.Focus(); NameBox.SelectAll(); Activate(); };
    }

    private void AddPlace(string text, string go, string iconPath)
    {
        var img = new Image { Width = 16, Height = 16, Margin = new Thickness(2, 0, 10, 0) };
        IconCache.PathIcon(iconPath, true, ic => img.Source = ic);
        var row = new StackPanel { Orientation = Orientation.Horizontal };
        row.Children.Add(img); row.Children.Add(new TextBlock { Text = text, TextTrimming = TextTrimming.CharacterEllipsis, MaxWidth = 120, VerticalAlignment = VerticalAlignment.Center });
        var b = new Button { Style = (Style)FindResource("Cmd"), Content = row, HorizontalContentAlignment = HorizontalAlignment.Left, ToolTip = go.Length > 0 ? go : "Drives" };
        b.Click += (_, _) => Go(go);
        Places.Children.Add(b);
    }

    /// The title strip takes the theme colour, like the main window.
    private void ApplyChrome()
    {
        var h = new WindowInteropHelper(this).Handle;
        int round = 2; Native.DwmSetWindowAttribute(h, 33, ref round, 4);
        int dark = Theme.IsLight ? 0 : 1; Native.DwmSetWindowAttribute(h, 20, ref dark, 4);
        if (Theme.IsGlass)
        {
            HwndSource.FromHwnd(h)!.CompositionTarget.BackgroundColor = Colors.Transparent;
            System.Windows.Shell.WindowChrome.GetWindowChrome(this).GlassFrameThickness = new Thickness(-1);
            var m = new Native.MARGINS { Left = -1, Right = -1, Top = -1, Bottom = -1 }; Native.DwmExtendFrameIntoClientArea(h, ref m);
            int acrylic = 3; Native.DwmSetWindowAttribute(h, 38, ref acrylic, 4);
        }
        else
        {
            var strip = ((SolidColorBrush)FindResource("StripBg")).Color; var fg = ((SolidColorBrush)FindResource("Fg")).Color;
            static int Ref(Color c) => c.R | (c.G << 8) | (c.B << 16);
            int cap = Ref(strip), txt = Ref(fg), edge = Ref(Color.FromRgb((byte)(strip.R + (255 - strip.R) * 0.28), (byte)(strip.G + (255 - strip.G) * 0.28), (byte)(strip.B + (255 - strip.B) * 0.28)));
            Native.DwmSetWindowAttribute(h, 35, ref cap, 4); Native.DwmSetWindowAttribute(h, 36, ref txt, 4); Native.DwmSetWindowAttribute(h, 34, ref edge, 4);
        }
    }

    /// The file-type box: the name of the type in use and a chevron.
    private void ShowFilter()
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal };
        row.Children.Add(new TextBlock { Text = _all ? "All files" : _typeIndex >= 0 ? _types[_typeIndex] : "", TextTrimming = TextTrimming.CharacterEllipsis, MaxWidth = 300, VerticalAlignment = VerticalAlignment.Center });
        row.Children.Add(new TextBlock { Text = "\uE70D", FontFamily = (FontFamily)FindResource("IconFont"), FontSize = 10, Margin = new Thickness(10, 1, 0, 0), Foreground = (Brush)FindResource("FgDim"), VerticalAlignment = VerticalAlignment.Center });
        FilterBtn.Content = row;
    }

    /// A drop-down of the dialog's own file types (plus "All files"): the same list Windows' dialog has, also when saving.
    private void Filter_Click(object s, RoutedEventArgs e)
    {
        var m = new ContextMenu { PlacementTarget = FilterBtn, Placement = System.Windows.Controls.Primitives.PlacementMode.Top };
        for (int i = 0; i < _types.Length; i++)
        {
            int k = i; var mi = new MenuItem { Header = ((!_all && k == _typeIndex) ? "✓  " : "     ") + _types[i].Replace("_", "__") };
            mi.Click += (_, _) => PickType(k); m.Items.Add(mi);
        }
        var allItem = new MenuItem { Header = (_all ? "✓  " : "     ") + "All files" }; allItem.Click += (_, _) => { _all = true; ShowFilter(); Go(_cur); };
        m.Items.Add(new Separator()); m.Items.Add(allItem);
        m.IsOpen = true;
    }

    /// Chooses one of the dialog's file types. When saving, the extension in the file name follows it (report.docx -> report.pdf), like Windows' dialog does.
    private void PickType(int k)
    {
        string? Ext(string[] pats) => pats.FirstOrDefault(x => x.StartsWith("*.") && x.IndexOfAny(new[] { '*', '?' }, 1) < 0) is { } x0 ? x0[1..] : null;
        string? from = _typeIndex >= 0 ? Ext(_patterns) : null, to = Ext(DialogSwitch.PatternsOf(_types[k]));
        var name = NameBox.Text.Trim();
        if (_save && from != null && to != null && !name.Contains('"') && name.EndsWith(from, StringComparison.OrdinalIgnoreCase)) NameBox.Text = name[..^from.Length] + to;
        _typeIndex = k; _all = false; _patterns = DialogSwitch.PatternsOf(_types[k]); ShowFilter(); Go(_cur);
    }

    private List<FileEntry> Selected() => List.SelectedItems.Cast<FileEntry>().ToList();

    // ---------- going to a folder ----------

    private void Up_Click(object s, RoutedEventArgs e) { if (_cur.Length == 0) return; Go(Directory.GetParent(_cur.TrimEnd('\\'))?.FullName ?? ""); }
    private void Refresh_Click(object s, RoutedEventArgs e) => Go(_cur);

    /// "" = the drives. Read off the UI thread (a OneDrive folder can take a while), shown when ready; an older, slower read never overwrites a newer one.
    public void Go(string dir)
    {
        int ticket = ++_ticket; bool all = _all;
        Task.Run(() => Read(dir, all)).ContinueWith(t =>
        {
            if (ticket != _ticket || t.Result == null) return;
            _cur = dir; PathBox.Text = dir; List.ItemsSource = t.Result;
            Message.Visibility = t.Result.Count == 0 ? Visibility.Visible : Visibility.Collapsed; Message.Text = "Nothing here";
            if (t.Result.Count > 0) { List.ScrollIntoView(t.Result[0]); IconCache.Request(t.Result); }
        }, TaskScheduler.FromCurrentSynchronizationContext());
    }

    /// Folders first, then the files that fit the dialog's file-type filter (unless showing all files).
    private List<FileEntry>? Read(string dir, bool all)
    {
        var rows = new List<FileEntry>();
        try
        {
            if (dir.Length == 0) { foreach (var d in DriveInfo.GetDrives()) if (d.IsReady) rows.Add(new FileEntry(new DirectoryInfo(d.RootDirectory.FullName))); return rows; }
            var di = new DirectoryInfo(dir); var dirs = new List<FileEntry>(); var files = new List<FileEntry>();
            foreach (var d in di.EnumerateDirectories()) { var e = new FileEntry(d); if (_hidden || !e.IsHidden) dirs.Add(e); }
            foreach (var f in di.EnumerateFiles())
            {
                var e = new FileEntry(f); if (!_hidden && e.IsHidden) continue;
                if (!all && _patterns.Length > 0 && !_patterns.Any(p => FileSystemName.MatchesSimpleExpression(p, e.Name, true))) continue;
                files.Add(e);
            }
            dirs.Sort((a, b) => Native.StrCmpLogicalW(a.Name, b.Name)); files.Sort((a, b) => Native.StrCmpLogicalW(a.Name, b.Name)); rows.AddRange(dirs); rows.AddRange(files);
        }
        catch { return null; }   // a folder that can't be read: stay where we are
        return rows;
    }

    private void Path_KeyDown(object s, KeyEventArgs e)
    {
        if (e.Key != Key.Enter) return;
        var p = PathBox.Text.Trim().Trim('"');
        if (Directory.Exists(p)) Go(p); else if (p.Length > 0 && File.Exists(p)) Done(new List<string> { p });
        e.Handled = true;
    }

    // ---------- choosing ----------

    private void List_SelectionChanged(object s, SelectionChangedEventArgs e)
    {
        var f = Selected().Where(x => !x.IsDir).ToList();
        if (f.Count > 0) NameBox.Text = string.Join(" ", f.Select(x => f.Count > 1 ? "\"" + x.Name + "\"" : x.Name));
    }

    private void List_DoubleClick(object s, MouseButtonEventArgs e) { if (e.OriginalSource is DependencyObject d && ItemsControl.ContainerFromElement(List, d) != null) Commit(); }

    private void List_KeyDown(object s, KeyEventArgs e)
    {
        if (e.Key == Key.Enter) { Commit(); e.Handled = true; }
        else if (e.Key == Key.Back) { Up_Click(s, e); e.Handled = true; }
    }

    private void Name_KeyDown(object s, KeyEventArgs e) { if (e.Key == Key.Enter) { Commit(); e.Handled = true; } }
    private void Ok_Click(object s, RoutedEventArgs e) => Commit();
    private void Cancel_Click(object s, RoutedEventArgs e) => Close();

    /// Enter / the button / a double click: a folder opens, a file (or a typed name) is the answer.
    private void Commit()
    {
        var typed = NameBox.Text.Trim(); var sel = Selected();
        if (sel.Count == 1 && sel[0].IsDir && (typed.Length == 0 || List.IsKeyboardFocusWithin)) { Go(sel[0].FullPath); return; }
        if (typed.Length == 0) return;
        var names = new List<string>();
        if (typed.Contains('"')) foreach (System.Text.RegularExpressions.Match m in System.Text.RegularExpressions.Regex.Matches(typed, "\"([^\"]+)\"")) names.Add(m.Groups[1].Value); else names.Add(typed);
        var full = names.Select(n => Path.IsPathRooted(n) ? n : Path.Combine(_cur, n)).ToList();
        if (full.Count == 1 && Directory.Exists(full[0])) { Go(full[0]); NameBox.Clear(); return; }   // typed a folder: go there
        if (_cur.Length == 0 && full.Count == 1 && !Path.IsPathRooted(names[0])) return;   // a bare name has no folder on the drives page
        Done(full);
    }

    internal void TestAnswer(string path) => Done(new List<string> { path });   // the self-test answers the way a click on a file would

    private void Done(List<string> paths) { Result = paths; Close(); }

    /// Over the dialog it replaces, kept inside that screen's work area (device pixels, so it lands on the right screen at any scaling).
    private void Centre()
    {
        var h = new WindowInteropHelper(this).Handle; GetWindowRect(h, out var me);
        var dlg = new RECT { L = _rect.L, T = _rect.T, R = _rect.R, B = _rect.B }; var mi = new MONITORINFO { cbSize = Marshal.SizeOf<MONITORINFO>() };
        GetMonitorInfo(MonitorFromRect(ref dlg, 2 /*MONITOR_DEFAULTTONEAREST*/), ref mi);
        int w = me.R - me.L, hh = me.B - me.T, wl = mi.rcWork.L, wt = mi.rcWork.T;
        int x = Math.Clamp((_rect.L + _rect.R) / 2 - w / 2, wl, Math.Max(wl, mi.rcWork.R - w)), y = Math.Clamp((_rect.T + _rect.B) / 2 - hh / 2, wt, Math.Max(wt, mi.rcWork.B - hh));
        SetWindowPos(h, IntPtr.Zero, x, y, 0, 0, 0x0001 | 0x0004 | 0x0010);   // SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
    }

    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] private struct MONITORINFO { public int cbSize; public RECT rcMonitor, rcWork; public uint dwFlags; }
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromRect(ref RECT r, uint flags);
    [DllImport("user32.dll")] private static extern bool GetMonitorInfo(IntPtr m, ref MONITORINFO mi);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
