using System.Runtime.InteropServices;

namespace DeskTune;

/// The Settings button's way to the one Desk settings window (DeskSettings.Open in Theme.cs), checked against a pretend DeskBar window in this process.
/// DeskBar's side of the same message is checked by "DeskBar.exe --selftest" (launchtest.cpp); the names both sides must share by .github/launch-contract.py.
/// The same file is in DeskDrawer and DeskTune: change both.
static class LaunchCheck
{
    delegate IntPtr WndProc(IntPtr h, uint m, IntPtr w, IntPtr l);
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct WNDCLASSW { public uint style; public IntPtr proc; public int clsExtra, wndExtra; public IntPtr inst, icon, cursor, bg; public string? menu; public string name; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern ushort RegisterClassW(ref WNDCLASSW c);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool UnregisterClassW(string cls, IntPtr inst);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreateWindowExW(uint ex, string cls, string? title, uint style, int x, int y, int w, int h, IntPtr parent, IntPtr menu, IntPtr inst, IntPtr param);
    [DllImport("user32.dll")] static extern bool DestroyWindow(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr DefWindowProcW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern uint RegisterWindowMessageW(string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandleW(string? name);

    /// page: what this app's Settings button passes ("deskdrawer" / "desktune"); code: the number DeskBar must get for it (settingswin.cpp SettingsPageForCode).
    public static List<(string name, bool ok, string detail)> Checks(string page, int code)
    {
        var r = new List<(string, bool, string)>();
        string keepCls = DeskSettings.WndClass; string? keepEnv = Environment.GetEnvironmentVariable("DESK_OWN_SETTINGS");
        uint msg = RegisterWindowMessageW("DeskOpenSettings"); var got = new List<int>(); long answer = 0x44534B;   // ui_wnd.cpp kOpenSettingsAck
        WndProc proc = (h, m, w, l) => { if (m == msg && msg != 0) { got.Add((int)w); return (IntPtr)answer; } return DefWindowProcW(h, m, w, l); };
        string cls = "DB3.Wnd.selftest." + Environment.ProcessId;   // never "DB3.Wnd": a real DeskBar on this PC would open its settings window
        IntPtr inst = GetModuleHandleW(null), hw = IntPtr.Zero;
        try
        {
            Environment.SetEnvironmentVariable("DESK_OWN_SETTINGS", null);
            var wc = new WNDCLASSW { proc = Marshal.GetFunctionPointerForDelegate(proc), inst = inst, name = cls };
            RegisterClassW(ref wc);
            hw = CreateWindowExW(0x80 /*WS_EX_TOOLWINDOW*/, cls, "", 0x80000000 /*WS_POPUP*/, -32000, -32000, 10, 10, IntPtr.Zero, IntPtr.Zero, inst, IntPtr.Zero);
            DeskSettings.WndClass = cls;
            r.Add(("launch: the pretend DeskBar window is up", hw != IntPtr.Zero, ""));
            bool mine = DeskSettings.Open(page);
            r.Add(("launch: Settings opens DeskBar's settings window when DeskBar answers", mine && got.Count == 1, $"answered {mine}, asked {got.Count} time(s)"));
            r.Add(($"launch: Settings asks for this app's page (code {code})", got.Count == 1 && got[0] == code, string.Join(",", got)));
            got.Clear(); bool col = DeskSettings.Open("colours");
            r.Add(("launch: the colour button asks for Colours (code 3)", col && got.Count == 1 && got[0] == 3, string.Join(",", got)));
            answer = 0; got.Clear(); bool old = DeskSettings.Open(page);   // DeskBar 3.x, or the supervisor's stand-in bar: it doesn't know the message
            r.Add(("launch: an older DeskBar that doesn't answer leaves it to this app's own settings", !old && got.Count == 1, $"answered {old}"));
            answer = 0x44534B; DestroyWindow(hw); hw = IntPtr.Zero; got.Clear(); bool none = DeskSettings.Open(page);
            r.Add(("launch: no DeskBar running: this app's own settings", !none && got.Count == 0, $"answered {none}"));
            Environment.SetEnvironmentVariable("DESK_OWN_SETTINGS", "1"); bool forced = DeskSettings.Open(page);
            r.Add(("launch: DESK_OWN_SETTINGS (tests, pictures) always uses this app's own window", !forced, ""));
        }
        catch (Exception ex) { r.Add(("launch: settings message checks ran", false, ex.Message)); }
        finally
        {
            if (hw != IntPtr.Zero) DestroyWindow(hw);
            UnregisterClassW(cls, inst); DeskSettings.WndClass = keepCls; Environment.SetEnvironmentVariable("DESK_OWN_SETTINGS", keepEnv); GC.KeepAlive(proc);
        }
        return r;
    }
}
