using System.ComponentModel;
using System.Windows.Data;

namespace DeskDrawer;

/// Everything one tab remembers: where it is, its back/forward history, its list and its filter text.
public sealed class TabState : INotifyPropertyChanged
{
    public string? Current;
    public readonly Stack<string> Back = new(), Fwd = new();
    public ListCollectionView? View;
    public List<FileEntry>? Entries;
    public string Filter = "", Kind = "";   // Kind = the Filter menu's choice (Folders, Pictures...)
    public string ViewSig = "";   // which sort / grouping the list was last arranged with (each folder has its own)
    public CancellationTokenSource? Cts;
    public bool IconsRequested, Loading;
    public string? Error;
    public HashSet<string> SelectAfterLoad = new(StringComparer.OrdinalIgnoreCase);
    public string? RenameAfterLoad;
    /// A search tab: results come in best match first until you click a column to sort them yourself; the banner above them says what is being searched.
    public bool ByRelevance, Searching, UsedIndex, ForceLive;   // UsedIndex: the results came from DeskBar's list; ForceLive: the "Search live" button was pressed
    public CancellationTokenSource? SearchStop;   // the Stop button: ends the search, keeps what it found
    public string SearchTitle = "", SearchDetail = "";

    private string _title = "New tab"; private bool _active;
    public string Title { get => _title; set { _title = value; Raise(nameof(Title)); } }
    public bool IsActive { get => _active; set { _active = value; Raise(nameof(IsActive)); } }
    private System.Windows.Media.ImageSource? _icon;
    /// The open folder's own shell icon; null on Home, which shows a house instead.
    public System.Windows.Media.ImageSource? Icon { get => _icon; set { _icon = value; Raise(nameof(Icon)); } }
    private string _glyph = "\uE8B7";
    /// Shown while there is no Icon: a page's own symbol (house for Home, monitor for This PC...).
    public string Glyph { get => _glyph; set { _glyph = value; Raise(nameof(Glyph)); } }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void Raise(string n) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(n));
}
