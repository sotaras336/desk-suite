using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace DeskDrawer;

/// DeskDrawer.exe --makeicon DeskDrawer.ico : draws the app icon (a folder with a D) at several sizes and writes a PNG-compressed .ico. Run once; the .ico is part of the project.
public static class IconMaker
{
    private static byte[] Render(int px)
    {
        var dv = new DrawingVisual();
        using (var dc = dv.RenderOpen())
        {
            double s = px;
            if (Folder(dc, s)) goto done;
            dc.DrawRoundedRectangle(new SolidColorBrush(Color.FromRgb(0x20, 0x20, 0x20)), null, new Rect(0, 0, s, s), s * 0.22, s * 0.22);
            // File Explorer's colours: amber back with its tab, warm yellow front, blue on top
            var back = new SolidColorBrush(Color.FromRgb(0xD9, 0x9A, 0x16));
            var front = new LinearGradientBrush(Color.FromRgb(0xFF, 0xD9, 0x5C), Color.FromRgb(0xF4, 0xB3, 0x1C), 90);
            var blue = new LinearGradientBrush(Color.FromRgb(0x3F, 0xA6, 0xF5), Color.FromRgb(0x0F, 0x62, 0xC9), 90);
            var folder = new PathGeometry();
            var f = new PathFigure { StartPoint = new Point(s * 0.17, s * 0.30), IsClosed = true };
            f.Segments.Add(new LineSegment(new Point(s * 0.40, s * 0.30), true)); f.Segments.Add(new LineSegment(new Point(s * 0.48, s * 0.38), true));
            f.Segments.Add(new LineSegment(new Point(s * 0.83, s * 0.38), true)); f.Segments.Add(new LineSegment(new Point(s * 0.83, s * 0.76), true)); f.Segments.Add(new LineSegment(new Point(s * 0.17, s * 0.76), true));
            folder.Figures.Add(f);
            dc.DrawGeometry(back, null, folder);
            dc.DrawRectangle(front, null, new Rect(new Point(s * 0.17, s * 0.43), new Point(s * 0.83, s * 0.76)));
            // a four-point sparkle on the folder's front: concave sides, so each point is a quadratic curve through a spot near the centre
            double cx = s * 0.5, cy = s * 0.595, r = s * 0.15, k = s * 0.022;
            var star = new PathFigure { StartPoint = new Point(cx, cy - r), IsClosed = true };
            star.Segments.Add(new QuadraticBezierSegment(new Point(cx + k, cy - k), new Point(cx + r, cy), true));
            star.Segments.Add(new QuadraticBezierSegment(new Point(cx + k, cy + k), new Point(cx, cy + r), true));
            star.Segments.Add(new QuadraticBezierSegment(new Point(cx - k, cy + k), new Point(cx - r, cy), true));
            star.Segments.Add(new QuadraticBezierSegment(new Point(cx - k, cy - k), new Point(cx, cy - r), true));
            var sg = new PathGeometry(); sg.Figures.Add(star);
            dc.DrawGeometry(blue, null, sg);
            done:;
        }
        var rtb = new RenderTargetBitmap(px, px, 96, 96, PixelFormats.Pbgra32); rtb.Render(dv);
        var enc = new PngBitmapEncoder(); enc.Frames.Add(BitmapFrame.Create(rtb));
        using var ms = new MemoryStream(); enc.Save(ms); return ms.ToArray();
    }

    /// The current logo: a plain yellow folder in the Windows family (no plate behind it) with a blue pill centred on its front.
    /// Own shapes and proportions; only the palette is borrowed.
    private static bool Folder(DrawingContext dc, double s)
    {
        Brush Lin(byte r1, byte g1, byte b1, byte r2, byte g2, byte b2) => new LinearGradientBrush(Color.FromRgb(r1, g1, b1), Color.FromRgb(r2, g2, b2), 90);
        dc.PushTransform(new ScaleTransform(1.12, 1.12, s / 2, s * 0.51));   // fill the square edge to edge, like other apps' icons (it looked small on the taskbar)
        double L = s * 0.06, R = s * 0.94, top = s * 0.16, bottom = s * 0.86, rad = s * 0.07;

        // back sheet with its tab (amber)
        var back = new PathFigure { StartPoint = new Point(L, top + rad), IsClosed = true };
        back.Segments.Add(new ArcSegment(new Point(L + rad, top), new Size(rad, rad), 0, false, SweepDirection.Clockwise, true));
        back.Segments.Add(new LineSegment(new Point(s * 0.36, top), true));
        back.Segments.Add(new LineSegment(new Point(s * 0.45, top + s * 0.09), true));
        back.Segments.Add(new LineSegment(new Point(R - rad, top + s * 0.09), true));
        back.Segments.Add(new ArcSegment(new Point(R, top + s * 0.09 + rad), new Size(rad, rad), 0, false, SweepDirection.Clockwise, true));
        back.Segments.Add(new LineSegment(new Point(R, bottom - rad), true));
        back.Segments.Add(new LineSegment(new Point(L, bottom - rad), true));
        var bg = new PathGeometry(); bg.Figures.Add(back);
        dc.DrawGeometry(Lin(0xF0, 0xB1, 0x2A, 0xD9, 0x92, 0x10), null, bg);

        // front sheet (warm yellow), slightly lower so the tab shows
        var front = new RectangleGeometry(new Rect(new Point(L, s * 0.33), new Point(R, bottom)), rad, rad);
        dc.DrawGeometry(Lin(0xFF, 0xDC, 0x6A, 0xF8, 0xC1, 0x2E), null, front);

        // the blue pocket on the bottom edge, like a real file's drawer front: rounded top, cut off by the folder's own bottom, with a handle slot
        double pl = s * 0.27, pw = s * 0.46, pt = s * 0.60, prad = s * 0.06;
        var pocket = new CombinedGeometry(GeometryCombineMode.Intersect, front, new RectangleGeometry(new Rect(pl, pt, pw, bottom - pt + prad), prad, prad));
        dc.DrawGeometry(Lin(0x45, 0xA8, 0xF5, 0x12, 0x66, 0xCC), null, pocket);
        dc.DrawRoundedRectangle(new SolidColorBrush(Color.FromArgb(0x60, 0xFF, 0xFF, 0xFF)), null, new Rect(pl + prad, pt + s * 0.018, pw - prad * 2, Math.Max(1, s * 0.012)), s * 0.006, s * 0.006);
        double hw = pw * 0.42, hh = Math.Max(1.5, s * 0.03);
        dc.DrawRoundedRectangle(new SolidColorBrush(Color.FromRgb(0x0B, 0x3F, 0x8A)), null, new Rect(s / 2 - hw / 2, pt + (bottom - pt) * 0.45, hw, hh), hh / 2, hh / 2);
        dc.Pop();
        return true;
    }

    public static void Make(string file)
    {
        int[] sizes = { 16, 24, 32, 48, 64, 128, 256 }; var pngs = sizes.Select(Render).ToList();
        using var w = new BinaryWriter(File.Create(file));
        w.Write((short)0); w.Write((short)1); w.Write((short)sizes.Length);
        int offset = 6 + 16 * sizes.Length;
        for (int i = 0; i < sizes.Length; i++)
        {
            w.Write((byte)(sizes[i] == 256 ? 0 : sizes[i])); w.Write((byte)(sizes[i] == 256 ? 0 : sizes[i])); w.Write((byte)0); w.Write((byte)0); w.Write((short)1); w.Write((short)32);
            w.Write(pngs[i].Length); w.Write(offset); offset += pngs[i].Length;
        }
        foreach (var p in pngs) w.Write(p);
    }
}
