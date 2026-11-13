using System.ComponentModel;
using System.IO;
using System.Windows.Media;

namespace DeskDrawer;

/// One row of the file list. The icon and the type name arrive later from the icon worker, so the list shows up at once.
public sealed class FileEntry : INotifyPropertyChanged
{
    public string Name { get; }
    public string FullPath { get; }
    public bool IsDir { get; }
    public DateTime Modified { get; }
    public string ModifiedText { get; }
    private long _size; private string _sizeText = "";
    public long Size => _size;
    public string SizeText => _sizeText;
    /// A folder's size arrives later from FolderSizes (it has to add up everything inside).
    public void SetFolderSize(long bytes) { _size = bytes; _sizeText = FormatSize(bytes); Raise(nameof(Size)); Raise(nameof(SizeText)); }
    public string Ext { get; }
    public string Folder => Path.GetDirectoryName(FullPath) ?? "";   // the Folder column of search results
    public bool IsHidden { get; }   // shown dimmed, like File Explorer
    /// "Group by": the heading this item sits under and that heading's place in the order (Today before Yesterday...).
    public double Score { get; set; }   // search results: how well the name fits (higher is better)
    public string Group { get; set; } = "";
    public int GroupRank { get; set; }

    private ImageSource? _icon; private string _type; private bool _renaming; private string _editName = "";
    /// Inline rename: the name cell swaps its label for a text box while this is true.
    private bool _cut;
    public bool IsCut { get => _cut; set { if (_cut != value) { _cut = value; Raise(nameof(IsCut)); } } }   // cut to the clipboard, not pasted yet: drawn dimmed
    public bool IsRenaming { get => _renaming; set { _renaming = value; Raise(nameof(IsRenaming)); } }
    public string EditName { get => _editName; set { _editName = value; Raise(nameof(EditName)); } }
    public ImageSource? Icon { get => _icon; set { _icon = value; Raise(nameof(Icon)); Raise(nameof(Big)); } }

    private ImageSource? _thumb;
    public int ThumbPx { get; private set; }
    /// The picture for tiles and icon views: a real thumbnail once ThumbLoader has one, the small icon until then.
    public ImageSource? Big => _thumb ?? _icon;
    public void SetThumb(ImageSource s, int px) { _thumb = s; ThumbPx = px; Raise(nameof(Big)); }
    public string TypeText { get => _type; set { _type = value; Raise(nameof(TypeText)); } }

    public FileEntry(FileSystemInfo fi)
    {
        Name = fi.Name; FullPath = fi.FullName; IsDir = (fi.Attributes & FileAttributes.Directory) != 0;
        Ext = IsDir ? "" : Path.GetExtension(Name).ToLowerInvariant();
        IsHidden = (fi.Attributes & (FileAttributes.Hidden | FileAttributes.System)) != 0;
        _size = fi is FileInfo f ? SafeLength(f) : 0;
        Modified = SafeTime(fi);
        ModifiedText = Modified == DateTime.MinValue ? "" : Modified.ToString("g");
        _sizeText = IsDir ? "" : FormatSize(_size);
        _type = IsDir ? "File folder" : Ext.Length > 1 ? Ext.TrimStart('.').ToUpperInvariant() + " file" : "File";
    }

    private static long SafeLength(FileInfo f) { try { return f.Length; } catch { return 0; } }
    private static DateTime SafeTime(FileSystemInfo fi) { try { return fi.LastWriteTime; } catch { return DateTime.MinValue; } }

    /// Picks the unit that fits: "512 B", "745 KB", "19.3 MB", "2.4 GB".
    public static string FormatSize(long bytes)
    {
        if (bytes < 1024) return bytes + " B";
        double v = bytes / 1024.0; string[] unit = { "KB", "MB", "GB", "TB" }; int i = 0;
        while (v >= 1024 && i < 3) { v /= 1024; i++; }
        return (i == 0 ? Math.Round(v).ToString("N0") : v.ToString(v >= 100 ? "N0" : "N1")) + " " + unit[i];
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void Raise(string n) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(n));
}
