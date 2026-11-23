using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Shapes;
using static DeskDrawer.Native;

namespace DeskDrawer;

/// Colour management beyond the fixed themes: follow the Windows accent colour, or pick any colour on a rainbow slider (live preview, Cancel undoes it);
/// and the switch that makes DeskDrawer open folders instead of File Explorer.
public partial class MainWindow
{
    private void AddColourExtras(ItemCollection items)
    {
        items.Add(new Separator());
        if (Theme.WindowsAccentHue() != null) items.Add(MI("Windows accent colour", Theme.Current.Id == "accent" ? Check : "", () => SetTheme("accent")));
        items.Add(MI("Custom colour…", Theme.Current.Id.StartsWith("custom:") ? Check : "", PickColour));
    }

    private void PreviewTheme(string id) { Theme.Load(id); Theme.Apply(); ApplyChrome(); }

    /// A coloured bar with one round handle that wears the colour it points at; click or drag anywhere on it.
    private sealed class GradBar : Grid
    {
        private readonly Border track = new() { Height = 20, CornerRadius = new CornerRadius(10), Margin = new Thickness(15, 0, 15, 0), VerticalAlignment = VerticalAlignment.Center, IsHitTestVisible = false };
        private readonly Ellipse dot = new() { Width = 30, Height = 30, Stroke = Brushes.White, StrokeThickness = 3, HorizontalAlignment = HorizontalAlignment.Left, IsHitTestVisible = false, Effect = new System.Windows.Media.Effects.DropShadowEffect { BlurRadius = 8, ShadowDepth = 1, Opacity = 0.45 } };
        private readonly double max; private double v;
        public Func<double, Color> Sample = _ => Colors.Gray;
        public event Action? Changed;
        public GradBar(double max, double value) { this.max = max; v = value; Height = 34; Background = Brushes.Transparent; Cursor = System.Windows.Input.Cursors.Hand; Children.Add(track); Children.Add(dot); SizeChanged += (_, _) => Place(); }
        public Brush Track { set => track.Background = value; }
        public double Value { get => v; set { v = Math.Clamp(value, 0, max); Place(); } }
        public void Place() { dot.Margin = new Thickness(max > 0 ? v / max * Math.Max(0, ActualWidth - 30) : 0, 0, 0, 0); dot.Fill = new SolidColorBrush(Sample(v)); }
        private void From(System.Windows.Input.MouseEventArgs e) { double w = Math.Max(1, ActualWidth - 30); Value = (e.GetPosition(this).X - 15) / w * max; Changed?.Invoke(); }
        protected override void OnMouseLeftButtonDown(System.Windows.Input.MouseButtonEventArgs e) { CaptureMouse(); From(e); }
        protected override void OnMouseMove(System.Windows.Input.MouseEventArgs e) { if (IsMouseCaptured) From(e); }
        protected override void OnMouseLeftButtonUp(System.Windows.Input.MouseButtonEventArgs e) { if (IsMouseCaptured) { From(e); ReleaseMouseCapture(); } }
    }

    /// The colour pad: every colour across (the hue), how vivid it is down (grey at the bottom); the handle wears the colour it points at.
    private sealed class ColourPad : Grid
    {
        private readonly Ellipse dot = new() { Width = 22, Height = 22, Stroke = Brushes.White, StrokeThickness = 3, HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Top, IsHitTestVisible = false, Effect = new System.Windows.Media.Effects.DropShadowEffect { BlurRadius = 8, ShadowDepth = 1, Opacity = 0.45 } };
        public double Hue, Sat;
        public event Action? Changed;
        public ColourPad(double hue, double sat)
        {
            Hue = hue; Sat = sat; Height = 150; Background = Brushes.Transparent; Cursor = System.Windows.Input.Cursors.Cross;
            var rb = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 0) };
            for (int h = 0; h <= 360; h += 20) rb.GradientStops.Add(new GradientStop(Theme.ColFor(new ThemeDef("", "", h % 360, false, 0), 0x4CC2FF), h / 360.0));
            var gc = Theme.ColFor(new ThemeDef("", "", 0, false, 0, 0, 1), 0x4CC2FF);
            Children.Add(new Border { CornerRadius = new CornerRadius(12), Margin = new Thickness(11), Background = rb, IsHitTestVisible = false });
            Children.Add(new Border { CornerRadius = new CornerRadius(12), Margin = new Thickness(11), Background = new LinearGradientBrush(Color.FromArgb(0, gc.R, gc.G, gc.B), gc, 90), IsHitTestVisible = false });   // vivid at the top, grey at the bottom
            Children.Add(dot); SizeChanged += (_, _) => Place();
        }
        public Color Surface { set => dot.Fill = new SolidColorBrush(value); }
        public void Place() => dot.Margin = new Thickness(Hue / 359 * Math.Max(0, ActualWidth - 22), (1 - Sat / 100) * Math.Max(0, ActualHeight - 22), 0, 0);
        private void From(System.Windows.Input.MouseEventArgs e)
        {
            var p = e.GetPosition(this); Hue = Math.Clamp((p.X - 11) / Math.Max(1, ActualWidth - 22), 0, 1) * 359; Sat = (1 - Math.Clamp((p.Y - 11) / Math.Max(1, ActualHeight - 22), 0, 1)) * 100;
            Place(); Changed?.Invoke();
        }
        protected override void OnMouseLeftButtonDown(System.Windows.Input.MouseButtonEventArgs e) { CaptureMouse(); From(e); }
        protected override void OnMouseMove(System.Windows.Input.MouseEventArgs e) { if (IsMouseCaptured) From(e); }
        protected override void OnMouseLeftButtonUp(System.Windows.Input.MouseButtonEventArgs e) { if (IsMouseCaptured) { From(e); ReleaseMouseCapture(); } }
    }

    /// The light bar beside the pad: white at the top, the colour, then black. T runs 0 (top) to 1 (bottom); the ends are the White and Black themes (LightZone).
    private sealed class LightBar : Grid
    {
        private readonly Border track = new() { Width = 22, CornerRadius = new CornerRadius(11), Margin = new Thickness(0, 11, 0, 11), HorizontalAlignment = HorizontalAlignment.Center, IsHitTestVisible = false };
        private readonly Ellipse dot = new() { Width = 28, Height = 28, Stroke = Brushes.White, StrokeThickness = 3, VerticalAlignment = VerticalAlignment.Top, IsHitTestVisible = false, Effect = new System.Windows.Media.Effects.DropShadowEffect { BlurRadius = 8, ShadowDepth = 1, Opacity = 0.45 } };
        public double T;
        public event Action? Changed;
        public LightBar(double t) { T = t; Width = 34; Height = 150; Background = Brushes.Transparent; Cursor = System.Windows.Input.Cursors.Hand; Children.Add(track); Children.Add(dot); SizeChanged += (_, _) => Place(); }
        public Color Surface { set => dot.Fill = new SolidColorBrush(value); }
        public void Tint(Color c) => track.Background = new LinearGradientBrush(new GradientStopCollection { new(Colors.White, 0), new(c, 0.2), new(c, 0.3), new(Color.FromRgb(10, 10, 10), 1) }, 90);
        public void Place() => dot.Margin = new Thickness(0, T * Math.Max(0, ActualHeight - 22) - 3, 0, 0);
        private void From(System.Windows.Input.MouseEventArgs e) { T = Math.Clamp((e.GetPosition(this).Y - 11) / Math.Max(1, ActualHeight - 22), 0, 1); Place(); Changed?.Invoke(); }
        protected override void OnMouseLeftButtonDown(System.Windows.Input.MouseButtonEventArgs e) { CaptureMouse(); From(e); }
        protected override void OnMouseMove(System.Windows.Input.MouseEventArgs e) { if (IsMouseCaptured) From(e); }
        protected override void OnMouseLeftButtonUp(System.Windows.Input.MouseButtonEventArgs e) { if (IsMouseCaptured) { From(e); ReleaseMouseCapture(); } }
    }

    // what the light bar position means: under 0.10 the White theme, over 0.92 Black, 0.2-0.3 the colour as it is, darker below
    private static int LightZone(double t) => t < 0.10 ? 2 : t > 0.92 ? 1 : 0;
    private static double LightVal(double t) => t <= 0.30 ? 1 : Math.Clamp((0.92 - t) / 0.62, 0, 1);

    private void PickColour()
    {
        string before = Theme.FullId; bool ok = false;
        // a ready-made colour keeps its exact look until the colour bar is moved; the fade then rides on top of it ("blue:40")
        string? baseId = Theme.Current.Kind == 0 && !Theme.Current.Id.StartsWith("custom:") ? Theme.Current.Id : null; bool moved = false;
        var cur = Theme.Current;
        double hue = cur.Kind != 0 ? 300 : cur.Hue >= 0 ? cur.Hue : 205, t0 = cur.Kind == 1 ? 0.96 : cur.Kind == 2 ? 0.04 : cur.Val >= 1 ? 0.25 : 0.92 - cur.Val * 0.62;
        var win = new Window { Title = "Custom colour", Owner = this, Width = 440, SizeToContent = SizeToContent.Height, WindowStartupLocation = WindowStartupLocation.CenterOwner, ResizeMode = ResizeMode.NoResize, ShowInTaskbar = false };
        win.SetResourceReference(BackgroundProperty, "MenuBg");
        win.SourceInitialized += (_, _) => { int dark = Theme.IsLight ? 0 : 1; DwmSetWindowAttribute(new WindowInteropHelper(win).Handle, 20, ref dark, 4); };

        Color Base() => Theme.Col(0x1257B8);

        var sp = new StackPanel { Margin = new Thickness(22, 18, 22, 18) };
        sp.Children.Add(Label("Pick a colour", 16, "Fg", FontWeights.SemiBold, margin: new Thickness(0, 0, 0, 12)));
        var preview = new Border { Height = 56, CornerRadius = new CornerRadius(14), Margin = new Thickness(0, 0, 0, 12) };
        sp.Children.Add(preview);

        var card = new StackPanel(); sp.Children.Add(new Border { CornerRadius = new CornerRadius(14), Padding = new Thickness(16, 12, 16, 14), Background = new SolidColorBrush(Color.FromArgb(0x14, 255, 255, 255)), Child = card });
        Grid Head(string name, out TextBlock hint) { var g = new Grid { Margin = new Thickness(0, 0, 0, 4) }; g.Children.Add(Label(name, 13, "Fg", FontWeights.SemiBold)); hint = Label("", 11.5, "FgDim"); hint.HorizontalAlignment = HorizontalAlignment.Right; hint.VerticalAlignment = VerticalAlignment.Center; g.Children.Add(hint); return g; }
        card.Children.Add(Head("Colour", out var hueHint)); hueHint.Text = "colour, vividness, light";
        var pad = new ColourPad(hue, cur.Sat * 100); var light = new LightBar(t0) { Margin = new Thickness(10, 0, 0, 0) }; Grid.SetColumn(light, 1);
        card.Children.Add(new Grid { ColumnDefinitions = { new ColumnDefinition(), new ColumnDefinition { Width = GridLength.Auto } }, Children = { pad, light } });
        var fadeBox = new StackPanel { Margin = new Thickness(0, 12, 0, 0) }; card.Children.Add(fadeBox);
        fadeBox.Children.Add(Head("Fade", out var fadeHint));
        var fadeBar = new GradBar(100, Theme.Grad > 0 ? Theme.Grad : 0) { Sample = f => f < 1 ? Base() : Theme.Shift(Base(), f, 0.002) };
        fadeBox.Children.Add(fadeBar);

        string Id()
        {
            int z = LightZone(light.T); if (z != 0) return z == 1 ? "black" : "white";
            string fade = ":" + (int)fadeBar.Value; if (baseId != null && !moved) return baseId + fade;
            int sv = (int)Math.Round(pad.Sat), v = (int)Math.Round(LightVal(light.T) * 100);
            return "custom:" + (int)pad.Hue + fade + (sv < 100 || v < 100 ? ":" + sv + ":" + v : "");
        }
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
        pad.Changed += () => { moved = true; PreviewTheme(Id()); Refresh(); }; light.Changed += () => { moved = true; PreviewTheme(Id()); Refresh(); };
        fadeBar.Changed += () => { PreviewTheme(Id()); Refresh(); };

        var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 16, 0, 0) };
        Button Btn(string text, Action act) { var b = new Button { Content = new TextBlock { Text = text, Margin = new Thickness(6, 0, 6, 0) }, Style = (Style)FindResource("Cmd"), Margin = new Thickness(6, 0, 0, 0) }; b.Click += (_, _) => act(); return b; }
        buttons.Children.Add(Btn("Cancel", () => win.Close()));
        buttons.Children.Add(Btn("Use this colour", () => { ok = true; win.Close(); }));
        sp.Children.Add(buttons);
        win.Content = sp;
        win.Closed += (_, _) => { if (ok) SetTheme(Id()); else PreviewTheme(before); };
        PreviewTheme(Id()); Refresh();
        win.ShowDialog();
    }

    /// A setting changed in the Desk settings window (DeskBar's): "Key=value", applied the same way our own switches do it.
    internal void ApplySetting(string kv)
    {
        int eq = kv.IndexOf('='); if (eq < 0) return; string k = kv[..eq], v = kv[(eq + 1)..]; bool on = v == "1";
        switch (k)
        {
            case "Default": SetDefault(on); break;
            case "TakeOverDialogs": _settings.TakeOverDialogs = on; DialogSwitch.TakeOver = on; break;
            case "ShowHidden": if (on != _settings.ShowHidden) ToggleHidden(); break;
            case "KeepReady": _settings.KeepReady = on; Setup.StartAtSignIn(on); break;
            case "ScrollSpeed": if (double.TryParse(v, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var sp)) { _settings.ScrollSpeed = sp; Wheel.Speed = sp; } break;
            case "Gpu": _settings.Gpu = on; break;
            default: return;
        }
        _settings.Save();
    }
    private void SetDefault(bool on)
    {
        if (Environment.ProcessPath is not { } exe) return;
        try { DefaultApp.Set(on, exe); }
        catch (Exception ex) { MessageBox.Show(this, ex.Message, "DeskDrawer", MessageBoxButton.OK, MessageBoxImage.Warning); return; }
        Flash(on ? "Folders and Win+E now open in DeskDrawer" : "Folders open in File Explorer again");
    }
}
