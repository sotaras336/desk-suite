using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;

namespace DeskDrawer;

/// Save / Open dialogs in other apps (Chrome, Word...) belong to those apps and can't be swapped for DeskDrawer. Instead, while one is in front, a small
/// "DeskDrawer" button sits under it; picking one of your open DeskDrawer folders makes the dialog jump there (it types the folder into the dialog's
/// file-name box and presses Open/Save, which is how those dialogs navigate, then puts the file name back).
public static partial class DialogSwitch
{
    private static Window? _chip;
    private static IntPtr _dialog, _hook;
    private static WinEventDelegate? _proc;   // kept alive: Windows calls it
    private static Func<List<string>>? _folders;
    private static readonly DispatcherTimer Follow = new() { Interval = TimeSpan.FromMilliseconds(250) };

    /// folders: the places to offer (open tabs, newest first). Call once on the UI thread.
    public static void Start(Func<List<string>> folders)
    {
        _folders = folders;
        _proc = (_, _, hwnd, _, _, _, _) => Check(hwnd);
        _hook = SetWinEventHook(3 /*EVENT_SYSTEM_FOREGROUND*/, 3, IntPtr.Zero, _proc, 0, 0, 0 /*WINEVENT_OUTOFCONTEXT*/);
        Follow.Tick += (_, _) => Place();
    }

    private static void Check(IntPtr fg)
    {
        if (_chip != null && fg == new WindowInteropHelper(_chip).Handle) return;   // clicking our own button
        if (_picker != null && fg == new WindowInteropHelper(_picker).Handle) return;   // our own picker is in front: leave everything as it is
        if (TakeOver && Taken.Contains(fg)) return;   // already handled (hidden, or answered): no button under it
        if (IsFileDialog(fg)) { _dialog = fg; if (TakeOver && TryTakeOver(fg)) { Hide(); return; } Show(); } else { if (fg != _dialog) Hide(); Retry(fg, 1); }
    }

    /// The common Save / Open dialog: a #32770 window holding the shell's folder view (DUIViewWndClassName) and a file-name box.
    public static bool IsFileDialog(IntPtr h)
    {
        if (h == IntPtr.Zero || ClassOf(h) != "#32770") return false;
        GetWindowThreadProcessId(h, out uint pid); if (pid == Environment.ProcessId) return false;
        bool view = false;
        EnumChildWindows(h, (c, _) => { if (ClassOf(c) is "DUIViewWndClassName" or "SHELLDLL_DefView") view = true; return !view; }, IntPtr.Zero);   // the Vista-style view, or the classic one (older programs such as Quartus)
        return view && FindNameBox(h) != IntPtr.Zero;
    }

    /// The file-name box: an Edit inside a ComboBox (Windows 11 hosts it in the dialog's own layout, so the classic control 1148 is gone);
    /// not the address bar's (inside ComboBoxEx32) and not the search box.
    public static IntPtr FindNameBox(IntPtr dialog)
    {
        IntPtr edit = IntPtr.Zero;
        EnumChildWindows(dialog, (c, _) =>
        {
            if (ClassOf(c) != "Edit") return true;
            IntPtr p = GetParent(c);
            if (GetDlgCtrlID(c) == 1148 || GetDlgCtrlID(p) == 1148 || GetDlgCtrlID(c) == 1152 || (ClassOf(p) == "ComboBox" && ClassOf(GetParent(p)) != "ComboBoxEx32")) { edit = c; return false; }
            return true;
        }, IntPtr.Zero);
        return edit;
    }

    private static void Show()
    {
        if (_chip == null)
        {
            var label = new TextBlock { Text = "  DeskDrawer folders  ", FontFamily = new FontFamily("Segoe Fluent Icons, Segoe MDL2 Assets, Segoe UI"), FontSize = 13, VerticalAlignment = VerticalAlignment.Center };
            label.SetResourceReference(TextBlock.ForegroundProperty, "Fg");
            var pill = new Border { Child = label, Padding = new Thickness(14, 7, 14, 7), CornerRadius = new CornerRadius(16), BorderThickness = new Thickness(1), Cursor = System.Windows.Input.Cursors.Hand };
            pill.SetResourceReference(Border.BackgroundProperty, "MenuBg"); pill.SetResourceReference(Border.BorderBrushProperty, "Accent");
            _chip = new Window { WindowStyle = WindowStyle.None, AllowsTransparency = true, Background = Brushes.Transparent, Topmost = true, ShowInTaskbar = false, ShowActivated = false, SizeToContent = SizeToContent.WidthAndHeight, Content = pill, ResizeMode = ResizeMode.NoResize };
            _chip.SourceInitialized += (_, _) => { var h = new WindowInteropHelper(_chip).Handle; SetWindowLong(h, -20, GetWindowLong(h, -20) | 0x08000000 | 0x80); };   // WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW: never steals focus from the dialog
            pill.MouseLeftButtonUp += (_, _) => OpenMenu(pill);
        }
        Place(); _chip.Show(); Follow.Start();
    }

    private static void Hide() { _dialog = IntPtr.Zero; Follow.Stop(); _chip?.Hide(); }

    /// Sits just under the dialog's bottom-left corner, and goes away with it.
    private static void Place()
    {
        if (_chip == null) return;
        if (_dialog == IntPtr.Zero || !IsWindow(_dialog) || !IsWindowVisible(_dialog) || IsIconic(_dialog)) { Hide(); return; }
        GetWindowRect(_dialog, out var r);
        var src = PresentationSource.FromVisual(_chip); double k = src?.CompositionTarget?.TransformToDevice.M11 ?? 1;
        _chip.Left = (r.Left + 12) / k; _chip.Top = (r.Bottom + 6) / k;
    }

    private static void OpenMenu(FrameworkElement anchor)
    {
        var m = new ContextMenu { PlacementTarget = anchor, Placement = System.Windows.Controls.Primitives.PlacementMode.Top };
        var list = _folders?.Invoke() ?? new();
        foreach (var f in list)
        {
            var path = f; var mi = new MenuItem { Header = path.Replace("_", "__"), ToolTip = path };
            mi.Click += (_, _) => Jump(_dialog, path); m.Items.Add(mi);
        }
        if (m.Items.Count == 0) m.Items.Add(new MenuItem { Header = "Open a folder in DeskDrawer first", IsEnabled = false });
        m.IsOpen = true;
    }

    /// Typing a folder path into the file-name box and pressing the default button makes these dialogs open that folder.
    public static bool Jump(IntPtr dialog, string folder)
    {
        IntPtr edit = FindNameBox(dialog), ok = IntPtr.Zero;
        EnumChildWindows(dialog, (c, _) => { if (ClassOf(c) == "Button" && GetDlgCtrlID(c) == 1) { ok = c; return false; } return true; }, IntPtr.Zero);
        if (edit == IntPtr.Zero) return false;
        var old = new StringBuilder(1024); SendMessage(edit, 0x000D /*WM_GETTEXT*/, (IntPtr)old.Capacity, old);
        SendMessage(edit, 0x000C /*WM_SETTEXT*/, IntPtr.Zero, folder);
        if (ok != IntPtr.Zero) SendMessage(ok, 0x00F5 /*BM_CLICK*/, IntPtr.Zero, IntPtr.Zero); else SendMessage(dialog, 0x0111 /*WM_COMMAND*/, (IntPtr)1 /*IDOK*/, IntPtr.Zero);
        string keep = old.ToString();
        var t = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(350) };
        t.Tick += (_, _) => { t.Stop(); if (IsWindow(edit)) SendMessage(edit, 0x000C, IntPtr.Zero, keep); SetForegroundWindow(dialog); };   // the file name you had typed comes back
        t.Start();
        return true;
    }

    private static string ClassOf(IntPtr h) { var sb = new StringBuilder(64); GetClassName(h, sb, sb.Capacity); return sb.ToString(); }

    private delegate void WinEventDelegate(IntPtr hook, uint ev, IntPtr hwnd, int obj, int child, uint thread, uint time);
    private delegate bool EnumProc(IntPtr h, IntPtr l);
    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] private static extern IntPtr SetWinEventHook(uint min, uint max, IntPtr mod, WinEventDelegate f, uint pid, uint tid, uint flags);
    [DllImport("user32.dll")] private static extern bool EnumChildWindows(IntPtr h, EnumProc f, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] private static extern int GetDlgCtrlID(IntPtr h);
    [DllImport("user32.dll")] private static extern IntPtr GetParent(IntPtr h);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] private static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] private static extern int GetWindowLong(IntPtr h, int i);
    [DllImport("user32.dll")] private static extern int SetWindowLong(IntPtr h, int i, int v);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, string l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, StringBuilder l);
    [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, IntPtr l);
}
