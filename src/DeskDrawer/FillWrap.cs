using System.Windows;
using System.Windows.Controls;

namespace DeskDrawer;

/// Cards in rows that always use the full width: as many columns as fit at MinItemWidth, then every card stretched to share the row
/// (a plain WrapPanel leaves a ragged empty strip on the right).
public sealed class FillWrap : Panel
{
    public double MinItemWidth { get; set; } = 240;
    public double Gap { get; set; } = 10;

    private (int Cols, double W) Layout(double width)
    {
        if (double.IsInfinity(width) || width <= 0) return (Math.Max(1, InternalChildren.Count), MinItemWidth);
        int cols = Math.Max(1, (int)((width + Gap) / (MinItemWidth + Gap)));
        return (cols, (width - Gap * (cols - 1)) / cols);
    }

    protected override Size MeasureOverride(Size available)
    {
        var (cols, w) = Layout(available.Width);
        double height = 0, rowH = 0;
        for (int i = 0; i < InternalChildren.Count; i++)
        {
            var c = InternalChildren[i]; c.Measure(new Size(w, double.PositiveInfinity));
            rowH = Math.Max(rowH, c.DesiredSize.Height);
            if (i % cols == cols - 1 || i == InternalChildren.Count - 1) { height += rowH + Gap; rowH = 0; }
        }
        return new Size(double.IsInfinity(available.Width) ? cols * (w + Gap) : available.Width, Math.Max(0, height - Gap));
    }

    protected override Size ArrangeOverride(Size final)
    {
        var (cols, w) = Layout(final.Width);
        double y = 0, rowH = 0;
        for (int i = 0; i < InternalChildren.Count; i++)
        {
            var c = InternalChildren[i]; int col = i % cols;
            if (col == 0 && i > 0) { y += rowH + Gap; rowH = 0; }
            for (int j = i - col; j < Math.Min(i - col + cols, InternalChildren.Count); j++) rowH = Math.Max(rowH, InternalChildren[j].DesiredSize.Height);   // even rows
            c.Arrange(new Rect(col * (w + Gap), y, w, rowH));
        }
        return final;
    }
}
