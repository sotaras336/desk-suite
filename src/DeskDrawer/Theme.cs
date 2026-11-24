using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;

namespace DeskDrawer;

/// The same colour themes as DeskBar (pink, blue, purple, teal, green, orange, red, black, white, glass), so the taskbar and the file manager look like one system.
/// Every colour is written as the original DeskBar blue and mapped through the current theme, exactly like DeskBar's Col().
/// Kind: 0 = hue theme, 1 = black, 2 = white (light), 3 = glass (translucent over an acrylic backdrop).
/// Sat and Val (0-1) belong to custom colours ("custom:<hue>:<fade>:<sat>:<val>"): how vivid the colour is and how bright its backgrounds are; 1 = the original.
public sealed record ThemeDef(string Id, string Name, double Hue, bool Soft, int Kind, double Sat = 1, double Val = 1);

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

    /// How far a custom colour fades to a lighter shade ("custom:<hue>:<0-100>"): 0 = flat, -1 = not set (DeskMode's own fade).
    public static double Grad = -1;
    /// The id to save: Current.Id plus the fade of a ready-made colour ("blue:40").
    public static string FullId = "black";

    /// The colour turned toward a lighter, hue-shifted shade as far as the gradient strength g (0-100) says; k = how much lighter per point.
    public static Color Shift(Color c, double g, double k)
    {
        ToHsl(c.R / 255.0, c.G / 255.0, c.B / 255.0, out double h, out double s, out double l);
        FromHsl((h - 0.5 * g + 360) % 360, s, Math.Min(0.9, l + k * g), out double r, out double gr, out double bl);
        return Color.FromArgb(c.A, (byte)Math.Round(r * 255), (byte)Math.Round(gr * 255), (byte)Math.Round(bl * 255));
    }

    /// DeskMode's accent fade, blue to teal: the colour it starts from, fading toward a lighter, hue-shifted shade as far as the custom colour's gradient says.
    public static Brush Glow()
    {
        if (Current.Soft) return Duo();
        var a = Ui(0x1167C4); Color b;
        if (Grad < 0) b = Ui(0x1FA7B9);
        else if (Grad == 0) b = a;
        else b = Shift(a, Grad, 0.002);
        var br = new LinearGradientBrush(a, b, 45); br.Freeze(); return br;
    }

    /// Black, like Windows' own dark mode, unless the settings (or --theme) say otherwise.
    public static void Load(string? id)
    {
        Grad = -1;
        var inv = System.Globalization.CultureInfo.InvariantCulture; var parts = (id ?? "").Split(':');
        if (parts.Length > 1 && parts[0] == "custom" && double.TryParse(parts[1], System.Globalization.NumberStyles.Float, inv, out var hue))
        {
            double Pct(int i) => parts.Length > i && double.TryParse(parts[i], System.Globalization.NumberStyles.Float, inv, out var p) ? Math.Clamp(p, 0, 100) / 100 : 1;
            Current = new ThemeDef(id!, "Custom", ((hue % 360) + 360) % 360, false, 0, Pct(3), Pct(4));
            if (parts.Length > 2 && double.TryParse(parts[2], System.Globalization.NumberStyles.Float, inv, out var h2)) Grad = Math.Clamp(h2, 0, 100);
        }
        else
        {   // a ready-made colour, optionally with a fade: "blue:40"
            if (parts.Length > 1 && double.TryParse(parts[1], System.Globalization.NumberStyles.Float, inv, out var gs)) Grad = Math.Clamp(gs, 0, 100);
            if (parts[0] == "accent" && WindowsAccentHue() is { } ah) Current = new ThemeDef("accent", "Windows accent", ah, false, 0);
            else Current = All.FirstOrDefault(t => string.Equals(t.Id, parts[0], StringComparison.OrdinalIgnoreCase)) ?? All.First(t => t.Id == "black");
        }
        FullId = Current.Kind == 0 && Grad >= 0 && !Current.Id.StartsWith("custom:") ? Current.Id + ":" + (int)Grad : Current.Id;
    }

    /// The hue of the accent colour picked in Windows Settings > Personalisation > Colours (stored by DWM as 0xAABBGGRR).
    public static double? WindowsAccentHue()
    {
        if (Microsoft.Win32.Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\DWM")?.GetValue("AccentColor") is not int abgr) return null;
        ToHsl((abgr & 255) / 255.0, ((abgr >> 8) & 255) / 255.0, ((abgr >> 16) & 255) / 255.0, out double h, out double s, out _);
        return s < 0.1 ? null : h;   // a grey accent has no hue to follow
    }

    public static Color Col(uint rgb, int a = 255) => ColFor(Current, rgb, a);

    /// The settings window's colours (DeskMode's look): they wear the colour themes; Blue, Black, White and Glass keep DeskMode's own blue.
    public static Color Ui(uint rgb, int a = 255) => Current.Kind == 0 ? Col(rgb, a) : Color.FromArgb((byte)a, (byte)(rgb >> 16), (byte)(rgb >> 8), (byte)rgb);

    /// The accent in two tones, light to deep: Pink #FFCCF9 to #F61693, every other theme a light and a deep shade of its own accent.
    public static Brush Duo()
    {
        var g = Current.Soft ? new LinearGradientBrush(Color.FromRgb(0xFF, 0xCC, 0xF9), Color.FromRgb(0xF6, 0x16, 0x93), 45) : new LinearGradientBrush(Col(0xCCEEFF), Col(0x1690F6), 45);
        g.Freeze(); return g;
    }

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
            else if (blue && l < 0.5) { r = g = b = white ? 1 - l * 0.3 : l * (t.Kind == 3 ? 0.3 : 0.5); if (t.Kind == 3) al *= 0.24; }   // backgrounds go neutral; glass: mostly the blurred wallpaper, a faint dark veil (the suite's one glass look, same numbers as DeskBar core.cpp)
            else if (blue && white) FromHsl(h, s, l * 0.72, out r, out g, out b);   // accents need contrast on a light surface
            else if (white && s < 0.1 && l < 0.35) r = g = b = 1 - l;        // dark greys
        }
        else if (t.Hue >= 0 && blue)
        {
            if (t.Soft && l >= 0.5)
            {   // the pink text and accents: on the line between the two pinks, the lighter blues near #FFCCF9, the deeper ones near #F61693
                double k = Math.Clamp((0.73 - l) / 0.15, 0, 1);
                return Color.FromArgb((byte)Math.Round(al * 255), (byte)Math.Round(0xFF + (0xF6 - 0xFF) * k), (byte)Math.Round(0xCC + (0x16 - 0xCC) * k), (byte)Math.Round(0xF9 + (0x93 - 0xF9) * k));
            }
            else { h = t.Hue + (h - 225) * 0.25; if (t.Soft) { s = Math.Min(s * 0.5, 0.5); l = l < 0.5 ? 0.52 + l * 0.22 : Math.Min(0.9, l + 0.14); } }
            if (h < 0) h += 360; if (h >= 360) h -= 360;
            s *= t.Sat; if (l < 0.5) l *= t.Val;   // vividness for everything blue, brightness only for the dark backgrounds (accents keep their contrast)
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

    private static Brush Fade(Color a, Color b) { var g = new LinearGradientBrush(a, b, 0); g.Freeze(); return g; }

    private static SolidColorBrush Solid(uint rgb, int a = 255) { var br = new SolidColorBrush(Col(rgb, a)); br.Freeze(); return br; }

    /// Fills the application's brush resources from the current theme (the XAML refers to them with DynamicResource).
    public static void Apply()
    {
        var r = Application.Current.Resources;
        var win = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(0.55, 1) };
        win.GradientStops.Add(new GradientStop(Current.Kind == 0 && Grad > 0 ? Shift(Col(0x1257B8), Grad, 0.002) : Col(0x1257B8), 0)); win.GradientStops.Add(new GradientStop(Col(0x0A4096), 0.5)); win.GradientStops.Add(new GradientStop(Col(0x072C70), 1));
        win.Freeze();
        bool fade = Current.Kind == 0 && Grad > 0;   // the fade runs left to right in the window, the toolbar and the title strip alike, so no band is darker than its neighbours
        r["WinBg"] = fade ? Fade(Col(0x0A4096), Shift(Col(0x0A4096), Grad, 0.0015)) : win;
        r["BarBg"] = Current.Kind == 0 && Grad > 0 ? Fade(Col(0x003D91), Shift(Col(0x003D91), Grad, 0.0015)) : Solid(0x003D91, 0xFF); r["BarLine"] = Solid(0xFFFFFF, 0x18);
        r["TabBg"] = r["BarBg"] is LinearGradientBrush tg ? new SolidColorBrush(tg.GradientStops[0].Color) : r["BarBg"];   // the active tab is a small piece of the toolbar: a fade squeezed into it would run blue to violet in 200 px
        r["StripBg"] = fade ? Fade(Col(0x002A6A), Shift(Col(0x002A6A), Grad, 0.0015)) : Solid(0x002A6A, 0xFF);   // title bar + tab strip: a step darker, so the active tab (BarBg) reads as part of the toolbar below
        r["PaneBg"] = Solid(0x001030, 0x30);
        r["Fg"] = Solid(0xFFFFFF); r["FgDim"] = Solid(0xFFFFFF, 0xB8); r["FgFaint"] = Solid(0xFFFFFF, 0x80);
        r["Accent"] = Duo(); r["OnAccent"] = Solid(0x000000);
        r["Card"] = Solid(0xFFFFFF, 0x16); r["CardHover"] = Solid(0xFFFFFF, 0x26); r["CardLine"] = Solid(0xFFFFFF, 0x20);
        r["Select"] = Solid(0x4CC2FF, 0x40); r["SelectLine"] = Solid(0x4CC2FF, 0x90);
        r["Thumb"] = Solid(0xFFFFFF, 0x60); r["ThumbHover"] = Solid(0xFFFFFF, 0xA0);
        r["Error"] = Solid(0xFF8A8A);
        var menu = Col(0x0A4096); var mb = new SolidColorBrush(Color.FromRgb(menu.R, menu.G, menu.B)); mb.Freeze(); r["MenuBg"] = mb;   // menus are always opaque, even in the glass theme
        if (Current.Kind == 1)
        {   // black = Windows' dark mode: flat greys and a neutral grey selection (the blue accent is only for focus rings and small touches)
            SolidColorBrush Raw(uint rgb, byte a = 255) { var b = new SolidColorBrush(Color.FromArgb(a, (byte)(rgb >> 16), (byte)(rgb >> 8), (byte)rgb)); b.Freeze(); return b; }
            r["WinBg"] = Raw(0x1F1F1F); r["BarBg"] = Raw(0x2A2A2A); r["TabBg"] = Raw(0x2A2A2A); r["StripBg"] = Raw(0x1A1A1A); r["BarLine"] = Raw(0xFFFFFF, 0x12); r["PaneBg"] = Raw(0x191919); r["MenuBg"] = Raw(0x2B2B2B);
            r["Card"] = Raw(0xFFFFFF, 0x0F); r["CardHover"] = Raw(0xFFFFFF, 0x1C); r["CardLine"] = Raw(0xFFFFFF, 0x16);
            r["Select"] = Raw(0xFFFFFF, 0x20); r["SelectLine"] = Raw(0xFFFFFF, 0x2E);
        }
    }
}

/// The Desk suite's colours (4.0), in %LOCALAPPDATA%\Desk: theme.txt is the one theme every app wears; theme-<app>.txt, when present, is an app's
/// own colour ("Same colour for every app" off, in the Desk settings window, which is DeskBar's). DeskDrawer (always running) follows changes live.
public static class SharedTheme
{
    public const string App = "deskdrawer";
    public static string Dir => System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Desk");
    public static string File => System.IO.Path.Combine(Dir, "theme.txt");
    static string Own => System.IO.Path.Combine(Dir, "theme-" + App + ".txt");
    static string? ReadFile(string f) { try { var t = System.IO.File.ReadAllText(f).Trim(); return t.Length is > 0 and < 200 ? t : null; } catch { return null; } }
    /// This app's colour: its own if it has one, else the suite's.
    public static string? Read() => ReadFile(Own) ?? ReadFile(File);
    /// A colour picked in this app: its own when the apps have separate colours, else the suite's.
    public static void Write(string id) { try { System.IO.Directory.CreateDirectory(Dir); var f = System.IO.File.Exists(Own) ? Own : File; if (ReadFile(f) != id) System.IO.File.WriteAllText(f, id); } catch { } }
}

/// One settings window for the Desk suite: DeskBar's. Settings here opens it at this app's page when DeskBar is running (false = it isn't: use our own).
public static class DeskSettings
{
    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string cls, string? title);
    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] static extern uint RegisterWindowMessageW(string name);
    [System.Runtime.InteropServices.DllImport("user32.dll")] static extern IntPtr SendMessageTimeoutW(IntPtr h, uint m, IntPtr w, IntPtr l, uint flags, uint ms, out IntPtr result);
    const long Ack = 0x44534B;   // DeskBar 4.0 answers this (ui_wnd.cpp kOpenSettingsAck); an older DeskBar or its stand-in bar answers 0
    [System.Runtime.InteropServices.DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out int pid);
    [System.Runtime.InteropServices.DllImport("user32.dll")] static extern bool AllowSetForegroundWindow(int pid);
    /// page: the page's name in DeskBar's settings ("deskdrawer", "desktune", "colours").
    public static bool Open(string page)
    {
        if (Environment.GetEnvironmentVariable("DESK_OWN_SETTINGS") != null) return false;   // tests and pictures of our own window
        int code = page switch { "deskdrawer" => 1, "desktune" => 2, "colours" => 3, _ => 0 }; uint msg = RegisterWindowMessageW("DeskOpenSettings");
        // every DeskBar window gets asked until one says it opened the window: a plain post to the first one found did nothing with DeskBar 3.x
        // (which doesn't know the message) and the Settings button looked dead
        IntPtr h = IntPtr.Zero;
        for (int n = 0; n < 64 && (h = FindWindowExW(IntPtr.Zero, h, "DB3.Wnd", null)) != IntPtr.Zero; n++)
        {
            GetWindowThreadProcessId(h, out int pid); AllowSetForegroundWindow(pid);
            if (SendMessageTimeoutW(h, msg, (IntPtr)code, IntPtr.Zero, 0x0002 /* SMTO_ABORTIFHUNG */, 1000, out var r) != IntPtr.Zero && (long)r == Ack) return true;
        }
        return false;
    }
}
