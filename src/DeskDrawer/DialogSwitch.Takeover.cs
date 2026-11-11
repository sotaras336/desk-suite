using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;

namespace DeskDrawer;

/// "Use DeskDrawer for other apps' Open and Save windows" (off by default). When another app opens a Save / Open dialog, that dialog is hidden, DeskDrawer's own picker
/// (MainWindow) opens in its place, and the answer is typed into the hidden dialog's file-name box and its Open / Save button is pressed: the app gets exactly what it
/// would have got from Windows' dialog. Anything unusual (no file-name box, a folder picker, an error) leaves the normal dialog on screen.
public static partial class DialogSwitch
{
    private static bool _takeOver;
    public static bool TakeOver { get => _takeOver; set { _takeOver = value; if (value) { StartEarly(); After(8000, () => { if (_takeOver) MainWindow.PrewarmPicker(); }); } else StopEarly(); } }
    private static readonly HashSet<IntPtr> Taken = new();   // dialogs already handled once: never taken over twice
    private static readonly Dictionary<IntPtr, Mutex> Owned = new();   // one named lock per dialog: with two DeskDrawers running (a trial and the installed one) only one answers
    private static MainWindow? _picker;

    private static bool TryTakeOver(IntPtr dlg)
    {
        foreach (var h in Taken.Where(h => !IsWindow(h)).ToList()) { Taken.Remove(h); Faded.Remove(h); if (Owned.Remove(h, out var old)) old.Dispose(); }
        if (_picker != null || Taken.Contains(dlg)) return false;
        var d = Describe(dlg);
        var mx = new Mutex(true, "Local\\DeskDrawerPicker" + dlg.ToInt64(), out bool mine);
        if (!mine) { mx.Dispose(); return false; }   // another DeskDrawer already took this one
        Owned[dlg] = mx; Taken.Add(dlg);
        try
        {
            var places = _folders?.Invoke() ?? new();
            string start = Directory.Exists(d.Folder) ? d.Folder : places.FirstOrDefault() ?? Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
            GetWindowRect(dlg, out var r);
            var sw0 = System.Diagnostics.Stopwatch.StartNew();
            var p = MainWindow.ForPick(start, d.Name, d.Button, d.Types, d.TypeIndex, d.Title, r.Left, r.Top, r.Right, r.Bottom);
            _picker = p;
            ShowWindow(dlg, 0 /*SW_HIDE*/); FadeOut(dlg); KeepHidden(dlg);   // see-through as well: an app that shows its dialog again (Word, WinForms) can no longer flash it
            p.OnAnswer = pw => Answer(dlg, pw);
            long built = sw0.ElapsedMilliseconds;
            p.PickShow(); p.Dispatcher.BeginInvoke(() => Note(dlg, $"taken over, picker ready in {built} ms, drawn after {sw0.ElapsedMilliseconds} ms"), System.Windows.Threading.DispatcherPriority.ContextIdle);
            return true;
        }
        catch (Exception ex) { _picker = null; Miss(dlg, "the picker could not open: " + ex.Message); Reveal(dlg); return false; }
    }

    /// The picker was answered (or cancelled). Order matters, and getting it wrong is what hid the app: the answer goes into the dialog while the picker still owns the
    /// foreground (it is only moved off screen), then we wait until the dialog is really gone, hand the foreground to the app that asked, and only then close the picker.
    /// Closing first let Windows give the foreground to whatever window was next, and the app dropped behind it.
    private static void Answer(IntPtr dlg, MainWindow pw)
    {
        _picker = null;   // KeepHidden stops; a new dialog may be taken over again
        IntPtr owner = IsWindow(dlg) ? GetWindow(dlg, 4 /*GW_OWNER*/) : IntPtr.Zero;
        if (!IsWindow(dlg)) { pw.FinishPick(); return; }
        if (pw.Result is { Count: > 0 } paths) Fill(dlg, paths, pw.TypeIndex, follow: false, folder: pw.PickIsFolder); else PressCancel(dlg);
        WaitGone(dlg, owner, 0,
            gone: () => { BringBack(owner); pw.FinishPick(); },
            still: () => { pw.FinishPick(); Reveal(dlg); SetForegroundWindow(dlg); });   // the app has something to say (replace this file? bad name?): its own dialog shows it
    }

    /// Gone = the window is destroyed, or the app that owns it is usable again (its modal loop ended). Still there after 2.4 s = the app is asking something.
    private static void WaitGone(IntPtr dlg, IntPtr owner, int n, Action gone, Action still) => After(30, () => { if (!IsWindow(dlg) || (n > 3 && owner != IntPtr.Zero && IsWindowEnabled(owner) && !IsWindowVisible(dlg))) gone(); else if (n >= 80) still(); else WaitGone(dlg, owner, n + 1, gone, still); });

    [DllImport("user32.dll")] private static extern bool IsWindowEnabled(IntPtr h);

    /// Some apps show their dialog again by themselves (Word does, now and then): while the picker is open it is put away again.
    private static void KeepHidden(IntPtr dlg) => After(250, () => { if (_picker == null || !IsWindow(dlg)) return; if (IsWindowVisible(dlg)) ShowWindow(dlg, 0); KeepHidden(dlg); });

    /// The file patterns inside a file-type name such as "Word Documents (*.docx;*.doc)"; empty = all files.
    public static string[] PatternsOf(string type)
    {
        var m = Regex.Match(type, @"\(([^()]*)\)\s*$");
        string list = m.Success ? m.Groups[1].Value : type.Contains('*') ? type : "";
        var pats = list.Split(new[] { ';', ',', ' ' }, StringSplitOptions.RemoveEmptyEntries).Where(s => s.Contains('*') || s.Contains('?')).ToList();
        if (pats.Any(s => s == "*.*" || s == "*")) pats.Clear();   // "All files"
        return pats.ToArray();
    }

    /// What the dialog looks like from outside: the folder it is in, the file name it holds, the label of its default button, its file-type list (and which one is chosen), its title.
    public static (string Folder, string Name, string Button, string[] Patterns, string Title, string[] Types, int TypeIndex) Describe(IntPtr dlg)
    {
        string folder = "", button = "", filter = ""; IntPtr combo = IntPtr.Zero;
        EnumChildWindows(dlg, (c, _) =>
        {
            string cls = ClassOf(c);
            if (cls == "ToolbarWindow32") { var t = TextOf(c); int i = t.IndexOf(": ", StringComparison.Ordinal); if (i >= 0 && t.StartsWith("Address", StringComparison.OrdinalIgnoreCase)) folder = t[(i + 2)..]; }
            else if (cls == "Button" && GetDlgCtrlID(c) == 1) button = TextOf(c).Replace("&", "");
            else if (cls == "ComboBox" && GetDlgCtrlID(c) == 0x470) { combo = c; filter = TextOf(c); }   // "file type" box
            return true;
        }, IntPtr.Zero);
        var edit = FindNameBox(dlg);
        var types = new List<string>(); int index = -1;
        if (combo != IntPtr.Zero)
        {
            int n = (int)SendMessage(combo, 0x0146 /*CB_GETCOUNT*/, IntPtr.Zero, IntPtr.Zero);
            for (int i = 0; i < n && i < 60; i++) { var sb = new StringBuilder(512); SendMessage(combo, 0x0148 /*CB_GETLBTEXT*/, (IntPtr)i, sb); if (sb.Length > 0) types.Add(sb.ToString()); }
            index = (int)SendMessage(combo, 0x0147 /*CB_GETCURSEL*/, IntPtr.Zero, IntPtr.Zero);
            if (types.Count != n) { types.Clear(); index = -1; }   // an item with no text: don't guess
        }
        if (types.Count == 0 && filter.Length > 0) { types.Add(filter); index = 0; }
        if (index < 0 || index >= types.Count) index = types.Count > 0 ? 0 : -1;
        return (folder, edit == IntPtr.Zero ? "" : TextOf(edit), button, index >= 0 ? PatternsOf(types[index]) : Array.Empty<string>(), TextOf(dlg), types.ToArray(), index);
    }

    /// Chooses a file type in the dialog's own list, as if it had been picked there (a Save dialog uses it to decide the extension).
    private static void SetType(IntPtr dlg, int index)
    {
        IntPtr combo = IntPtr.Zero;
        EnumChildWindows(dlg, (c, _) => { if (ClassOf(c) == "ComboBox" && GetDlgCtrlID(c) == 0x470) { combo = c; return false; } return true; }, IntPtr.Zero);
        if (combo == IntPtr.Zero || (int)SendMessage(combo, 0x0147, IntPtr.Zero, IntPtr.Zero) == index) return;
        SendMessage(combo, 0x014E /*CB_SETCURSEL*/, (IntPtr)index, IntPtr.Zero);
        SendMessage(GetParent(combo), 0x0111 /*WM_COMMAND*/, (IntPtr)((1 /*CBN_SELCHANGE*/ << 16) | 0x470), combo);
    }

    /// Types the answer into the dialog and presses its default button. Several files in one folder: quoted full paths.
    public static void Fill(IntPtr dlg, List<string> paths, int typeIndex = -1, bool follow = true, bool folder = false)
    {
        IntPtr edit = FindNameBox(dlg), owner = GetWindow(dlg, 4 /*GW_OWNER*/);
        if (edit == IntPtr.Zero) { Reveal(dlg); return; }
        IntPtr ok = IntPtr.Zero;
        EnumChildWindows(dlg, (c, _) => { if (ClassOf(c) == "Button" && GetDlgCtrlID(c) == 1) { ok = c; return false; } return true; }, IntPtr.Zero);
        if (typeIndex >= 0) SetType(dlg, typeIndex);
        void Press(string text)
        {
            if (!IsWindow(edit)) return;
            SendMessage(edit, 0x000C /*WM_SETTEXT*/, IntPtr.Zero, text);
            if (ok != IntPtr.Zero) SendMessage(ok, 0x00F5 /*BM_CLICK*/, IntPtr.Zero, IntPtr.Zero); else SendMessage(dlg, 0x0111 /*WM_COMMAND*/, (IntPtr)1, IntPtr.Zero);
        }
        string dir = Path.GetDirectoryName(paths[0]) ?? "";
        bool several = paths.Count > 1 && paths.All(p => string.Equals(Path.GetDirectoryName(p), dir, StringComparison.OrdinalIgnoreCase));
        if (several) Press(string.Join(" ", paths.Select(p => "\"" + p + "\"")));
        else Press(paths[0]);   // files from different folders can't be picked together in these dialogs: the first one
        if (folder) After(350, () => { if (IsWindow(dlg)) Press(""); });   // a folder picker opens a typed folder instead of choosing it: pressed again with an empty name, it chooses the folder it is in
        // still there a moment later = it had something to say (replace this file? bad name?): show it again so the app's answer is visible
        if (follow) After(several ? 1500 : 1000, () =>
        {
            if (IsWindow(dlg)) { Reveal(dlg); SetForegroundWindow(dlg); }
            else BringBack(owner);   // the app gets the focus back
        });
    }

    /// Word's dialog is slow to build: the moment it first gets focus its folder view and file-name box may not exist yet, so look again a few times while it is in front.
    private static void Retry(IntPtr fg, int n)
    {
        if (n > 6 || fg == IntPtr.Zero || ClassOf(fg) != "#32770") return;
        GetWindowThreadProcessId(fg, out uint pid); if (pid == Environment.ProcessId) return;
        After(350, () => { if (!IsWindow(fg) || fg == _dialog || GetForegroundWindow() != fg) return; if (IsFileDialog(fg)) Check(fg); else Retry(fg, n + 1); });
    }

    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();

    // ---------- no flash of Windows' own dialog ----------
    // The foreground event comes after the dialog has already been drawn, so the original used to flash up before the picker. Instead, the moment a big dialog window is SHOWN
    // it is made fully see-through (a layered window with alpha 0: still there, just invisible), then recognised within a few tens of milliseconds and hidden for good. Anything that
    // turns out not to be an Open / Save dialog (or a folder picker) gets its colours back at once. A dialog that was taken over stays see-through until it is revealed again.

    private static readonly HashSet<IntPtr> Faded = new();
    private static WinEventDelegate? _early; private static IntPtr _earlyHook;

    private static void StartEarly()
    {
        if (_earlyHook != IntPtr.Zero) return;
        _early = (_, _, hwnd, obj, child, _, _) => { if (_takeOver && obj == 0 && child == 0) Early(hwnd); };
        _earlyHook = SetWinEventHook(0x8002 /*EVENT_OBJECT_SHOW*/, 0x8002, IntPtr.Zero, _early, 0, 0, 0);
    }

    private static void StopEarly() { if (_earlyHook != IntPtr.Zero) { UnhookWinEvent(_earlyHook); _earlyHook = IntPtr.Zero; } }

    private static void Early(IntPtr h)
    {
        if (_picker != null || Taken.Contains(h) || Faded.Contains(h) || ClassOf(h) != "#32770") return;
        GetWindowThreadProcessId(h, out uint pid); if (pid == Environment.ProcessId) return;
        GetWindowRect(h, out var r); if (r.Right - r.Left < 400 || r.Bottom - r.Top < 250) return;   // message boxes and the like are small: leave them alone
        if ((GetWindowLong(h, -20) & 0x80000) != 0) return;   // already layered by its own app: not ours to touch
        SetWindowLong(h, -20, GetWindowLong(h, -20) | 0x80000); SetLayeredWindowAttributes(h, 0, 0, 2); Faded.Add(h);
        Poll(h, 0);
    }

    private static void Poll(IntPtr h, int n)
    {
        After(40, () =>
        {
          try
          {
            if (!IsWindow(h)) { Faded.Remove(h); return; }
            if (Taken.Contains(h)) return;   // the foreground event got there first: it is hidden, and stays see-through until revealed
            if (IsFileDialog(h)) { _dialog = h; if (TryTakeOver(h)) { Hide(); return; } Miss(h, "not taken over (folder picker, picker already open, or another DeskDrawer has it)"); Unfade(h); return; }   // not taken over (a folder picker, or another DeskDrawer has it): back to normal
            if (n >= 2 && HasView(h) && FindNameBox(h) == IntPtr.Zero) { Unfade(h); return; }   // built, but no file-name box: some other dialog, give it back at once
            if (n >= 3 && !LooksLikeFileDialog(h)) { Unfade(h); return; }   // built without any of a file dialog's parts (a settings window, a wizard): back at once
            if (n == 38) Unfade(h);   // never invisible longer than 1.5 s: a slow dialog shows normally and is still taken over if it turns into a file dialog
            if (n >= 100) { if (Faded.Contains(h)) Unfade(h); Miss(h, "not a file dialog (or never finished building) after 4 s"); } else Poll(h, n + 1);
          }
          catch { Unfade(h); }   // whatever goes wrong, the dialog must never stay invisible
        });
    }

    /// A file dialog that is still being built already has one of these; any other dialog never does.
    private static bool LooksLikeFileDialog(IntPtr h) { int kids = 0; bool part = false; EnumChildWindows(h, (c, _) => { kids++; if (ClassOf(c) is "DUIViewWndClassName" or "SHELLDLL_DefView" or "ComboBoxEx32" or "CtrlNotifySink" or "DirectUIHWND") part = true; return !part; }, IntPtr.Zero); return part || kids == 0; }
    private static void FadeOut(IntPtr h)
    {
        if (Faded.Contains(h) || (GetWindowLong(h, -20) & 0x80000) != 0) return;
        SetWindowLong(h, -20, GetWindowLong(h, -20) | 0x80000); SetLayeredWindowAttributes(h, 0, 0, 2); Faded.Add(h);
    }
    private static bool HasView(IntPtr h) { bool v = false; EnumChildWindows(h, (c, _) => { if (ClassOf(c) is "DUIViewWndClassName" or "SHELLDLL_DefView") v = true; return !v; }, IntPtr.Zero); return v; }
    private static void Note(IntPtr h, string what) => Miss(h, what);

    /// takeover.log (next to settings.json): every dialog that looked like one but was left alone, with what it contained, so a miss can be understood instead of guessed.
    private static void Miss(IntPtr h, string why)
    {
        try
        {
            GetWindowThreadProcessId(h, out uint pid); string proc = "?"; try { proc = System.Diagnostics.Process.GetProcessById((int)pid).ProcessName; } catch { }
            var kids = new List<string>(); EnumChildWindows(h, (c, _) => { var k = ClassOf(c) + "#" + GetDlgCtrlID(c); if (!kids.Contains(k) && kids.Count < 40) kids.Add(k); return true; }, IntPtr.Zero);
            GetWindowRect(h, out var r);
            string path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DeskDrawer", "takeover.log");
            if (File.Exists(path) && new FileInfo(path).Length > 200_000) File.Delete(path);
            File.AppendAllText(path, $"{DateTime.Now:MM-dd HH:mm:ss} {proc} {r.Right - r.Left}x{r.Bottom - r.Top} \"{TextOf(h)}\": {why}; nameBox={(FindNameBox(h) != IntPtr.Zero)}; children: {string.Join(" ", kids)}{Environment.NewLine}");
        }
        catch { }
    }

    private static void Unfade(IntPtr h)
    {
        if (!Faded.Remove(h) || !IsWindow(h)) return;
        SetLayeredWindowAttributes(h, 0, 255, 2); SetWindowLong(h, -20, GetWindowLong(h, -20) & ~0x80000);
    }

    /// The original dialog back on screen (something to answer, or the picker failed).
    private static void Reveal(IntPtr h) { Unfade(h); ShowWindow(h, 5); }

    [DllImport("user32.dll")] private static extern bool UnhookWinEvent(IntPtr hook);
    [DllImport("user32.dll")] private static extern bool SetLayeredWindowAttributes(IntPtr h, uint key, byte alpha, uint flags);

    internal static bool PickerOpen => _picker != null;
    internal static void TestAnswer(string path) => _picker?.TestAnswer(path);
    internal static void TestCancel() => _picker?.Close();

    /// The app that asked for the dialog comes to the front again, restored if it ended up minimised.
    private static void BringBack(IntPtr owner)
    {
        if (owner == IntPtr.Zero || !IsWindow(owner)) return;
        if (IsIconic(owner)) ShowWindow(owner, 9 /*SW_RESTORE*/);
        SetForegroundWindow(owner);
    }

    private static void PressCancel(IntPtr dlg)
    {
        IntPtr cancel = IntPtr.Zero;
        EnumChildWindows(dlg, (c, _) => { if (ClassOf(c) == "Button" && GetDlgCtrlID(c) == 2) { cancel = c; return false; } return true; }, IntPtr.Zero);
        if (cancel != IntPtr.Zero) PostMessage(cancel, 0x00F5, IntPtr.Zero, IntPtr.Zero); else PostMessage(dlg, 0x0111, (IntPtr)2, IntPtr.Zero);
    }

    private static void After(int ms, Action a) { var t = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(ms) }; t.Tick += (_, _) => { t.Stop(); a(); }; t.Start(); }

    /// WM_GETTEXT, not GetWindowText: that one returns nothing for an edit or combo box that belongs to another process.
    private static string TextOf(IntPtr h) { var sb = new StringBuilder(1024); SendMessage(h, 0x000D, (IntPtr)sb.Capacity, sb); return sb.ToString(); }

    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr h, int m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] private static extern IntPtr GetWindow(IntPtr h, uint cmd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
}
