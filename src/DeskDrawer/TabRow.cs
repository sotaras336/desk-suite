using System.Windows;
using System.Windows.Controls;

namespace DeskDrawer;

/// The tab strip's row: tabs share the width like a browser's (up to MaxItem each, narrower as more open) and leave Reserve px for the new-tab button.
/// A plain StackPanel let many tabs run past the window, under the new-tab and window buttons.
public sealed class TabRow : Panel
{
    public double MaxItem { get; set; } = 260;
    public double Reserve { get; set; } = 40;

    double Each(double width) { int n = InternalChildren.Count; return n == 0 ? 0 : double.IsInfinity(width) ? MaxItem : Math.Max(0, Math.Min(MaxItem, (width - Reserve) / n)); }

    protected override Size MeasureOverride(Size avail)
    {
        double w = Each(avail.Width), h = 0;
        foreach (UIElement c in InternalChildren) { c.Measure(new Size(w, avail.Height)); h = Math.Max(h, c.DesiredSize.Height); }
        return new Size(w * InternalChildren.Count, h);
    }

    protected override Size ArrangeOverride(Size final)
    {
        double w = InternalChildren.Count == 0 ? 0 : final.Width / InternalChildren.Count, x = 0;
        foreach (UIElement c in InternalChildren) { c.Arrange(new Rect(x, 0, w, final.Height)); x += w; }
        return final;
    }
}
