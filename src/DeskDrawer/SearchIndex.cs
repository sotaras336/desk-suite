using System.Globalization;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Text;

namespace DeskDrawer;

/// How a search query is read and how names are scored, shared by the live walk and the index lookup so both rank alike.
/// Words are folded the way DeskBar folds its index: lower case, accents removed ("Εργασία" = "εργασια"), final sigma = sigma.
public sealed class Matcher
{
    public readonly string[] Tokens; public readonly string Folded;
    public Matcher(string query) { Folded = Fold(query); Tokens = Folded.Split(new[] { ' ', '\\', '/', '*', '?', '\t' }, StringSplitOptions.RemoveEmptyEntries); }

    public static string Fold(string s)
    {
        string d; try { d = s.Normalize(NormalizationForm.FormKD); } catch (ArgumentException) { d = s; }   // a lone surrogate in a file name
        var sb = new StringBuilder(d.Length);
        foreach (char c in d) { if (CharUnicodeInfo.GetUnicodeCategory(c) == UnicodeCategory.NonSpacingMark) continue; char l = char.ToLowerInvariant(c); sb.Append(l == 'ς' ? 'σ' : l); }
        return sb.ToString();
    }

    /// Plain ASCII text is compared in place (no copy, case-insensitive); anything else is folded once first.
    private readonly ref struct Text
    {
        private readonly ReadOnlySpan<char> _span; private readonly string? _folded;
        public Text(ReadOnlySpan<char> s) { _span = s; _folded = s.IsEmpty || System.Text.Ascii.IsValid(s) ? null : Fold(s.ToString()); }
        public bool Has(string tok) => !_span.IsEmpty && (_folded == null ? _span.IndexOf(tok, StringComparison.OrdinalIgnoreCase) >= 0 : _folded.Contains(tok, StringComparison.Ordinal));
    }

    /// Every word must be in the name, or in the folders between the search start and the item ("report 2024"); at least one has to be in the name itself.
    public bool IsMatch(ReadOnlySpan<char> name, ReadOnlySpan<char> folders)
    {
        if (Tokens.Length == 0) return false;
        var n = new Text(name); bool any = false, all = true;
        foreach (var t in Tokens) { if (n.Has(t)) any = true; else all = false; }
        if (!any) return false; if (all) return true;
        var f = new Text(folders);
        foreach (var t in Tokens) if (!n.Has(t) && !f.Has(t)) return false;
        return true;
    }

    /// Higher is better: the exact name, then names that start with the first word, then word starts, then anywhere; nearer, shorter, folders and your own files first.
    public double Score(string name, bool isDir, int depth, string fullDir, bool hidden) => ScoreFolded(Fold(name), isDir, depth, fullDir, hidden);

    public double ScoreFolded(string n, bool isDir, int depth, string fullDir, bool hidden)
    {
        string stem = n; if (!isDir) { int dot = n.LastIndexOf('.'); if (dot > 0) stem = n[..dot]; }
        string t0 = Tokens[0]; bool all = Tokens.All(t => n.Contains(t, StringComparison.Ordinal));
        double v = stem == Folded ? 120 : n.StartsWith(t0, StringComparison.Ordinal) ? 100 : WordStart(n, t0) ? 85 : n.Contains(t0, StringComparison.Ordinal) ? 55 : 45;
        if (!all) v -= 8;
        v += isDir ? 3 : 0; v -= Math.Min(10, n.Length * 0.08) + Math.Min(12, depth * 0.8) + (hidden ? 4 : 0);
        if (fullDir.Length > 0)
        {
            string up = fullDir;
            bool app = up.Contains("\\AppData\\", StringComparison.OrdinalIgnoreCase), sys = up.StartsWith("C:\\Windows", StringComparison.OrdinalIgnoreCase) || up.Contains("\\ProgramData", StringComparison.OrdinalIgnoreCase) || up.Contains("\\Program Files", StringComparison.OrdinalIgnoreCase);
            bool noise = up.Contains("\\node_modules", StringComparison.OrdinalIgnoreCase) || up.Contains("\\.git", StringComparison.OrdinalIgnoreCase) || up.Contains("\\site-packages", StringComparison.OrdinalIgnoreCase) || up.Contains("\\WinSxS", StringComparison.OrdinalIgnoreCase);
            if (up.StartsWith(Home, StringComparison.OrdinalIgnoreCase) && !app) v += 5; else if (app || sys) v -= 6;
            if (noise) v -= 10;
        }
        return v;
    }

    private static readonly string Home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
    private static bool WordStart(string n, string t) { for (int i = n.IndexOf(t, StringComparison.Ordinal); i >= 0; i = n.IndexOf(t, i + 1, StringComparison.Ordinal)) if (i == 0 || !char.IsLetterOrDigit(n[i - 1])) return true; return false; }
}

/// DeskBar's file index, when DeskBar is installed: a read-only map of %LOCALAPPDATA%\DeskBar3\fileindex.&lt;time&gt;.bin, which lists every file and folder on
/// the PC (layout in DeskBar's fileindex.cpp). Looking through it takes tens of milliseconds where walking every disk takes minutes. Without DeskBar, or
/// before its first scan has finished, DeskDrawer walks the disks live instead. Everything read from the file is range-checked first.
public sealed unsafe class SharedIndex : IDisposable
{
    private MemoryMappedFile? _mmf; private MemoryMappedViewAccessor? _view; private byte* _base;
    private uint _count; private uint* _noff, _ooff, _parent; private byte* _dir, _low, _orig; private long _lowBytes; private string[] _roots = Array.Empty<string>();
    private const uint kRoot = 0x80000000;
    public string FileName = ""; public DateTime Built;
    public int Count => (int)_count;

    // A map must never be unmapped while a search is reading it (that would be an access violation, i.e. DeskDrawer closing on you): whoever gets one with
    // Acquire() must give it back with Done(); a retired map (replaced by a newer index, or released for sleep) is unmapped when its last user is done.
    private static SharedIndex? _cached; private static readonly object Gate = new(); private int _users; private bool _retired;
    /// The newest usable index (kept open between searches), or null. Call Done() on it when finished.
    public static SharedIndex? Acquire()
    {
        lock (Gate)
        {
            try
            {
                string dir = Environment.GetEnvironmentVariable("DB3_DATA") is { Length: > 0 } d ? d : System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DeskBar3");
                if (Directory.Exists(dir))
                    foreach (var f in Directory.GetFiles(dir, "fileindex.*.bin").OrderByDescending(x => x, StringComparer.Ordinal))
                    {
                        if (_cached != null && _cached.FileName == f) break;   // still the newest
                        var ix = new SharedIndex(); if (ix.TryMap(f)) { _cached?.Retire(); _cached = ix; break; }
                        ix.Dispose();
                    }
            }
            catch { }
            if (_cached != null) _cached._users++;
            return _cached;
        }
    }
    public void Done() { lock (Gate) { if (--_users == 0 && _retired) Dispose(); } }
    private void Retire() { _retired = true; if (_users == 0) Dispose(); }
    /// Lets go of the map (DeskDrawer is going to sleep in the background).
    public static void Release() { lock (Gate) { _cached?.Retire(); _cached = null; } }

    private bool TryMap(string file)
    {
        try
        {
            var fs = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete); long size = fs.Length; if (size < 56) { fs.Dispose(); return false; }
            _mmf = MemoryMappedFile.CreateFromFile(fs, null, 0, MemoryMappedFileAccess.Read, HandleInheritability.None, false);
            _view = _mmf.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read); byte* p = null; _view.SafeMemoryMappedViewHandle.AcquirePointer(ref p); _base = p + _view.PointerOffset;
            // header: magic[8] version count rootCount pad (u32 each) lowBytes origBytes rootBytes fileBytes (u64 each), packed
            if (!(_base[0] == 'D' && _base[1] == 'B' && _base[2] == '3' && _base[3] == 'I' && _base[4] == 'D' && _base[5] == 'X' && _base[6] == '2' && _base[7] == 0)) return false;
            uint version = *(uint*)(_base + 8), count = *(uint*)(_base + 12), rootCount = *(uint*)(_base + 16); ulong lowB = *(ulong*)(_base + 24), origB = *(ulong*)(_base + 32), rootB = *(ulong*)(_base + 40), fileB = *(ulong*)(_base + 48);
            if (version != 2 || count == 0 || count > 0x7FFFFFF0u || lowB == 0 || origB == 0 || lowB > 0xFFFFFFFFul || origB > 0xFFFFFFFFul) return false;
            ulong c = count, oN = 56, oO = oN + 4 * (c + 1), oP = oO + 4 * (c + 1), oD = oP + 4 * c, oL = oD + c, oR = oL + lowB + origB, end = oR + rootB;
            if (end != (ulong)size || fileB != (ulong)size) return false;
            _count = count; _noff = (uint*)(_base + oN); _ooff = (uint*)(_base + oO); _parent = (uint*)(_base + oP); _dir = _base + oD; _low = _base + oL; _orig = _low + lowB; _lowBytes = (long)lowB;
            if (_noff[0] != 0 || _noff[count] != lowB || _ooff[0] != 0 || _ooff[count] != origB || _low[lowB - 1] != (byte)'\n' || _orig[origB - 1] != (byte)'\n') return false;
            for (uint i = 0; i < count; i++)
            {
                if (_noff[i + 1] <= _noff[i] || _noff[i + 1] > lowB || _ooff[i + 1] <= _ooff[i] || _ooff[i + 1] > origB) return false;
                uint par = _parent[i]; if ((par & kRoot) != 0) { if ((par & ~kRoot) >= rootCount) return false; } else if (par >= i) return false;
            }
            var roots = new List<string>(); byte* r = _base + oR, re = r + rootB;
            for (uint i = 0; i < rootCount; i++) { byte* z = r; while (z < re && *z != 0) z++; if (z >= re) return false; roots.Add(Encoding.UTF8.GetString(r, (int)(z - r))); r = z + 1; }
            if (r != re) return false;
            _roots = roots.ToArray(); FileName = file; Built = File.GetLastWriteTime(file); return true;
        }
        catch { return false; }
    }

    private string Name(uint i) => Encoding.UTF8.GetString(_orig + _ooff[i], (int)(_ooff[i + 1] - _ooff[i] - 1));
    private string Folded(uint i) => Encoding.UTF8.GetString(_low + _noff[i], (int)(_noff[i + 1] - _noff[i] - 1));
    public bool IsDir(int i) => _dir[i] != 0;
    public string Path(int id)
    {
        var parts = new List<string>(); uint p = (uint)id;
        for (;;) { parts.Add(Name(p)); uint q = _parent[p]; if ((q & kRoot) != 0) { parts.Add(_roots[q & ~kRoot]); parts.Reverse(); return string.Join('\\', parts); } p = q; }
    }
    private int Depth(int id) { int n = 1; uint p = _parent[id]; while ((p & kRoot) == 0) { n++; p = _parent[p]; } foreach (char c in _roots[p & ~kRoot]) if (c == '\\') n++; return n; }

    private uint EntryAt(long offset)   // the entry whose name holds this byte of the list
    {
        uint lo = 0, hi = _count;   // last entry whose start is <= offset
        while (hi - lo > 1) { uint mid = (lo + hi) / 2; if (_noff[mid] <= offset) lo = mid; else hi = mid; }
        return lo;
    }

    /// The best matches, best first. Same rules as the live search: all words in the name or in a folder above it.
    public List<(int Id, double Score)> Find(Matcher m, int keep, CancellationToken ct)
    {
        var toks = m.Tokens; var tb = toks.Select(t => Encoding.UTF8.GetBytes(t)).ToArray();
        var order = Enumerable.Range(0, toks.Length).OrderByDescending(i => tb[i].Length).ToArray();
        var hits = new Dictionary<uint, double>();
        foreach (int a in order)
        {
            var needle = tb[a]; if (needle.Length < 2) continue; long pos = 0;
            while (pos < _lowBytes)
            {
                ct.ThrowIfCancellationRequested();
                int len = (int)Math.Min(32L << 20, _lowBytes - pos);
                int at = new ReadOnlySpan<byte>(_low + pos, len).IndexOf(needle);
                if (at < 0) { if (pos + len >= _lowBytes) break; pos += len - (needle.Length - 1); continue; }
                uint ei = EntryAt(pos + at); pos = _noff[ei + 1];
                if (hits.ContainsKey(ei)) continue;
                string name = Folded(ei); bool ok = true;
                for (int k = 0; k < toks.Length && ok; k++)
                {
                    if (k == a || name.Contains(toks[k], StringComparison.Ordinal)) continue;
                    ok = false; for (uint q = _parent[ei]; (q & kRoot) == 0; q = _parent[q]) if (Folded(q).Contains(toks[k], StringComparison.Ordinal)) { ok = true; break; }
                }
                if (!ok) continue;
                hits[ei] = m.ScoreFolded(name, _dir[ei] != 0, 0, "", false);
                if (hits.Count >= 2_000_000) break;
            }
            if (toks.Length == 1 || hits.Count >= 30) break;   // one pass is enough unless a word only exists in a folder name above the file
        }
        var top = hits.OrderByDescending(h => h.Value).Take(Math.Max(keep, 600)).Select(h => (Id: (int)h.Key, Score: h.Value)).ToList();
        for (int k = 0; k < top.Count; k++)   // the best by name, then where they live decides (your own folders before Windows and AppData)
        {
            string p = Path(top[k].Id); string dir = System.IO.Path.GetDirectoryName(p) ?? "";
            top[k] = (top[k].Id, m.ScoreFolded(Folded((uint)top[k].Id), IsDir(top[k].Id), Depth(top[k].Id), dir + "\\", false));
        }
        return top.OrderByDescending(h => h.Score).Take(keep).ToList();
    }

    public void Dispose()
    {
        if (_view != null) { if (_base != null) _view.SafeMemoryMappedViewHandle.ReleasePointer(); _view.Dispose(); } _mmf?.Dispose(); _base = null; _view = null; _mmf = null;
    }
}
