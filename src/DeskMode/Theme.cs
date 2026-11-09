using System.Windows;
using System.Windows.Media;
using Microsoft.Win32;

namespace DeskMode;

/// Dark or light (follows Windows unless chosen) and the accent colour (Windows' own unless chosen). Both are picked in Settings > Appearance.
public static class Theme
{
    private const string Personalize = @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize";

    /// The accent choices: "windows" follows the system accent, the rest are fixed.
    public static readonly (string Id, string Name, Color Base)[] AccentChoices =
    {
        ("windows", "Windows accent", Colors.Transparent),
        ("blue", "Indigo", Color.FromRgb(0x5B, 0x5F, 0xEF)), ("teal", "Teal", Color.FromRgb(0x00, 0xB7, 0xC3)), ("green", "Green", Color.FromRgb(0x10, 0x89, 0x3E)),
        ("orange", "Orange", Color.FromRgb(0xF7, 0x63, 0x0C)), ("red", "Red", Color.FromRgb(0xE7, 0x48, 0x56)), ("pink", "Pink", Color.FromRgb(0xE3, 0x00, 0x8C)), ("lightpink", "Light pink", Color.FromRgb(0xF4, 0x8F, 0xB1)), ("violet", "Violet", Color.FromRgb(0x87, 0x64, 0xB8)),
    };

    public static string AccentId = "windows", Look = "auto";
    private static bool _loaded;

    private static bool ReadFlag(string name)
    {
        using var k = Registry.CurrentUser.OpenSubKey(Personalize);
        return k?.GetValue(name) is int v && v == 1;
    }

    public static bool AppsAreLight() => ReadFlag("AppsUseLightTheme");
    public static bool TaskbarIsLight() => ReadFlag("SystemUsesLightTheme");

    private static Color SystemAccent()
    {
        var accent = SystemParameters.WindowGlassColor;
        using var k = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\DWM");
        if (k?.GetValue("AccentColor") is int abgr) accent = Color.FromRgb((byte)(abgr & 0xFF), (byte)((abgr >> 8) & 0xFF), (byte)((abgr >> 16) & 0xFF));
        return accent;
    }

    /// The colour a choice shows as in its dot.
    public static Color Swatch(string id)
    {
        var b = AccentChoices.FirstOrDefault(c => c.Id == id).Base;
        return id == "windows" ? SystemAccent() : b;
    }

    public static void Apply()
    {
        if (!_loaded) { _loaded = true; var s = Settings.Load(); AccentId = s.Accent; Look = s.Look; }
        var r = System.Windows.Application.Current.Resources;
        bool light = Look == "light" || (Look != "dark" && AppsAreLight());
        var accent = AccentId == "windows" ? SystemAccent() : AccentChoices.FirstOrDefault(c => c.Id == AccentId).Base;
        if (accent == default) accent = SystemAccent();
        // Windows uses a lighter accent on dark backgrounds and a darker one on light.
        var shown = light ? Shade(accent, 0.82) : Tint(accent, 0.45);

        void Set(string key, Color c) => r[key] = new SolidColorBrush(c);
        if (light)
        {
            Set("WindowBg", Color.FromArgb(0xF7, 0xF3, 0xF3, 0xF3));
            Set("PageBg", Color.FromRgb(0xF3, 0xF3, 0xF3));
            Set("WindowBgSolid", Color.FromRgb(0xFF, 0xFF, 0xFF));
            Set("CardBg", Color.FromArgb(0xC0, 0xFF, 0xFF, 0xFF));
            Set("CardBorder", Color.FromArgb(0x16, 0, 0, 0));
            Set("Fg", Color.FromRgb(0x1A, 0x1A, 0x1A));
            Set("FgDim", Color.FromArgb(0x9E, 0, 0, 0));
            Set("Control", Color.FromArgb(0x0C, 0, 0, 0));
            Set("ControlHover", Color.FromArgb(0x14, 0, 0, 0));
            Set("Track", Color.FromArgb(0x30, 0, 0, 0));
            Set("OnAccent", Colors.White);
        }
        else
        {
            Set("WindowBg", Color.FromArgb(0xF5, 0x20, 0x20, 0x20));
            Set("PageBg", Color.FromRgb(0x20, 0x20, 0x20));
            Set("WindowBgSolid", Color.FromRgb(0x45, 0x45, 0x45));
            Set("CardBg", Color.FromArgb(0x0D, 0xFF, 0xFF, 0xFF));
            Set("CardBorder", Color.FromArgb(0x17, 0xFF, 0xFF, 0xFF));
            Set("Fg", Color.FromRgb(0xEC, 0xEF, 0xF4));   // not pure white: easier on the eyes on a dark panel
            Set("FgDim", Color.FromArgb(0xA8, 0xEC, 0xEF, 0xF4));
            Set("Control", Color.FromArgb(0x10, 0xFF, 0xFF, 0xFF));
            Set("ControlHover", Color.FromArgb(0x1E, 0xFF, 0xFF, 0xFF));
            Set("Track", Color.FromArgb(0x40, 0xFF, 0xFF, 0xFF));
            Set("OnAccent", Colors.Black);
        }
        Set("Accent", shown);
        Set("AccentSoft", Color.FromArgb(light ? (byte)0x2A : (byte)0x30, shown.R, shown.G, shown.B));
        r["AccentColor"] = shown;
        // chip text is always the normal text colour (white on dark, near-black on light), so the checked chip needs a background that suits it: darker than the accent on dark, lighter on light
        var chip = light ? Tint(accent, 0.6) : Shade(accent, 0.8);
        Set("ChipBg", chip);
        r["AccentColor2"] = HueShift(accent, 40);
        r["GlowColor"] = accent;   // the glows and shadows use the real, saturated accent: the tinted one (for text and sliders) made a blue look pale
        r["ChipGradient"] = new LinearGradientBrush(chip, HueShift(light ? chip : Tint(chip, 0.12), -16), 40);   // a deep accent running into a lighter neighbour on the colour wheel
    }

    /// The same colour with its hue turned by `deg` degrees (a neighbour on the colour wheel: the second glow and the end of the chip gradient).
    private static Color HueShift(Color c, double deg)
    {
        double r = c.R / 255.0, g = c.G / 255.0, b = c.B / 255.0, max = Math.Max(r, Math.Max(g, b)), min = Math.Min(r, Math.Min(g, b)), d = max - min, h;
        if (d == 0) return c;
        h = max == r ? ((g - b) / d) % 6 : max == g ? (b - r) / d + 2 : (r - g) / d + 4; h = (h * 60 + deg + 360) % 360;
        double s = max == 0 ? 0 : d / max, v = max, ch = v * s, x = ch * (1 - Math.Abs(h / 60 % 2 - 1)), m = v - ch;
        double rr, gg, bb;
        if (h < 60) { rr = ch; gg = x; bb = 0; } else if (h < 120) { rr = x; gg = ch; bb = 0; } else if (h < 180) { rr = 0; gg = ch; bb = x; } else if (h < 240) { rr = 0; gg = x; bb = ch; } else if (h < 300) { rr = x; gg = 0; bb = ch; } else { rr = ch; gg = 0; bb = x; }
        return Color.FromRgb((byte)((rr + m) * 255), (byte)((gg + m) * 255), (byte)((bb + m) * 255));
    }

    private static Color Tint(Color c, double t) =>
        Color.FromRgb((byte)(c.R + (255 - c.R) * t), (byte)(c.G + (255 - c.G) * t), (byte)(c.B + (255 - c.B) * t));

    private static Color Shade(Color c, double t) =>
        Color.FromRgb((byte)(c.R * t), (byte)(c.G * t), (byte)(c.B * t));
}
