using System.Diagnostics;
using System.IO;
using System.Text;

namespace DeskDrawer;

public static partial class SelfTest
{
    /// --pickshot file.png: opens the picker over a pretend dialog, waits for the folder and icons, saves a picture of it (for looking at the design).
    public static int PickShot(string png)
    {
        var docs = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
        var w = new PickerWindow(docs, "", "Open", new[] { "Word Documents (*.docx;*.doc)", "OpenDocument Text (*.odt)", "All Files (*.*)" }, 0, "Open", new List<string> { Path.Combine(docs, "..") }, 300, 200, 1300, 800);
        w.Show();
        var frame = new System.Windows.Threading.DispatcherFrame(); var t = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromMilliseconds(3000) };
        t.Tick += (_, _) => { t.Stop(); frame.Continue = false; }; t.Start(); System.Windows.Threading.Dispatcher.PushFrame(frame);
        var el = (System.Windows.FrameworkElement)w.Content; double k = System.Windows.Media.VisualTreeHelper.GetDpi(el).DpiScaleX;
        var rtb = new System.Windows.Media.Imaging.RenderTargetBitmap((int)(el.ActualWidth * k), (int)(el.ActualHeight * k), 96 * k, 96 * k, System.Windows.Media.PixelFormats.Pbgra32);
        var dv = new System.Windows.Media.DrawingVisual();   // the window's own colour first: the list is see-through
        using (var dc = dv.RenderOpen()) { var r = new System.Windows.Rect(0, 0, el.ActualWidth, el.ActualHeight); dc.DrawRectangle(w.Background, null, r); dc.DrawRectangle(new System.Windows.Media.VisualBrush(el), null, r); }
        rtb.Render(dv);
        var enc = new System.Windows.Media.Imaging.PngBitmapEncoder(); enc.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(rtb));
        using (var fs = File.Create(png)) enc.Save(fs);
        w.Close(); return 0;
    }

    /// --picktest report.txt: real Save and Open dialogs in another process (PowerShell) are answered the way the picker answers them (DialogSwitch.Fill) and the
    /// process reports what it received. Shows a dialog on screen for a moment each, so it is not part of --selftest.
    public static int PickTest(string reportFile)
    {
        var sb = new StringBuilder(); int fail = 0;
        void Check(string n, bool ok, string x = "") { sb.AppendLine((ok ? "PASS " : "FAIL ") + n + (x.Length > 0 ? "  " + x : "")); if (!ok) fail++; }
        string root = Path.Combine(Path.GetTempPath(), "dd-pick-" + Environment.ProcessId); Directory.CreateDirectory(root);
        string a = Path.Combine(root, "one one.txt"), b = Path.Combine(root, "two.txt"), saved = Path.Combine(root, "saved.txt");
        File.WriteAllText(a, "x"); File.WriteAllText(b, "y");
        const string head = "Add-Type -AssemblyName System.Windows.Forms; ";
        string Run(string script, Action<IntPtr> act)
        {
            var ps = Process.Start(new ProcessStartInfo("powershell.exe", "-NoProfile -Command \"" + head + script + "\"") { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true })!;
            IntPtr dlg = IntPtr.Zero;
            try
            {
                for (int i = 0; i < 60 && dlg == IntPtr.Zero; i++) { Thread.Sleep(250); dlg = FindDialog(ps.Id); }
                if (dlg == IntPtr.Zero) return "(no dialog)";
                act(dlg);
                var frame = new System.Windows.Threading.DispatcherFrame(); var t = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromMilliseconds(2600) };
                t.Tick += (_, _) => { t.Stop(); frame.Continue = false; }; t.Start(); System.Windows.Threading.Dispatcher.PushFrame(frame);
                if (!ps.WaitForExit(4000)) { PostMessage(dlg, 0x0010, IntPtr.Zero, IntPtr.Zero); return "(dialog still open)"; }
                return ps.StandardOutput.ReadToEnd().Trim();
            }
            finally { Thread.Sleep(300); try { if (!ps.HasExited) ps.Kill(); } catch { } }
        }
        try
        {
            string got = Run("$d = New-Object System.Windows.Forms.SaveFileDialog; $d.InitialDirectory = 'C:\\Windows'; $d.FileName = 'keep.txt'; $d.Filter = 'Text (*.txt)|*.txt|All files (*.*)|*.*'; if ($d.ShowDialog() -eq 'OK') { [Console]::Out.Write($d.FileName) }",
                dlg =>
                {
                    var d = DialogSwitch.Describe(dlg);
                    Check("save: the dialog's folder, name, button and file types are read", d.Folder.Contains("Windows", StringComparison.OrdinalIgnoreCase) && d.Name == "keep.txt" && d.Button.Contains("Save"), $"(file types are best effort: this Windows may not expose them) folder=\"{d.Folder}\" name=\"{d.Name}\" button=\"{d.Button}\" patterns=\"{string.Join(";", d.Patterns)}\" title=\"{d.Title}\"");
                    DialogSwitch.Fill(dlg, new() { saved });
                });
            Check("save: the app receives the path that was picked", got == saved, $"got \"{got}\"");

            got = Run("$d = New-Object System.Windows.Forms.OpenFileDialog; $d.InitialDirectory = 'C:\\Windows'; if ($d.ShowDialog() -eq 'OK') { [Console]::Out.Write($d.FileName) }",
                dlg => { var d = DialogSwitch.Describe(dlg); Check("open: the button is Open", d.Button.Contains("Open"), $"button=\"{d.Button}\""); DialogSwitch.Fill(dlg, new() { a }); });
            Check("open: one file (a name with a space)", got == a, $"got \"{got}\"");

            got = Run("$d = New-Object System.Windows.Forms.OpenFileDialog; $d.Multiselect = $true; $d.InitialDirectory = 'C:\\Windows'; if ($d.ShowDialog() -eq 'OK') { [Console]::Out.Write($d.FileNames -join '|') }",
                dlg => DialogSwitch.Fill(dlg, new() { a, b }));
            Check("open: several files from one folder", got == a + "|" + b || got == b + "|" + a, $"got \"{got}\"");
        if (Environment.GetEnvironmentVariable("DD_PICK_LIVE") == "1")
        {   // the whole takeover with the real hooks: the dialog goes invisible at once, the picker opens, answering it gives the app the path
            try
            {
                DialogSwitch.Start(() => new List<string> { root }); DialogSwitch.TakeOver = true;
                var ps = Process.Start(new ProcessStartInfo("powershell.exe", "-NoProfile -Command \"" + head + "$d = New-Object System.Windows.Forms.SaveFileDialog; $d.InitialDirectory = 'C:\\Windows'; $d.FileName = 'keep.txt'; if ($d.ShowDialog() -eq 'OK') { [Console]::Out.Write($d.FileName) }\"") { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true })!;
                void Pump(int ms) { var fr = new System.Windows.Threading.DispatcherFrame(); var tm = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromMilliseconds(ms) }; tm.Tick += (_, _) => { tm.Stop(); fr.Continue = false; }; tm.Start(); System.Windows.Threading.Dispatcher.PushFrame(fr); }
                var sw = Stopwatch.StartNew(); while (!DialogSwitch.PickerOpen && sw.ElapsedMilliseconds < 10000) Pump(25);
                Check("takeover: the picker opens by itself", DialogSwitch.PickerOpen, $"{sw.ElapsedMilliseconds} ms after the process started");
                IntPtr any = IntPtr.Zero; EnumWindows((h, _) => { GetWindowThreadProcessId(h, out uint p); if (p == ps.Id && DialogSwitchProbe(h)) { any = h; return false; } return true; }, IntPtr.Zero);
                Check("takeover: Windows' own dialog is not on screen", any != IntPtr.Zero && !IsWindowVisible(any));
                string want = Path.Combine(root, "answered.txt"); DialogSwitch.TestAnswer(want); Pump(2500);
                string ans = ps.WaitForExit(4000) ? ps.StandardOutput.ReadToEnd().Trim() : "(still open)";
                Check("takeover: the app gets the path answered in the picker", ans == want, $"got \"{ans}\"");
                if (!ps.HasExited) ps.Kill();
            }
            catch (Exception ex) { Check("takeover live: no exception", false, ex.ToString()); }
        }
        }
        catch (Exception ex) { Check("no exception", false, ex.ToString()); }
        finally { try { Directory.Delete(root, true); } catch { } }
        sb.AppendLine($"{fail} failure(s)");
        File.WriteAllText(reportFile, sb.ToString());
        return fail;
    }
}
