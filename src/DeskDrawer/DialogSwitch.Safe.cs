using System.Collections.Concurrent;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;

namespace DeskDrawer;

/// Talking to another app's Save / Open dialog without being dragged down by it, and never leaving it hidden.
///
/// 1. A time limit on every message. A plain SendMessage waits for as long as the other app takes: when that app hung while its dialog was being read or
///    filled in, DeskDrawer froze with it. Now each message gives up after a second, and once an app has not answered, the next messages to it are skipped
///    for a few seconds instead of each waiting again.
/// 2. Style changes (see-through on / off) are made on a helper thread, in order: changing another window's style waits for that window's app too.
///    Showing and hiding use ShowWindowAsync, which never waits.
/// 3. If DeskDrawer quits, crashes or is replaced by an update while it stands in for a dialog, that dialog used to stay hidden (and see-through), and the app
///    that asked looked frozen. Now the dialog is put back on screen when DeskDrawer goes away: at once on a normal exit or an error, and by a tiny guard
///    process (DeskDrawer.exe --dialog-guard, see DialogGuard) when DeskDrawer is killed outright and can't do anything itself.
public static partial class DialogSwitch
{
    private const uint MsgLimitMs = 1000, SmtoAbortIfHung = 0x0002, SmtoErrorOnExit = 0x0020;
    private static readonly ConcurrentDictionary<uint, long> Unanswered = new();   // window thread -> until when its messages are skipped

    /// Messages that make the dialog do something (press a button) may run long without anything being wrong, e.g. while the app asks "replace this file?".
    private static bool IsAction(int m, IntPtr w) => m == 0x00F5 /*BM_CLICK*/ || m == 0x0111 /*WM_COMMAND*/;

    private static bool Skipped(IntPtr h, out uint tid)
    {
        tid = GetWindowThreadProcessId(h, out _);
        if (!Unanswered.TryGetValue(tid, out long until)) return false;
        if (Environment.TickCount64 < until) return true;
        Unanswered.TryRemove(tid, out _); return false;
    }

    private static IntPtr Done(IntPtr h, uint tid, int m, IntPtr w, IntPtr ok, IntPtr result)
    {
        if (ok != IntPtr.Zero) return result;
        if (!IsAction(m, w) && IsWindow(h)) Unanswered[tid] = Environment.TickCount64 + 4000;   // didn't answer in time: leave that app alone for a few seconds
        return IntPtr.Zero;
    }

    // Same names as before, so every caller got the time limit without changing.
    private static IntPtr SendMessage(IntPtr h, int m, IntPtr w, string l) { if (Skipped(h, out uint t)) return IntPtr.Zero; var ok = SendMessageTimeout(h, m, w, l, SmtoAbortIfHung | SmtoErrorOnExit, MsgLimitMs, out var r); return Done(h, t, m, w, ok, r); }
    private static IntPtr SendMessage(IntPtr h, int m, IntPtr w, StringBuilder l) { if (Skipped(h, out uint t)) return IntPtr.Zero; var ok = SendMessageTimeout(h, m, w, l, SmtoAbortIfHung | SmtoErrorOnExit, MsgLimitMs, out var r); return Done(h, t, m, w, ok, r); }
    private static IntPtr SendMessage(IntPtr h, int m, IntPtr w, IntPtr l) { if (Skipped(h, out uint t)) return IntPtr.Zero; var ok = SendMessageTimeout(h, m, w, l, SmtoAbortIfHung | SmtoErrorOnExit, MsgLimitMs, out var r); return Done(h, t, m, w, ok, r); }

    /// Never waits for the window's app (ShowWindow does, when the window belongs to another one).
    private static bool ShowWindow(IntPtr h, int cmd) => ShowWindowAsync(h, cmd);

    // ---------- style changes off the UI thread, in the order they were asked for ----------

    private static readonly BlockingCollection<Action> Styles = new();
    private static Thread? _styler;

    private static void Restyle(Action a)
    {
        if (_styler == null)
        {
            _styler = new Thread(() => { foreach (var x in Styles.GetConsumingEnumerable()) try { x(); } catch { } }) { IsBackground = true, Name = "DeskDrawer dialog styles" };
            _styler.Start();
        }
        Styles.Add(a);
    }

    /// See-through: layered with alpha 0. The style change goes to the helper thread; the alpha only works once the style is there, so it goes with it.
    private static void MakeSeeThrough(IntPtr h) => Restyle(() => { if (!IsWindow(h)) return; SetWindowLong(h, -20, GetWindowLong(h, -20) | 0x80000); SetLayeredWindowAttributes(h, 0, 0, 2); });

    /// Colours back. Alpha 255 is set at once (it doesn't wait for the app), and the layered style is taken off on the helper thread, after any fade still queued.
    private static void MakeOpaque(IntPtr h)
    {
        if ((GetWindowLong(h, -20) & 0x80000) != 0) SetLayeredWindowAttributes(h, 0, 255, 2);
        Restyle(() => { if (!IsWindow(h)) return; SetLayeredWindowAttributes(h, 0, 255, 2); SetWindowLong(h, -20, GetWindowLong(h, -20) & ~0x80000); });
    }

    // ---------- never leave a dialog hidden ----------

    private static IntPtr _standingIn;   // the dialog the picker is answering for right now (hidden), until it is gone or shown again
    private static bool _exitHooked;

    /// Called when a dialog is taken over: it will be put back if DeskDrawer goes away before it is answered.
    private static void Guard(IntPtr dlg)
    {
        _standingIn = dlg;
        if (!_exitHooked)
        {
            _exitHooked = true;
            AppDomain.CurrentDomain.ProcessExit += (_, _) => RestoreAll();
            AppDomain.CurrentDomain.UnhandledException += (_, _) => RestoreAll();
            try { if (Application.Current != null) Application.Current.Exit += (_, _) => RestoreAll(); } catch { }
        }
        string exe = Environment.ProcessPath ?? ""; int me = Environment.ProcessId; long h = dlg.ToInt64();
        if (exe.Length > 0) System.Threading.Tasks.Task.Run(() =>
        {
            try { using var _ = Process.Start(new ProcessStartInfo(exe, $"--dialog-guard {me} {h}") { UseShellExecute = false, CreateNoWindow = true }); }
            catch { }   // the guard is the back-up: DeskDrawer's own exit handling still runs
        });
    }

    private static void Released(IntPtr dlg) { if (_standingIn == dlg) _standingIn = IntPtr.Zero; }

    /// On the way out: every dialog DeskDrawer made see-through gets its colours back, and the one it stood in for is shown again. Only calls that don't wait
    /// for the other app: this may run while DeskDrawer is crashing.
    internal static void RestoreAll()
    {
        try
        {
            foreach (var h in Faded.ToArray()) if (IsWindow(h)) SetLayeredWindowAttributes(h, 0, 255, 2);
            IntPtr d = _standingIn; _standingIn = IntPtr.Zero;
            if (d != IntPtr.Zero && IsWindow(d)) { SetLayeredWindowAttributes(d, 0, 255, 2); ShowWindowAsync(d, 5 /*SW_SHOW*/); }
        }
        catch { }
    }

    // for the self-test
    internal static string TestTextOf(IntPtr h) => TextOf(h);
    internal static void TestFade(IntPtr h) => FadeOut(h);
    internal static void TestReveal(IntPtr h) => Reveal(h);
    internal static bool TestStylesDone(int ms) { var done = new ManualResetEventSlim(); Restyle(done.Set); return done.Wait(ms); }
    internal static void TestForget(IntPtr h) { Faded.Remove(h); foreach (var t in Unanswered.Keys) Unanswered.TryRemove(t, out _); }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageTimeoutW")] private static extern IntPtr SendMessageTimeout(IntPtr h, int m, IntPtr w, string l, uint flags, uint ms, out IntPtr result);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageTimeoutW")] private static extern IntPtr SendMessageTimeout(IntPtr h, int m, IntPtr w, StringBuilder l, uint flags, uint ms, out IntPtr result);
    [DllImport("user32.dll", EntryPoint = "SendMessageTimeoutW")] private static extern IntPtr SendMessageTimeout(IntPtr h, int m, IntPtr w, IntPtr l, uint flags, uint ms, out IntPtr result);
    [DllImport("user32.dll")] private static extern bool ShowWindowAsync(IntPtr h, int cmd);
}
