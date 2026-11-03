using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace DeskTune;

/// DeskTune.exe --makeicon DeskTune.ico: a gauge (blue-to-green arc, white needle pointing to "light") at several sizes, written as a PNG-compressed .ico.
public static class IconMaker
{
    private static byte[] Render(int px)
    {
        var dv = new DrawingVisual();
        using (var dc = dv.RenderOpen())
        {
            double s = px, cx = s / 2, cy = s * 0.56, r = s * 0.40, w = Math.Max(2, s * 0.13);
            // the dial: a 240-degree arc, blue on the "heavy" side fading to green on the "light" side
            Point At(double deg, double rad) => new(cx + rad * Math.Cos(deg * Math.PI / 180), cy - rad * Math.Sin(deg * Math.PI / 180));
            var arc = new PathFigure { StartPoint = At(210, r) };
            arc.Segments.Add(new ArcSegment(At(-30, r), new Size(r, r), 0, true, SweepDirection.Clockwise, true));
            var geo = new PathGeometry(); geo.Figures.Add(arc);
            var pen = new Pen(new LinearGradientBrush(Color.FromRgb(0x2F, 0x8B, 0xF0), Color.FromRgb(0x3C, 0xD6, 0x8C), 0), w) { StartLineCap = PenLineCap.Round, EndLineCap = PenLineCap.Round };
            dc.DrawGeometry(null, pen, geo);
            // the needle, pointing well into the green
            var needle = new Pen(new SolidColorBrush(Color.FromRgb(0xF4, 0xF6, 0xFA)), Math.Max(1.5, s * 0.075)) { StartLineCap = PenLineCap.Round, EndLineCap = PenLineCap.Round };
            dc.DrawLine(needle, new Point(cx, cy), At(20, r * 0.72));
            dc.DrawEllipse(new SolidColorBrush(Color.FromRgb(0xF4, 0xF6, 0xFA)), null, new Point(cx, cy), s * 0.08, s * 0.08);
        }
        var rtb = new RenderTargetBitmap(px, px, 96, 96, PixelFormats.Pbgra32); rtb.Render(dv);
        var enc = new PngBitmapEncoder(); enc.Frames.Add(BitmapFrame.Create(rtb));
        using var ms = new MemoryStream(); enc.Save(ms); return ms.ToArray();
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
