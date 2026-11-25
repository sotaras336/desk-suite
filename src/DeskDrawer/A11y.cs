using System.Windows;
using System.Windows.Automation;
using System.Windows.Automation.Peers;
using System.Windows.Automation.Provider;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Media;

namespace DeskDrawer;

/// Accessibility for the whole app (the same file is in DeskTune and DeskDrawer, only the namespace differs):
///  - a focus ring you can see on every theme (Windows' own dotted one is black and vanishes on the dark blues)
///  - icon-only buttons get a name a screen reader can say: their tooltip ("Back (Alt+Left)" says "Back", with Alt+Left as its shortcut), or their icon's name
///  - TapBorder / TapText: the app's own tappable cards, pills and links work with the keyboard (Tab, Enter, Space) and show up to screen readers
///    as buttons, switches and links
///  - high contrast: the theme's brushes become Windows' high contrast colours, live when it is switched
///  - text size: every window follows Settings > Accessibility > Text size (WPF on its own ignores it)
public static class A11y
{
    /// Once, at start-up (App.OnStartup).
    public static void Init()
    {
        EventManager.RegisterClassHandler(typeof(ButtonBase), FrameworkElement.LoadedEvent, new RoutedEventHandler((s, _) => NameIconButton((ButtonBase)s)));
        EventManager.RegisterClassHandler(typeof(Control), FrameworkElement.LoadedEvent, new RoutedEventHandler((s, _) => UseFocusRing((Control)s)));
        EventManager.RegisterClassHandler(typeof(Window), FrameworkElement.LoadedEvent, new RoutedEventHandler((s, _) => ScaleText((Window)s)));
        EventManager.RegisterClassHandler(typeof(ContextMenu), ContextMenu.OpenedEvent, new RoutedEventHandler((s, _) => ScaleText((FrameworkElement)s)));
        EventManager.RegisterClassHandler(typeof(ToolTip), ToolTip.OpenedEvent, new RoutedEventHandler((s, _) => ScaleText((FrameworkElement)s)));
        SystemParameters.StaticPropertyChanged += (_, e) =>
        {
            if (e.PropertyName != nameof(SystemParameters.HighContrast)) return;
            Application.Current?.Dispatcher.BeginInvoke(() => Theme.Apply());   // Theme.Apply ends with HighContrast(), which wins while it is on
        };
    }

    // ------------------------------------------------------------ focus ring

    private static Style? _ring;
    /// Two pixels in the text colour around whatever has the keyboard: white on the dark themes, near black on White, the window text colour in high contrast.
    public static Style FocusRing => _ring ??= MakeRing();
    private static Style MakeRing()
    {
        var b = new FrameworkElementFactory(typeof(Border));
        b.SetValue(Border.BorderThicknessProperty, new Thickness(2)); b.SetValue(Border.CornerRadiusProperty, new CornerRadius(6));
        b.SetValue(FrameworkElement.MarginProperty, new Thickness(-3)); b.SetValue(UIElement.SnapsToDevicePixelsProperty, true);
        b.SetResourceReference(Border.BorderBrushProperty, "Fg");
        var s = new Style(typeof(Control)); s.Setters.Add(new Setter(Control.TemplateProperty, new ControlTemplate(typeof(Control)) { VisualTree = b })); s.Seal(); return s;
    }
    private static void UseFocusRing(Control c)
    {
        if (c is MenuItem || c is TextBoxBase || c is ScrollViewer || c is ItemsControl and not TreeViewItem and not ComboBox) return;   // they show focus their own way (a highlight, the caret)
        if (c.FocusVisualStyle == null) return;   // turned off on purpose
        var src = DependencyPropertyHelper.GetValueSource(c, FrameworkElement.FocusVisualStyleProperty).BaseValueSource;
        if (src is BaseValueSource.Default or BaseValueSource.DefaultStyle or BaseValueSource.DefaultStyleTrigger) c.FocusVisualStyle = FocusRing;   // only Windows' default: a style of ours that sets one wins
    }

    // ------------------------------------------------------------ names

    /// Segoe Fluent Icons / MDL2 code points the apps use on icon-only buttons.
    public static string? GlyphName(char c) => c switch
    {
        '' => "Menu", '' => "Add", '' => "Close", '' => "More options", '' => "Settings", '' => "Search", '' => "Forward",
        '' => "Back", '' => "Refresh", '' => "Up", '' => "Delete", '' => "Close", '' => "Minimise", '' => "Maximise",
        '' => "Restore", '' => "Expand", '' => "Collapse", '' => "Previous", '' => "Next", '' => "Done", '' => "Pin",
        '' => "Unpin", '' => "Folder", '' => "Open folder", '' => "Open file", '' => "Information", '' => "Open in new window",
        '' => "Copy", '' => "Paste", '' => "Cut", '' => "Rename", '' => "View", '' => "Sort", '' => "Filter", '' => "Open with",
        '' => "New folder", '' => "New file", '' => "Favourite", '' => "Preview", '' => "Quick look", '' => "Colour",
        _ => null,
    };
    private static bool IsGlyph(char c) => c is >= '' and <= '';
    private static bool GlyphOnly(string? s) => !string.IsNullOrWhiteSpace(s) && s.All(c => IsGlyph(c) || char.IsWhiteSpace(c));
    private static string? GlyphText(object? content) => content switch { string s => s, TextBlock t => t.Text, _ => null };

    /// "Back (Alt+Left)" -> ("Back", "Alt+Left"); a tooltip that isn't text gives nothing.
    public static (string Name, string? Keys) SplitTip(object? tip)
    {
        if (tip is ToolTip tt) tip = tt.Content;
        if (tip is not string s || s.Length == 0) return ("", null);
        int open = s.LastIndexOf(" ("); if (open > 0 && s.EndsWith(')')) { var k = s[(open + 2)..^1]; if (k.Contains('+') || k.StartsWith('F') || k is "Esc" or "Space" or "Enter" or "Delete") return (s[..open], k); }
        return (s, null);
    }

    private sealed class TipToName : IValueConverter
    {
        public static readonly TipToName Instance = new();
        public object Convert(object? value, Type t, object? p, System.Globalization.CultureInfo c)
        {
            var n = SplitTip(value).Name; if (n.Length > 0) return n;
            return p is ButtonBase b && GlyphText(b.Content) is { Length: > 0 } g ? GlyphName(g.Trim()[0]) ?? "" : "";
        }
        public object ConvertBack(object? v, Type t, object? p, System.Globalization.CultureInfo c) => Binding.DoNothing;
    }

    /// A button that only shows an icon is read out as a private-use character: name it from its tooltip (kept in step when the tooltip changes), else its icon.
    private static void NameIconButton(ButtonBase b)
    {
        if (b.ReadLocalValue(AutomationProperties.NameProperty) != DependencyProperty.UnsetValue) return;
        if (!GlyphOnly(GlyphText(b.Content)) && b.Content != null) return;   // it has words: WPF reads those
        BindingOperations.SetBinding(b, AutomationProperties.NameProperty, new Binding(nameof(b.ToolTip)) { RelativeSource = RelativeSource.Self, Converter = TipToName.Instance, ConverterParameter = b });
        if (SplitTip(b.ToolTip).Keys is { } keys && b.ReadLocalValue(AutomationProperties.AcceleratorKeyProperty) == DependencyProperty.UnsetValue) AutomationProperties.SetAcceleratorKey(b, keys);
    }

    /// The words in an element, without the icon-font characters: what a sighted user reads on it.
    public static string TextOf(DependencyObject d)
    {
        var parts = new List<string>();
        void Walk(DependencyObject o)
        {
            if (o is UIElement u && u.Visibility != Visibility.Visible) return;
            if (o is TextBlock t) { var s = new string(t.Text.Where(c => !IsGlyph(c)).ToArray()).Trim(); if (s.Length > 0) parts.Add(s); return; }
            if (o is ContentPresenter { Content: string cs }) { var s = new string(cs.Where(c => !IsGlyph(c)).ToArray()).Trim(); if (s.Length > 0) parts.Add(s); }
            int n = o is Visual or System.Windows.Media.Media3D.Visual3D ? VisualTreeHelper.GetChildrenCount(o) : 0;
            if (n > 0) { for (int i = 0; i < n; i++) Walk(VisualTreeHelper.GetChild(o, i)); return; }
            foreach (var c in LogicalTreeHelper.GetChildren(o)) if (c is DependencyObject cd) Walk(cd);   // not laid out yet: the logical children
        }
        Walk(d);
        return string.Join(", ", parts.Take(3));
    }

    // ------------------------------------------------------------ tappable cards, pills and links

    /// A card or pill becomes a button: Tab reaches it, Enter or Space presses it, a screen reader reads its name (default: its words) and can press it.
    public static T Button<T>(T el, string? name, Action act, string? help = null) where T : FrameworkElement, ITap
    {
        el.Activate = act; Describe(el, name, help); return el;
    }
    /// A row that switches something: a screen reader hears "on" or "off" and can flip it.
    public static T Switch<T>(T el, string? name, Func<bool> isOn, Action? act, string? help = null) where T : FrameworkElement, ITap
    {
        el.IsOn = isOn; el.Activate = act; Describe(el, name, help); return el;
    }
    private static void Describe(FrameworkElement el, string? name, string? help)
    {
        if (!string.IsNullOrEmpty(name)) AutomationProperties.SetName(el, name);
        if (!string.IsNullOrEmpty(help)) AutomationProperties.SetHelpText(el, help);
        else if (SplitTip(el.ToolTip).Name is { Length: > 0 } t && t != name) AutomationProperties.SetHelpText(el, t);
    }

    /// Enter or Space on a focused tappable element. Shared by TapBorder and TapText.
    internal static void OnKey(FrameworkElement el, ITap t, KeyEventArgs e)
    {
        if (e.Handled || e.Key is not (Key.Enter or Key.Space) || Keyboard.Modifiers != ModifierKeys.None) return;
        if (t.Activate == null || !el.IsEnabled || !el.IsHitTestVisible) return;
        Press(el, t); e.Handled = true;
    }
    internal static void Press(FrameworkElement el, ITap t)
    {
        bool? was = t.IsOn?.Invoke();
        t.Activate?.Invoke();
        if (was is { } w && t.IsOn?.Invoke() is { } now && now != w && UIElementAutomationPeer.FromElement(el) is { } peer)
            peer.RaisePropertyChangedEvent(TogglePatternIdentifiers.ToggleStateProperty, w ? ToggleState.On : ToggleState.Off, now ? ToggleState.On : ToggleState.Off);
    }

    /// The keys of a custom slider: arrows a step (Ctrl: four), Page Up / Down four steps, Home / End the ends. vertical: Down is "more" (a top-to-bottom bar).
    public static bool Step(KeyEventArgs e, double v, double min, double max, double step, out double n, bool vertical = false)
    {
        n = v; if (e.Handled) return false;
        double s = Keyboard.Modifiers.HasFlag(ModifierKeys.Control) ? step * 4 : step, up = vertical ? -1 : 1;
        switch (e.Key)
        {
            case System.Windows.Input.Key.Right: n += s; break;
            case System.Windows.Input.Key.Left: n -= s; break;
            case System.Windows.Input.Key.Up: n += s * up; break;
            case System.Windows.Input.Key.Down: n -= s * up; break;
            case System.Windows.Input.Key.PageUp: n += step * 4 * up; break;
            case System.Windows.Input.Key.PageDown: n -= step * 4 * up; break;
            case System.Windows.Input.Key.Home: n = min; break;
            case System.Windows.Input.Key.End: n = max; break;
            default: return false;
        }
        n = Math.Clamp(n, min, max); e.Handled = true; return true;
    }

    // ------------------------------------------------------------ high contrast

    /// Called at the end of Theme.Apply: while high contrast is on, every theme brush becomes the matching system colour.
    /// Backgrounds -> Window, text -> WindowText, accents -> Highlight (with HighlightText on them), lines -> WindowText so cards and selections keep an edge.
    public static void HighContrast(ResourceDictionary r, bool force = false)
    {
        if (!SystemParameters.HighContrast && !force) return;
        Brush win = SystemColors.WindowBrush, text = SystemColors.WindowTextBrush, hi = SystemColors.HighlightBrush, hiText = SystemColors.HighlightTextBrush;
        foreach (var k in new[] { "WinBg", "BarBg", "TabBg", "StripBg", "PaneBg", "MenuBg", "Card", "CardHover" }) r[k] = win;
        foreach (var k in new[] { "Fg", "FgDim", "FgFaint", "BarLine", "CardLine", "Thumb", "Error" }) r[k] = text;
        r["Accent"] = hi; r["OnAccent"] = hiText; r["Select"] = win; r["SelectLine"] = hi; r["ThumbHover"] = hi;
    }

    // ------------------------------------------------------------ self-test

    /// Part of --selftest: the pieces a screen reader and the keyboard depend on, without a window on screen.
    public delegate void CheckFn(string name, bool ok, string extra = "");
    public static void SelfTest(CheckFn check)
    {
        int pressed = 0; var pill = Button(new TapBorder { Child = new TextBlock { Text = "\uE72C  Rescan" } }, null, () => pressed++);
        var pp = UIElementAutomationPeer.CreatePeerForElement(pill);
        string pn = pp.GetName();
        check("accessibility: a pill is a button named by its words, not its icon", pn == "Rescan" && pp.GetAutomationControlType() == AutomationControlType.Button && pp.GetPattern(PatternInterface.Invoke) != null, $"\"{pn}\"");
        bool on = false; var row = Switch(new TapBorder(), "Telemetry", () => on, () => on = !on, "You lose: nothing");
        var rp = UIElementAutomationPeer.CreatePeerForElement(row);
        var tg = rp.GetPattern(PatternInterface.Toggle) as IToggleProvider; tg?.Toggle();
        check("accessibility: a switch row is a check box a screen reader can flip", tg != null && on && tg.ToggleState == ToggleState.On && rp.GetAutomationControlType() == AutomationControlType.CheckBox && rp.GetHelpText() == "You lose: nothing" && rp.GetChildren() == null);
        var locked = Switch(new TapBorder(), "Locked", () => false, null);
        check("accessibility: a row that can't change says so", !UIElementAutomationPeer.CreatePeerForElement(locked).IsEnabled());
        var b = new System.Windows.Controls.Button { Content = "\uE72B", ToolTip = "Back (Alt+Left)" }; NameIconButton(b);
        var ib = new System.Windows.Controls.Button { Content = "\uE8BB" }; NameIconButton(ib);
        var wb = new System.Windows.Controls.Button { Content = "Cancel", ToolTip = "Esc" }; NameIconButton(wb);
        check("accessibility: icon buttons are named from their tooltip or icon, worded ones keep their words",
            AutomationProperties.GetName(b) == "Back" && AutomationProperties.GetAcceleratorKey(b) == "Alt+Left" && AutomationProperties.GetName(ib) == "Close" && AutomationProperties.GetName(wb) == "",
            $"\"{AutomationProperties.GetName(b)}\" \"{AutomationProperties.GetAcceleratorKey(b)}\" \"{AutomationProperties.GetName(ib)}\"");
        var rd = new ResourceDictionary(); HighContrast(rd, force: true); static Color? C(object o) => (o as SolidColorBrush)?.Color;
        check("accessibility: high contrast uses the system's window, text and highlight colours",
            C(rd["WinBg"]) == SystemColors.WindowColor && C(rd["Fg"]) == SystemColors.WindowTextColor && C(rd["Accent"]) == SystemColors.HighlightColor && C(rd["OnAccent"]) == SystemColors.HighlightTextColor);
        check("accessibility: a focus ring style to show the keyboard", FocusRing.IsSealed && FocusRing.Setters.Count == 1);
        check("accessibility: text size read", TextScale is >= 1 and <= 2.25, TextScale.ToString("0.00"));
    }

    // ------------------------------------------------------------ text size

    private static double? _scale;
    /// Settings > Accessibility > Text size, 1 to 2.25. DESK_TEXT_SCALE overrides it (tests, pictures).
    public static double TextScale
    {
        get
        {
            if (_scale is { } s) return s;
            double v = 100;
            if (double.TryParse(Environment.GetEnvironmentVariable("DESK_TEXT_SCALE"), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var env)) v = env;
            else if (Microsoft.Win32.Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Accessibility")?.GetValue("TextScaleFactor") is int f) v = f;
            return (_scale = Math.Clamp(v, 100, 225) / 100).Value;
        }
    }
    /// WPF ignores Windows' text size: a window (or a menu, a tooltip) is drawn that much larger instead, laid out again at the bigger size.
    private static void ScaleText(FrameworkElement fe)
    {
        double s = TextScale; if (s < 1.01) return;
        var target = fe is Window w ? w.Content as FrameworkElement : fe; if (target == null) return;
        if (target.LayoutTransform is ScaleTransform { ScaleX: > 1.0 }) return;   // done already (or the app zooms it itself)
        if (!target.LayoutTransform.Value.IsIdentity) return;
        target.LayoutTransform = new ScaleTransform(s, s);
    }
}

/// What makes an element tappable from the keyboard and to screen readers (TapBorder, TapText).
public interface ITap { Action? Activate { get; set; } Func<bool>? IsOn { get; set; } }

/// A Border that the keyboard and screen readers can press like a button (A11y.Button) or flip like a switch (A11y.Switch).
public class TapBorder : Border, ITap
{
    public Action? Activate { get; set; }
    public Func<bool>? IsOn { get; set; }
    public TapBorder() { Focusable = true; FocusVisualStyle = A11y.FocusRing; }
    protected override void OnKeyDown(KeyEventArgs e) { A11y.OnKey(this, this, e); base.OnKeyDown(e); }
    protected override AutomationPeer OnCreateAutomationPeer() => new TapPeer(this, this, false);
}

/// A TextBlock used as a link ("Undo all", "Settings"): the keyboard and screen readers can follow it.
public class TapText : TextBlock, ITap
{
    public Action? Activate { get; set; }
    public Func<bool>? IsOn { get; set; }
    public TapText() { Focusable = true; FocusVisualStyle = A11y.FocusRing; }
    protected override void OnKeyDown(KeyEventArgs e) { A11y.OnKey(this, this, e); base.OnKeyDown(e); }
    protected override AutomationPeer OnCreateAutomationPeer() => new TapPeer(this, this, true);
}

internal sealed class TapPeer : FrameworkElementAutomationPeer, IInvokeProvider, IToggleProvider
{
    private readonly ITap _t; private readonly bool _link; private readonly FrameworkElement _el;
    public TapPeer(FrameworkElement owner, ITap t, bool link) : base(owner) { _t = t; _link = link; _el = owner; }
    protected override AutomationControlType GetAutomationControlTypeCore() => _t.IsOn != null ? AutomationControlType.CheckBox : _link ? AutomationControlType.Hyperlink : AutomationControlType.Button;
    protected override string GetClassNameCore() => Owner.GetType().Name;
    protected override string GetNameCore() { var n = base.GetNameCore(); return string.IsNullOrEmpty(n) ? A11y.TextOf(Owner) : n; }
    protected override List<AutomationPeer>? GetChildrenCore() => null;   // its words are its name; the switch drawn inside is only a picture of the state
    protected override bool IsEnabledCore() => Owner.IsEnabled && Owner.IsHitTestVisible && _t.Activate != null;
    protected override bool IsKeyboardFocusableCore() => Owner.Focusable && Owner.IsVisible;
    protected override bool IsControlElementCore() => true;
    protected override bool IsContentElementCore() => true;
    public override object? GetPattern(PatternInterface p) => p switch
    {
        PatternInterface.Invoke when _t.IsOn == null => this,
        PatternInterface.Toggle when _t.IsOn != null => this,
        _ => base.GetPattern(p),
    };
    void IInvokeProvider.Invoke() { if (!IsEnabled()) throw new ElementNotEnabledException(); _el.Dispatcher.BeginInvoke(() => A11y.Press(_el, _t)); }
    ToggleState IToggleProvider.ToggleState => _t.IsOn?.Invoke() == true ? ToggleState.On : ToggleState.Off;
    void IToggleProvider.Toggle() { if (!IsEnabled()) throw new ElementNotEnabledException(); A11y.Press(_el, _t); }
}

/// A screen reader's view of a custom slider (the colour pickers): a value between a minimum and a maximum it can read and set.
public sealed class RangePeer : FrameworkElementAutomationPeer, IRangeValueProvider
{
    private readonly Func<double> _get; private readonly Action<double> _set; private readonly double _min, _max, _small, _large;
    public RangePeer(FrameworkElement owner, Func<double> get, Action<double> set, double min, double max, double small, double large) : base(owner) { _get = get; _set = set; _min = min; _max = max; _small = small; _large = large; }
    protected override AutomationControlType GetAutomationControlTypeCore() => AutomationControlType.Slider;
    protected override string GetClassNameCore() => Owner.GetType().Name;
    protected override List<AutomationPeer>? GetChildrenCore() => null;
    protected override bool IsKeyboardFocusableCore() => Owner.Focusable;
    public override object? GetPattern(PatternInterface p) => p == PatternInterface.RangeValue ? this : base.GetPattern(p);
    public double Value => _get();
    public bool IsReadOnly => !Owner.IsEnabled;
    public double Maximum => _max; public double Minimum => _min; public double LargeChange => _large; public double SmallChange => _small;
    public void SetValue(double v) { if (v < _min || v > _max) throw new ArgumentOutOfRangeException(nameof(v)); _set(v); }
    public void Changed(double old) => RaisePropertyChangedEvent(RangeValuePatternIdentifiers.ValueProperty, old, _get());
}
