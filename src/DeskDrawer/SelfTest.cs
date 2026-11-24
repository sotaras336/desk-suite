using System.IO;
using System.Text;

namespace DeskDrawer;

/// DeskDrawer.exe --selftest report.txt : exercises the file operations and the shell menu on a throw-away folder, no windows, then exits.
public static partial class SelfTest
{
    public static int Run(string reportFile)
    {
        var sb = new StringBuilder(); int fail = 0;
        void Check(string name, bool ok, string extra = "") { sb.AppendLine((ok ? "PASS " : "FAIL ") + name + (extra.Length > 0 ? "  " + extra : "")); if (!ok) fail++; }
        {   // the update check: reading the checksum list, and (only when a test points DESK_UPDATE_BASE at a local release folder) the whole check and a verified download that is not run
            string sums = new string('a', 64) + " *" + Updater.Asset + "\r\n" + new string('b', 64) + " *Other-Setup.exe\n";
            {   // a shortcut to a folder opens inside DeskDrawer (handed to Windows, it opened in File Explorer)
                string dir = Path.Combine(Path.GetTempPath(), "dd-lnk-" + Guid.NewGuid().ToString("N")[..6]), target = Path.Combine(dir, "target"), lnk = Path.Combine(dir, "to target.lnk");
                Directory.CreateDirectory(target);
                try { dynamic sh = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell")!)!; dynamic l = sh.CreateShortcut(lnk); l.TargetPath = target; l.Save(); } catch { }
                Check("a shortcut to a folder is followed, not handed to File Explorer", string.Equals(MainWindow.FolderBehind(lnk), target, StringComparison.OrdinalIgnoreCase) && MainWindow.FolderBehind(Path.Combine(dir, "nope.txt")) == null);
                try { Directory.Delete(dir, true); } catch { }
            }
            {   // OneDrive must show in the sidebar even where its environment variables are missing (other PCs, or a DeskDrawer started before OneDrive)
                bool signedIn; using (var acc = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\OneDrive\Accounts")) signedIn = acc?.GetSubKeyNames().Any(n => acc.OpenSubKey(n)?.GetValue("UserFolder") is string f && Directory.Exists(f)) == true;
                var roots = MainWindow.CloudRoots(envToo: false);
                Check("OneDrive found from its own registry entries, without environment variables", !signedIn || roots.Count > 0, string.Join("; ", roots.Select(r => r.Path)));
            }
            Check("update: reads the checksum for this app's file", Updater.HashFor(sums, Updater.Asset) == new string('a', 64) && Updater.HashFor(sums, "Nope.exe") == null);
            if (Environment.GetEnvironmentVariable("DESK_UPDATE_BASE") != null)
            {
                bool got = Task.Run(() => Updater.UpdateNowAsync()).GetAwaiter().GetResult();   // no earlier check: Update now must look at the release list itself (a checksum remembered from an older check once made it reject the newest file)
                Check("update: looks again, downloads, verifies and (in a test) does not run it", got && Updater.LastDownload != null && System.IO.File.Exists(Updater.LastDownload), Updater.Status + " " + Updater.LastDownload);
                Task.Run(() => Updater.CheckAsync()).GetAwaiter().GetResult();
                Check("update: sees a different build in the release", Updater.Newer, Updater.Status);
            }
        }
        string root = Path.Combine(Path.GetTempPath(), "deskdrawer-test-" + Guid.NewGuid().ToString("N")[..8]);
        try
        {
            ShellOps.Silent = true;
            Directory.CreateDirectory(root); string a = Path.Combine(root, "a"), b = Path.Combine(root, "b"); Directory.CreateDirectory(a); Directory.CreateDirectory(b);
            string f1 = Path.Combine(a, "one.txt"), f2 = Path.Combine(a, "two.txt"); File.WriteAllText(f1, "1"); File.WriteAllText(f2, "2");

            {
                var s = new Settings { Theme = "teal", Tabs = { @"C:\a" }, Folders = { ["c:\\x"] = new FolderView { Group = "Date modified", Icon = 48 } } };
                string json = System.Text.Json.JsonSerializer.Serialize(s, SettingsJson.Default.Settings);
                var back = System.Text.Json.JsonSerializer.Deserialize(json, SettingsJson.Default.Settings)!;
                Check("settings file round trip", back.Theme == "teal" && back.Tabs.SequenceEqual(s.Tabs) && back.Folders["c:\\x"] is { Group: "Date modified", Icon: 48 } && json.Contains("\"Theme\": \"teal\""));
            }
            Check("new folder name", Path.GetFileName(ShellOps.NewFolder(root)) == "New folder");
            Check("new folder, second one", Path.GetFileName(ShellOps.NewFolder(root)) == "New folder (2)");
            Check("new text file", File.Exists(ShellOps.NewTextFile(root)));

            Check("copy", ShellOps.Copy(new[] { f1, f2 }, b, IntPtr.Zero) && File.Exists(Path.Combine(b, "one.txt")) && File.Exists(f1), "(both copies exist)");
            Check("copy keep-both in same folder", ShellOps.Copy(new[] { f1 }, a, IntPtr.Zero, keepBothOnCollision: true) && Directory.GetFiles(a).Length == 3);
            Check("move", ShellOps.Move(new[] { f2 }, root, IntPtr.Zero) && File.Exists(Path.Combine(root, "two.txt")) && !File.Exists(f2));
            Check("recycle", ShellOps.Delete(new[] { Path.Combine(root, "two.txt") }, IntPtr.Zero) && !File.Exists(Path.Combine(root, "two.txt")));
            string rb = Path.Combine(root, "restore-me.txt"); File.WriteAllText(rb, "r");
            bool binned = ShellOps.Delete(new[] { rb }, IntPtr.Zero) && !File.Exists(rb);
            Check("delete can be undone from the Recycle Bin", binned && ShellOps.RestoreFromRecycleBin(new[] { rb }) && File.Exists(rb));
            string tmp = Path.Combine(root, "gone.txt"); File.WriteAllText(tmp, "x");
            Check("permanent delete", ShellOps.Delete(new[] { tmp }, IntPtr.Zero, permanent: true) && !File.Exists(tmp));

            ShellOps.SetClipboard(new[] { f1 }, cut: true);
            var clip = ShellOps.ReadClipboard();
            Check("clipboard cut round-trip", clip is { Cut: true } c && c.Paths.Count == 1 && c.Paths[0] == f1);
            ShellOps.SetClipboard(new[] { f1, Path.Combine(b, "one.txt") }, cut: false);
            clip = ShellOps.ReadClipboard();
            Check("clipboard copy round-trip", clip is { Cut: false } c2 && c2.Paths.Count == 2);

            int n = ShellMenu.CountItems(new[] { f1 }, IntPtr.Zero);
            Check("shell menu builds for a file", n > 3, $"({n} entries)");
            n = ShellMenu.CountItems(new[] { a }, IntPtr.Zero);
            Check("shell menu builds for a folder", n > 3, $"({n} entries)");

            var swOpen = System.Diagnostics.Stopwatch.StartNew();
            var asyncOpen = ShellMenu.OpenAsync(IntPtr.Zero, new[] { f1 }, false); long handBack = swOpen.ElapsedMilliseconds;
            Check("shell menu is built off the UI thread (call returns at once)", handBack < 100, $"({handBack} ms; the build itself took {(asyncOpen.Wait(20000) ? swOpen.ElapsedMilliseconds : -1)} ms)");
            asyncOpen.Result?.Dispose();
            using (var sess = ShellMenu.Open(IntPtr.Zero, new[] { f1 }, extended: false))
            {
                int named = 0, sub = 0, icons = 0; if (sess != null) foreach (var e in sess.Items) { if (!e.Separator) named++; if (e.Children != null) sub++; if (e.Icon != null) icons++; }
                Check("shell menu as data (themed 'more options')", sess != null && named >= 4, $"({named} items, {sub} submenus, {icons} icons: " + string.Join(", ", sess?.Items.Where(x => !x.Separator).Take(8).Select(x => x.Text) ?? Array.Empty<string>()) + ")");
            }

            var kinds = ShellNew.ReadNow();
            Check("New menu types from the registry", kinds.Count >= 3 && kinds.Any(k => k.Ext == ".txt"), $"({kinds.Count}: {string.Join(", ", kinds.Take(8).Select(k => k.Name))}...)");
            if (kinds.FirstOrDefault(k => k.Ext == ".zip") is { } zk)
            {
                string made = ShellNew.Create(zk, root); bool okZip = false;
                try { using var z = System.IO.Compression.ZipFile.OpenRead(made); okZip = z.Entries.Count == 0; } catch { }
                Check("New > Compressed (zipped) Folder is a valid empty zip", okZip);
            }
            Instance.PipeName += "-selftest";   // never the real one: that would open tabs in the DeskDrawer you are using
            List<string>? got = null; var frame = new System.Windows.Threading.DispatcherFrame();
            Instance.Listen(p => { got = p; frame.Continue = false; });
            var limit = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(3) }; limit.Tick += (_, _) => frame.Continue = false; limit.Start();
            for (int w = 0; w < 40 && !Instance.IsRunning(); w++) Thread.Sleep(50);   // the listener creates its pipe on its own thread
            bool handed = Instance.HandOff(new[] { root, "shell:Home" });
            System.Windows.Threading.Dispatcher.PushFrame(frame); limit.Stop();
            Check("second launch hands its folders to the open window", handed && got != null && got.SequenceEqual(new[] { root, "shell:Home" }), got == null ? "(nothing arrived)" : "");
            Check("natural sort", Native.StrCmpLogicalW("file2", "file10") < 0);
            Check("size text", FileEntry.FormatSize(1) == "1 B" && FileEntry.FormatSize(0) == "0 B" && FileEntry.FormatSize(1536) == "2 KB" && FileEntry.FormatSize(1 << 20) == "1.0 MB" && FileEntry.FormatSize(3L << 30) == "3.0 GB" && FileEntry.FormatSize(20_000_000) == "19.1 MB");
        }
        catch (Exception ex) { Check("no exception", false, ex.ToString()); }
        finally { try { Directory.Delete(root, true); } catch { } }
        {   // F2 on several files: one name, numbered, own extensions kept, taken numbers skipped
            var plan = MainWindow.BatchPlan(new[] { ("x/a.jpg", false), ("x/b.png", false), ("x/c.jpg", false) }, "Trip.jpg", p => p.EndsWith("Trip (2).png"));
            Check("renaming a group numbers the files and keeps each extension", plan.Select(p => Path.GetFileName(p.To)).SequenceEqual(new[] { "Trip (1).jpg", "Trip (3).png", "Trip (4).jpg" }), string.Join(", ", plan.Select(p => Path.GetFileName(p.To))));
        }
        {   // a Save As dialog in Downloads shows only "Downloads" (in the system's language): the picker must open there, not wherever DeskDrawer is
            string dl = MainWindow.KnownDownloads(); var fi = new Native.SHFILEINFO(); Native.SHGetFileInfo(dl, 0, ref fi, (uint)System.Runtime.InteropServices.Marshal.SizeOf<Native.SHFILEINFO>(), Native.SHGFI_DISPLAYNAME);
            string shown = fi.szDisplayName ?? "", got = DialogSwitch.FolderPathOf(shown);
            Check("save as: a known folder shown by its name is found as a path", string.Equals(got, dl, StringComparison.OrdinalIgnoreCase) && DialogSwitch.FolderPathOf(@"C:\Windows") == @"C:\Windows" && DialogSwitch.FolderPathOf("") == "" && DialogSwitch.FolderPathOf("This PC") == "", $"\"{shown}\" -> \"{got}\"");
        }
        {   // the picker (DeskDrawer in place of an app's Open / Save dialog): file types, typed names, what the app's dialog is given, its size
            bool pats = DialogSwitch.PatternsOf("Word Documents (*.docx;*.doc)").SequenceEqual(new[] { "*.docx", "*.doc" }) && DialogSwitch.PatternsOf("All Files (*.*)").Length == 0 && DialogSwitch.PatternsOf("*.txt").SequenceEqual(new[] { "*.txt" })
                && DialogSwitch.PatternsOf("Images (*.png, *.jpg)").SequenceEqual(new[] { "*.png", "*.jpg" }) && DialogSwitch.PatternsOf("Text").Length == 0;
            Check("picker: file types read from the dialog's list", pats, string.Join(";", DialogSwitch.PatternsOf("Images (*.png, *.jpg)")));
            string[] docx = { "*.docx", "*.doc" }, pdf = { "*.pdf" };
            bool swap = MainWindow.SwapExt("report.docx", docx, pdf) == "report.pdf" && MainWindow.SwapExt("REPORT.DOCX", docx, pdf) == "REPORT.pdf" && MainWindow.SwapExt("notes.txt", docx, pdf) == null
                && MainWindow.SwapExt("\"a.docx\" \"b.docx\"", docx, pdf) == null && MainWindow.SwapExt("report.docx", docx, Array.Empty<string>()) == null;
            Check("picker: saving as another type changes the name's extension", swap, MainWindow.SwapExt("REPORT.DOCX", docx, pdf) ?? "(none)");
            Check("picker: one typed name, or several in quotes", MainWindow.PickNames("\"a b.txt\" \"c.txt\"").SequenceEqual(new[] { "a b.txt", "c.txt" }) && MainWindow.PickNames("plain name.txt").SequenceEqual(new[] { "plain name.txt" }));
            bool shows = MainWindow.PickMatches("a.docx", false, false, false, docx) && !MainWindow.PickMatches("a.txt", false, false, false, docx) && MainWindow.PickMatches("sub", true, false, false, docx) && MainWindow.PickMatches("A.DOCX", false, false, false, docx)
                && MainWindow.PickMatches("a.txt", false, false, true, docx) && !MainWindow.PickMatches("a.txt", false, true, true, docx) && MainWindow.PickMatches("a.txt", false, false, false, Array.Empty<string>());
            Check("picker: shows folders and the files of the chosen type", shows);
            Check("picker: the name is selected without its extension", MainWindow.StemLength("report.docx") == 6 && MainWindow.StemLength(".gitignore") == 10 && MainWindow.StemLength("noext") == 5 && MainWindow.StemLength("\"a.txt\" \"b.txt\"") == 15);
            var two = new List<string> { @"C:\x\a.txt", @"c:\X\b.txt" };
            bool answer = DialogSwitch.AnswerText(two) == "\"C:\\x\\a.txt\" \"c:\\X\\b.txt\"" && DialogSwitch.AnswerText(new List<string> { @"C:\x\a.txt", @"C:\y\b.txt" }) == @"C:\x\a.txt" && DialogSwitch.AnswerText(new List<string> { @"C:\x\a b.txt" }) == @"C:\x\a b.txt";
            Check("picker: what the app's dialog is given (several files from one folder in quotes, else the first)", answer, DialogSwitch.AnswerText(two));
            bool size = MainWindow.PickSize(1920, 1032, 1, 400, 300) == (820, 560) && MainWindow.PickSize(2880, 1824, 1.5, 400, 300) == (1230, 840) && MainWindow.PickSize(1366, 600, 1, 400, 300) == (819, 420) && MainWindow.PickSize(1920, 1032, 1, 1000, 700) == (1000, 700)
                && MainWindow.PickSize(2880, 1824, 1.5, 400, 300, 600, 400) == (900, 600) && MainWindow.PickSize(2880, 1824, 1.5, 400, 300, 100, 100) == (720, 540) && MainWindow.PickSize(1920, 1032, 1, 400, 300, 5000, 5000) == (1896, 1008);
            Check("picker: its size on small, large and scaled screens, and the size you gave it", size, MainWindow.PickSize(2880, 1824, 1.5, 400, 300).ToString());
        }
        {   // colours: what each theme id means and the colours it gives. DeskBar's and DeskTune's self-tests check the very same table, so the three apps look like one.
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
        { var (l, r) = MainWindow.Halves(0, 0, 1921, 1040); Check("side by side splits a screen into two halves that cover it", l == "0,0,960,1040" && r == "960,0,961,1040", l + " | " + r); }
        DialogSafety((n, ok, x) => Check(n, ok, x));   // SelfTest.Guard.cs
        IndexSearchTest(Check);
        sb.AppendLine($"{fail} failure(s)");
        File.WriteAllText(reportFile, sb.ToString());
        return fail;
    }

    /// --dialogtest report.txt: opens a real Save dialog in another process (PowerShell), makes it jump to a folder the way the "DeskDrawer folders"
    /// button does, reads the dialog's address bar, then closes it. Shows a dialog on screen for a few seconds, so it is not part of --selftest.
    public static int DialogTest(string reportFile)
    {
        string target = Environment.GetFolderPath(Environment.SpecialFolder.MyMusic); string result;
        var ps = System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("powershell.exe",
            "-NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.SaveFileDialog; $d.InitialDirectory = 'C:\\Windows'; $d.FileName = 'keep-me.txt'; [void]$d.ShowDialog()\"") { UseShellExecute = false, CreateNoWindow = true })!;
        try
        {
            IntPtr dlg = IntPtr.Zero;
            for (int i = 0; i < 60 && dlg == IntPtr.Zero; i++) { Thread.Sleep(250); dlg = FindDialog(ps.Id); }
            if (dlg == IntPtr.Zero) result = "FAIL no dialog appeared";
            else
            {
                bool jumped = DialogSwitch.Jump(dlg, target);
                var frame = new System.Windows.Threading.DispatcherFrame(); var t = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromMilliseconds(1500) };
                t.Tick += (_, _) => { t.Stop(); frame.Continue = false; }; t.Start(); System.Windows.Threading.Dispatcher.PushFrame(frame);
                string addr = AddressOf(dlg), name = NameBoxOf(dlg);
                bool ok = jumped && addr.Contains(Path.GetFileName(target), StringComparison.OrdinalIgnoreCase) && name == "keep-me.txt";
                result = (ok ? "PASS" : "FAIL") + $" dialog jump  (address: \"{addr}\", file name kept: \"{name}\")";
                PostMessage(dlg, 0x0010 /*WM_CLOSE*/, IntPtr.Zero, IntPtr.Zero);
            }
        }
        finally { Thread.Sleep(500); try { if (!ps.HasExited) ps.Kill(); } catch { } }
        File.WriteAllText(reportFile, result);
        return result.StartsWith("PASS") ? 0 : 1;
    }

    private static IntPtr FindDialog(int pid)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, _) => { GetWindowThreadProcessId(h, out uint p); if (p == pid && IsWindowVisible(h) && DialogSwitchProbe(h) && DialogSwitch.FindNameBox(h) != IntPtr.Zero) { found = h; return false; } return true; }, IntPtr.Zero);
        return found;
    }

    // the same test DialogSwitch uses, minus its "not our own process" rule
    private static bool DialogSwitchProbe(IntPtr h)
    {
        var sb = new System.Text.StringBuilder(64); GetClassName(h, sb, 64); if (sb.ToString() != "#32770") return false;
        bool view = false; EnumChildWindows(h, (c, _) => { var s = new System.Text.StringBuilder(64); GetClassName(c, s, 64); if (s.ToString() == "DUIViewWndClassName") view = true; return !view; }, IntPtr.Zero);
        return view;
    }

    private static string AddressOf(IntPtr dlg)
    {
        string addr = "";
        EnumChildWindows(dlg, (c, _) => { var s = new System.Text.StringBuilder(64); GetClassName(c, s, 64); if (s.ToString() == "ToolbarWindow32") { var t = new System.Text.StringBuilder(512); GetWindowText(c, t, 512); if (t.ToString().Contains(':')) { addr = t.ToString(); return false; } } return true; }, IntPtr.Zero);
        return addr;
    }

    private static string NameBoxOf(IntPtr dlg)
    {
        IntPtr c = DialogSwitch.FindNameBox(dlg); if (c == IntPtr.Zero) return "(no file-name box)";
        var t = new System.Text.StringBuilder(1024); SendMessage(c, 0x000D, (IntPtr)1024, t); return t.ToString();
    }

    [System.Runtime.InteropServices.DllImport("user32.dll")] private static extern bool GetLayeredWindowAttributes(IntPtr h, out uint key, out byte alpha, out uint flags);
    private delegate bool EnumProc(IntPtr h, IntPtr l);
    [System.Runtime.InteropServices.DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc f, IntPtr l);
    [System.Runtime.InteropServices.DllImport("user32.dll")] private static extern bool EnumChildWindows(IntPtr h, EnumProc f, IntPtr l);
    [System.Runtime.InteropServices.DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [System.Runtime.InteropServices.DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr h);
    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] private static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] private static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
    [System.Runtime.InteropServices.DllImport("user32.dll")] private static extern int GetDlgCtrlID(IntPtr h);
    [System.Runtime.InteropServices.DllImport("user32.dll")] private static extern IntPtr GetParent(IntPtr h);
    [System.Runtime.InteropServices.DllImport("user32.dll")] private static extern bool PostMessage(IntPtr h, int m, IntPtr w, IntPtr l);
    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] private static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, System.Text.StringBuilder l);
}
