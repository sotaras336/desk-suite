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
        string before = Theme.Current.Id; bool ok = false;
        int hue = (int)(Theme.Current.Hue >= 0 ? Theme.Current.Hue : 210);
        var win = new Window { Title = "Custom colour", Owner = this, Width = 440, SizeToContent = SizeToContent.Height, WindowStartupLocation = WindowStartupLocation.CenterOwner, ResizeMode = ResizeMode.NoResize, ShowInTaskbar = false };
        win.SetResourceReference(BackgroundProperty, "MenuBg");
        win.SourceInitialized += (_, _) => { int dark = Theme.IsLight ? 0 : 1; DwmSetWindowAttribute(new WindowInteropHelper(win).Handle, 20, ref dark, 4); };

        var sp = new StackPanel { Margin = new Thickness(22, 18, 22, 18) };
        sp.Children.Add(Label("Pick a colour", 16, "Fg", FontWeights.SemiBold));
        sp.Children.Add(Label("Everything updates as you slide", 12, "FgDim", margin: new Thickness(0, 2, 0, 14)));
        var rainbow = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 0) };
        for (int h = 0; h <= 360; h += 20) rainbow.GradientStops.Add(new GradientStop(Theme.ColFor(new ThemeDef("", "", h % 360, false, 0), 0x4CC2FF), h / 360.0));
        var track = new Grid { Height = 26 };
        track.Children.Add(new Border { Height = 12, CornerRadius = new CornerRadius(6), Background = rainbow, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(5, 0, 5, 0) });
        var slider = new Slider { Minimum = 0, Maximum = 359, Value = hue, VerticalAlignment = VerticalAlignment.Center, Focusable = true };
        slider.ValueChanged += (_, e) => PreviewTheme("custom:" + (int)e.NewValue);
        track.Children.Add(slider); sp.Children.Add(track);

        var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 18, 0, 0) };
        Button Btn(string text, Action act) { var b = new Button { Content = new TextBlock { Text = text, Margin = new Thickness(6, 0, 6, 0) }, Style = (Style)FindResource("Cmd"), Margin = new Thickness(6, 0, 0, 0) }; b.Click += (_, _) => act(); return b; }
        buttons.Children.Add(Btn("Cancel", () => win.Close()));
        buttons.Children.Add(Btn("Use this colour", () => { ok = true; win.Close(); }));
        sp.Children.Add(buttons);
        win.Content = sp;
        win.Closed += (_, _) => { if (ok) SetTheme("custom:" + (int)slider.Value); else PreviewTheme(before); };
        PreviewTheme("custom:" + hue);
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
