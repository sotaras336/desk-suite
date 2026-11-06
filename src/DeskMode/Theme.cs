using System.Windows;
using System.Windows.Media;
using Microsoft.Win32;

namespace DeskMode;

/// Follows the Windows light/dark setting and accent color.
public static class Theme
{
    private const string Personalize = @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize";

    private static bool ReadFlag(string name)
    {
        using var k = Registry.CurrentUser.OpenSubKey(Personalize);
        return k?.GetValue(name) is int v && v == 1;
    }

    public static bool AppsAreLight() => ReadFlag("AppsUseLightTheme");
    public static bool TaskbarIsLight() => ReadFlag("SystemUsesLightTheme");

    public static void Apply()
    {
        var r = System.Windows.Application.Current.Resources;
        bool light = AppsAreLight();
        var accent = SystemParameters.WindowGlassColor;
        using (var k = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\DWM"))
        {
            if (k?.GetValue("AccentColor") is int abgr)
                accent = Color.FromRgb((byte)(abgr & 0xFF), (byte)((abgr >> 8) & 0xFF), (byte)((abgr >> 16) & 0xFF));
        }
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
            Set("Fg", Colors.White);
            Set("FgDim", Color.FromArgb(0xA0, 0xFF, 0xFF, 0xFF));
            Set("Control", Color.FromArgb(0x10, 0xFF, 0xFF, 0xFF));
            Set("ControlHover", Color.FromArgb(0x1E, 0xFF, 0xFF, 0xFF));
            Set("Track", Color.FromArgb(0x40, 0xFF, 0xFF, 0xFF));
            Set("OnAccent", Colors.Black);
        }
        Set("Accent", shown);
    }

    private static Color Tint(Color c, double t) =>
        Color.FromRgb((byte)(c.R + (255 - c.R) * t), (byte)(c.G + (255 - c.G) * t), (byte)(c.B + (255 - c.B) * t));

    private static Color Shade(Color c, double t) =>
        Color.FromRgb((byte)(c.R * t), (byte)(c.G * t), (byte)(c.B * t));
}
