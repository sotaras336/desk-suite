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
        {   // colours: what each theme id means and the colours it gives. DeskBar's and DeskDrawer's self-tests check the very same table, so the three apps look like one.
            uint[] ins = { 0x1167C4, 0x0A4096, 0x4CC2FF, 0xFFFFFF, 0x000000, 0x2BB673 };   // the accent, a background, a light accent, white, black, a green (greens never move)
            var rows = new (string Id, uint[] Want)[] {   // 0xAARRGGBB
                ("pink", new uint[] { 0xFFC67299, 0xFFC46890, 0xFFFA6AC2, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673 }), ("blue", new uint[] { 0xFF1167C4, 0xFF0A4096, 0xFF4CC2FF, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673 }),
                ("teal", new uint[] { 0xFF11C4AE, 0xFF0A9688, 0xFF4CFFE1, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673 }), ("red:30", new uint[] { 0xFFC4112D, 0xFF960A1D, 0xFFFF4C70, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673 }),
                ("black", new uint[] { 0xFF353535, 0xFF282828, 0xFF4CC2FF, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673 }), ("white", new uint[] { 0xFFDFDFDF, 0xFFE7E7E7, 0xFF009DEE, 0xFF1B1B1B, 0xFFFFFFFF, 0xFF2BB673 }),
                ("glass", new uint[] { 0x3D202020, 0x3D181818, 0xFF4CC2FF, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673 }), ("custom:200", new uint[] { 0xFF1193C4, 0xFF0A6C96, 0xFF4CD6FF, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673 }),
                ("custom:120:40:50:60", new uint[] { 0xFF285B25, 0xFF1C451B, 0xFF82D279, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673 }),
            };
            var bad = new StringBuilder();
            foreach (var (id, want) in rows)
            {
                Theme.Load(id);
                for (int i = 0; i < ins.Length; i++)
                {
                    var c = Theme.Col(ins[i]); int[] got = { c.A, c.R, c.G, c.B }; bool near = true;
                    for (int k = 0; k < 4; k++) near &= Math.Abs(got[k] - (int)((want[i] >> (24 - 8 * k)) & 255)) <= 2;   // double here, float in DeskBar: a step or two apart at most
                    if (!near) bad.Append($"{id} {ins[i]:X6} gave {c.A:X2}{c.R:X2}{c.G:X2}{c.B:X2}, want {want[i]:X8}; ");
                }
            }
            Check("colours: every theme gives the suite's colours (the table all three apps check)", bad.Length == 0, bad.ToString());
            Theme.Load("blue:40"); bool ia = Theme.Current.Id == "blue" && Theme.Grad == 40 && Theme.FullId == "blue:40";
            Theme.Load("custom:-30"); bool ib = Theme.Current.Hue == 330 && Theme.Grad == -1;
            Theme.Load("custom:200:150:120:-5"); bool ic = Theme.Current.Hue == 200 && Theme.Grad == 100 && Theme.Current.Sat == 1 && Theme.Current.Val == 0;
            Theme.Load("PURPLE"); bool id2 = Theme.Current.Id == "purple" && Theme.Grad == -1;
            Check("colours: theme ids are read the same way as in DeskBar", ia && ib && ic && id2, $"blue:40 {ia}, custom:-30 {ib}, clamped {ic}, PURPLE {id2}");
            Theme.Load("nonsense"); string broken = Theme.Current.Id; Theme.Load("x:40"); string broken2 = Theme.Current.Id; Theme.Load(""); string none = Theme.Current.Id;
            Check("colours: a broken theme name falls back to Blue, as in DeskBar (no theme at all: Black)", broken == "blue" && broken2 == "blue" && none == "black", $"{broken} {broken2} {none}");
            Check("colour picker: the colour and the fade are rounded as DeskBar rounds them", Theme.ColourId(0.25, null, true, 199.6, 39.5, 100) == "custom:200:40" && Theme.ColourId(0.25, null, true, 200.4, 0.4, 100) == "custom:200:0", Theme.ColourId(0.25, null, true, 199.6, 39.5, 100));
            bool ends = Theme.ColourId(0.05, null, true, 200, 30, 100) == "white" && Theme.ColourId(0.95, "blue", false, 200, 30, 100) == "black";
            bool named = Theme.ColourId(0.25, "blue", false, 205, 40, 100) == "blue:40" && Theme.ColourId(0.25, "blue", true, 200, 0, 100) == "custom:200:0" && Theme.ColourId(0.25, null, true, 200, 40, 80) == "custom:200:40:80:100";
            Check("colour picker: the ids it makes (black and white ends, a ready-made colour with a fade, a custom one)", ends && named, Theme.ColourId(0.25, null, true, 200, 40, 80));
            var back = new StringBuilder();   // open the picker on a colour and press OK without touching it: the same colour comes back
            foreach (var cid in new[] { "custom:120:40:50:60", "custom:200:0", "custom:10:25:100:30", "black", "white" })
            {
                Theme.Load(cid); var got = Theme.ColourId(Theme.LightStart(Theme.Current), null, false, Theme.Current.Hue, Theme.Grad > 0 ? Theme.Grad : 0, Theme.Current.Sat * 100);
                if (got != cid) back.Append($"{cid} -> {got}; ");
            }
            Check("colour picker: reopened on a colour, it gives the same colour back", back.Length == 0, back.ToString());
            Theme.Load("black");
        }
        {   // one colour for every app, or each app its own: the files DeskBar writes, read the way DeskBar reads them (in the test's own folder, never the real one)
            string? realDir = Environment.GetEnvironmentVariable("DESK_DIR"); string dir = Path.Combine(Path.GetTempPath(), "desk-selftest-" + Guid.NewGuid().ToString("N")[..8]);
            Environment.SetEnvironmentVariable("DESK_DIR", dir);
            try
            {
                string all = Path.Combine(dir, "theme.txt"), own = Path.Combine(dir, "theme-" + SharedTheme.App + ".txt"), bar = Path.Combine(dir, "theme-deskbar.txt");
                bool inTest = SharedTheme.File == all, none = SharedTheme.Read() == null;
                SharedTheme.Write("teal"); bool one = File.ReadAllText(all) == "teal" && SharedTheme.Read() == "teal" && !File.Exists(own);
                File.WriteAllText(own, "teal"); File.WriteAllText(bar, "teal");   // "Same colour for every app" turned off in DeskBar: every app gets its own file, starting from the suite's colour
                SharedTheme.Write("custom:120:40"); bool sep = File.ReadAllText(own) == "custom:120:40" && File.ReadAllText(all) == "teal" && File.ReadAllText(bar) == "teal" && SharedTheme.Read() == "custom:120:40";
                File.Delete(own); File.Delete(bar);   // turned back on: DeskBar removes the apps' own files
                bool again = SharedTheme.Read() == "teal";
                File.WriteAllText(all, "  pink\r\n"); File.WriteAllText(own, new string('x', 300));
                bool tidy = SharedTheme.Read() == "pink";
                Check("shared colours: one for all, or each app its own (read and written as DeskBar does)", inTest && none && one && sep && again && tidy, $"own folder {inTest}, empty {none}, one {one}, separate {sep}, one again {again}, untidy files {tidy}");
            }
            catch (Exception ex) { Check("shared colours: no exception", false, ex.Message); }
            finally { Environment.SetEnvironmentVariable("DESK_DIR", realDir); try { Directory.Delete(dir, true); } catch { } }
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

            // one change that can't be put back must not stop the others, and what was put back is saved
            var entriesField = typeof(Log).GetField("_entries", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)!;
            Log.Apply(tw);
            var raw = System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(Log.File))!.AsArray();
            raw.Add(new System.Text.Json.Nodes.JsonObject { ["id"] = "broken", ["title"] = "Broken", ["when"] = DateTime.Now.ToString("s"), ["steps"] = new System.Text.Json.Nodes.JsonArray(new System.Text.Json.Nodes.JsonObject { ["op"] = new System.Text.Json.Nodes.JsonObject { ["type"] = "nope" }, ["before"] = new System.Text.Json.Nodes.JsonObject() }) });
            File.WriteAllText(Log.File, raw.ToJsonString()); entriesField.SetValue(null, null);
            var failedUndo = Log.UndoAll(); entriesField.SetValue(null, null);
            Check("log: a change that can't be undone doesn't stop the rest", failedUndo.Count == 1 && !tw.IsApplied && !Log.Touched("test") && Log.Count == 1, string.Join("; ", failedUndo));

            // an unreadable log is kept aside, not overwritten with an empty one
            File.WriteAllText(Log.File, "[{\"id\":\"cut short"); entriesField.SetValue(null, null);
            bool kept = Log.Count == 0 && Directory.GetFiles(Path.GetDirectoryName(Log.File)!, Path.GetFileName(Log.File) + ".unreadable-*").Length > 0;
            Check("log: an unreadable change log is kept aside", kept);
            foreach (var f in Directory.GetFiles(Path.GetDirectoryName(Log.File)!, Path.GetFileName(Log.File) + ".unreadable-*")) try { File.Delete(f); } catch { }
            entriesField.SetValue(null, null);

            ServiceUndo(Check);   // services: only with administrator rights (the GitHub check has them)

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

    /// A throw-away service, set to "Automatic (delayed start)", is switched off and put back the way the Services switches do it: it must come back delayed,
    /// and putting back a service that no longer exists is not an error.
    private static void ServiceUndo(Action<string, bool, string> check)
    {
        const string name = "DeskTuneSelfTest";
        var op = new ServiceOp(name);
        if (!Elevation.IsAdmin) return;   // creating a test service needs administrator rights
        Op.Run("sc.exe", $"delete {name}");
        try
        {
            int made = Op.Run("sc.exe", $"create {name} binPath= \"%SystemRoot%\\System32\\svchost.exe -k DeskTuneSelfTestNoSuchGroup\" start= delayed-auto");
            if (made != 0 || !op.Exists) { check("service: test service created", false, $"(sc.exe answered {made})"); return; }
            var before = op.Apply();
            check("service: switched off", op.IsApplied && (int?)before["delayed"] == 1, before.ToJsonString());
            var sw = System.Diagnostics.Stopwatch.StartNew(); op.Restore(before);
            using var k = Registry.LocalMachine.OpenSubKey($@"SYSTEM\CurrentControlSet\Services\{name}");
            check("service: undo brings back Automatic (delayed start)", !op.IsApplied && k?.GetValue("Start") is 2 && k.GetValue("DelayedAutostart") is 1, $"({sw.ElapsedMilliseconds} ms)");
        }
        catch (Exception ex) { check("service: no exception", false, ex.ToString()); }
        finally { Op.Run("sc.exe", $"stop {name}", 10000); Op.Run("sc.exe", $"delete {name}"); }
        bool gone = !Throws(() => op.Restore(new System.Text.Json.Nodes.JsonObject { ["start"] = 2, ["delayed"] = 1 }));
        check("service: putting back a service that no longer exists is not an error", gone, "");
    }

    private static bool Throws(Action a) { try { a(); return false; } catch { return true; } }

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
