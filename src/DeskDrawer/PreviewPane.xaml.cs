using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace DeskDrawer;

/// The right-hand pane: a picture (real thumbnail), the start of a text file, or a folder summary, plus the file's details.
public partial class PreviewPane : UserControl
{
    private static readonly HashSet<string> TextExt = new(StringComparer.OrdinalIgnoreCase)
    {
        ".txt", ".md", ".log", ".json", ".xml", ".csv", ".ini", ".cfg", ".conf", ".yaml", ".yml", ".toml", ".bat", ".cmd", ".ps1", ".psm1", ".sh", ".py", ".js", ".mjs", ".ts", ".tsx", ".jsx",
        ".cs", ".csproj", ".sln", ".cpp", ".c", ".h", ".hpp", ".java", ".kt", ".go", ".rs", ".html", ".htm", ".css", ".xaml", ".sql", ".gitignore", ".env", ".properties", ".rc", ".manifest", ".reg", ".inf", ".srt",
    };
    private static readonly HashSet<string> ImageExt = new(StringComparer.OrdinalIgnoreCase) { ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tif", ".tiff", ".webp", ".ico", ".jfif", ".heic" };

    private int _version;
    private const int MaxText = 64 * 1024;

    public PreviewPane() { InitializeComponent(); Clear("Select a file to preview"); }

    private void Clear(string hint)
    {
        PicCard.Visibility = Visibility.Visible; TextCard.Visibility = Visibility.Collapsed; Pic.Source = null; PicHint.Text = hint; PicHint.Visibility = Visibility.Visible;
    }

    public static string Human(long b)
    {
        string[] u = { "bytes", "KB", "MB", "GB", "TB" }; double v = b; int i = 0;
        while (v >= 1024 && i < u.Length - 1) { v /= 1024; i++; }
        return i == 0 ? $"{b:N0} bytes" : $"{v:0.#} {u[i]}" + (b >= 1024 ? $" ({b:N0} bytes)" : "");
    }

    private void Row(string label, string value)
    {
        if (string.IsNullOrEmpty(value)) return;
        var g = new Grid { Margin = new Thickness(0, 0, 0, 7) };
        g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(92) }); g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        var l = new TextBlock { Text = label, FontSize = 12 }; l.SetResourceReference(TextBlock.ForegroundProperty, "FgDim");
        var v = new TextBlock { Text = value, FontSize = 12, TextWrapping = TextWrapping.Wrap }; Grid.SetColumn(v, 1);
        g.Children.Add(l); g.Children.Add(v); Details.Children.Add(g);
    }

    /// What to show for the current selection. folder = the open folder (used when nothing is selected).
    public void Show(IReadOnlyList<FileEntry> sel, string? folder, int itemCount)
    {
        int v = ++_version; Details.Children.Clear();
        if (sel.Count == 0)
        {
            Clear(folder == null ? "" : "Select a file to preview");
            TitleText.Text = folder == null ? "" : (Path.GetFileName(folder.TrimEnd('\\')) is { Length: > 0 } n ? n : folder);
            Subtitle.Text = itemCount == 1 ? "1 item" : $"{itemCount:N0} items"; return;
        }
        if (sel.Count > 1)
        {
            Clear("");
            PicCard.Visibility = Visibility.Collapsed;
            TitleText.Text = $"{sel.Count:N0} items selected";
            long bytes = sel.Where(x => !x.IsDir).Sum(x => x.Size); int dirs = sel.Count(x => x.IsDir);
            Subtitle.Text = (dirs > 0 ? $"{dirs:N0} folder{(dirs == 1 ? "" : "s")}, " : "") + $"{sel.Count - dirs:N0} file{(sel.Count - dirs == 1 ? "" : "s")}";
            Row("Total size", bytes > 0 ? Human(bytes) : ""); return;
        }

        var f = sel[0];
        TitleText.Text = f.Name; Subtitle.Text = f.TypeText;
        Row("Type", f.TypeText); Row("Size", f.SizeText); Row("Modified", f.ModifiedText); Row("Location", Path.GetDirectoryName(f.FullPath) ?? "");
        try { Row("Created", (f.IsDir ? Directory.GetCreationTime(f.FullPath) : File.GetCreationTime(f.FullPath)).ToString("g")); } catch { }

        if (!f.IsDir && TextExt.Contains(f.Ext.Length > 0 ? f.Ext : f.Name))
        {
            PicCard.Visibility = Visibility.Collapsed; TextCard.Visibility = Visibility.Visible; Body.Text = "Loading…";
            Task.Run(() => ReadText(f.FullPath)).ContinueWith(t => Dispatcher.BeginInvoke(() => { if (v == _version) Body.Text = t.Result; }));
            return;
        }

        PicCard.Visibility = Visibility.Visible; TextCard.Visibility = Visibility.Collapsed; Pic.Source = null; PicHint.Text = "Loading…"; PicHint.Visibility = Visibility.Visible;
        string path = f.FullPath; bool image = !f.IsDir && ImageExt.Contains(f.Ext), dir = f.IsDir;
        Thumbs.OnSta(() =>
        {
            var bs = Thumbs.Get(path, 512);
            string dims = ""; string folderInfo = "";
            if (image) { try { using var s = File.OpenRead(path); var fr = BitmapDecoder.Create(s, BitmapCreateOptions.DelayCreation | BitmapCreateOptions.IgnoreColorProfile, BitmapCacheOption.None).Frames[0]; dims = $"{fr.PixelWidth} × {fr.PixelHeight}"; } catch { } }
            if (dir) { try { int n = 0; foreach (var _ in Directory.EnumerateFileSystemEntries(path)) { if (++n >= 10000) break; } folderInfo = n >= 10000 ? "10,000+ items" : n == 1 ? "1 item" : $"{n:N0} items"; } catch { folderInfo = "Can't open"; } }
            Dispatcher.BeginInvoke(() =>
            {
                if (v != _version) return;
                Pic.Source = bs; PicHint.Visibility = bs == null ? Visibility.Visible : Visibility.Collapsed; if (bs == null) PicHint.Text = "No preview";
                Row("Dimensions", dims); Row("Contains", folderInfo);
            });
        });
    }

    private static string ReadText(string path)
    {
        try
        {
            using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            var buf = new byte[MaxText + 1]; int n = fs.Read(buf, 0, buf.Length);
            for (int i = 0; i < Math.Min(n, 4096); i++) if (buf[i] == 0) return "(binary file)";
            using var sr = new StreamReader(new MemoryStream(buf, 0, Math.Min(n, MaxText)), Encoding.UTF8, true);
            var text = sr.ReadToEnd();
            return n > MaxText ? text + "\n…" : text;
        }
        catch (Exception ex) { return "Can't read this file: " + ex.Message; }
    }
}
