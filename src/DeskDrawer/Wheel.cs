using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Controls;

namespace DeskDrawer;

/// WPF's ScrollViewer moves a whole notch (3 lines) for every wheel message, whatever its size. A precision touchpad sends dozens of tiny ones per swipe,
/// so a short swipe flew across the folder. This scrolls by the message's own delta (120 = one notch = Windows' "lines per notch"), times Settings > Speed > Scrolling.
static class Wheel
{
    public static double Speed = 1;
    public static double Px(int delta) { int lines = SystemParameters.WheelScrollLines; return delta / 120.0 * 16 * (lines is > 0 and <= 20 ? lines : 3) * Speed; }

    /// Messages that arrive before the next layout pass build on each other, not on the stale VerticalOffset.
    static readonly ConditionalWeakTable<ScrollViewer, StrongBox<double?>> Pending = new();

    public static void Scroll(ScrollViewer sv, int delta)
    {
        var p = Pending.GetValue(sv, s => { var b = new StrongBox<double?>(); s.ScrollChanged += (_, _) => b.Value = null; return b; });
        if (sv.ScrollableHeight <= 0) return;
        double to = Math.Clamp((p.Value ?? sv.VerticalOffset) - Px(delta), 0, sv.ScrollableHeight);
        p.Value = to; sv.ScrollToVerticalOffset(to);
    }

    public static void Attach(ScrollViewer sv) => sv.PreviewMouseWheel += (_, e) => { if (e.Handled || sv.ScrollableHeight <= 0) return; Scroll(sv, e.Delta); e.Handled = true; };
}
