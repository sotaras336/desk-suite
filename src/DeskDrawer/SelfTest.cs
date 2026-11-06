using System.IO;
using System.Text;

namespace DeskDrawer;

/// DeskDrawer.exe --selftest report.txt : exercises the file operations and the shell menu on a throw-away folder, no windows, then exits.
public static class SelfTest
{
    public static int Run(string reportFile)
    {
        var sb = new StringBuilder(); int fail = 0;
        void Check(string name, bool ok, string extra = "") { sb.AppendLine((ok ? "PASS " : "FAIL ") + name + (extra.Length > 0 ? "  " + extra : "")); if (!ok) fail++; }
        {   // the update check: reading the checksum list, and (only when a test points DESK_UPDATE_BASE at a local release folder) the whole check and a verified download that is not run
            string sums = new string('a', 64) + " *" + Updater.Asset + "\r\n" + new string('b', 64) + " *Other-Setup.exe\n";
            Check("update: reads the checksum for this app's file", Updater.HashFor(sums, Updater.Asset) == new string('a', 64) && Updater.HashFor(sums, "Nope.exe") == null);
            if (Environment.GetEnvironmentVariable("DESK_UPDATE_BASE") != null)
            {
                Task.Run(() => Updater.CheckAsync()).GetAwaiter().GetResult();
                Check("update: sees a different build in the release", Updater.Newer, Updater.Status);
                bool got = Task.Run(() => Updater.UpdateNowAsync()).GetAwaiter().GetResult();
                Check("update: downloads, verifies and (in a test) does not run it", got && Updater.LastDownload != null && System.IO.File.Exists(Updater.LastDownload), Updater.Status + " " + Updater.LastDownload);
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
