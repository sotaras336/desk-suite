using System.Windows.Input;

namespace DeskDrawer;

/// The small rules behind tabs and split view, kept apart from the window so --selftest can check them without one.
internal static class TabMath
{
    /// Split view turning on: the tab for the other side is the next one, else the one before; -1 = there is only one tab, open a new one.
    public static int PartnerFor(int count, int active) => count < 2 || active < 0 || active >= count ? -1 : active + 1 < count ? active + 1 : active - 1;

    /// Ctrl+1 to Ctrl+8 pick that tab, Ctrl+9 the last one (as in a browser); -1 = no such tab.
    public static int ByNumber(int digit, int count) => count <= 0 || digit < 1 || digit > 9 ? -1 : digit == 9 ? count - 1 : digit <= count ? digit - 1 : -1;

    /// Ctrl+Tab, Ctrl+Page Up / Page Down: round the ring of tabs.
    public static int Cycle(int index, int dir, int count) => count <= 0 ? -1 : ((index + dir) % count + count) % count;

    /// Ctrl+Shift+Page Up / Page Down: one place left or right, stopping at the ends.
    public static int Step(int index, int dir, int count) => count <= 0 ? -1 : Math.Clamp(index + dir, 0, count - 1);

    /// A saved tab number after the tabs whose folders have gone were left out. kept[i] = the saved number of the i-th tab that came back. -1 = that tab didn't come back.
    public static int Remap(IReadOnlyList<int> kept, int saved) { for (int i = 0; i < kept.Count; i++) if (kept[i] == saved) return i; return -1; }

    /// The split saved last time comes back only if its other tab came back too and isn't the active one.
    public static int RestorePartner(int partner, int active, int count) => partner >= 0 && partner < count && partner != active ? partner : -1;

    /// How much of the width the left side has (from the two sides' widths), kept between a fifth and four fifths so neither side vanishes.
    public static double Share(double left, double right) => left <= 0 || right <= 0 ? 0.5 : Math.Clamp(left / (left + right), 0.2, 0.8);

    /// The tab and split view keys. null = not one of them.
    public static string? KeyFor(Key key, bool ctrl, bool shift, bool alt)
    {
        if (alt) return null;
        if (key == Key.F6 && !ctrl) return "other side";
        if (!ctrl) return null;
        if (key is Key.Oem5 or Key.OemBackslash && !shift) return "split";   // Ctrl+\ (Oem5 on most keyboards, the extra key left of Z on others)
        if (key == Key.PageDown) return shift ? "move right" : "next";
        if (key == Key.PageUp) return shift ? "move left" : "previous";
        if (!shift && key is >= Key.D1 and <= Key.D9) return "tab " + (key - Key.D0);
        if (!shift && key is >= Key.NumPad1 and <= Key.NumPad9) return "tab " + (key - Key.NumPad0);
        return null;
    }

    /// The keys that, pressed while the other side of a split has the keyboard, make it the side you work on first (so Delete deletes what is selected there).
    public static bool WorksOnPeer(Key key, bool ctrl, bool shift, bool alt) =>
        (!ctrl && !alt && key is Key.Delete or Key.F2 or Key.Space)
        || (ctrl && !shift && key is Key.C or Key.X or Key.V)
        || (ctrl && shift && key == Key.N)
        || (alt && key is Key.Enter or Key.Left or Key.Right or Key.Up);
}
