using System.Collections.Concurrent;
using System.IO;
using System.Runtime.InteropServices;

namespace DeskDrawer;

/// "Is this folder there?" asked from the window's own thread. On a local disk that is instant. On a network drive that has gone away, a share whose server is off
/// or a failing USB stick, Windows can take 20 seconds or more to answer, and the window was frozen for all of it (once per check: a start with five such tabs
/// froze five times over). Here those paths are asked on another thread and given a second and a half; a place that did not answer is not asked again for ten seconds.
internal static class Reach
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern uint GetDriveType(string root);
    private static readonly ConcurrentDictionary<string, long> Dead = new(StringComparer.OrdinalIgnoreCase);   // root -> when it last failed to answer
    private const int WaitMs = 1500, RememberMs = 10_000;

    /// "C:\" for C:\x\y, "\\server\share\" for a network path, "" when it has neither.
    internal static string RootOf(string path)
    {
        try { return Path.GetPathRoot(path) is { Length: > 0 } r ? (r.EndsWith('\\') ? r : r + "\\") : ""; } catch { return ""; }
    }

    /// A drive letter on a disk inside this PC: the only kind that always answers at once. (GetDriveType reads what Windows already knows; it does not touch the disk.)
    internal static bool IsLocalDisk(string path) { string r = RootOf(path); return r.Length == 3 && r[1] == ':' && GetDriveType(r) == 3 /*DRIVE_FIXED*/; }

    private static bool Ask(string path, Func<string, bool> exists)
    {
        if (string.IsNullOrEmpty(path)) return false;
        if (IsLocalDisk(path)) return exists(path);
        string root = RootOf(path);
        if (Dead.TryGetValue(root, out long when)) { if (Environment.TickCount64 - when < RememberMs) return false; Dead.TryRemove(root, out _); }
        var t = Task.Run(() => { try { return exists(path); } catch { return false; } });
        if (t.Wait(WaitMs)) return t.Result;
        Dead[root] = Environment.TickCount64; return false;   // still waiting: the place is not answering
    }

    internal static bool Dir(string path) => Ask(path, Directory.Exists);
    internal static bool File(string path) => Ask(path, System.IO.File.Exists);
}
