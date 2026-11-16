using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Input;

namespace DeskDrawer;

/// Dragging the selection out of a DeskDrawer window that is BEHIND another app (Chrome on top of a maximised DeskDrawer, files dragged into Chrome):
/// the press must not bring DeskDrawer forward, or it covers the very window you are dragging to. File Explorer behaves the same way.
///
/// How: before a click, Windows asks the window under the pointer whether the click should activate it (WM_MOUSEACTIVATE). For a left press on a row that
/// is already selected, while another window is in front, we answer "no, but still give me the click". The press then reaches the list as usual and a drag
/// can start. If the button comes up without a drag it was a plain click, and the window comes forward then, as any clicked window does.
/// Nothing in between may take the keyboard focus (WPF's Focus() is a Win32 SetFocus, which activates the window): see the lazy branch in List_PreviewDown.
public partial class MainWindow
{
    private bool _lazyPress;                  // this press was answered with "don't activate": no focus changes until it ends
    internal string TestTrace = "";              // what the last press did, for the UI test
    internal Point? TestPressAt;              // the UI test presses here (window coordinates) instead of at the real pointer

    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] private static extern bool GetCursorPos(out POINT p);
    [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr h, int msg, IntPtr w, IntPtr l);   // these three: the UI test only
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr h, int msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] private static extern IntPtr GetWindow(IntPtr h, uint cmd);
    [StructLayout(LayoutKind.Sequential)] private struct POINT { public int X, Y; }

    /// Would a left press at this point (window coordinates) be a grab of the selection? The same rule List_PreviewDown uses for a grab, minus everything
    /// that needs the window in front: a rename being typed, the picker, the card pages, Ctrl / Shift (they change the selection, they don't drag it).
    internal bool IsGrabOfSelection(Point inWindow)
    {
        if (_pick != null || _tab?.Current == null || InHome || _mqActive) return false;
        if ((Keyboard.Modifiers & (ModifierKeys.Control | ModifierKeys.Shift | ModifierKeys.Alt)) != 0) return false;
        if (_tab.Entries?.Any(x => x.IsRenaming) ?? false) return false;
        if (InputHitTest(inWindow) is not DependencyObject hit || !IsUnder(hit, List)) return false;
        return EntryUnder(hit) is { } en && List.SelectedItems.Contains(en);
    }

    private static bool IsUnder(DependencyObject d, DependencyObject ancestor)
    {
        for (DependencyObject? x = d; x != null; x = x is System.Windows.Media.Visual or System.Windows.Media.Media3D.Visual3D ? System.Windows.Media.VisualTreeHelper.GetParent(x) : LogicalTreeHelper.GetParent(x))
            if (x == ancestor) return true;
        return false;
    }

    /// Windows' question before a click. Returns true (and the answer) only for the one case above; everything else is left to WPF.
    private bool OnMouseActivate(IntPtr l, out IntPtr answer)
    {
        const int HTCLIENT = 1, WM_LBUTTONDOWN = 0x201, MA_NOACTIVATE = 3;
        answer = IntPtr.Zero; _lazyPress = false;   // a new press: whatever the last one was, it is over
        long lp = l.ToInt64(); int hit = (int)(lp & 0xFFFF), mouseMsg = (int)((lp >> 16) & 0xFFFF);
        if (hit != HTCLIENT || mouseMsg != WM_LBUTTONDOWN || GetForegroundWindow() == Hwnd) return false;
        Point at;
        if (TestPressAt is { } t) at = t;
        else { if (!GetCursorPos(out var c)) return false; try { at = PointFromScreen(new Point(c.X, c.Y)); } catch { return false; } }
        if (!IsGrabOfSelection(at)) return false;
        _lazyPress = true; answer = (IntPtr)MA_NOACTIVATE; return true;
    }

    /// The press ended without a drag: it was a click, so the window comes forward now.
    private void EndLazyPress(bool comeForward)
    {
        if (!_lazyPress) return; _lazyPress = false;
        if (comeForward) { SetForegroundWindow(Hwnd); List.Focus(); }
    }
}
