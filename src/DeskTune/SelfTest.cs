using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Microsoft.Win32;

namespace DeskTune;

/// DeskTune.exe --selftest report.txt: applies and undoes changes on its OWN test keys (HKCU\Software\DeskTuneSelfTest) with its own log file,
/// so nothing real on the PC is touched; then builds the real catalogue read-only.
public static class SelfTest
{
    const string TestKey = @"Software\DeskTuneSelfTest";

    public static int Run(string reportFile)
    {
        var sb = new StringBuilder(); int fail = 0;
        void Check(string name, bool ok, string extra = "") { sb.AppendLine((ok ? "PASS " : "FAIL ") + name + (extra.Length > 0 ? "  " + extra : "")); if (!ok) fail++; }
        {   // the update check: reading the checksum list, and (only when a test points DESK_UPDATE_BASE at a local release folder) the whole check and a verified download that is not run
            string sums = new string('a', 64) + " *" + Updater.Asset + "\r\n" + new string('b', 64) + " *Other-Setup.exe\n";
            Check("update: reads the checksum for this app's file", Updater.HashFor(sums, Updater.Asset) == new string('a', 64) && Updater.HashFor(sums, "Nope.exe") == null);
            if (Environment.GetEnvironmentVariable("DESK_UPDATE_BASE") != null)
            {
                bool got = Task.Run(() => Updater.UpdateNowAsync()).GetAwaiter().GetResult();   // no earlier check: Update now must look at the release list itself (a checksum remembered from an older check once made it reject the newest file)
                Check("update: looks again, downloads, verifies and (in a test) does not run it", got && Updater.LastDownload != null && System.IO.File.Exists(Updater.LastDownload), Updater.Status + " " + Updater.LastDownload);
                Task.Run(() => Updater.CheckAsync()).GetAwaiter().GetResult();
                Check("update: sees a different build in the release", Updater.Newer, Updater.Status);
            }
        }
        {   // ten tiny wheel messages (one touchpad swipe) move one notch in total, not ten
            var sv = new System.Windows.Controls.ScrollViewer { Width = 100, Height = 100, Content = new System.Windows.Controls.Border { Height = 5000 } };
            Wheel.Attach(sv); sv.Measure(new System.Windows.Size(100, 100)); sv.Arrange(new System.Windows.Rect(0, 0, 100, 100)); sv.UpdateLayout();
            for (int i = 0; i < 10; i++) sv.RaiseEvent(new System.Windows.Input.MouseWheelEventArgs(System.Windows.Input.Mouse.PrimaryDevice, Environment.TickCount, -12) { RoutedEvent = System.Windows.UIElement.PreviewMouseWheelEvent });
            sv.UpdateLayout();
            Check("wheel: a touchpad swipe moves one notch, not ten", Math.Abs(sv.VerticalOffset - Wheel.Px(120)) < 0.5 && Wheel.Px(120) >= 16, $"{sv.VerticalOffset} px");
        }
        string realLog = Log.File; Log.File = Path.Combine(Path.GetTempPath(), "desktune-selftest-" + Guid.NewGuid().ToString("N")[..8] + ".json");
        try
        {
            Registry.CurrentUser.DeleteSubKeyTree(TestKey, false);

            // a value that didn't exist: applied, then undone = gone again
            var a = new RegOp(RegistryHive.CurrentUser, TestKey, "NewValue", 0);
            Check("reg: not applied at first", !a.IsApplied);
            var before = a.Apply(); Check("reg: applied", a.IsApplied);
            a.Restore(before); Check("reg: undo removes a value that wasn't there", a.Current == null);

            // a value that existed: undone = old value back, same type
            using (var k = Registry.CurrentUser.CreateSubKey(TestKey)) k.SetValue("Old", 7, RegistryValueKind.DWord);
            var b = new RegOp(RegistryHive.CurrentUser, TestKey, "Old", 0);
            var bb = b.Apply(); Check("reg: overwrites", b.IsApplied);
            b.Restore(bb); Check("reg: undo restores the old value", b.Current is int i && i == 7);

            // binary value (StartupApproved style)
            using (var k = Registry.CurrentUser.CreateSubKey(TestKey + @"\Approved")) k.SetValue("App", new byte[] { 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, RegistryValueKind.Binary);
            var s = new StartupOp(RegistryHive.CurrentUser, TestKey + @"\Approved", "App");
            Check("startup: enabled at first", !s.IsApplied);
            var sbk = s.Apply(); Check("startup: disabled", s.IsApplied);
            s.Restore(sbk); Check("startup: undo enables it again", !s.IsApplied);

            // store-app startup task
            using (var k = Registry.CurrentUser.CreateSubKey(TestKey + @"\Task")) k.SetValue("State", 2, RegistryValueKind.DWord);
            var t = new AppStartupOp(TestKey + @"\Task"); var tb = t.Apply(); Check("app startup: off", t.IsApplied); t.Restore(tb); Check("app startup: undo", !t.IsApplied);

            // the log: apply a switch, undo by id, and undo-all after a restart (fresh load from disk)
            var tw = new Tweak { Id = "test", Category = "Test", Title = "Test", Lose = "", Ops = new() { new RegOp(RegistryHive.CurrentUser, TestKey, "L1", 1), new RegOp(RegistryHive.CurrentUser, TestKey, "L2", 1) } };
            Log.Apply(tw); Check("log: applied and recorded", tw.IsApplied && Log.Touched("test"));
            Log.Undo("test"); Check("log: undo by switch", !tw.IsApplied && !Log.Touched("test"));
            Log.Apply(tw); typeof(Log).GetField("_entries", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)!.SetValue(null, null);
            Log.UndoAll(); Check("log: undo everything after reloading", !tw.IsApplied && Log.Count == 0);

            Check("command parsing", Catalogue.ExeOf("\"C:\\Program Files\\App\\app.exe\" --min") == @"C:\Program Files\App\app.exe" && Catalogue.ExeOf(@"C:\x\tool.exe /s") == @"C:\x\tool.exe");
            Check("store app names", Catalogue.FriendlyPackage("SpotifyAB.SpotifyMusic_zpdnekdrzrea0") == "SpotifyMusic" && Catalogue.FriendlyPackage("MicrosoftTeams_8wekyb3d8bbwe") == "MicrosoftTeams");

            var cat = Catalogue.Build(); var cats = cat.Select(x => x.Category).Distinct().ToList();
            Check("catalogue builds", cat.Count > 5, $"({cat.Count} switches: {string.Join(", ", cats)}; already off: {cat.Count(x => x.IsApplied)})");
            Check("every switch has a unique id", cat.Select(x => x.Id).Distinct().Count() == cat.Count);
        }
        catch (Exception ex) { Check("no exception", false, ex.ToString()); }
        finally { Registry.CurrentUser.DeleteSubKeyTree(TestKey, false); try { File.Delete(Log.File); } catch { } Log.File = realLog; }
        sb.AppendLine($"{fail} failure(s)");
        File.WriteAllText(reportFile, sb.ToString());
        return fail;
    }

    /// --snapshot file.png: renders the window once the scan has had time to finish, then exits.
    public static void SnapshotLater(Window w, string file)
    {
        var t = new DispatcherTimer { Interval = TimeSpan.FromSeconds(9) };
        t.Tick += (_, _) =>
        {
            t.Stop();
            var root = (FrameworkElement)w.Content; var dpi = VisualTreeHelper.GetDpi(root);
            var rtb = new RenderTargetBitmap((int)(root.ActualWidth * dpi.DpiScaleX), (int)(root.ActualHeight * dpi.DpiScaleY), 96 * dpi.DpiScaleX, 96 * dpi.DpiScaleY, PixelFormats.Pbgra32);
            var bg = new DrawingVisual(); using (var dc = bg.RenderOpen()) dc.DrawRectangle((Brush)w.FindResource("WinBg"), null, new Rect(0, 0, root.ActualWidth, root.ActualHeight));
            rtb.Render(bg); rtb.Render(root);
            var enc = new PngBitmapEncoder(); enc.Frames.Add(BitmapFrame.Create(rtb)); using (var fs = File.Create(file)) enc.Save(fs);
            Application.Current.Shutdown();
        };
        t.Start();
    }
}
