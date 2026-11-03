using System.IO;
using System.Windows;

namespace DeskDrawer;

/// The folders pinned in File Explorer's "Quick access" (Desktop, Downloads, your course folders, Google Drive...), read from the shell so DeskDrawer follows your pins.
public static class Pinned
{
    public static string LastError = "";
    private static readonly object ShellGate = new();

    /// The pinned folders live in the shell's "Frequent folders" list; a pinned one has an "Unpin from Quick access" verb. Calls back on the UI thread.
    public static void Read(Action<List<(string Name, string Path)>> done)
    {
        Thumbs.OnSta(() =>
        {
            var list = new List<(string, string)>(); var sw = System.Diagnostics.Stopwatch.StartNew();
            lock (ShellGate)   // two shell reads on two threads at once break each other's COM objects ("separated from its RCW")
            for (int attempt = 0; attempt < 3; attempt++, Thread.Sleep(80))
            try
            {
                list.Clear(); LastError = "";
                dynamic sh = Activator.CreateInstance(Type.GetTypeFromProgID("Shell.Application")!)!;
                dynamic ns = sh.Namespace("shell:::{3936E9E4-D92C-4EEE-A85A-BC16D5EA0819}");
                dynamic items = ns.Items(); int count = items.Count;   // indexed, because COM collections do not always enumerate through dynamic
                for (int i = 0; i < count; i++)
                {
                    dynamic it = items.Item(i);
                    if (!(bool)it.IsFolder) continue;
                    string path = it.Path; if (string.IsNullOrEmpty(path) || !Directory.Exists(path)) continue;
                    if (it.ExtendedProperty("System.Home.IsPinned") is true) list.Add(((string)it.Name, path));   // Verbs() would also work but takes ~8 s
                }
                break;
            }
            catch (Exception ex) { LastError = ex.GetType().Name + ": " + ex.Message; }
            if (LastError.Length == 0) LastError = $"read {list.Count} in {sw.ElapsedMilliseconds}ms";
            Application.Current?.Dispatcher.BeginInvoke(() => done(list));
        });
    }

    /// Pins or unpins a folder in Quick access (the same list File Explorer shows), then calls back on the UI thread.
    public static void SetPinned(string path, bool pin, Action done)
    {
        Thumbs.OnSta(() =>
        {
            lock (ShellGate)
                try { dynamic sh = Activator.CreateInstance(Type.GetTypeFromProgID("Shell.Application")!)!; sh.Namespace(path).Self.InvokeVerb(pin ? "pintohome" : "unpinfromhome"); }
                catch (Exception ex) { LastError = ex.Message; }
            Application.Current?.Dispatcher.BeginInvoke(done);
        });
    }

    /// Files you opened lately, newest first: Windows keeps a shortcut for each in the Recent folder.
    public static void Recent(int max, Action<List<(string Path, DateTime When)>> done)
    {
        Thumbs.OnSta(() =>
        {
            var list = new List<(string, DateTime)>(); var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            try
            {
                dynamic sh = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell")!)!;
                foreach (var f in new DirectoryInfo(Environment.GetFolderPath(Environment.SpecialFolder.Recent)).GetFiles("*.lnk").OrderByDescending(f => f.LastWriteTime))
                {
                    string target; try { target = sh.CreateShortcut(f.FullName).TargetPath; } catch { continue; }
                    if (string.IsNullOrEmpty(target) || !File.Exists(target) || !seen.Add(target)) continue;   // files only, still there
                    list.Add((target, f.LastWriteTime)); if (list.Count >= max) break;
                }
            }
            catch { }
            Application.Current?.Dispatcher.BeginInvoke(() => done(list));
        });
    }

    /// The names File Explorer's navigation pane gives these folders (OneDrive roots show as "Your Name - Personal").
    public static void DisplayNames(List<string> paths, Action<List<(string Path, string Name)>> done)
    {
        Thumbs.OnSta(() =>
        {
            var list = new List<(string, string)>();
            lock (ShellGate)
            foreach (var p in paths)
                for (int attempt = 0; attempt < 3; attempt++)   // the first call sometimes hits a released COM object (the shell is still warming up): try again
                    try { dynamic sh = Activator.CreateInstance(Type.GetTypeFromProgID("Shell.Application")!)!; string name = sh.Namespace(p).Self.Name; if (name.Length > 0) list.Add((p, name)); break; }
                    catch (Exception ex) { LastError += " names: " + ex.Message; Thread.Sleep(50); }
            Application.Current?.Dispatcher.BeginInvoke(() => done(list));
        });
    }
}
