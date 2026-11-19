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

    private void PickColour()
    {
        string before = Theme.FullId; bool ok = false;
        // a ready-made colour keeps its exact look until the colour bar is moved; the fade then rides on top of it ("blue:40")
        string? baseId = Theme.Current.Kind == 0 && !Theme.Current.Id.StartsWith("custom:") ? Theme.Current.Id : null; bool moved = false;
        int hue = (int)(Theme.Current.Hue >= 0 ? Theme.Current.Hue : 205);
        var win = new Window { Title = "Custom colour", Owner = this, Width = 440, SizeToContent = SizeToContent.Height, WindowStartupLocation = WindowStartupLocation.CenterOwner, ResizeMode = ResizeMode.NoResize, ShowInTaskbar = false };
        win.SetResourceReference(BackgroundProperty, "MenuBg");
        win.SourceInitialized += (_, _) => { int dark = Theme.IsLight ? 0 : 1; DwmSetWindowAttribute(new WindowInteropHelper(win).Handle, 20, ref dark, 4); };

        // the colour bar runs 0..399: the first 20 are black, the last 20 white, the 360 between are the hues
        Color HueCol(double x) => x < 20 ? Color.FromRgb(10, 10, 10) : x >= 380 ? Colors.White : Theme.ColFor(new ThemeDef("", "", x - 20, false, 0), 0x4CC2FF);
        var rainbow = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 0) };
        for (int x = 0; x <= 400; x += 10) rainbow.GradientStops.Add(new GradientStop(HueCol(x < 20 ? 0 : x > 380 ? 399 : x), x / 400.0));
        Color Base() => Theme.Col(0x1257B8);

        var sp = new StackPanel { Margin = new Thickness(22, 18, 22, 18) };
        sp.Children.Add(Label("Pick a colour", 16, "Fg", FontWeights.SemiBold, margin: new Thickness(0, 0, 0, 12)));
        var preview = new Border { Height = 56, CornerRadius = new CornerRadius(14), Margin = new Thickness(0, 0, 0, 12) };
        sp.Children.Add(preview);

        var card = new StackPanel(); sp.Children.Add(new Border { CornerRadius = new CornerRadius(14), Padding = new Thickness(16, 12, 16, 14), Background = new SolidColorBrush(Color.FromArgb(0x14, 255, 255, 255)), Child = card });
        Grid Head(string name, out TextBlock hint) { var g = new Grid { Margin = new Thickness(0, 0, 0, 4) }; g.Children.Add(Label(name, 13, "Fg", FontWeights.SemiBold)); hint = Label("", 11.5, "FgDim"); hint.HorizontalAlignment = HorizontalAlignment.Right; hint.VerticalAlignment = VerticalAlignment.Center; g.Children.Add(hint); return g; }
        card.Children.Add(Head("Colour", out var hueHint)); hueHint.Text = "far left black, far right white";
        var hueBar = new GradBar(399, Theme.Current.Kind == 1 ? 10 : Theme.Current.Kind == 2 ? 390 : hue + 20) { Track = rainbow, Sample = HueCol };
        card.Children.Add(hueBar);
        var fadeBox = new StackPanel { Margin = new Thickness(0, 12, 0, 0) }; card.Children.Add(fadeBox);
        fadeBox.Children.Add(Head("Fade", out var fadeHint));
        var fadeBar = new GradBar(100, Theme.Grad > 0 ? Theme.Grad : 0) { Sample = f => f < 1 ? Base() : Theme.Shift(Base(), f, 0.002) };
        fadeBox.Children.Add(fadeBar);

        string Id() => hueBar.Value < 20 ? "black" : hueBar.Value >= 380 ? "white" : (baseId != null && !moved ? baseId : "custom:" + ((int)hueBar.Value - 20)) + ":" + (int)fadeBar.Value;
        void Refresh()
        {
            double f = fadeBar.Value; bool plain = hueBar.Value < 20 || hueBar.Value >= 380;
            fadeBox.Visibility = plain ? Visibility.Collapsed : Visibility.Visible;
            fadeHint.Text = f >= 1 ? (int)f + "% lighter on the right" : "off";
            preview.Background = new LinearGradientBrush(Base(), f >= 1 && !plain ? Theme.Shift(Base(), f, 0.002) : Base(), 0);
            fadeBar.Track = new LinearGradientBrush(Base(), Theme.Shift(Base(), 100, 0.002), 0);
            hueBar.Place(); fadeBar.Place();
        }
        hueBar.Changed += () => { moved = true; PreviewTheme(Id()); Refresh(); };
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

    private void SetDefault(bool on)
    {
        if (Environment.ProcessPath is not { } exe) return;
        try { DefaultApp.Set(on, exe); }
        catch (Exception ex) { MessageBox.Show(this, ex.Message, "DeskDrawer", MessageBoxButton.OK, MessageBoxImage.Warning); return; }
        Flash(on ? "Folders and Win+E now open in DeskDrawer" : "Folders open in File Explorer again");
    }
}
