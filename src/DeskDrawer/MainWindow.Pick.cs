using System.IO;
using System.IO.Enumeration;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;

namespace DeskDrawer;

/// Picker mode: the real DeskDrawer window (tabs, path bar, search, folder tree, every view, preview) standing in for another app's Open / Save dialog, with one extra bar at the
/// bottom: file name, the dialog's own file types, and its Open / Save button. DialogSwitch opens it and types the answer into the hidden dialog. It never saves the session
/// (your own tabs and window size stay as they were) and never goes to the background.
public partial class MainWindow
{
    public sealed record PickRequest(string Folder, string Name, string Button, string[] Types, int TypeIndex, string Title, int L, int T, int R, int B);

    private PickRequest? _pick;
    private string[] _pickPatterns = Array.Empty<string>(); private int _pickType; private bool _pickAll, _pickSave;
    /// The chosen full paths (null = cancelled) and which of the dialog's file types was chosen.
    public List<string>? Result { get; private set; }
    public int TypeIndex => _pickType;

    /// One picker window is built once and kept (hidden) between dialogs: building and first drawing the whole DeskDrawer window took 0.5-0.8 s every time,
    /// which is the lag you saw. Showing a window that has been drawn before takes a fraction of that.
    private static MainWindow? _spare;

    public static MainWindow ForPick(string folder, string name, string button, string[] types, int typeIndex, string title, int l, int t, int r, int b)
    {
        var req = new PickRequest(folder, name, string.IsNullOrWhiteSpace(button) ? "Open" : button, types, typeIndex, title, l, t, r, b);
        var w = _spare ?? new MainWindow(req); _spare = null; w.PickUse(req); return w;
    }

    /// Built and drawn once, off screen and invisible, so even the first dialog opens fast (only while the switch is on).
    public static void PrewarmPicker()
    {
        if (_spare != null) return;
        var docs = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
        var w = new MainWindow(new PickRequest(docs, "", "Open", Array.Empty<string>(), -1, "", 0, 0, 0, 0)) { ShowActivated = false, Opacity = 0 };
        w.ContentRendered += Rendered; w.Show();
        void Rendered(object? s, EventArgs e) { w.ContentRendered -= Rendered; w.Hide(); w.Opacity = 1; w.ShowActivated = true; _spare ??= w; }
    }

    /// Shows the picker for the request it was given, over the dialog, with the file-name box ready.
    public void PickShow()
    {
        if (new WindowInteropHelper(this).Handle != IntPtr.Zero) PickCentre(); else { Left = -32000; Top = -32000; }   // drawn before: put it in place first, then show it (no step off screen)
        Show(); Activate();
        Dispatcher.BeginInvoke(() => { PickCentre(); PickCentre(); PickName.Focus(); PickName.SelectAll(); Activate(); }, System.Windows.Threading.DispatcherPriority.Loaded);   // twice: the first move can change the scale (another screen)
    }

    private void PickInit()
    {
        var wa = SystemParameters.WorkArea; Width = Math.Min(1040, wa.Width - 24); Height = Math.Min(680, wa.Height - 24); WindowState = WindowState.Normal;   // your own window size and maximised state stay yours
        Topmost = true; ShowInTaskbar = false; WindowStartupLocation = WindowStartupLocation.Manual; Left = -32000; Top = -32000; PickBar.Visibility = Visibility.Visible;
        PreviewKeyDown += (_, e) => { if (e.Key == Key.Escape && (Keyboard.FocusedElement is not TextBox || Keyboard.FocusedElement == PickName) && !InSearch) { Close(); e.Handled = true; } };
    }

    /// Takes one dialog's request: its folder, name, button, file types and title. The window may have answered another dialog before: everything of that one is reset.
    private void PickUse(PickRequest req)
    {
        _pick = req; var p = req; Result = null; _pickAnswered = false; _pickAll = false; OnAnswer = null;
        RefreshPins();   // the picker is built ahead of time and reused: read the pins again, or folders pinned since then are missing
        if (_tabs.Count > 0) { foreach (var t in _tabs.Skip(1).ToList()) _tabs.Remove(t); Activate(_tabs[0]); Search.Text = ""; _tab.Filter = ""; NavigateTab(_tab, p.Folder, push: false); }
        _pickType = p.TypeIndex; _pickSave = p.Button.Contains("Save", StringComparison.OrdinalIgnoreCase);
        _pickPatterns = _pickType >= 0 && _pickType < p.Types.Length ? DialogSwitch.PatternsOf(p.Types[_pickType]) : Array.Empty<string>();
        Title = "DeskDrawer · " + (string.IsNullOrWhiteSpace(p.Title) ? p.Button : p.Title);
        var wa = SystemParameters.WorkArea; Width = Math.Min(1040, wa.Width - 24); Height = Math.Min(680, wa.Height - 24); WindowState = WindowState.Normal;
        _pickFolder = p.Button.Contains("Folder", StringComparison.OrdinalIgnoreCase);   // a folder picker (Quartus' "Select Folder"): the answer is a folder
        PickOk.Content = p.Button; PickName.Text = _pickFolder ? "" : p.Name; PickLabel.Text = _pickFolder ? "Folder" : "File name";
        PickName.ToolTip = _pickFolder ? "Leave it empty to choose the folder that is open, or type a path" : null;
        PickType.Visibility = p.Types.Length == 0 ? Visibility.Collapsed : Visibility.Visible; PickShowType();
        foreach (var t in _tabs) ApplyFilter(t);
    }

    /// The file-type filter of the list: the dialog's patterns for the chosen type (none = everything).
    private bool _pickFolder;
    public bool PickIsFolder => _pickFolder;
    private bool PickShows(FileEntry e) => e.IsDir || !_pickFolder && _pickAll || _pickPatterns.Length == 0 || _pickPatterns.Any(p => FileSystemName.MatchesSimpleExpression(p, e.Name, true));

    private void PickShowType()
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal };
        row.Children.Add(new TextBlock { Text = _pickAll ? "All files" : _pickType >= 0 && _pickType < _pick!.Types.Length ? _pick.Types[_pickType] : "", TextTrimming = TextTrimming.CharacterEllipsis, MaxWidth = 300, VerticalAlignment = VerticalAlignment.Center });
        row.Children.Add(new TextBlock { Text = "", FontFamily = (FontFamily)FindResource("IconFont"), FontSize = 10, Margin = new Thickness(10, 1, 0, 0), Foreground = (Brush)FindResource("FgDim"), VerticalAlignment = VerticalAlignment.Center });
        PickType.Content = row;
    }

    private void PickType_Click(object s, RoutedEventArgs e)
    {
        var m = new ContextMenu { PlacementTarget = PickType, Placement = System.Windows.Controls.Primitives.PlacementMode.Top };
        for (int i = 0; i < _pick!.Types.Length; i++)
        {
            int k = i; var mi = new MenuItem { Header = ((!_pickAll && k == _pickType) ? "✓  " : "     ") + _pick.Types[i].Replace("_", "__") };
            mi.Click += (_, _) => PickSetType(k); m.Items.Add(mi);
        }
        var all = new MenuItem { Header = (_pickAll ? "✓  " : "     ") + "All files" }; all.Click += (_, _) => { _pickAll = true; PickShowType(); foreach (var t in _tabs) ApplyFilter(t); };
        m.Items.Add(new Separator()); m.Items.Add(all); m.IsOpen = true;
    }

    /// When saving, the extension in the file name follows the type (report.docx -> report.pdf), like Windows' dialog does.
    private void PickSetType(int k)
    {
        string? Ext(string[] pats) => pats.FirstOrDefault(x => x.StartsWith("*.") && x.IndexOfAny(new[] { '*', '?' }, 1) < 0) is { } x0 ? x0[1..] : null;
        string? from = Ext(_pickPatterns), to = Ext(DialogSwitch.PatternsOf(_pick!.Types[k])); var name = PickName.Text.Trim();
        if (_pickSave && from != null && to != null && !name.Contains('"') && name.EndsWith(from, StringComparison.OrdinalIgnoreCase)) PickName.Text = name[..^from.Length] + to;
        _pickType = k; _pickAll = false; _pickPatterns = DialogSwitch.PatternsOf(_pick.Types[k]); PickShowType(); foreach (var t in _tabs) ApplyFilter(t);
    }

    /// The selected files show in the name box (several = quoted), as in Windows' dialog.
    private void PickSelection()
    {
        var f = Sel().Where(x => !x.IsDir).ToList();
        if (f.Count > 0) PickName.Text = string.Join(" ", f.Select(x => f.Count > 1 ? "\"" + x.Name + "\"" : x.Name));
    }

    private void PickName_KeyDown(object s, KeyEventArgs e) { if (e.Key == Key.Enter) { PickCommit(); e.Handled = true; } }
    private void PickOk_Click(object s, RoutedEventArgs e) => PickCommit();
    private void PickCancel_Click(object s, RoutedEventArgs e) => Close();

    /// The button / Enter in the name box: a typed folder opens, a name (or several quoted names) is the answer, placed in the folder that is open.
    private void PickCommit()
    {
        var typed = PickName.Text.Trim(); var sel = Sel();
        if (_pickFolder && typed.Length == 0)   // the selected folder, or the one that is open
        {
            string? f = sel.Count == 1 && sel[0].IsDir ? sel[0].FullPath : _tab.Current != null && !IsVirtual(_tab.Current) ? _tab.Current : null;
            if (f != null) PickDone(new List<string> { f }); else Flash("Open a folder first");
            return;
        }
        if (_pickFolder) { var p = Path.IsPathRooted(typed) ? typed : _tab.Current != null && !IsVirtual(_tab.Current) ? Path.Combine(_tab.Current, typed) : typed; if (Directory.Exists(p)) PickDone(new List<string> { p }); else Flash("No such folder"); return; }
        if (typed.Length == 0) { if (sel.Count == 1 && sel[0].IsDir) Navigate(sel[0].FullPath); return; }
        var names = new List<string>();
        if (typed.Contains('"')) foreach (System.Text.RegularExpressions.Match m in System.Text.RegularExpressions.Regex.Matches(typed, "\"([^\"]+)\"")) names.Add(m.Groups[1].Value); else names.Add(typed);
        string? here = _tab.Current != null && !IsVirtual(_tab.Current) ? _tab.Current : null;
        if (names.Any(n => !Path.IsPathRooted(n)) && here == null)   // Home, This PC, search results: no folder to put a bare name in; selected files carry their own
        {
            var picked = sel.Where(x => !x.IsDir).Select(x => x.FullPath).ToList(); if (picked.Count > 0) PickDone(picked); else Flash("Open a folder first");
            return;
        }
        var full = names.Select(n => Path.IsPathRooted(n) ? n : Path.Combine(here!, n)).ToList();
        if (full.Count == 1 && Directory.Exists(full[0])) { Navigate(full[0]); PickName.Clear(); return; }
        PickDone(full);
    }

    private void PickDone(List<string> paths) { Result = paths; Close(); }

    /// Every way out (the buttons, Esc, the window's X) comes here first: the picker steps off screen but keeps the foreground until the dialog it answers is gone,
    /// so DialogSwitch can hand the foreground back to the app that asked. FinishPick closes it for real.
    public Action<MainWindow>? OnAnswer;
    private bool _pickAnswered;
    /// Done with this dialog: hidden and kept for the next one.
    internal void FinishPick() { Hide(); _spare ??= this; }
    private bool PickClosing()
    {
        if (!_pickAnswered) { _pickAnswered = true; Left = -32000; Top = -32000; var a = OnAnswer; Dispatcher.BeginInvoke(() => { if (a != null) a(this); else FinishPick(); }); }   // after the close request has returned: closing from inside it crashed
        return true;   // never really closes: it steps off screen until FinishPick hides it
    }
    internal void TestAnswer(string path) => PickDone(new List<string> { path });   // the self-test answers the way a click on a file would

    /// Over the dialog it replaces, kept inside that screen's work area (device pixels, so it lands on the right screen at any scaling).
    private void PickCentre()
    {
        var h = new WindowInteropHelper(this).Handle; PickGetWindowRect(h, out var me);
        var dlg = new PRECT { L = _pick!.L, T = _pick.T, R = _pick.R, B = _pick.B }; var mi = new PMONITORINFO { cbSize = Marshal.SizeOf<PMONITORINFO>() };
        PickGetMonitorInfo(PickMonitorFromRect(ref dlg, 2), ref mi);
        int w = me.R - me.L, hh = me.B - me.T, wl = mi.rcWork.L, wt = mi.rcWork.T;
        int x = Math.Clamp((dlg.L + dlg.R) / 2 - w / 2, wl, Math.Max(wl, mi.rcWork.R - w)), y = Math.Clamp((dlg.T + dlg.B) / 2 - hh / 2, wt, Math.Max(wt, mi.rcWork.B - hh));
        PickSetWindowPos(h, IntPtr.Zero, x, y, 0, 0, 0x0001 | 0x0004 | 0x0010);
    }

    [StructLayout(LayoutKind.Sequential)] private struct PRECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] private struct PMONITORINFO { public int cbSize; public PRECT rcMonitor, rcWork; public uint dwFlags; }
    [DllImport("user32.dll", EntryPoint = "MonitorFromRect")] private static extern IntPtr PickMonitorFromRect(ref PRECT r, uint flags);
    [DllImport("user32.dll", EntryPoint = "GetMonitorInfoW")] private static extern bool PickGetMonitorInfo(IntPtr m, ref PMONITORINFO mi);
    [DllImport("user32.dll", EntryPoint = "GetWindowRect")] private static extern bool PickGetWindowRect(IntPtr h, out PRECT r);
    [DllImport("user32.dll", EntryPoint = "SetWindowPos")] private static extern bool PickSetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
