using System.Windows;
using System.Windows.Automation.Peers;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Shapes;

namespace DeskTune;

/// DeskTune's colour (Desk 4.0): the ready-made colours, then any colour on the pad (colour across, vividness down), the light bar and the fade, with a
/// live preview; Cancel puts the old one back. "Same colour for every app" on (Desk settings > Colours): the pick is every Desk app's colour. Off: DeskTune's own.
/// The pad, light bar and fade bar are DeskDrawer's colour picker (MainWindow.Colour.cs), so the two apps pick colours the same way.
public static class ColourWindow
{
    public static void Show(Window owner)
    {
        string before = Theme.FullId; bool ok = false;
        string? baseId = Theme.Current.Kind == 0 && !Theme.Current.Id.StartsWith("custom:") ? Theme.Current.Id : null; bool moved = false;
        var cur = Theme.Current;
        double hue = cur.Kind != 0 ? 300 : cur.Hue >= 0 ? cur.Hue : 205, t0 = Theme.LightStart(cur);
        var win = new Window { Title = "DeskTune colour", Owner = owner, Width = 460, SizeToContent = SizeToContent.Height, WindowStartupLocation = WindowStartupLocation.CenterOwner, ResizeMode = ResizeMode.NoResize, ShowInTaskbar = false };
        win.SetResourceReference(Window.BackgroundProperty, "MenuBg");
        win.SourceInitialized += (_, _) => { int dark = Theme.IsLight ? 0 : 1; MainWindow.DwmSetWindowAttribute(new WindowInteropHelper(win).Handle, 20, ref dark, 4); };
        void Preview(string id) { Theme.Load(id); Theme.Apply(); }
        TextBlock T(string s, double size, string fg, FontWeight? w = null, Thickness? m = null) { var t = new TextBlock { Text = s, FontSize = size, FontWeight = w ?? FontWeights.Normal, TextWrapping = TextWrapping.Wrap, Margin = m ?? new Thickness(0) }; t.SetResourceReference(TextBlock.ForegroundProperty, fg); return t; }
        Color Base() => Theme.Col(0x1257B8);

        var sp = new StackPanel { Margin = new Thickness(22, 18, 22, 18) };
        sp.Children.Add(T("DeskTune colour", 16, "Fg", FontWeights.SemiBold, new Thickness(0, 0, 0, 4)));
        sp.Children.Add(T(SharedTheme.Separate ? "DeskTune's own colour. The other Desk apps keep theirs." : "Every Desk app's colour: one colour for all is on in Desk settings > Colours.", 12, "FgDim", null, new Thickness(0, 0, 0, 12)));
        var preview = new Border { Height = 48, CornerRadius = new CornerRadius(12), Margin = new Thickness(0, 0, 0, 12) }; sp.Children.Add(preview);

        // the ready-made colours, as dots
        var dots = new WrapPanel { Margin = new Thickness(0, 0, 0, 12) }; sp.Children.Add(dots);
        foreach (var t in Theme.All)
        {
            var c = t.Kind == 1 ? Color.FromRgb(0x1A, 0x1A, 0x1A) : t.Kind == 2 ? Color.FromRgb(0xF2, 0xF2, 0xF2) : t.Kind == 3 ? Color.FromArgb(0x90, 0x9A, 0xA4, 0xB4) : Theme.ColFor(t, 0x1167C4);
            var dot = new Ellipse { Width = 30, Height = 30, Margin = new Thickness(0, 0, 8, 8), Fill = new SolidColorBrush(c), Stroke = Brushes.White, StrokeThickness = string.Equals(Theme.Current.Id, t.Id, StringComparison.OrdinalIgnoreCase) ? 3 : 0.6, Cursor = System.Windows.Input.Cursors.Hand, ToolTip = t.Name };
            string id = t.Id; void Pick() { ok = true; SharedTheme.Write(id); Preview(id); win.Close(); }
            dot.MouseLeftButtonUp += (_, _) => Pick();
            bool picked = string.Equals(Theme.Current.Id, t.Id, StringComparison.OrdinalIgnoreCase);
            dots.Children.Add(A11y.Button(new TapBorder { Child = dot, CornerRadius = new CornerRadius(15) }, t.Name + (picked ? ", in use" : ""), Pick));   // the keyboard and screen readers can pick it too
        }

        var card = new StackPanel(); sp.Children.Add(new Border { CornerRadius = new CornerRadius(14), Padding = new Thickness(16, 12, 16, 14), Background = new SolidColorBrush(Color.FromArgb(0x14, 255, 255, 255)), Child = card });
        Grid Head(string name, out TextBlock hint) { var g = new Grid { Margin = new Thickness(0, 0, 0, 4) }; g.Children.Add(T(name, 13, "Fg", FontWeights.SemiBold)); hint = T("", 11.5, "FgDim"); hint.HorizontalAlignment = HorizontalAlignment.Right; hint.VerticalAlignment = VerticalAlignment.Center; g.Children.Add(hint); return g; }
        card.Children.Add(Head("Any colour", out var hueHint)); hueHint.Text = "colour, vividness, light";
        var pad = new ColourPad(hue, cur.Sat * 100); var light = new LightBar(t0) { Margin = new Thickness(10, 0, 0, 0) }; Grid.SetColumn(light, 1);
        System.Windows.Automation.AutomationProperties.SetName(pad, "Colour"); System.Windows.Automation.AutomationProperties.SetHelpText(pad, "Left and right: the colour. Up and down: how vivid it is.");
        System.Windows.Automation.AutomationProperties.SetName(light, "Lightness"); System.Windows.Automation.AutomationProperties.SetHelpText(light, "Top: White. Bottom: Black.");
        card.Children.Add(new Grid { ColumnDefinitions = { new ColumnDefinition(), new ColumnDefinition { Width = GridLength.Auto } }, Children = { pad, light } });
        var fadeBox = new StackPanel { Margin = new Thickness(0, 12, 0, 0) }; card.Children.Add(fadeBox);
        fadeBox.Children.Add(Head("Fade", out var fadeHint));
        var fadeBar = new GradBar(100, Theme.Grad > 0 ? Theme.Grad : 0) { Sample = f => f < 1 ? Base() : Theme.Shift(Base(), f, 0.002) };
        System.Windows.Automation.AutomationProperties.SetName(fadeBar, "Fade");
        fadeBox.Children.Add(fadeBar);

        string Id() => Theme.ColourId(light.T, baseId, moved, pad.Hue, fadeBar.Value, pad.Sat);
        void Refresh()
        {
            double f = fadeBar.Value; int z = LightZone(light.T); bool plain = z != 0;
            fadeBox.Visibility = plain ? Visibility.Collapsed : Visibility.Visible;
            fadeHint.Text = f >= 1 ? (int)f + "% lighter on the right" : "off";
            preview.Background = new LinearGradientBrush(Base(), f >= 1 && !plain ? Theme.Shift(Base(), f, 0.002) : Base(), 0);
            fadeBar.Track = new LinearGradientBrush(Base(), Theme.Shift(Base(), 100, 0.002), 0);
            Color Surface(double val) => Theme.ColFor(new ThemeDef("", "", pad.Hue, false, 0, pad.Sat / 100, val), 0x1257B8);
            light.Tint(Theme.ColFor(new ThemeDef("", "", pad.Hue, false, 0, pad.Sat / 100, 1), 0x4CC2FF));
            pad.Surface = Surface(1); light.Surface = z == 2 ? Colors.White : z == 1 ? Color.FromRgb(10, 10, 10) : Surface(LightVal(light.T));
            pad.Place(); light.Place(); fadeBar.Place();
        }
        pad.Changed += () => { moved = true; Preview(Id()); Refresh(); }; light.Changed += () => { moved = true; Preview(Id()); Refresh(); };
        fadeBar.Changed += () => { Preview(Id()); Refresh(); };

        var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 16, 0, 0) };
        Border Btn(string text, bool primary, Action act)   // in the theme's colours, like DeskTune's own buttons
        {
            var t = T(text, 13, primary ? "OnAccent" : "Fg", primary ? FontWeights.SemiBold : FontWeights.Normal);
            var b = new TapBorder { Child = t, CornerRadius = new CornerRadius(10), Padding = new Thickness(16, 7, 16, 8), Margin = new Thickness(8, 0, 0, 0), Cursor = System.Windows.Input.Cursors.Hand };
            b.SetResourceReference(Border.BackgroundProperty, primary ? "Accent" : "Card"); b.MouseLeftButtonUp += (_, _) => act(); return A11y.Button(b, text, act);
        }
        buttons.Children.Add(Btn("Cancel", false, () => win.Close()));
        buttons.Children.Add(Btn("Use this colour", true, () => { ok = true; SharedTheme.Write(Id()); win.Close(); }));
        sp.Children.Add(buttons);
        win.Content = sp;
        win.Closed += (_, _) => { if (!ok) Preview(before); };
        Preview(Id()); Refresh();
        win.ShowDialog();
    }

    // what the light bar position means: under 0.10 the White theme, over 0.92 Black, 0.2-0.3 the colour as it is, darker below (as in DeskDrawer)
    static int LightZone(double t) => Theme.LightZone(t);
    static double LightVal(double t) => Theme.LightVal(t);

    sealed class GradBar : Grid
    {
        private readonly Border track = new() { Height = 20, CornerRadius = new CornerRadius(10), Margin = new Thickness(15, 0, 15, 0), VerticalAlignment = VerticalAlignment.Center, IsHitTestVisible = false };
        private readonly Ellipse dot = new() { Width = 30, Height = 30, Stroke = Brushes.White, StrokeThickness = 3, HorizontalAlignment = HorizontalAlignment.Left, IsHitTestVisible = false };
        private readonly double max; private double v;
        public Func<double, Color> Sample = _ => Colors.Gray;
        public event Action? Changed;
        public GradBar(double max, double value) { this.max = max; v = value; Height = 34; Background = Brushes.Transparent; Cursor = System.Windows.Input.Cursors.Hand; Focusable = true; FocusVisualStyle = A11y.FocusRing; Children.Add(track); Children.Add(dot); SizeChanged += (_, _) => Place(); }
        public Brush Track { set => track.Background = value; }
        public double Value { get => v; set { v = Math.Clamp(value, 0, max); Place(); } }
        public void Place() { dot.Margin = new Thickness(max > 0 ? v / max * Math.Max(0, ActualWidth - 30) : 0, 0, 0, 0); dot.Fill = new SolidColorBrush(Sample(v)); }
        private void From(System.Windows.Input.MouseEventArgs e) { double w = Math.Max(1, ActualWidth - 30); Value = (e.GetPosition(this).X - 15) / w * max; Changed?.Invoke(); }
        protected override void OnMouseLeftButtonDown(System.Windows.Input.MouseButtonEventArgs e) { CaptureMouse(); From(e); }
        protected override void OnMouseMove(System.Windows.Input.MouseEventArgs e) { if (IsMouseCaptured) From(e); }
        protected override void OnMouseLeftButtonUp(System.Windows.Input.MouseButtonEventArgs e) { if (IsMouseCaptured) { From(e); ReleaseMouseCapture(); } }
        // the keyboard: arrows move it (Ctrl: further), Home and End to the ends; a screen reader sees a slider (A11y.cs)
        protected override void OnKeyDown(System.Windows.Input.KeyEventArgs e) { if (A11y.Step(e, v, 0, max, 5, out var n)) { double old = v; Value = n; Changed?.Invoke(); (UIElementAutomationPeer.FromElement(this) as RangePeer)?.Changed(old); } base.OnKeyDown(e); }
        protected override AutomationPeer OnCreateAutomationPeer() => new RangePeer(this, () => v, x => { Value = x; Changed?.Invoke(); }, 0, max, 5, 20);
    }
    sealed class ColourPad : Grid
    {
        private readonly Ellipse dot = new() { Width = 22, Height = 22, Stroke = Brushes.White, StrokeThickness = 3, HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Top, IsHitTestVisible = false };
        public double Hue, Sat;
        public event Action? Changed;
        public ColourPad(double hue, double sat)
        {
            Hue = hue; Sat = sat; Height = 150; Background = Brushes.Transparent; Cursor = System.Windows.Input.Cursors.Cross; Focusable = true; FocusVisualStyle = A11y.FocusRing;
            var rb = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 0) };
            for (int h = 0; h <= 360; h += 20) rb.GradientStops.Add(new GradientStop(Theme.ColFor(new ThemeDef("", "", h % 360, false, 0), 0x4CC2FF), h / 360.0));
            var gc = Theme.ColFor(new ThemeDef("", "", 0, false, 0, 0, 1), 0x4CC2FF);
            Children.Add(new Border { CornerRadius = new CornerRadius(12), Margin = new Thickness(11), Background = rb, IsHitTestVisible = false });
            Children.Add(new Border { CornerRadius = new CornerRadius(12), Margin = new Thickness(11), Background = new LinearGradientBrush(Color.FromArgb(0, gc.R, gc.G, gc.B), gc, 90), IsHitTestVisible = false });
            Children.Add(dot); SizeChanged += (_, _) => Place();
        }
        public Color Surface { set => dot.Fill = new SolidColorBrush(value); }
        public void Place() => dot.Margin = new Thickness(Hue / 359 * Math.Max(0, ActualWidth - 22), (1 - Sat / 100) * Math.Max(0, ActualHeight - 22), 0, 0);
        private void From(System.Windows.Input.MouseEventArgs e)
        {
            var p = e.GetPosition(this); Hue = Math.Clamp((p.X - 11) / Math.Max(1, ActualWidth - 22), 0, 1) * 359; Sat = (1 - Math.Clamp((p.Y - 11) / Math.Max(1, ActualHeight - 22), 0, 1)) * 100;
            Place(); Changed?.Invoke();
        }
        protected override void OnKeyDown(System.Windows.Input.KeyEventArgs e)   // left / right: the colour, up / down: how vivid
        {
            double old = Hue;
            if (e.Key is System.Windows.Input.Key.Up or System.Windows.Input.Key.Down) { Sat = Math.Clamp(Sat + (e.Key == System.Windows.Input.Key.Up ? 5 : -5), 0, 100); Place(); Changed?.Invoke(); e.Handled = true; }
            else if (A11y.Step(e, Hue, 0, 359, 5, out var n)) { Hue = n; Place(); Changed?.Invoke(); (UIElementAutomationPeer.FromElement(this) as RangePeer)?.Changed(old); }
            base.OnKeyDown(e);
        }
        protected override AutomationPeer OnCreateAutomationPeer() => new RangePeer(this, () => Hue, x => { Hue = x; Place(); Changed?.Invoke(); }, 0, 359, 5, 30);
        protected override void OnMouseLeftButtonDown(System.Windows.Input.MouseButtonEventArgs e) { CaptureMouse(); From(e); }
        protected override void OnMouseMove(System.Windows.Input.MouseEventArgs e) { if (IsMouseCaptured) From(e); }
        protected override void OnMouseLeftButtonUp(System.Windows.Input.MouseButtonEventArgs e) { if (IsMouseCaptured) { From(e); ReleaseMouseCapture(); } }
    }
    sealed class LightBar : Grid
    {
        private readonly Border track = new() { Width = 22, CornerRadius = new CornerRadius(11), Margin = new Thickness(0, 11, 0, 11), HorizontalAlignment = HorizontalAlignment.Center, IsHitTestVisible = false };
        private readonly Ellipse dot = new() { Width = 28, Height = 28, Stroke = Brushes.White, StrokeThickness = 3, VerticalAlignment = VerticalAlignment.Top, IsHitTestVisible = false };
        public double T;
        public event Action? Changed;
        public LightBar(double t) { T = t; Width = 34; Height = 150; Background = Brushes.Transparent; Cursor = System.Windows.Input.Cursors.Hand; Focusable = true; FocusVisualStyle = A11y.FocusRing; Children.Add(track); Children.Add(dot); SizeChanged += (_, _) => Place(); }
        protected override void OnKeyDown(System.Windows.Input.KeyEventArgs e) { if (A11y.Step(e, T * 100, 0, 100, 2, out var n, vertical: true)) { double old = T * 100; T = n / 100; Place(); Changed?.Invoke(); (UIElementAutomationPeer.FromElement(this) as RangePeer)?.Changed(old); } base.OnKeyDown(e); }
        protected override AutomationPeer OnCreateAutomationPeer() => new RangePeer(this, () => T * 100, x => { T = x / 100; Place(); Changed?.Invoke(); }, 0, 100, 2, 10);
        public Color Surface { set => dot.Fill = new SolidColorBrush(value); }
        public void Tint(Color c) => track.Background = new LinearGradientBrush(new GradientStopCollection { new(Colors.White, 0), new(c, 0.2), new(c, 0.3), new(Color.FromRgb(10, 10, 10), 1) }, 90);
        public void Place() => dot.Margin = new Thickness(0, T * Math.Max(0, ActualHeight - 22) - 3, 0, 0);
        private void From(System.Windows.Input.MouseEventArgs e) { T = Math.Clamp((e.GetPosition(this).Y - 11) / Math.Max(1, ActualHeight - 22), 0, 1); Place(); Changed?.Invoke(); }
        protected override void OnMouseLeftButtonDown(System.Windows.Input.MouseButtonEventArgs e) { CaptureMouse(); From(e); }
        protected override void OnMouseMove(System.Windows.Input.MouseEventArgs e) { if (IsMouseCaptured) From(e); }
        protected override void OnMouseLeftButtonUp(System.Windows.Input.MouseButtonEventArgs e) { if (IsMouseCaptured) { From(e); ReleaseMouseCapture(); } }
    }
}
