using System.Windows;
using System.Windows.Controls;

namespace DeskTune;

/// WPF's ScrollViewer moves a whole notch (3 lines) for every wheel message, whatever its size. A precision touchpad sends dozens of tiny ones per swipe, so
/// a short swipe flew across the page. This scrolls by the message's own delta instead: 120 = one notch = what Windows' "lines per notch" says.
static class Wheel
{
    public static double Px(int delta) { int lines = SystemParameters.WheelScrollLines; return delta / 120.0 * 16 * (lines is > 0 and <= 20 ? lines : 3); }

    public static void Attach(ScrollViewer sv)
    {
        double? pending = null;   // messages that arrive before the next layout pass must build on each other, not on the stale VerticalOffset
        sv.ScrollChanged += (_, _) => pending = null;
        sv.PreviewMouseWheel += (_, e) =>
        {
            if (sv.ScrollableHeight <= 0) return;
            double to = Math.Clamp((pending ?? sv.VerticalOffset) - Px(e.Delta), 0, sv.ScrollableHeight);
            pending = to; sv.ScrollToVerticalOffset(to); e.Handled = true;
        };
    }
}
