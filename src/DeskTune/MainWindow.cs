using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;

namespace DeskTune;

/// The DeskTune window. You tick as many switches as you like (nothing happens yet, and nothing else locks), see what that would free, then press
/// Apply once. A switch that is on means "this is turned off to save resources". Every change goes through Log, so it can be undone.
/// Built for touch too: whole-row tap targets, 44 px buttons, finger-drag scrolling.
public sealed class MainWindow : Window
{
    private sealed record RowUi(Tweak T, CheckBox Sw, Border Card, Border Edge, bool Locked);

    private readonly StackPanel _list = new();
    private readonly WrapPanel _chips = new();
    /// Section order: the short, high-value ones first; the long startup list last so it doesn't make DeskTune look like a startup manager.
    private static readonly string[] Order = { "Windows extras", "Ads and suggestions", "Background services", "Preinstalled apps", "Starts with Windows" };
    private static readonly Dictionary<string, string> Icons = new() { ["Windows extras"] = "", ["Ads and suggestions"] = "", ["Background services"] = "", ["Preinstalled apps"] = "", ["Starts with Windows"] = "" };
    private readonly TextBlock _memText = new(), _offText = new(), _freeText = new(), _status = new(), _undoLink = new(), _applyText = new();
    private readonly Border _memFill = new() { Height = 8, CornerRadius = new CornerRadius(4), HorizontalAlignment = HorizontalAlignment.Left };
    private readonly Border _memTrack = new() { Height = 8, CornerRadius = new CornerRadius(4), Margin = new Thickness(0, 10, 0, 0) };
    private readonly Border _applyBtn = new(), _clearBtn = new();
    private readonly List<RowUi> _rows = new();
    private List<Tweak> _tweaks = new();
    private Dictionary<string, long> _mb = new();
    private bool _busy, _appsScanned, _compact;
    private TextBlock _sub = null!;
    private UniformGrid _stats = null!;
    private const string Intro = "Make Windows lighter. Tick what you want off, then apply. Every change can be undone.";
    private uint _load;
    /// What you ticked but haven't applied, by switch id: survives redraws (the app list arriving, a rescan) so a selection is never lost.
    private readonly Dictionary<string, bool> _wanted = new();

    public MainWindow()
    {
        // The work area is in layout units, which shrink as Windows scaling grows: a 1080p screen at 150% is only ~1280 x 670, so a fixed 980 x 820 window overflowed it.
        var wa = SystemParameters.WorkArea;
        Title = "DeskTune"; Width = App.ForcedSize?.Width ?? Math.Min(980, wa.Width - 24); Height = App.ForcedSize?.Height ?? Math.Min(820, wa.Height - 24);
        MinWidth = Math.Min(520, Width); MinHeight = Math.Min(480, Height); WindowStartupLocation = WindowStartupLocation.CenterScreen;
        SetResourceReference(BackgroundProperty, "WinBg"); UseLayoutRounding = true; Stylus.SetIsPressAndHoldEnabled(this, false);
        SourceInitialized += (_, _) => { int dark = Theme.IsLight ? 0 : 1; DwmSetWindowAttribute(new WindowInteropHelper(this).Handle, 20, ref dark, 4); };

        var root = new Grid(); Content = root;
        foreach (var h in new[] { GridLength.Auto, new GridLength(1, GridUnitType.Star), GridLength.Auto }) root.RowDefinitions.Add(new RowDefinition { Height = h });

        // ---- top: title, three stat cards, presets, section chips. It scrolls away with the list (on a short screen it used to take most of the window), the bar below stays put
        var top = new StackPanel { Margin = new Thickness(0, 22, 0, 6) };
        var titleRow = new StackPanel { Orientation = Orientation.Horizontal };
        var logo = Glyph("", "Accent", 30); logo.Margin = new Thickness(0, 4, 14, 0); titleRow.Children.Add(logo);
        var titles = new StackPanel();
        titles.Children.Add(Text("DeskTune", 28, "Fg", FontWeights.SemiBold));
        _sub = Text(Intro, 13, "FgDim"); titles.Children.Add(_sub);
        titleRow.Children.Add(titles); top.Children.Add(titleRow);

        var stats = _stats = new UniformGrid { Rows = 1, Margin = new Thickness(0, 18, 0, 0) };
        var memCard = Card(); var mp = new StackPanel(); mp.Children.Add(Text("MEMORY IN USE", 11, "FgFaint", FontWeights.SemiBold)); _memText.FontSize = 16; _memText.SetResourceReference(TextBlock.ForegroundProperty, "Fg"); _memText.Margin = new Thickness(0, 4, 0, 0); mp.Children.Add(_memText);
        _memTrack.SetResourceReference(Border.BackgroundProperty, "CardLine"); _memFill.SetResourceReference(Border.BackgroundProperty, "Accent"); _memTrack.Child = _memFill; mp.Children.Add(_memTrack); memCard.Child = mp;
        _memTrack.SizeChanged += (_, _) => _memFill.Width = _memTrack.ActualWidth * _load / 100.0;
        var offCard = Card(); var op = new StackPanel(); op.Children.Add(Text("SWITCHED OFF", 11, "FgFaint", FontWeights.SemiBold)); _offText.FontSize = 16; _offText.SetResourceReference(TextBlock.ForegroundProperty, "Fg"); _offText.Margin = new Thickness(0, 4, 0, 0); op.Children.Add(_offText); offCard.Child = op;
        var freeCard = Card(); var fp = new StackPanel(); fp.Children.Add(Text("YOUR SELECTION WOULD FREE", 11, "FgFaint", FontWeights.SemiBold)); _freeText.FontSize = 16; _freeText.SetResourceReference(TextBlock.ForegroundProperty, "Accent"); _freeText.Margin = new Thickness(0, 4, 0, 0); fp.Children.Add(_freeText); freeCard.Child = fp;
        stats.Children.Add(memCard); stats.Children.Add(offCard); stats.Children.Add(freeCard); top.Children.Add(stats);

        var presets = new WrapPanel { Margin = new Thickness(0, 12, 0, 0) };
        presets.Children.Add(Pill("", "Select Safe", "Ads, suggestions and nags only. Nothing you use stops working.", () => SelectPreset("safe")));
        presets.Children.Add(Pill("", "Select Recommended", "Safe, plus background extras, telemetry services and junk apps. Startup apps stay your choice.", () => SelectPreset("recommended")));
        if (!Elevation.IsAdmin) presets.Children.Add(Pill("", "Unlock services (administrator)", "Services and machine-wide settings need administrator rights.", () => { if (Elevation.RestartAsAdmin()) Close(); }));
        presets.Children.Add(Pill("", "Rescan", "Look at the PC again and re-measure memory.", () => { _appsScanned = false; Scan(); }));
        top.Children.Add(presets);
        _chips.Margin = new Thickness(0, 8, 0, 0); top.Children.Add(_chips);

        // ---- the list: finger-drag scrolling, generous padding
        _list.Margin = new Thickness(0, 0, 0, 24);
        var page = new StackPanel(); page.Children.Add(top); page.Children.Add(_list);
        var scroll = new ScrollViewer { Content = page, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, Padding = new Thickness(28, 0, 28, 0), PanningMode = PanningMode.VerticalOnly, PanningDeceleration = 0.004 };   // WPF's default coast (0.001) sends one flick across the whole list
        Wheel.Attach(scroll);
        Grid.SetRow(scroll, 1); root.Children.Add(scroll);

        // ---- bottom: status on the left, Clear and Apply on the right (Apply wakes up when something is ticked)
        var foot = new Border { Padding = new Thickness(28, 10, 28, 12), BorderThickness = new Thickness(0, 1, 0, 0) }; Grid.SetRow(foot, 2);
        foot.SetResourceReference(Border.BackgroundProperty, "BarBg"); foot.SetResourceReference(Border.BorderBrushProperty, "BarLine");
        var fg = new Grid(); fg.ColumnDefinitions.Add(new ColumnDefinition()); fg.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto }); fg.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto }); fg.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var left = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
        _status.SetResourceReference(TextBlock.ForegroundProperty, "FgDim"); _status.TextWrapping = TextWrapping.Wrap; left.Children.Add(_status);
        _undoLink.SetResourceReference(TextBlock.ForegroundProperty, "Accent"); _undoLink.FontSize = 12; _undoLink.Cursor = Cursors.Hand; _undoLink.Margin = new Thickness(0, 4, 0, 0); _undoLink.MouseLeftButtonUp += (_, _) => UndoAll(); left.Children.Add(_undoLink);
        {   // updates (Updater.cs): a bright line when a newer build is out, and two quiet links for checking and for turning the daily check off
            var newer = new TextBlock { FontSize = 12.5, FontWeight = FontWeights.SemiBold, Margin = new Thickness(0, 6, 0, 0), Cursor = Cursors.Hand, Visibility = Visibility.Collapsed };
            newer.SetResourceReference(TextBlock.ForegroundProperty, "Accent");
            newer.MouseLeftButtonUp += async (_, _) => { if (await Updater.UpdateNowAsync()) Application.Current.Shutdown(); };   // the setup that just started replaces this copy and opens the new one
            var opts = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 4, 0, 0) };
            TextBlock Link(Action click) { var t = new TextBlock { FontSize = 11.5, Cursor = Cursors.Hand, Margin = new Thickness(0, 0, 14, 0) }; t.SetResourceReference(TextBlock.ForegroundProperty, "FgFaint"); t.MouseLeftButtonUp += (_, _) => click(); opts.Children.Add(t); return t; }
            var check = Link(OpenSettings); var auto = Link(OpenSettings);   // both lead to Settings, where updates and the safety net live now
            void Show()
            {
                newer.Visibility = Updater.Newer || Updater.Busy && Updater.Status != "Checking…" ? Visibility.Visible : Visibility.Collapsed;
                newer.Text = Updater.Busy ? Updater.Status : "A new version of DeskTune is available · Update now";
                check.Text = "  Settings"; check.FontFamily = new FontFamily("Segoe Fluent Icons, Segoe MDL2 Assets, Segoe UI Variable Text, Segoe UI");
                auto.Visibility = Visibility.Collapsed;
            }
            Updater.Changed += () => Dispatcher.BeginInvoke(Show); Show();
            left.Children.Add(newer); left.Children.Add(opts);
        }
        fg.Children.Add(left);
        Button(_clearBtn, "Clear selection", false, ClearSelection); Grid.SetColumn(_clearBtn, 2); _clearBtn.Margin = new Thickness(0, 0, 10, 0); fg.Children.Add(_clearBtn);
        Button(_applyBtn, "", true, ApplySelection); _applyBtn.Child = _applyText; Grid.SetColumn(_applyBtn, 3); fg.Children.Add(_applyBtn);
        foot.Child = fg; root.Children.Add(foot);

        UpdateBar();
        SizeChanged += (_, _) => { bool c = ActualHeight < 760; if (c == _compact) return; _compact = c; Summarise(); };   // a short window (a 1080p screen at 150% is ~670 high) gets one summary line instead of three cards
        Loaded += (_, _) => Scan();
        PreviewKeyDown += (_, e) => { if (e.Key == Key.OemComma && Keyboard.Modifiers == ModifierKeys.Control) { OpenSettings(); e.Handled = true; } };
    }

    // ---------------------------------------------------------------- scan and draw

    /// Everything but the preinstalled apps first (instant), then the app list, which needs a PowerShell call of 2–4 s, fills in on its own.
    private void Scan(string? after = null)
    {
        _status.Text = "Looking at this PC…"; _busy = true;
        Task.Run(() => { var t = Catalogue.Build(); var mb = Catalogue.MeasureMemory(); return (t, mb); })
            .ContinueWith(r => Dispatcher.Invoke(() =>
            {
                _busy = false;
                if (r.Exception != null) { _status.Text = "Scan failed: " + r.Exception.InnerException?.Message; return; }
                (_tweaks, _mb) = r.Result; Draw();
                _status.Text = after ?? $"Found {_tweaks.Count} things you can switch off." + (_appsScanned ? "" : " Checking preinstalled apps…");
                UpdateBar();
                if (_appsScanned) return;
                _appsScanned = true;
                Task.Run(() => { AppxOp.Scan(); return Catalogue.Build(); }).ContinueWith(r2 => Dispatcher.Invoke(() =>
                {
                    if (r2.Exception != null || _busy) return;
                    _tweaks = r2.Result; Draw(); UpdateBar();
                    if (after == null && Chosen().Count == 0) _status.Text = $"Found {_tweaks.Count} things you can switch off.";
                }));
            }));
    }

    private void Draw()
    {
        _list.Children.Clear(); _chips.Children.Clear(); _rows.Clear();
        UpdateMemory();
        _offText.Text = $"{_tweaks.Count(t => t.IsApplied)} of {_tweaks.Count}"; Summarise();
        foreach (var group in _tweaks.GroupBy(t => t.Category).OrderBy(g => Array.IndexOf(Order, g.Key) is var i && i < 0 ? 99 : i))
        {
            var items = group.OrderByDescending(t => t.MemoryMB(_mb)).ThenBy(t => t.Title).ToList();
            int off = items.Count(t => t.IsApplied);

            var head = new Grid { Margin = new Thickness(0, 22, 0, 10) };
            head.ColumnDefinitions.Add(new ColumnDefinition()); head.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            var title = new StackPanel { Orientation = Orientation.Horizontal };
            title.Children.Add(Glyph(Icons.GetValueOrDefault(group.Key, ""), "Accent", 18)); var tt = Text(group.Key, 18, "Fg", FontWeights.SemiBold); tt.Margin = new Thickness(10, 0, 10, 0); title.Children.Add(tt);
            var cnt = Text($"{off} of {items.Count} off", 12, "FgFaint"); cnt.VerticalAlignment = VerticalAlignment.Center; title.Children.Add(cnt);
            head.Children.Add(title);
            var all = Link("Select all"); Grid.SetColumn(all, 1); var sect = items; all.MouseLeftButtonUp += (_, _) => SelectMany(sect.Where(x => !x.IsApplied)); head.Children.Add(all);
            _list.Children.Add(head);
            if (group.Key == "Starts with Windows") { var n = Text("The biggest win on most PCs. Memory shown is what the program uses right now; it frees up after the next restart.", 12, "FgFaint"); n.Margin = new Thickness(0, -4, 0, 10); n.TextWrapping = TextWrapping.Wrap; _list.Children.Add(n); }
            foreach (var t in items) _list.Children.Add(Row(t));

            var chip = new Border { CornerRadius = new CornerRadius(16), Padding = new Thickness(14, 8, 14, 9), Margin = new Thickness(0, 0, 8, 8), Cursor = Cursors.Hand, BorderThickness = new Thickness(1), Child = Text($"{group.Key}  {off}/{items.Count}", 12, "Fg"), ToolTip = "Jump to " + group.Key };
            chip.SetResourceReference(Border.BackgroundProperty, "Card"); chip.SetResourceReference(Border.BorderBrushProperty, "CardLine");
            chip.MouseLeftButtonUp += (_, _) => head.BringIntoView(new Rect(0, 0, 1, 520));
            _chips.Children.Add(chip);
        }
        _undoLink.Text = Log.Count == 0 ? "" : $"Undo all {Log.Count} change{(Log.Count == 1 ? "" : "s")} DeskTune made";
    }

    private Border Row(Tweak t)
    {
        bool applied = t.IsApplied, ours = Log.Touched(t.Id), needsAdmin = t.Admin && !Elevation.IsAdmin, locked = needsAdmin || (applied && !ours);
        var sw = new CheckBox { Style = (Style)FindResource("Switch"), IsChecked = !locked && _wanted.TryGetValue(t.Id, out var want) ? want : applied, VerticalAlignment = VerticalAlignment.Center, IsHitTestVisible = false, Focusable = false, LayoutTransform = new ScaleTransform(1.25, 1.25) };

        var g = new Grid { Margin = new Thickness(0, 12, 14, 12), MinHeight = 44 };
        g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(78) }); g.ColumnDefinitions.Add(new ColumnDefinition()); g.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        sw.HorizontalAlignment = HorizontalAlignment.Center; g.Children.Add(sw);
        var text = new StackPanel { VerticalAlignment = VerticalAlignment.Center }; Grid.SetColumn(text, 1);
        var title = Text(t.Title, 15, "Fg"); title.TextWrapping = TextWrapping.Wrap; text.Children.Add(title);
        var lose = Text("You lose: " + t.Lose, 12.5, "FgDim"); lose.TextWrapping = TextWrapping.Wrap; lose.Margin = new Thickness(0, 3, 0, 0); text.Children.Add(lose);
        if (locked) { var why = Text(needsAdmin ? "Needs administrator rights: use \"Unlock services\" at the top." : "Already off, changed outside DeskTune, so DeskTune can't undo it.", 11.5, "FgFaint"); why.Margin = new Thickness(0, 3, 0, 0); text.Children.Add(why); }
        g.Children.Add(text);
        var right = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(12, 0, 0, 0) }; Grid.SetColumn(right, 2);
        long mb = t.MemoryMB(_mb);
        if (mb > 0) { var badge = new Border { CornerRadius = new CornerRadius(11), Padding = new Thickness(10, 3, 10, 4), Child = Text($"{mb:N0} MB", 12.5, "Accent", FontWeights.SemiBold), ToolTip = "What it uses right now" }; badge.SetResourceReference(Border.BackgroundProperty, "Card"); right.Children.Add(badge); }
        if (t.Preset.Length > 0) { var p = Text(t.Preset == "safe" ? "Safe" : "Recommended", 11, "FgFaint"); p.Margin = new Thickness(10, 0, 0, 0); p.VerticalAlignment = VerticalAlignment.Center; right.Children.Add(p); }
        if (t.Admin) { var s = Glyph("", needsAdmin ? "FgFaint" : "FgDim", 14); s.Margin = new Thickness(10, 0, 0, 0); s.ToolTip = "Needs administrator rights"; right.Children.Add(s); }
        g.Children.Add(right);

        var edge = new Border { Width = 4, CornerRadius = new CornerRadius(2), Margin = new Thickness(0, 10, 0, 10), Background = Brushes.Transparent };
        var inner = new Grid(); inner.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(10) }); inner.ColumnDefinitions.Add(new ColumnDefinition());
        inner.Children.Add(edge); Grid.SetColumn(g, 1); inner.Children.Add(g);
        var card = new Border { Child = inner, CornerRadius = new CornerRadius(12), Margin = new Thickness(0, 0, 0, 8), BorderThickness = new Thickness(1), Opacity = locked ? 0.6 : 1, Cursor = locked ? Cursors.Arrow : Cursors.Hand };
        var ui = new RowUi(t, sw, card, edge, locked); _rows.Add(ui); Paint(ui);
        if (!locked)
        {
            card.MouseLeftButtonUp += (_, e) => { if (!_busy) { Want(ui, sw.IsChecked != true); UpdateBar(); } };
            card.MouseEnter += (_, _) => { if (!Pending(ui)) card.SetResourceReference(Border.BackgroundProperty, "CardHover"); };
            card.MouseLeave += (_, _) => Paint(ui);
        }
        return card;
    }

    private static bool Pending(RowUi r) => (r.Sw.IsChecked == true) != r.T.IsApplied;

    /// Ticks or unticks a row and remembers it (or forgets it, if that's just how things already are).
    private void Want(RowUi r, bool on)
    {
        r.Sw.IsChecked = on;
        if (on == r.T.IsApplied) _wanted.Remove(r.T.Id); else _wanted[r.T.Id] = on;
        Paint(r);
    }

    /// Ticked-but-not-applied rows get an accent edge and tint, so you can see your selection at a glance.
    private void Paint(RowUi r)
    {
        bool p = Pending(r);
        r.Card.SetResourceReference(Border.BackgroundProperty, p ? "Select" : "Card"); r.Card.SetResourceReference(Border.BorderBrushProperty, p ? "SelectLine" : "CardLine");
        if (p) r.Edge.SetResourceReference(Border.BackgroundProperty, "Accent"); else r.Edge.Background = Brushes.Transparent;
    }

    private void UpdateMemory()
    {
        var m = new MEMORYSTATUSEX { dwLength = (uint)Marshal.SizeOf<MEMORYSTATUSEX>() }; GlobalMemoryStatusEx(ref m);
        double total = m.ullTotalPhys / 1073741824.0, used = (m.ullTotalPhys - m.ullAvailPhys) / 1073741824.0;
        _memText.Text = $"{used:N1} of {total:N1} GB  ·  {m.dwMemoryLoad}%"; Summarise();
        _load = m.dwMemoryLoad; _memFill.Width = _memTrack.ActualWidth * _load / 100.0;
    }

    // ---------------------------------------------------------------- choose, then apply

    private List<RowUi> Chosen() => _rows.Where(Pending).ToList();

    private void UpdateBar()
    {
        var c = Chosen(); int on = c.Count(r => r.Sw.IsChecked == true), back = c.Count - on;
        long mb = c.Where(r => r.Sw.IsChecked == true).Sum(r => r.T.MemoryMB(_mb));
        _freeText.Text = c.Count == 0 ? "nothing yet" : mb > 0 ? $"about {mb:N0} MB" : "ticked, little memory";
        bool any = c.Count > 0;
        _applyText.Text = !any ? "Apply" : back == 0 ? $"Apply {c.Count} change{(c.Count == 1 ? "" : "s")}" : $"Apply {c.Count} changes";
        _applyBtn.Opacity = any && !_busy ? 1 : 0.4; _applyBtn.IsHitTestVisible = any && !_busy; _clearBtn.Visibility = any ? Visibility.Visible : Visibility.Collapsed;
        if (any) _status.Text = $"{on} to switch off" + (back > 0 ? $", {back} to put back" : "") + ". Nothing changes until you press Apply.";
    }

    private void SelectMany(IEnumerable<Tweak> tweaks)
    {
        var ids = tweaks.Select(t => t.Id).ToHashSet(); int n = 0, skipped = 0;
        foreach (var r in _rows.Where(r => ids.Contains(r.T.Id))) { if (r.Locked) { skipped++; continue; } if (r.Sw.IsChecked != true) n++; Want(r, true); }
        UpdateBar();
        if (skipped > 0 && n == 0) _status.Text = $"{skipped} can't be selected: they need administrator rights or were changed outside DeskTune.";
    }

    private void SelectPreset(string preset) => SelectMany(_tweaks.Where(t => t.Preset == "safe" || (preset == "recommended" && t.Preset == "recommended")));

    private void ClearSelection() { _wanted.Clear(); foreach (var r in _rows) { r.Sw.IsChecked = r.T.IsApplied; Paint(r); } UpdateBar(); _status.Text = "Selection cleared."; }

    private void ApplySelection()
    {
        var c = Chosen(); if (c.Count == 0 || _busy) return;
        var turnOff = c.Where(r => r.Sw.IsChecked == true).Select(r => r.T).ToList(); var putBack = c.Where(r => r.Sw.IsChecked != true).Select(r => r.T).ToList();
        Work($"Applying {c.Count} change{(c.Count == 1 ? "" : "s")}…", () =>
        {
            string rp = turnOff.Any(t => t.Admin) ? Elevation.EnsureRestorePoint() : "";
            var failed = new List<string>(); int done = 0;
            foreach (var t in turnOff) try { Log.Apply(t); done++; } catch (Exception ex) { failed.Add($"{t.Title} ({ex.Message})"); }
            foreach (var t in putBack) try { Log.Undo(t.Id); done++; } catch (Exception ex) { failed.Add($"{t.Title} ({ex.Message})"); }
            return $"Done: {done} change{(done == 1 ? "" : "s")}." + (failed.Count > 0 ? " Couldn't: " + string.Join("; ", failed) + "." : "") + " " + rp + " Startup and service changes fully take effect after a restart.";
        });
    }

    internal SettingsWindow? Settings;
    internal void OpenSettings() { if (Settings != null) { Settings.Activate(); return; } Settings = new SettingsWindow(this); Settings.Closed += (_, _) => Settings = null; Settings.Show(); }

    internal void UndoAll()
    {
        if (_busy || Log.Count == 0) return;
        if (MessageBox.Show(this, $"Put back all {Log.Count} changes DeskTune made?", "DeskTune", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
        Work("Putting everything back…", () => { Log.UndoAll(); return "Everything DeskTune changed is back as it was. Removed apps open in the Microsoft Store to reinstall."; });
    }

    /// Runs changes off the UI thread (services and app removal can take seconds), then rescans so every switch shows the truth.
    /// The list stays readable the whole time; only Apply is held until it finishes.
    private void Work(string doing, Func<string> job)
    {
        _busy = true; _status.Text = doing; UpdateBar();
        Task.Run(job).ContinueWith(r => Dispatcher.Invoke(() =>
        {
            _busy = false; _wanted.Clear();   // applied (or failed and shown): the switches now show the real state again
            Scan(r.Exception != null ? "Something went wrong: " + r.Exception.InnerException?.Message : r.Result);
        }));
    }

    // ---------------------------------------------------------------- bits

    /// Compact mode: the three stat cards are hidden and the line under the title carries the numbers instead.
    private void Summarise()
    {
        _stats.Visibility = _compact ? Visibility.Collapsed : Visibility.Visible;
        _sub.Text = _compact && _memText.Text.Length > 0 ? $"Memory {_memText.Text}  ·  {_offText.Text} switched off" : Intro;
    }

    private static TextBlock Text(string s, double size, string brush, FontWeight? w = null)
    { var t = new TextBlock { Text = s, FontSize = size, FontWeight = w ?? FontWeights.Normal }; t.SetResourceReference(TextBlock.ForegroundProperty, brush); return t; }

    private TextBlock Glyph(string g, string brush, double size) { var t = new TextBlock { Text = g, FontFamily = (FontFamily)FindResource("IconFont"), FontSize = size, VerticalAlignment = VerticalAlignment.Center }; t.SetResourceReference(TextBlock.ForegroundProperty, brush); return t; }

    private static Border Card()
    {
        var b = new Border { CornerRadius = new CornerRadius(12), Padding = new Thickness(16, 12, 16, 14), Margin = new Thickness(0, 0, 10, 0), BorderThickness = new Thickness(1) };
        b.SetResourceReference(Border.BackgroundProperty, "Card"); b.SetResourceReference(Border.BorderBrushProperty, "CardLine"); return b;
    }

    private TextBlock Link(string s) { var t = Text(s, 13, "Accent"); t.Cursor = Cursors.Hand; t.VerticalAlignment = VerticalAlignment.Center; t.Padding = new Thickness(8, 8, 0, 8); return t; }

    /// A tappable pill (44 px tall) used for the presets and Rescan.
    private Border Pill(string glyph, string label, string tip, Action click)
    {
        var sp = new StackPanel { Orientation = Orientation.Horizontal };
        sp.Children.Add(Glyph(glyph, "Accent", 15)); var l = Text(label, 13.5, "Fg"); l.Margin = new Thickness(9, 0, 0, 0); l.VerticalAlignment = VerticalAlignment.Center; sp.Children.Add(l);
        var b = new Border { Child = sp, MinHeight = 44, CornerRadius = new CornerRadius(22), Padding = new Thickness(18, 0, 20, 0), Margin = new Thickness(0, 0, 8, 8), BorderThickness = new Thickness(1), Cursor = Cursors.Hand, ToolTip = tip };
        sp.VerticalAlignment = VerticalAlignment.Center;
        b.SetResourceReference(Border.BackgroundProperty, "Card"); b.SetResourceReference(Border.BorderBrushProperty, "CardLine");
        b.MouseEnter += (_, _) => b.SetResourceReference(Border.BackgroundProperty, "CardHover"); b.MouseLeave += (_, _) => b.SetResourceReference(Border.BackgroundProperty, "Card");
        b.MouseLeftButtonUp += (_, _) => { if (!_busy) click(); };
        return b;
    }

    /// The bottom buttons: Apply is the filled accent one, Clear is outlined. 48 px tall for fingers.
    private void Button(Border b, string label, bool primary, Action click)
    {
        b.MinHeight = 48; b.CornerRadius = new CornerRadius(24); b.Padding = new Thickness(26, 0, 26, 0); b.BorderThickness = new Thickness(1); b.Cursor = Cursors.Hand; b.VerticalAlignment = VerticalAlignment.Center;
        if (primary) { b.SetResourceReference(Border.BackgroundProperty, "Accent"); b.SetResourceReference(Border.BorderBrushProperty, "Accent"); _applyText.FontSize = 15; _applyText.FontWeight = FontWeights.SemiBold; _applyText.VerticalAlignment = VerticalAlignment.Center; _applyText.SetResourceReference(TextBlock.ForegroundProperty, "OnAccent"); }
        else { b.SetResourceReference(Border.BackgroundProperty, "Card"); b.SetResourceReference(Border.BorderBrushProperty, "CardLine"); var t = Text(label, 14, "Fg"); t.VerticalAlignment = VerticalAlignment.Center; b.Child = t; }
        b.MouseLeftButtonUp += (_, _) => click();
    }

    [StructLayout(LayoutKind.Sequential)] private struct MEMORYSTATUSEX { public uint dwLength, dwMemoryLoad; public ulong ullTotalPhys, ullAvailPhys, ullTotalPageFile, ullAvailPageFile, ullTotalVirtual, ullAvailVirtual, ullAvailExtendedVirtual; }
    [DllImport("kernel32.dll")] private static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX m);
    [DllImport("dwmapi.dll")] internal static extern int DwmSetWindowAttribute(IntPtr h, int a, ref int v, int s);
}
