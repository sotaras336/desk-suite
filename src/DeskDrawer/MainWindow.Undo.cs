using System.Diagnostics;
using System.IO;
using System.Windows;

namespace DeskDrawer;

/// Ctrl+Z: takes back the last rename, move, copy or new item. Only what DeskDrawer itself did, and only while it is still safe to: a move goes back
/// only if nothing has taken the old name since, a copy is undone by sending the copies (never the originals) to the Recycle Bin.
public partial class MainWindow
{
    private sealed record UndoStep(string Label, Func<IntPtr, bool> Run);
    private readonly List<UndoStep> _undo = new();

    private void PushUndo(string label, Func<IntPtr, bool> run) { _undo.Add(new UndoStep(label, run)); if (_undo.Count > 50) _undo.RemoveAt(0); }

    private void Undo()
    {
        if (_undo.Count == 0) { Flash("Nothing to undo"); return; }
        var u = _undo[^1]; _undo.RemoveAt(_undo.Count - 1);
        var h = Hwnd;
        RunOp(() => u.Run(h), null, null, () => Flash("Undid " + u.Label));
    }

    private static bool Exists(string p) => File.Exists(p) || Directory.Exists(p);

    /// Copy or move through the shell (its own progress and conflict dialogs), remembering how to take it back.
    private void Transfer(IList<string> src, string dest, bool move, bool sameFolder)
    {
        var h = Hwnd;
        var landed = src.Select(p => Path.Combine(dest, Path.GetFileName(p.TrimEnd('\\')))).ToList();
        // only items that did not exist at the destination before can be undone safely (anything else was replaced or renamed by the shell)
        var fresh = src.Select((p, i) => (From: p, To: landed[i])).Where(x => !Exists(x.To)).ToList();
        RunOp(() => move ? ShellOps.Move(src, dest, h) : ShellOps.Copy(src, dest, h, keepBothOnCollision: sameFolder), landed, dest, () =>
        {
            Flash($"{(move ? "Moved" : "Copied")} " + (src.Count == 1 ? "“" + Path.GetFileName(src[0].TrimEnd('\\')) + "”" : $"{src.Count:N0} items"));
            if (fresh.Count == 0 || (!move && sameFolder)) return;   // "keep both" copies got new names we can't know
            string what = (fresh.Count == 1 ? Path.GetFileName(fresh[0].To) : $"{fresh.Count} items");
            if (move)
                PushUndo("move of " + what, owner => fresh.Where(x => Exists(x.To) && !Exists(x.From)).GroupBy(x => Path.GetDirectoryName(x.From)!)
                                                      .All(g => ShellOps.Move(g.Select(x => x.To).ToList(), g.Key, owner)));
            else
                PushUndo("copy of " + what, owner => { var del = fresh.Select(x => x.To).Where(Exists).ToList(); return del.Count == 0 || ShellOps.Delete(del, owner); });
        });
    }

    private void UndoRename(string from, string to) => PushUndo("rename", _ =>
    {
        if (!Exists(to) || (Exists(from) && !string.Equals(from, to, StringComparison.OrdinalIgnoreCase))) return false;
        if (Directory.Exists(to)) Directory.Move(to, from); else File.Move(to, from);
        return true;
    });

    private void UndoNew(string path) => PushUndo("new " + Path.GetFileName(path), owner => !Exists(path) || ShellOps.Delete(new[] { path }, owner));

    /// Folders sent by a later launch (double-click on the desktop, Win+E...): each becomes a tab here, and the window comes to the front.
    public void OpenFromOutside(List<string> paths)
    {
        if (paths is ["--quit"]) { App.Quitting = true; Close(); Application.Current.Shutdown(); return; }   // Shutdown too: a hidden helper window (the dialog button, the picker) would keep the process, and the exe locked, after Close()   // Setup is replacing or removing this copy
        if (App.Background) EndWarmUp();   // asked for before the sign-in warm-up finished
        if (!IsVisible) Wake(paths);   // waking from the background: the folders asked for, or the last session
        else foreach (var p in paths)
        {
            if (!IsVirtual(p) && File.Exists(p) && Path.GetDirectoryName(p) is { } dir) { NewTab(dir); _tab.SelectAfterLoad.Add(p); }   // a file: its folder, the file selected
            else if (IsVirtual(p) || Directory.Exists(p)) NewTab(p);
        }
        if (WindowState == WindowState.Minimized) WindowState = WindowState.Normal;
        Activate(); Topmost = true; Topmost = false; Focus();
    }

    /// What a Save / Open dialog's "DeskDrawer folders" button offers: the open tabs (this one first), then the pinned folders.
    public List<string> FoldersToOffer()
    {
        var list = new List<string>();
        foreach (var p in new[] { _tab.Current }.Concat(_tabs.Select(t => t.Current)).Concat(_pinned))
            if (p != null && !IsVirtual(p) && Directory.Exists(p) && !list.Contains(p, StringComparer.OrdinalIgnoreCase)) list.Add(p);
        return list;
    }

    /// Ctrl+N: another DeskDrawer window on the same folder.
    private void OpenNewWindow(string? at = null)
    {
        string here = at ?? _tab.Current ?? ""; if (here.EndsWith('\\')) here += ".";   // "C:\" would escape the closing quote
        try { if (Environment.ProcessPath is { } exe) Process.Start(new ProcessStartInfo(exe, "--new-window \"" + here + "\"") { UseShellExecute = false }); }
        catch (Exception ex) { MessageBox.Show(this, ex.Message, "DeskDrawer", MessageBoxButton.OK, MessageBoxImage.Warning); }
    }
}
