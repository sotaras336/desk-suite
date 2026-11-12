using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;

namespace DeskDrawer;

/// The same colour themes as DeskBar (pink, blue, purple, teal, green, orange, red, black, white, glass), so the taskbar and the file manager look like one system.
/// Every colour is written as the original DeskBar blue and mapped through the current theme, exactly like DeskBar's Col().
/// Kind: 0 = hue theme, 1 = black, 2 = white (light), 3 = glass (translucent over an acrylic backdrop).
public sealed record ThemeDef(string Id, string Name, double Hue, bool Soft, int Kind);

public static class Theme
{
    public static readonly ThemeDef[] All =
    {
        new("pink", "Pink", 336, true, 0), new("blue", "Blue", -1, false, 0), new("purple", "Purple", 268, false, 0), new("teal", "Teal", 176, false, 0),
        new("green", "Green", 140, false, 0), new("orange", "Orange", 26, false, 0), new("red", "Red", 354, false, 0),
        new("black", "Black", -1, false, 1), new("white", "White", -1, false, 2), new("glass", "Glass", -1, false, 3),
    };

    public static ThemeDef Current { get; private set; } = All[0];
    public static bool IsLight => Current.Kind == 2;
    public static bool IsGlass => Current.Kind == 3;

    /// Black, like Windows' own dark mode, unless the settings (or --theme) say otherwise.
    public static void Load(string? id)
    {
        if (id != null && id.StartsWith("custom:") && double.TryParse(id[7..], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var hue))
            Current = new ThemeDef(id, "Custom", ((hue % 360) + 360) % 360, false, 0);
        else if (id == "accent" && WindowsAccentHue() is { } ah) Current = new ThemeDef("accent", "Windows accent", ah, false, 0);
        else Current = All.FirstOrDefault(t => string.Equals(t.Id, id, StringComparison.OrdinalIgnoreCase)) ?? All.First(t => t.Id == "black");
    }

    /// The hue of the accent colour picked in Windows Settings > Personalisation > Colours (stored by DWM as 0xAABBGGRR).
    public static double? WindowsAccentHue()
    {
        if (Microsoft.Win32.Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\DWM")?.GetValue("AccentColor") is not int abgr) return null;
        ToHsl((abgr & 255) / 255.0, ((abgr >> 8) & 255) / 255.0, ((abgr >> 16) & 255) / 255.0, out double h, out double s, out _);
        return s < 0.1 ? null : h;   // a grey accent has no hue to follow
    }

    public static Color Col(uint rgb, int a = 255) => ColFor(Current, rgb, a);

    public static Color ColFor(ThemeDef t, uint rgb, int a = 255)
    {
        double r = ((rgb >> 16) & 255) / 255.0, g = ((rgb >> 8) & 255) / 255.0, b = (rgb & 255) / 255.0, al = a / 255.0;
        ToHsl(r, g, b, out double h, out double s, out double l);
        bool blue = h >= 185 && h <= 280 && s > 0.2;
        if (t.Kind > 0)
        {
            bool white = t.Kind == 2;
            if (white && rgb == 0xFFFFFF) r = g = b = 0x1B / 255.0;          // text and highlights: white -> near black
            else if (white && rgb == 0x000000) r = g = b = 1;                // text on accent fills
            else if (blue && l < 0.5) { r = g = b = white ? 1 - l * 0.3 : l * (t.Kind == 3 ? 0.4 : 0.5); if (t.Kind == 3) al *= 0.55; }   // backgrounds go neutral
            else if (blue && white) FromHsl(h, s, l * 0.72, out r, out g, out b);   // accents need contrast on a light surface
            else if (white && s < 0.1 && l < 0.35) r = g = b = 1 - l;        // dark greys
        }
        else if (t.Hue >= 0 && blue)
        {
            if (t.Soft && l >= 0.5) { h = 302 + (h - 201) * 0.25; s = Math.Min(s * 0.85, 0.85); l = Math.Min(0.9, l + 0.09); }   // the pink text and accents: DeskBar's accent 0x4CC2FF lands exactly on the orchid pink #F584F1
            else { h = t.Hue + (h - 225) * 0.25; if (t.Soft) { s = Math.Min(s * 0.5, 0.5); l = l < 0.5 ? 0.52 + l * 0.22 : Math.Min(0.9, l + 0.14); } }
            if (h < 0) h += 360; if (h >= 360) h -= 360;
            FromHsl(h, s, l, out r, out g, out b);
        }
        return Color.FromArgb((byte)Math.Round(al * 255), (byte)Math.Round(r * 255), (byte)Math.Round(g * 255), (byte)Math.Round(b * 255));
    }

    private static void ToHsl(double r, double g, double b, out double h, out double s, out double l)
    {
        double mx = Math.Max(r, Math.Max(g, b)), mn = Math.Min(r, Math.Min(g, b)); l = (mx + mn) / 2; h = s = 0;
        if (mx == mn) return;
        double d = mx - mn; s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
        h = mx == r ? (g - b) / d + (g < b ? 6 : 0) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4; h *= 60;
    }

    private static double Hue2(double p, double q, double t)
    {
        if (t < 0) t += 1; if (t > 1) t -= 1;
        if (t < 1.0 / 6) return p + (q - p) * 6 * t; if (t < 0.5) return q; if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6; return p;
    }

    private static void FromHsl(double h, double s, double l, out double r, out double g, out double b)
    {
        double q = l < .5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q, x = h / 360;
        r = Hue2(p, q, x + 1.0 / 3); g = Hue2(p, q, x); b = Hue2(p, q, x - 1.0 / 3);
    }

    private static SolidColorBrush Solid(uint rgb, int a = 255) { var br = new SolidColorBrush(Col(rgb, a)); br.Freeze(); return br; }

    /// Fills the application's brush resources from the current theme (the XAML refers to them with DynamicResource).
    public static void Apply()
    {
        var r = Application.Current.Resources;
        var win = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(0.55, 1) };
        win.GradientStops.Add(new GradientStop(Col(0x1257B8), 0)); win.GradientStops.Add(new GradientStop(Col(0x0A4096), 0.5)); win.GradientStops.Add(new GradientStop(Col(0x072C70), 1));
        win.Freeze();
        r["WinBg"] = win;
        r["BarBg"] = Solid(0x003D91, 0xFF); r["BarLine"] = Solid(0xFFFFFF, 0x18);
        r["StripBg"] = Solid(0x002A6A, 0xFF);   // title bar + tab strip: a step darker, so the active tab (BarBg) reads as part of the toolbar below
        r["PaneBg"] = Solid(0x001030, 0x30);
        r["Fg"] = Solid(0xFFFFFF); r["FgDim"] = Solid(0xFFFFFF, 0xB8); r["FgFaint"] = Solid(0xFFFFFF, 0x80);
        r["Accent"] = Solid(0x4CC2FF); r["OnAccent"] = Solid(0x000000);
        r["Card"] = Solid(0xFFFFFF, 0x16); r["CardHover"] = Solid(0xFFFFFF, 0x26); r["CardLine"] = Solid(0xFFFFFF, 0x20);
        r["Select"] = Solid(0x4CC2FF, 0x40); r["SelectLine"] = Solid(0x4CC2FF, 0x90);
        r["Thumb"] = Solid(0xFFFFFF, 0x60); r["ThumbHover"] = Solid(0xFFFFFF, 0xA0);
        r["Error"] = Solid(0xFF8A8A);
        var menu = Col(0x0A4096); var mb = new SolidColorBrush(Color.FromRgb(menu.R, menu.G, menu.B)); mb.Freeze(); r["MenuBg"] = mb;   // menus are always opaque, even in the glass theme
        if (Current.Kind == 1)
        {   // black = Windows' dark mode: flat greys and a neutral grey selection (the blue accent is only for focus rings and small touches)
            SolidColorBrush Raw(uint rgb, byte a = 255) { var b = new SolidColorBrush(Color.FromArgb(a, (byte)(rgb >> 16), (byte)(rgb >> 8), (byte)rgb)); b.Freeze(); return b; }
            r["WinBg"] = Raw(0x1F1F1F); r["BarBg"] = Raw(0x2A2A2A); r["StripBg"] = Raw(0x1A1A1A); r["BarLine"] = Raw(0xFFFFFF, 0x12); r["PaneBg"] = Raw(0x191919); r["MenuBg"] = Raw(0x2B2B2B);
            r["Card"] = Raw(0xFFFFFF, 0x0F); r["CardHover"] = Raw(0xFFFFFF, 0x1C); r["CardLine"] = Raw(0xFFFFFF, 0x16);
            r["Select"] = Raw(0xFFFFFF, 0x20); r["SelectLine"] = Raw(0xFFFFFF, 0x2E);
        }
    }
}
