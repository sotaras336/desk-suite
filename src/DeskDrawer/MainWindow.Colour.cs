using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
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

    private void PickColour()
    {
        string before = Theme.FullId; bool ok = false;
        // a ready-made colour keeps its exact look until the colour bar is moved; the fade then rides on top of it ("blue:40")
        string? baseId = Theme.Current.Kind == 0 && !Theme.Current.Id.StartsWith("custom:") ? Theme.Current.Id : null; bool moved = false;
        int hue = (int)(Theme.Current.Hue >= 0 ? Theme.Current.Hue : 205);
        var win = new Window { Title = "Custom colour", Owner = this, Width = 440, SizeToContent = SizeToContent.Height, WindowStartupLocation = WindowStartupLocation.CenterOwner, ResizeMode = ResizeMode.NoResize, ShowInTaskbar = false };
        win.SetResourceReference(BackgroundProperty, "MenuBg");
        win.SourceInitialized += (_, _) => { int dark = Theme.IsLight ? 0 : 1; DwmSetWindowAttribute(new WindowInteropHelper(win).Handle, 20, ref dark, 4); };

        var sp = new StackPanel { Margin = new Thickness(22, 18, 22, 18) };
        sp.Children.Add(Label("Pick a colour", 16, "Fg", FontWeights.SemiBold));
        sp.Children.Add(Label("Everything updates as you slide", 12, "FgDim", margin: new Thickness(0, 2, 0, 14)));
        LinearGradientBrush Rainbow() { var r = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 0) }; var dark = Color.FromRgb(10, 10, 10); r.GradientStops.Add(new GradientStop(dark, 0)); r.GradientStops.Add(new GradientStop(dark, 20 / 400.0)); for (int h = 0; h <= 360; h += 20) r.GradientStops.Add(new GradientStop(Theme.ColFor(new ThemeDef("", "", h % 360, false, 0), 0x4CC2FF), (20 + h) / 400.0)); r.GradientStops.Add(new GradientStop(Colors.White, 380 / 400.0)); r.GradientStops.Add(new GradientStop(Colors.White, 1)); return r; }
        Brush FadeFill() { var a = Theme.Ui(0x1167C4); return new LinearGradientBrush(a, Theme.Shift(a, 100, 0.002), 0); }   // what the fade slider will do to the colour above
        // a plain track (the coloured bar behind it) with one big round handle; a click anywhere on the bar jumps there
        var sliderT = (ControlTemplate)System.Windows.Markup.XamlReader.Parse("<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='Slider'><Grid Background='Transparent' Height='28'><Track x:Name='PART_Track'><Track.Thumb><Thumb Width='26' Height='26' Cursor='Hand'><Thumb.Template><ControlTemplate TargetType='Thumb'><Ellipse Fill='White' Stroke='#66000000' StrokeThickness='1'><Ellipse.Effect><DropShadowEffect BlurRadius='6' ShadowDepth='1' Opacity='0.5'/></Ellipse.Effect></Ellipse></ControlTemplate></Thumb.Template></Thumb></Track.Thumb></Track></Grid></ControlTemplate>");
        Slider Bar(int value, StackPanel host, UIElement? side = null, double max = 359, Brush? fill = null)
        {
            var track = new Grid { Height = 28, ColumnDefinitions = { new ColumnDefinition(), new ColumnDefinition { Width = GridLength.Auto } } };
            var cell = new Grid(); var fillBar = new Border { Height = 18, CornerRadius = new CornerRadius(9), Background = fill ?? Rainbow(), VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0) }; cell.Children.Add(fillBar);
            var sl = new Slider { Minimum = 0, Maximum = max, Value = value, VerticalAlignment = VerticalAlignment.Center, Focusable = true, Tag = fillBar, Template = sliderT, IsMoveToPointEnabled = true };
            cell.Children.Add(sl); track.Children.Add(cell);
            if (side != null) { Grid.SetColumn(side, 1); track.Children.Add(side); }
            host.Children.Add(track); return sl;
        }
        sp.Children.Add(Label("Colour", 13, "Fg", FontWeights.SemiBold, margin: new Thickness(0, 0, 0, 2)));
        sp.Children.Add(Label("Far left is black, far right is white", 11.5, "FgDim", margin: new Thickness(0, 0, 0, 6)));
        var slider = Bar(Theme.Current.Kind == 1 ? 10 : Theme.Current.Kind == 2 ? 390 : hue + 20, sp, null, 399);   // the ends of the bar are Black and White
        var gradHost = new StackPanel { Margin = new Thickness(0, 14, 0, 0) };
        var fadeVal = Label(Theme.Grad > 0 ? (int)Theme.Grad + "% lighter on the right" : "Fade: off", 11.5, "FgDim", margin: new Thickness(0, 0, 0, 6));
        gradHost.Children.Add(Label("Fade", 13, "Fg", FontWeights.SemiBold, margin: new Thickness(0, 0, 0, 2))); gradHost.Children.Add(fadeVal);
        var slider2 = Bar((int)(Theme.Grad > 0 ? Theme.Grad : 0), gradHost, max: 100, fill: FadeFill());
        sp.Children.Add(gradHost);
        string Id() => slider.Value < 20 ? "black" : slider.Value >= 380 ? "white" : (baseId != null && !moved ? baseId : "custom:" + ((int)slider.Value - 20)) + ":" + (int)slider2.Value;
        slider.ValueChanged += (_, _) => { moved = true; PreviewTheme(Id()); ((Border)slider2.Tag).Background = FadeFill(); }; slider2.ValueChanged += (_, _) => { fadeVal.Text = slider2.Value >= 1 ? (int)slider2.Value + "% lighter on the right" : "Fade: off"; PreviewTheme(Id()); };

        var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 18, 0, 0) };
        Button Btn(string text, Action act) { var b = new Button { Content = new TextBlock { Text = text, Margin = new Thickness(6, 0, 6, 0) }, Style = (Style)FindResource("Cmd"), Margin = new Thickness(6, 0, 0, 0) }; b.Click += (_, _) => act(); return b; }
        buttons.Children.Add(Btn("Cancel", () => win.Close()));
        buttons.Children.Add(Btn("Use this colour", () => { ok = true; win.Close(); }));
        sp.Children.Add(buttons);
        win.Content = sp;
        win.Closed += (_, _) => { if (ok) SetTheme(Id()); else PreviewTheme(before); };
        PreviewTheme(Id());
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
