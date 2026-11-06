using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;

namespace DeskMode;

/// Everything DeskMode does is decided here: which monitor counts, what "Go max" and "Go easy" set on each screen, what happens by itself, games, startup.
/// Each control applies the moment you touch it (nothing to save); the window is built in code because most of its choices come from what is plugged in right now.
public sealed class SettingsPage : ScrollViewer
{
    private readonly Controller _c;
    private Settings S => _c.Settings;
    private readonly StackPanel _page = new() { Margin = new Thickness(0, 0, 10, 6) };
    private readonly Action _back;
    private int _group;
    private static readonly int[] CommonRates = { 30, 48, 50, 60, 75, 90, 100, 120, 144, 165, 180, 240 };

    public SettingsPage(Controller controller, Action back)
    {
        _c = controller; _back = back;
        VerticalScrollBarVisibility = ScrollBarVisibility.Auto; HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled; Content = _page;
        Wheel.Attach(this);
        PanningMode = PanningMode.VerticalOnly; PanningDeceleration = 0.004;   // a flick coasts a hand's width, not across the whole page (WPF's default 0.001 is far too slippery)
        Build();
    }

    // ---------- small builders ----------

    private TextBlock Heading(string text) => Add(new TextBlock { Text = text, Style = (Style)FindResource("SectionTitle"), Margin = new Thickness(4, 16, 0, 6) });

    private T Add<T>(T e) where T : UIElement { _page.Children.Add(e); return e; }

    private StackPanel Card()
    {
        var sp = new StackPanel();
        var b = new Border { Style = (Style)FindResource("Card"), Child = sp }; _page.Children.Add(b); return sp;
    }

    private static TextBlock Hint(string text) => new() { Text = text, FontSize = 12, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(2, 6, 0, 0), Opacity = 0.8 };

    private TextBlock Dim(string text, double size = 12)
    {
        var t = new TextBlock { Text = text, FontSize = size, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(2, 0, 0, 8) };
        t.SetResourceReference(TextBlock.ForegroundProperty, "FgDim"); return t;
    }

    private CheckBox Switch(StackPanel into, string text, bool on, Action<bool> set, string? tip = null, bool first = false)
    {
        var cb = new CheckBox { Style = (Style)FindResource("Switch"), IsChecked = on, Margin = new Thickness(2, first ? 0 : 10, 0, 0), ToolTip = tip, Content = new TextBlock { Text = text, TextWrapping = TextWrapping.Wrap, FontSize = 12.5 } };
        cb.Click += (_, _) => { set(cb.IsChecked == true); Changed(); };
        into.Children.Add(cb); return cb;
    }

    private Border Segments<T>(IEnumerable<(string Label, T Value)> options, T current, Action<T> pick, int columns = 0)
    {
        var items = options.ToList(); if (columns == 0 && items.Count > 6) columns = 5;   // a long row of rates wraps to a second line instead of squeezing
        var grid = new UniformGrid { Rows = columns > 0 ? 0 : 1, Columns = columns };
        string group = "g" + _group++;
        foreach (var (label, value) in items)
        {
            var rb = new RadioButton { Style = (Style)FindResource("Segment"), GroupName = group, Content = label, IsChecked = EqualityComparer<T>.Default.Equals(value, current) };
            var v = value; rb.Checked += (_, _) => { pick(v); Changed(); };
            grid.Children.Add(rb);
        }
        return new Border { Style = (Style)FindResource("SegmentHost"), Child = grid };
    }

    /// A label above its control, so a long row of refresh rates has the whole width.
    private StackPanel Row(string label, UIElement control, double labelWidth = 0, double top = 0)
    {
        var sp = new StackPanel { Margin = new Thickness(0, top, 0, 0) };
        var t = new TextBlock { Text = label, FontSize = 12, Margin = new Thickness(2, 0, 0, 4), TextTrimming = TextTrimming.CharacterEllipsis };
        t.SetResourceReference(TextBlock.ForegroundProperty, "FgDim");
        sp.Children.Add(t); sp.Children.Add(control); return sp;
    }

    private Button ButtonRow(string text, Action click)
    {
        var b = new Button { Style = (Style)FindResource("Preset"), Content = new TextBlock { Text = text, HorizontalAlignment = HorizontalAlignment.Center, FontWeight = FontWeights.SemiBold }, Margin = new Thickness(0, 10, 0, 0) };
        b.Click += (_, _) => click(); return b;
    }

    private void Changed() => _c.SettingsChanged();

    private static string RateText(int hz) => hz == 0 ? "Max" : $"{hz}";

    // ---------- the page ----------

    private void Build()
    {
        _page.Children.Clear(); _group = 0;
        var displays = _c.Displays();
        var laptop = DisplayService.Internal(displays);
        var externals = DisplayService.Externals(displays);
        var monitor = DisplayService.External(displays, S.MonitorMatch);

        var head = new Grid(); head.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto }); head.ColumnDefinitions.Add(new ColumnDefinition());
        var back = new Button { Style = (Style)FindResource("IconButton"), Content = new TextBlock { Text = "\uE72B", FontFamily = (FontFamily)FindResource("IconFont"), FontSize = 15 }, ToolTip = "Back (Esc)", HorizontalAlignment = HorizontalAlignment.Left };
        back.Click += (_, _) => _back(); head.Children.Add(back);
        var title = new TextBlock { Text = "Settings", FontSize = 17, FontWeight = FontWeights.SemiBold, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(8, 0, 0, 0) }; Grid.SetColumn(title, 1); head.Children.Add(title);
        Add(head);
        Add(Dim("Everything here applies as soon as you change it.")).Margin = new Thickness(4, 8, 0, 0);

        // ---- monitor ----
        Heading("Monitor");
        var c = Card();
        c.Children.Add(Dim("DeskMode treats this monitor as the trigger: when it is connected (and the PC is plugged in) it can switch to Go max by itself."));
        var choices = new List<(string, string)> { ("Any external monitor", "") };
        foreach (var d in externals) choices.Add(($"{d.FriendlyName}  ·  {d.Width}×{d.Height}, {d.RefreshRate} Hz", d.HardwareId));
        if (S.MonitorMatch.Length > 0 && !externals.Any(d => d.HardwareId.StartsWith(S.MonitorMatch, StringComparison.OrdinalIgnoreCase)))
            choices.Add(($"{(S.MonitorName.Length > 0 ? S.MonitorName : S.MonitorMatch)}  (not connected now)", S.MonitorMatch));
        string current = choices.FirstOrDefault(x => x.Item2.Length > 0 && S.MonitorMatch.Length > 0 && x.Item2.StartsWith(S.MonitorMatch, StringComparison.OrdinalIgnoreCase)).Item2 ?? "";
        if (S.MonitorMatch.Length == 0) current = "";
        var list = Segments(choices, current, id =>
        {
            S.MonitorMatch = id; var chosen = externals.FirstOrDefault(d => d.HardwareId == id);
            if (chosen != null) S.MonitorName = chosen.FriendlyName;
        }, columns: 1);
        c.Children.Add(list);
        if (externals.Count == 0) c.Children.Add(Hint("No external monitor is connected right now. Plug one in and open Settings again to pick it."));

        // ---- go max ----
        Heading("“Go max”");
        c = Card();
        c.Children.Add(Dim("What the Go max button (and Ctrl+Alt+M) sets. “Max” uses the best refresh rate each screen offers."));
        if (laptop != null) c.Children.Add(Row("Built-in screen", Segments(RateChoices(laptop, S.LaptopMaxHz), S.LaptopMaxHz, v => S.LaptopMaxHz = v), top: 0));
        c.Children.Add(Row(monitor?.FriendlyName ?? (S.MonitorName.Length > 0 ? S.MonitorName : "Monitor"), Segments(RateChoices(monitor, S.MonitorMaxHz), S.MonitorMaxHz, v => S.MonitorMaxHz = v), top: laptop != null ? 8 : 0));
        if (PowerService.IsSupported) c.Children.Add(Row("Power mode", Segments(PowerChoices(S.MaxPowerMode), S.MaxPowerMode, v => S.MaxPowerMode = v), top: 8));
        Switch(c, "Turn Energy saver off", S.MaxEnergySaverOff, v => S.MaxEnergySaverOff = v);

        // ---- go easy ----
        Heading("“Go easy”");
        c = Card();
        c.Children.Add(Dim("What the Go easy button (and Ctrl+Alt+B) sets: a gentle refresh rate on every screen, for battery life and quiet fans. The same rate is used for the built-in screen by “monitor first”."));
        c.Children.Add(Row("Refresh rate", Segments(EasyChoices(displays), S.EasyHz, v => S.EasyHz = v)));
        if (PowerService.IsSupported) c.Children.Add(Row("Power mode", Segments(PowerChoices(S.EasyPowerMode), S.EasyPowerMode, v => S.EasyPowerMode = v), top: 8));
        Switch(c, "Turn Energy saver on", S.EasyEnergySaverOn, v => S.EasyEnergySaverOn = v);

        // ---- automatic ----
        Heading("By itself");
        c = Card();
        Switch(c, "Go max when the monitor is connected and the PC is plugged in", S.AutoMaxWhenPlugged, v => S.AutoMaxWhenPlugged = v, first: true);
        Switch(c, "Go back to how things were when the PC runs on battery or the monitor is unplugged", S.RestoreOnBattery, v => S.RestoreOnBattery = v, "Refresh rates, Energy saver and the power mode are put back to what they were before DeskMode changed them");
        Switch(c, "Open the panel when switching to battery", S.ShowPanelOnBattery, v => S.ShowPanelOnBattery = v);
        Switch(c, "Show notifications", S.Notifications, v => S.Notifications = v);

        // ---- brightness ----
        Heading("Brightness");
        c = Card();
        Switch(c, "The built-in screen's brightness keys also change the monitor (keeping the gap between them)", S.LinkBrightness, v => S.LinkBrightness = v, first: true);
        c.Children.Add(ButtonRow("Reset the brightness gap", () => { S.BrightnessOffset = 0; Changed(); }));

        // ---- games ----
        Heading("Games");
        c = Card();
        Switch(c, "Leave screen modes alone while a full-screen game is up", S.GameLock, v => S.GameLock = v, "Every refresh-rate or power-mode change is one more mode switch, which can blank the screen for a moment", first: true);
        Switch(c, "Make the games on my list borderless automatically", S.AutoBorderless, v => S.AutoBorderless = v, "A borderless window never switches the monitor's mode: no black screens and alt-tab is instant");
        Switch(c, "Keep the taskbar visible over borderless games", S.BorderlessKeepBar, v => S.BorderlessKeepBar = v);
        c.Children.Add(new TextBlock { Text = "My games: a window class or program name per line (GLFW30 is Minecraft and other GLFW games; javaw.exe works too)", FontSize = 12, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(2, 14, 0, 6) });
        var box = new TextBox { AcceptsReturn = true, TextWrapping = TextWrapping.Wrap, MinHeight = 64, MaxHeight = 140, Padding = new Thickness(8, 6, 8, 6), Text = string.Join(Environment.NewLine, S.BorderlessMatch), VerticalScrollBarVisibility = ScrollBarVisibility.Auto, BorderThickness = new Thickness(1) };
        box.SetResourceReference(Control.BackgroundProperty, "Control"); box.SetResourceReference(Control.ForegroundProperty, "Fg"); box.SetResourceReference(Control.BorderBrushProperty, "CardBorder"); box.SetResourceReference(TextBoxBase.CaretBrushProperty, "Fg");
        box.LostFocus += (_, _) => { S.BorderlessMatch = box.Text.Split('\n', '\r', ',', ';').Select(x => x.Trim()).Where(x => x.Length > 0).Distinct().ToList(); Changed(); };
        c.Children.Add(box);

        // ---- general ----
        Heading("General");
        c = Card();
        Switch(c, "Start DeskMode with Windows", Settings.StartWithWindows, v => Settings.StartWithWindows = v, first: true);
        Switch(c, "Keyboard shortcuts: Ctrl+Alt+D panel · M max · B easy · F borderless", S.Hotkeys, v => { S.Hotkeys = v; ((App)Application.Current).Panel?.SetHotkeys(v); }, "Switch these off if another program already uses them");
        Switch(c, "Check for updates once a day", Updater.Enabled, v => Updater.Enabled = v, "Reads the list of releases on GitHub; nothing else is sent. A new version is only installed when you press Update now.");
        var upd = Dim(Updater.Status.Length > 0 ? Updater.Status : "Not checked yet."); upd.Margin = new Thickness(2, 6, 0, 0); c.Children.Add(upd);
        Action refresh = () => { upd.Text = Updater.Status.Length > 0 ? Updater.Status : "Not checked yet."; };
        Action onChanged = () => Dispatcher.BeginInvoke(refresh); Updater.Changed += onChanged; Unloaded += (_, _) => Updater.Changed -= onChanged;
        c.Children.Add(ButtonRow("Check for updates now", () => _ = Updater.CheckAsync()));
        if (Updater.Newer) c.Children.Add(ButtonRow("Update now", async () => { if (await Updater.UpdateNowAsync()) Application.Current.Shutdown(); }));
        if (PowerService.IsSupported)
        {
            c.Children.Add(Row("Power modes my PC offers", Segments(new List<(string, int)> { ("Automatic", 0), ("Recommended · Better · Best", 1), ("Efficient · Balanced · Best", 2) }, S.PowerModeStyle, v => { S.PowerModeStyle = v; PowerService.StylePref = v; Build(); }, columns: 1), top: 12));
            c.Children.Add(Hint("Windows shows three power modes, and which three depends on the PC. Surface PCs: Recommended, Better, Best. Most others: Efficient, Balanced, Best."));
        }
        var reset = ButtonRow("Reset every setting to the defaults", () => { });
        bool armed = false;
        reset.Click += (_, _) =>
        {
            if (!armed) { armed = true; ((TextBlock)reset.Content).Text = "Click again to reset everything"; var t = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(4) }; t.Tick += (_, _) => { t.Stop(); armed = false; ((TextBlock)reset.Content).Text = "Reset every setting to the defaults"; }; t.Start(); return; }
            S.ResetToDefaults(); Changed(); Build();
        };
        c.Children.Add(reset);
        c.Children.Add(ButtonRow("Open the settings folder", () => { try { Process.Start(new ProcessStartInfo("explorer.exe", $"\"{Path.GetDirectoryName(SettingsPath())}\"") { UseShellExecute = true }); } catch { } }));
        Add(new TextBlock { Text = "DeskMode  ·  part of Desk", FontSize = 11.5, Opacity = 0.6, Margin = new Thickness(4, 16, 0, 0) });
    }

    private static string SettingsPath() => Path.Combine(Environment.GetEnvironmentVariable("DESKMODE_DATA") is { Length: > 0 } d ? d : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DeskMode"), "settings.json");

    /// "Max" plus the rates this screen offers (or the usual ones when it isn't connected right now), plus whatever is chosen already.
    private static List<(string, int)> RateChoices(DisplayInfo? d, int chosen)
    {
        var rates = d != null && d.AvailableRates.Count > 0 ? d.AvailableRates.Where(r => CommonRates.Contains(r)).ToList() : CommonRates.Where(r => r >= 60).ToList();
        if (chosen > 0 && !rates.Contains(chosen)) { rates.Add(chosen); rates.Sort(); }
        var l = new List<(string, int)> { ("Max", 0) };
        l.AddRange(rates.Select(r => (RateText(r), r))); return l;
    }

    /// Rates every connected screen can do, so one number works everywhere; the usual low ones when nothing is known.
    private List<(string, int)> EasyChoices(List<DisplayInfo> displays)
    {
        IEnumerable<int> rates = displays.Count > 0 ? displays.Select(d => d.AvailableRates.Where(r => CommonRates.Contains(r))).Aggregate((x, y) => x.Intersect(y)) : CommonRates;
        var list = rates.Where(r => r <= 120).Distinct().ToList();
        if (list.Count == 0) list.Add(60);
        if (!list.Contains(S.EasyHz)) list.Add(S.EasyHz);
        return list.OrderBy(r => r).Select(r => ($"{r}", r)).ToList();
    }

    private List<(string, PowerMode)> PowerChoices(PowerMode chosen) => PowerService.Choices(chosen);
}
