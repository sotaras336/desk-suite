using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Windows;

namespace DeskDrawer;

/// Compress to a .zip and Extract all. Both show how far they are in the status bar and stop when you press Esc (they used to run with no sign of life and no
/// way to stop them: a 10 GB folder was a long blind wait). Extract also opens .7z, .rar, .tar and friends, through the tar.exe that Windows itself ships.
public partial class MainWindow
{
    private static readonly HashSet<string> Archives = new(StringComparer.OrdinalIgnoreCase) { ".zip", ".7z", ".rar", ".tar", ".gz", ".tgz", ".bz2", ".xz" };
    private CancellationTokenSource? _zipCts;

    /// Esc while a zip job runs. True when there was one to stop.
    private bool CancelZip() { if (_zipCts == null) return false; _zipCts.Cancel(); Status.Text = "Stopping…"; return true; }

    private void Zip(List<string> paths)
    {
        if (paths.Count == 0 || Path.GetDirectoryName(paths[0].TrimEnd('\\')) is not { } dir) return;
        string stem = paths.Count == 1 ? Path.GetFileNameWithoutExtension(paths[0].TrimEnd('\\')) : Path.GetFileName(dir.TrimEnd('\\'));
        string target = ShellOps.Unique(dir, string.IsNullOrEmpty(stem) ? "Archive" : stem, ".zip");
        RunZipJob("Compressing", (say, ct) => ZipTo(paths, target, say, ct), dir, target, "Compressed to ");
    }

    internal static void ZipTo(List<string> paths, string target, Action<double>? say, CancellationToken ct)
    {
        // first what there is to do (so the percentage is real), then the work
        var files = new List<(string File, string Name)>(); var emptyDirs = new List<string>();
        foreach (var p in paths)
        {
            if (!Directory.Exists(p)) { files.Add((p, Path.GetFileName(p))); continue; }
            string baseDir = Path.GetDirectoryName(p.TrimEnd('\\'))!;
            foreach (var f in Directory.EnumerateFiles(p, "*", SearchOption.AllDirectories)) { ct.ThrowIfCancellationRequested(); files.Add((f, Path.GetRelativePath(baseDir, f).Replace('\\', '/'))); }
            foreach (var d in Directory.EnumerateDirectories(p, "*", SearchOption.AllDirectories).Prepend(p))   // keep empty folders too
                if (!Directory.EnumerateFileSystemEntries(d).Any()) emptyDirs.Add(Path.GetRelativePath(baseDir, d).Replace('\\', '/') + "/");
        }
        long total = 0, done = 0; foreach (var f in files) { try { total += new FileInfo(f.File).Length; } catch { } }
        using var z = ZipFile.Open(target, ZipArchiveMode.Create); var buf = new byte[1 << 17];
        foreach (var (file, name) in files)
        {
            ct.ThrowIfCancellationRequested();
            var e = z.CreateEntry(name, CompressionLevel.Optimal); try { e.LastWriteTime = File.GetLastWriteTime(file); } catch { }
            using var src = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete); using var dst = e.Open();
            for (int n; (n = src.Read(buf, 0, buf.Length)) > 0;) { ct.ThrowIfCancellationRequested(); dst.Write(buf, 0, n); done += n; say?.Invoke(total > 0 ? (double)done / total : 1); }
        }
        foreach (var d in emptyDirs) z.CreateEntry(d);
    }

    private void Unzip(string archive)
    {
        if (Path.GetDirectoryName(archive) is not { } dir) return;
        string stem = Path.GetFileNameWithoutExtension(archive); if (stem.EndsWith(".tar", StringComparison.OrdinalIgnoreCase)) stem = stem[..^4];   // "x.tar.gz" -> "x"
        string target = ShellOps.Unique(dir, stem, "");
        bool zip = Path.GetExtension(archive).Equals(".zip", StringComparison.OrdinalIgnoreCase);
        RunZipJob("Extracting", (say, ct) => { if (zip) UnzipTo(archive, target, say, ct); else UntarTo(archive, target, ct); }, dir, target, "Extracted to ");
    }

    internal static void UnzipTo(string zip, string target, Action<double>? say, CancellationToken ct)
    {
        using var z = ZipFile.OpenRead(zip); long total = 0, done = 0; foreach (var e in z.Entries) total += e.Length;
        string root = Path.GetFullPath(target).TrimEnd('\\') + "\\"; Directory.CreateDirectory(root); var buf = new byte[1 << 17];
        foreach (var e in z.Entries)
        {
            ct.ThrowIfCancellationRequested();
            // a name inside a zip is not to be trusted: "..\..\Startup\x.exe" would land outside the folder being made. Anything that does not stay inside is refused.
            string dest = Path.GetFullPath(Path.Combine(root, e.FullName));
            if (!dest.StartsWith(root, StringComparison.OrdinalIgnoreCase)) throw new IOException("This zip has an entry that tries to write outside its own folder (“" + e.FullName + "”). Nothing more was extracted.");
            if (e.FullName.EndsWith('/') || e.FullName.EndsWith('\\')) { Directory.CreateDirectory(dest); continue; }
            Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
            using (var src = e.Open()) using (var dst = new FileStream(dest, FileMode.CreateNew, FileAccess.Write))
                for (int n; (n = src.Read(buf, 0, buf.Length)) > 0;) { ct.ThrowIfCancellationRequested(); dst.Write(buf, 0, n); done += n; say?.Invoke(total > 0 ? (double)done / total : 1); }
            try { File.SetLastWriteTime(dest, e.LastWriteTime.DateTime); } catch { }
        }
    }

    /// .7z, .rar, .tar, .gz...: Windows 10 and 11 ship tar.exe (libarchive), which reads them all. It gives no progress, so the status bar only says it is working.
    private static void UntarTo(string archive, string target, CancellationToken ct)
    {
        string tar = Path.Combine(Environment.SystemDirectory, "tar.exe");
        if (!File.Exists(tar)) throw new IOException("This Windows has no built-in tool for this kind of archive. Only .zip can be extracted here.");
        Directory.CreateDirectory(target);
        var psi = new ProcessStartInfo(tar) { UseShellExecute = false, CreateNoWindow = true, RedirectStandardError = true };
        psi.ArgumentList.Add("-xf"); psi.ArgumentList.Add(archive); psi.ArgumentList.Add("-C"); psi.ArgumentList.Add(target);   // as separate arguments: no quoting to get wrong
        using var p = Process.Start(psi)!; var err = p.StandardError.ReadToEndAsync();
        while (!p.WaitForExit(200)) if (ct.IsCancellationRequested) { try { p.Kill(true); } catch { } ct.ThrowIfCancellationRequested(); }
        if (p.ExitCode != 0) { string m = err.Result.Trim(); throw new IOException(m.Length > 0 ? m.Split('\n')[^1].Trim() : "Windows couldn't open this archive."); }
    }

    /// Runs a zip / unzip off the UI thread with progress in the status bar; on failure or Esc removes the half-made result, on success shows and selects it.
    private void RunZipJob(string verb, Action<Action<double>, CancellationToken> job, string dir, string target, string done)
    {
        if (_zipCts != null) { Flash("One archive at a time: the other is still running"); return; }
        var cts = _zipCts = new CancellationTokenSource(); Status.Text = verb + "…"; long last = 0;
        void Say(double part)   // from the worker: at most five updates a second reach the window
        {
            long now = Environment.TickCount64; if (now - Interlocked.Read(ref last) < 200) return; Interlocked.Exchange(ref last, now);
            Dispatcher.BeginInvoke(() => { if (_zipCts == cts && !cts.IsCancellationRequested) Status.Text = $"{verb}… {Math.Clamp(part, 0, 1):P0}   ·   Esc to stop"; });
        }
        if (verb == "Extracting" && !target.EndsWith(".zip")) Status.Text = verb + "…   ·   Esc to stop";
        Task.Run(() => job(Say, cts.Token)).ContinueWith(t => Dispatcher.BeginInvoke(() =>
        {
            _zipCts = null; cts.Dispose();
            if (t.Exception != null || t.IsCanceled)
            {
                try { if (File.Exists(target)) File.Delete(target); else if (Directory.Exists(target)) Directory.Delete(target, true); } catch { }
                var why = t.Exception?.GetBaseException();
                if (why is OperationCanceledException || t.IsCanceled) Flash("Stopped. Nothing was kept.");
                else { MessageBox.Show(this, why?.Message ?? "It didn't work.", "DeskDrawer", MessageBoxButton.OK, MessageBoxImage.Warning); UpdateStatus(); }
                return;
            }
            foreach (var tab in _tabs)
                if (string.Equals(tab.Current, dir, StringComparison.OrdinalIgnoreCase)) { tab.SelectAfterLoad = new HashSet<string>(new[] { target }, StringComparer.OrdinalIgnoreCase); LoadTab(tab); }
            Flash(done + Path.GetFileName(target));
        }));
    }
}
