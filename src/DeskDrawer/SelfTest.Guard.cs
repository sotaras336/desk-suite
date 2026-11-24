using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;

namespace DeskDrawer;

/// Part of --selftest: the Save / Open stand-in can't freeze with a hung app, and a hidden dialog comes back when DeskDrawer goes away.
/// A window on a thread of its own plays the other app's dialog: that thread can be made to hang on purpose.
public static partial class SelfTest
{
    internal static void DialogSafety(Action<string, bool, string> check)
    {
        IntPtr h = IntPtr.Zero; Dispatcher? d = null; Window? w = null; var ready = new ManualResetEventSlim();
        var th = new Thread(() =>
        {
            w = new Window { Width = 500, Height = 300, Left = -3000, Top = -3000, ShowActivated = false, ShowInTaskbar = false, Title = "DeskDrawer guard test", WindowStyle = WindowStyle.None };
            w.Show(); h = new WindowInteropHelper(w).Handle; d = Dispatcher.CurrentDispatcher; ready.Set(); Dispatcher.Run();
        });
        th.SetApartmentState(ApartmentState.STA); th.IsBackground = true; th.Start();
        if (!ready.Wait(15000) || d == null) { check("dialog safety: test window opens", false, ""); return; }
        var cleanup = new List<Process>();
        try
        {
            // 1. the "app" hangs for 3 s: reading its window gives up within about a second, the next read at once, and see-through on / off doesn't wait at all
            d.BeginInvoke(() => Thread.Sleep(3000)); Thread.Sleep(150);
            var sw = Stopwatch.StartNew(); DialogSwitch.TestTextOf(h); long first = sw.ElapsedMilliseconds;
            sw.Restart(); DialogSwitch.TestTextOf(h); long second = sw.ElapsedMilliseconds;
            sw.Restart(); DialogSwitch.TestFade(h); DialogSwitch.TestReveal(h); long styles = sw.ElapsedMilliseconds;
            check("dialog safety: a hung app can't freeze DeskDrawer (reading its dialog gives up, then skips it)", first < 1600 && second < 150, $"({first} ms, then {second} ms; it used to wait as long as the app hung)");
            check("dialog safety: making a hung app's dialog see-through and back doesn't wait for it", styles < 150, $"({styles} ms)");
            bool back = false; for (int i = 0; i < 60 && !(back = Visible(h) && !Layered(h)); i++) Thread.Sleep(100);
            check("dialog safety: once the app answers again, its dialog is shown normally", back, "");
            DialogSwitch.TestForget(h);
            check("dialog safety: an app that answers is read normally", DialogSwitch.TestTextOf(h) == "DeskDrawer guard test", "");

            // 2. DeskDrawer goes away (killed) while the dialog is hidden and see-through: the guard puts it back; while DeskDrawer lives, it leaves it alone
            GuardWin.ShowWindowAsync(h, 0); GuardWin.SetWindowLong(h, -20, GuardWin.GetWindowLong(h, -20) | 0x80000); GuardWin.SetLayeredWindowAttributes(h, 0, 0, 2);
            var stand = Stand(cleanup); var guard = Guard(stand.Id, h, cleanup);
            Thread.Sleep(1200);
            bool waited = !Visible(h) && Layered(h) && !guard.HasExited;
            stand.Kill(true);
            bool ended = guard.WaitForExit(5000); bool restored = false; for (int i = 0; i < 30 && !(restored = Visible(h) && !Layered(h)); i++) Thread.Sleep(100);
            check("dialog safety: the guard leaves the dialog alone while DeskDrawer is running", waited, "");
            check("dialog safety: DeskDrawer killed while standing in for a dialog: the dialog comes back on screen", ended && restored, $"(guard ended: {ended}, dialog shown: {restored})");

            // 3. the dialog is answered (closed): the guard ends by itself
            var stand2 = Stand(cleanup); var guard2 = Guard(stand2.Id, h, cleanup);
            Thread.Sleep(500); d.Invoke(() => w!.Close());
            check("dialog safety: the guard ends by itself once the dialog is gone", guard2.WaitForExit(5000), "");
        }
        catch (Exception ex) { check("dialog safety: no exception", false, ex.ToString()); }
        finally
        {
            foreach (var p in cleanup) try { if (!p.HasExited) p.Kill(true); p.Dispose(); } catch { }
            try { d.InvokeShutdown(); } catch { }
        }
    }

    private static Process Stand(List<Process> keep) { var p = Process.Start(new ProcessStartInfo("cmd.exe", "/c ping -n 60 127.0.0.1 > nul") { UseShellExecute = false, CreateNoWindow = true })!; keep.Add(p); return p; }
    private static Process Guard(int pid, IntPtr h, List<Process> keep) { var p = Process.Start(new ProcessStartInfo(Environment.ProcessPath!, $"--dialog-guard {pid} {h.ToInt64()}") { UseShellExecute = false, CreateNoWindow = true })!; keep.Add(p); return p; }
    private static bool Visible(IntPtr h) => GuardWin.IsWindowVisible(h);
    private static bool Layered(IntPtr h) => (GuardWin.GetWindowLong(h, -20) & 0x80000) != 0;
}

/// Its own Win32 declarations, so they can't clash with the other self-test files'.
internal static class GuardWin
{
    [DllImport("user32.dll")] internal static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] internal static extern bool ShowWindowAsync(IntPtr h, int cmd);
    [DllImport("user32.dll")] internal static extern int GetWindowLong(IntPtr h, int i);
    [DllImport("user32.dll")] internal static extern int SetWindowLong(IntPtr h, int i, int v);
    [DllImport("user32.dll")] internal static extern bool SetLayeredWindowAttributes(IntPtr h, uint key, byte alpha, uint flags);
}
