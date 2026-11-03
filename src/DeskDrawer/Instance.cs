using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Windows;

namespace DeskDrawer;

/// One DeskDrawer window: opening a folder (double-click on the desktop, Win+E, a shortcut) while DeskDrawer is already open sends the folder to it
/// as a new tab instead of starting a second window. Ctrl+N passes --new-window to get a real second window.
public static class Instance
{
    internal static string PipeName = "DeskDrawer-" + Environment.UserName + "-" + System.Diagnostics.Process.GetCurrentProcess().SessionId;

    [DllImport("user32.dll")] private static extern bool AllowSetForegroundWindow(int pid);

    /// Quick check without connecting: is someone listening on our pipe? Listing the pipe folder doesn't open the pipe: File.Exists() would, and use up the one waiting connection.
    public static bool IsRunning() => System.IO.Directory.EnumerateFileSystemEntries(@"\\.\pipe\").Any(n => n.EndsWith(PipeName, StringComparison.Ordinal));

    /// True if a running DeskDrawer took the folders (this process should then quit).
    public static bool HandOff(IReadOnlyList<string> paths)
    {
        try
        {
            using var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.Out, PipeOptions.CurrentUserOnly);
            if (!IsRunning()) return false;
            pipe.Connect(250);
            AllowSetForegroundWindow(-1);   // ASFW_ANY: let the running window come to the front
            using var w = new StreamWriter(pipe); w.Write(string.Join("\n", paths)); w.Flush();
            return true;
        }
        catch { return false; }   // nobody listening: we are the first one
    }

    /// The first window listens for folders sent by later launches. receive runs on the UI thread with the paths (empty = just come to the front).
    public static void Listen(Action<List<string>> receive)
    {
        new Thread(() =>
        {
            for (;;)
            {
                try
                {
                    using var server = new NamedPipeServerStream(PipeName, PipeDirection.In, 1, PipeTransmissionMode.Byte, PipeOptions.CurrentUserOnly);
                    server.WaitForConnection();
                    using var r = new StreamReader(server); var text = r.ReadToEnd();
                    var paths = text.Split('\n', StringSplitOptions.RemoveEmptyEntries).ToList();
                    Application.Current?.Dispatcher.BeginInvoke(() => receive(paths));
                }
                catch { Thread.Sleep(500); }
            }
        }) { IsBackground = true, Name = "DeskDrawer instance" }.Start();
    }
}
