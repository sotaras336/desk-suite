using System.Diagnostics;
using System.IO;
using System.Text;
using System.Windows;

namespace DeskDrawer;

public static partial class SelfTest
{
    /// --settingsshot file.png: opens the Settings window off screen and saves a picture of it.
    public static int SettingsShot(string png)
    {
        var w = new MainWindow { Left = -32000, Top = 0, ShowInTaskbar = false }; w.Show(); w.ShowSettings(); var sw = w._settingsWin!; sw.Left = -32000;
        var frame = new System.Windows.Threading.DispatcherFrame(); var t = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromMilliseconds(1500) };
        t.Tick += (_, _) => { t.Stop(); frame.Continue = false; }; t.Start(); System.Windows.Threading.Dispatcher.PushFrame(frame);
        var el = (System.Windows.FrameworkElement)sw.Content; double k = System.Windows.Media.VisualTreeHelper.GetDpi(el).DpiScaleX;
        var rtb = new System.Windows.Media.Imaging.RenderTargetBitmap((int)(el.ActualWidth * k), (int)(el.ActualHeight * k), 96 * k, 96 * k, System.Windows.Media.PixelFormats.Pbgra32);
        var dv = new System.Windows.Media.DrawingVisual(); using (var dc = dv.RenderOpen()) { var r = new System.Windows.Rect(0, 0, el.ActualWidth, el.ActualHeight); dc.DrawRectangle(sw.Background, null, r); dc.DrawRectangle(new System.Windows.Media.VisualBrush(el), null, r); }
        rtb.Render(dv); var enc = new System.Windows.Media.Imaging.PngBitmapEncoder(); enc.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(rtb));
        using (var fs = File.Create(png)) enc.Save(fs);
        App.Quitting = true; return 0;
    }

    /// --pickshot file.png: opens the picker over a pretend dialog, waits for the folder and icons, saves a picture of it (for looking at the design).
    public static int PickShot(string png)
    {
        var docs = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
        var w = MainWindow.ForPick(docs, "", "Open", new[] { "Word Documents (*.docx;*.doc)", "OpenDocument Text (*.odt)", "All Files (*.*)" }, 0, "Open", 300, 200, 1300, 800);
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
                Application.Current.ShutdownMode = ShutdownMode.OnExplicitShutdown;   // the test has no main window: closing the first picker must not end it
                DialogSwitch.Start(() => new List<string> { root }); DialogSwitch.TakeOver = true; MainWindow.PrewarmPicker(); { var fr0 = new System.Windows.Threading.DispatcherFrame(); var t0 = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromMilliseconds(3000) }; t0.Tick += (_, _) => { t0.Stop(); fr0.Continue = false; }; t0.Start(); System.Windows.Threading.Dispatcher.PushFrame(fr0); }   // as in the app: built and drawn before the first dialog
                var ps = Process.Start(new ProcessStartInfo("powershell.exe", "-NoProfile -Command \"" + head + "$d = New-Object System.Windows.Forms.SaveFileDialog; $d.InitialDirectory = 'C:\\Windows'; $d.FileName = 'keep.txt'; if ($d.ShowDialog() -eq 'OK') { [Console]::Out.Write($d.FileName) }\"") { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true })!;
                void Pump(int ms) { var fr = new System.Windows.Threading.DispatcherFrame(); var tm = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromMilliseconds(ms) }; tm.Tick += (_, _) => { tm.Stop(); fr.Continue = false; }; tm.Start(); System.Windows.Threading.Dispatcher.PushFrame(fr); }
                var sw = Stopwatch.StartNew(); while (!DialogSwitch.PickerOpen && sw.ElapsedMilliseconds < 10000) Pump(25);
                Check("takeover: the picker opens by itself", DialogSwitch.PickerOpen, $"{sw.ElapsedMilliseconds} ms after the process started");
                IntPtr any = IntPtr.Zero; EnumWindows((h, _) => { GetWindowThreadProcessId(h, out uint p); if (p == ps.Id && DialogSwitchProbe(h)) { any = h; return false; } return true; }, IntPtr.Zero);
                Check("takeover: Windows' own dialog is not on screen", any != IntPtr.Zero && (!IsWindowVisible(any) || GetLayeredWindowAttributes(any, out _, out byte al, out _) && al == 0));   // hidden, or fully see-through
                string want = Path.Combine(root, "answered.txt"); DialogSwitch.TestAnswer(want); Pump(2500);
                string ans = ps.WaitForExit(4000) ? ps.StandardOutput.ReadToEnd().Trim() : "(still open)";
                Check("takeover: the app gets the path answered in the picker", ans == want, $"got \"{ans}\"");
                if (!ps.HasExited) ps.Kill();
                // Cancel: the app gets "cancelled", its dialog never comes back on screen, and the picker is gone
                var ps2 = Process.Start(new ProcessStartInfo("powershell.exe", "-NoProfile -Command \"" + head + "$d = New-Object System.Windows.Forms.OpenFileDialog; $r = $d.ShowDialog(); [Console]::Out.Write([string]$r)\"") { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true })!;
                sw.Restart(); while (!DialogSwitch.PickerOpen && sw.ElapsedMilliseconds < 10000) Pump(25);
                Check("cancel: the picker opens for a second dialog", DialogSwitch.PickerOpen);
                DialogSwitch.TestCancel(); bool flashed = false;
                for (int i = 0; i < 60 && !ps2.HasExited; i++) { Pump(50); EnumWindows((h, _) => { GetWindowThreadProcessId(h, out uint pp); if (pp == ps2.Id && DialogSwitchProbe(h) && IsWindowVisible(h)) flashed = true; return true; }, IntPtr.Zero); }
                string r2 = ps2.WaitForExit(3000) ? ps2.StandardOutput.ReadToEnd().Trim() : "(still open)";
                Check("cancel: the app is told Cancel and its own dialog never shows", r2 == "Cancel" && !flashed, $"got \"{r2}\", dialog shown: {flashed}");
                Pump(300); Check("cancel: the picker is gone", !DialogSwitch.PickerOpen && !Application.Current.Windows.OfType<MainWindow>().Any(w => w.IsVisible));
                if (!ps2.HasExited) ps2.Kill();
                // A folder picker (the Vista-style "Select Folder" Quartus shows): the app gets the folder that was open in the picker
                string fps = Path.Combine(root, "pickfolder.ps1"), target = Path.Combine(root, "chosen folder"); Directory.CreateDirectory(target);
                File.WriteAllText(fps, "Add-Type -TypeDefinition @'\nusing System; using System.Runtime.InteropServices;\n" +
                    "[ComImport, Guid(\"43826d1e-e718-42ee-bc55-a1e261c37bfe\"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)] public interface IShellItem { void BindToHandler(IntPtr a, ref Guid b, ref Guid c, out IntPtr d); void GetParent(out IShellItem p); void GetDisplayName(uint s, [MarshalAs(UnmanagedType.LPWStr)] out string n); }\n" +
                    "[ComImport, Guid(\"42f85136-db7e-439c-85f1-e4075d135fc8\"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)] public interface IFileDialog { [PreserveSig] int Show(IntPtr p); void SetFileTypes(uint c, IntPtr f); void SetFileTypeIndex(uint i); void GetFileTypeIndex(out uint i); void Advise(IntPtr e, out uint c); void Unadvise(uint c); void SetOptions(uint o); void GetOptions(out uint o); void SetDefaultFolder(IShellItem i); void SetFolder(IShellItem i); void GetFolder(out IShellItem i); void GetCurrentSelection(out IShellItem i); void SetFileName([MarshalAs(UnmanagedType.LPWStr)] string n); void GetFileName([MarshalAs(UnmanagedType.LPWStr)] out string n); void SetTitle([MarshalAs(UnmanagedType.LPWStr)] string t); void SetOkButtonLabel([MarshalAs(UnmanagedType.LPWStr)] string t); void SetFileNameLabel([MarshalAs(UnmanagedType.LPWStr)] string t); void GetResult(out IShellItem i); }\n" +
                    "[ComImport, Guid(\"DC1C5A9C-E88A-4dde-A5A1-60F82A20AEF7\")] public class FileOpenDialogCo { }\n" +
                    "public static class Pick { public static string Folder() { var d = (IFileDialog)new FileOpenDialogCo(); d.SetOptions(0x20 | 0x40); if (d.Show(IntPtr.Zero) != 0) return \"Cancel\"; IShellItem r; d.GetResult(out r); string p; r.GetDisplayName(0x80058000, out p); return p; } }\n" +
                    "'@\n[Console]::Out.Write([Pick]::Folder())\n");
                var ps3 = Process.Start(new ProcessStartInfo("powershell.exe", "-NoProfile -ExecutionPolicy Bypass -File \"" + fps + "\"") { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true })!;
                sw.Restart(); while (!DialogSwitch.PickerOpen && sw.ElapsedMilliseconds < 15000) Pump(25);
                Check("folder picker: the picker opens for a Select Folder dialog", DialogSwitch.PickerOpen);
                if (DialogSwitch.PickerOpen) DialogSwitch.TestAnswer(target);
                Pump(3000); EnumWindows((h, _) => { GetWindowThreadProcessId(h, out uint pp); if (pp == ps3.Id && DialogSwitchProbe(h)) { var t = new System.Text.StringBuilder(256); GetWindowText(h, t, 256); sb.AppendLine($"INFO folder dialog still there: title \"{t}\" visible={IsWindowVisible(h)} address \"{AddressOf(h)}\" name \"{NameBoxOf(h)}\""); } return true; }, IntPtr.Zero);
                string r3 = ps3.WaitForExit(6000) ? ps3.StandardOutput.ReadToEnd().Trim() : "(still open)";
                Check("folder picker: the app gets the folder chosen in the picker", string.Equals(r3, target, StringComparison.OrdinalIgnoreCase), $"got \"{r3}\"");
                if (!ps3.HasExited) ps3.Kill();
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
