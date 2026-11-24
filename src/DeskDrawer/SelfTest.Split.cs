using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;

namespace DeskDrawer;

/// Part of --selftest: tabs and split view (TabMath.cs, MainWindow.Split.cs). The rules first, then a real window off screen with two throw-away folders.
public static partial class SelfTest
{
    internal static void SplitTest(Action<string, bool, string> check)
    {
        check("tabs: split view picks the next tab for the other side, else the one before, else a new one",
            TabMath.PartnerFor(3, 0) == 1 && TabMath.PartnerFor(3, 2) == 1 && TabMath.PartnerFor(1, 0) == -1 && TabMath.PartnerFor(0, -1) == -1, "");
        check("tabs: Ctrl+1..8 pick that tab, Ctrl+9 the last, a missing number nothing",
            TabMath.ByNumber(1, 4) == 0 && TabMath.ByNumber(4, 4) == 3 && TabMath.ByNumber(5, 4) == -1 && TabMath.ByNumber(9, 4) == 3 && TabMath.ByNumber(9, 1) == 0 && TabMath.ByNumber(0, 4) == -1, "");
        check("tabs: next / previous go round, moving a tab stops at the ends",
            TabMath.Cycle(3, 1, 4) == 0 && TabMath.Cycle(0, -1, 4) == 3 && TabMath.Step(0, -1, 4) == 0 && TabMath.Step(3, 1, 4) == 3 && TabMath.Step(1, 1, 4) == 2, "");
        var kept = new List<int> { 0, 2, 3 };   // saved tab 1's folder was deleted
        check("tabs: after a missing folder is left out, the active tab and the split's other tab are still found",
            TabMath.Remap(kept, 2) == 1 && TabMath.Remap(kept, 1) == -1 && TabMath.Remap(kept, -1) == -1 && TabMath.RestorePartner(1, 0, 3) == 1 && TabMath.RestorePartner(0, 0, 3) == -1 && TabMath.RestorePartner(5, 0, 3) == -1, "");
        check("split view: neither side can be squeezed away", TabMath.Share(100, 900) == 0.2 && TabMath.Share(500, 500) == 0.5 && TabMath.Share(0, 0) == 0.5, "");
        check("split view: its keys (Ctrl+\\, F6, Ctrl+number, Ctrl+Page Up / Down, with Shift to move a tab)",
            TabMath.KeyFor(Key.Oem5, true, false, false) == "split" && TabMath.KeyFor(Key.F6, false, false, false) == "other side" && TabMath.KeyFor(Key.D3, true, false, false) == "tab 3"
            && TabMath.KeyFor(Key.NumPad9, true, false, false) == "tab 9" && TabMath.KeyFor(Key.PageDown, true, true, false) == "move right" && TabMath.KeyFor(Key.PageUp, true, false, false) == "previous"
            && TabMath.KeyFor(Key.D3, true, true, false) == null && TabMath.KeyFor(Key.D3, false, false, false) == null && TabMath.KeyFor(Key.F6, false, false, true) == null, "");   // Ctrl+Shift+3 is still a view
        check("split view: Delete, F2, Ctrl+C and Alt+Enter on the other side act there; plain arrows stay the list's own",
            TabMath.WorksOnPeer(Key.Delete, false, true, false) && TabMath.WorksOnPeer(Key.C, true, false, false) && TabMath.WorksOnPeer(Key.Enter, false, false, true)
            && !TabMath.WorksOnPeer(Key.Down, false, false, false) && !TabMath.WorksOnPeer(Key.A, true, false, false) && !TabMath.WorksOnPeer(Key.Enter, false, false, false), "");
        {
            var s = new Settings { Tabs = { @"C:\a", @"C:\b" }, ActiveTab = 1, SplitWith = 0, SplitRight = true, SplitShare = 0.35 };
            var back = System.Text.Json.JsonSerializer.Deserialize(System.Text.Json.JsonSerializer.Serialize(s, SettingsJson.Default.Settings), SettingsJson.Default.Settings)!;
            check("split view: saved with the tabs", back.SplitWith == 0 && back.SplitRight && Math.Abs(back.SplitShare - 0.35) < 1e-9 && new Settings().SplitWith == -1, "");
        }

        // ---- a real window, off screen (your own settings file is never written: Settings.NoSave) ----
        string root = Path.Combine(Path.GetTempPath(), "dd-split-" + Guid.NewGuid().ToString("N")[..8]);
        bool keep = Settings.NoSave; Settings.NoSave = true;
        MainWindow? w = null;
        try
        {
            Directory.CreateDirectory(root); string a = Path.Combine(root, "left"), b = Path.Combine(root, "right");
            Directory.CreateDirectory(a); Directory.CreateDirectory(b);
            File.WriteAllText(Path.Combine(a, "move me.txt"), "1"); File.WriteAllText(Path.Combine(a, "stay.txt"), "2"); File.WriteAllText(Path.Combine(b, "already here.txt"), "3");
            ShellOps.Silent = true;
            Theme.Load(Settings.Load().Theme); Theme.Apply();   // the window's brushes (as --settingsshot does)
            w = new MainWindow { Left = -32000, Top = 0, ShowInTaskbar = false, ShowActivated = false };
            w.Show(); Until(() => w.IsLoaded, 5000); Pump(300);
            w.SplitSelfTest(a, b, (n, ok, x) => check(n, ok, x), Until, Pump);
        }
        catch (Exception ex) { check("split view: no exception", false, ex.ToString()); }
        finally
        {
            try { w?.Close(); } catch { }
            Settings.NoSave = keep;
            try { Directory.Delete(root, true); } catch { }
        }
    }

    /// Lets the window work (load folders, lay out, run timers) for a while.
    internal static void Pump(int ms)
    {
        var frame = new DispatcherFrame(); var t = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(ms) };
        t.Tick += (_, _) => { t.Stop(); frame.Continue = false; }; t.Start(); Dispatcher.PushFrame(frame);
    }

    /// Lets the window work until cond is true (or ms have gone). Returns cond.
    internal static bool Until(Func<bool> cond, int ms)
    {
        var sw = System.Diagnostics.Stopwatch.StartNew();
        while (!cond() && sw.ElapsedMilliseconds < ms) Pump(50);
        return cond();
    }
}

public partial class MainWindow
{
    /// The window half of SelfTest.SplitTest: drives split view the way a person would, through the same methods the clicks and keys call.
    internal void SplitSelfTest(string a, string b, Action<string, bool, string> check, Func<Func<bool>, int, bool> until, Action<int> pump)
    {
        foreach (var o in _tabs.Where(x => x != _tab).ToList()) _tabs.Remove(o);   // only one tab to start with, whatever was open last time
        Navigate(a, push: false); until(() => !_tab.Loading && _tab.Entries != null, 5000);
        var left = _tab;

        SetSplit(true); pump(200);
        check("split view: turns on with a second tab of the same folder on the other side",
            SplitOn && _tabs.Count == 2 && _partner != left && _partner!.Current == a && _partner.IsPartner && !left.IsPartner && PeerPane.Visibility == Visibility.Visible
            && Grid.GetColumn(MainPane) == 0 && Grid.GetColumn(PeerPane) == 2 && ActiveMark.Visibility == Visibility.Visible, $"tabs {_tabs.Count}");
        var right = _partner;

        NavigateTab(right, b, push: true); until(() => !right.Loading && right.Entries != null, 5000); pump(100);
        check("split view: the other side shows its own folder", PeerList.Items.Count == 1 && PeerTitle.Text == "right" && List.Items.Count == 2 && _tab == left, $"peer {PeerList.Items.Count}, this side {List.Items.Count}");

        string moving = Path.Combine(a, "move me.txt");
        Transfer(new[] { moving }, b, move: true, sameFolder: false);   // what a drop on the other side does (Peer_Drop -> DoDrop -> Transfer)
        until(() => File.Exists(Path.Combine(b, "move me.txt")), 8000);
        until(() => PeerList.Items.Count == 2 && List.Items.Count == 1, 5000);
        check("split view: a file moved across shows up on the other side and leaves this one", PeerList.Items.Count == 2 && List.Items.Count == 1 && !File.Exists(moving), $"peer {PeerList.Items.Count}, this side {List.Items.Count}");

        var landed = right.Entries?.FirstOrDefault(x => x.Name == "move me.txt");
        if (landed != null) { PeerList.SelectedItems.Clear(); PeerList.SelectedItems.Add(landed); }
        WorkOnPeer(); pump(150);
        check("split view: working on the other side moves the full list there, keeps its selection, and each folder stays on its side",
            _tab == right && _partner == left && Grid.GetColumn(MainPane) == 2 && Grid.GetColumn(PeerPane) == 0 && right.IsActive && left.IsPartner
            && List.SelectedItems.Count == 1 && (List.SelectedItem as FileEntry)?.Name == "move me.txt" && PeerTitle.Text == "left", $"main column {Grid.GetColumn(MainPane)}, selected {List.SelectedItems.Count}");

        Activate(left); pump(100);
        check("split view: picking the other side's tab works on that side", _tab == left && _partner == right && Grid.GetColumn(MainPane) == 0, "");

        SwapSides(); pump(100);
        check("split view: swap puts the folders the other way round", _tab == left && Grid.GetColumn(MainPane) == 2 && Grid.GetColumn(PeerPane) == 0, "");

        SaveSession();
        check("split view: remembered for next time", _settings.SplitWith == _tabs.IndexOf(right) && _settings.SplitRight && _settings.Tabs.Count == 2, $"with {_settings.SplitWith}, right {_settings.SplitRight}");
        SetSplit(false); RestoreSplit(_settings.SplitWith); pump(100);
        check("split view: comes back as it was", SplitOn && _partner == right && _tab == left && Grid.GetColumn(MainPane) == 2, "");

        SetSplit(false); pump(50);
        var third = new TabState(); _tabs.Add(third); NavigateTab(third, a, push: false); Activate(third); pump(100);
        int at = _tabs.IndexOf(third);
        TabKey(Key.PageUp, true, true, false);
        check("tabs: Ctrl+Shift+Page Up moves the tab one place left", at == 2 && _tabs.IndexOf(third) == 1 && _tab == third, $"{at} -> {_tabs.IndexOf(third)}");
        TabKey(Key.D9, true, false, false);
        check("tabs: Ctrl+9 opens the last tab", _tab == _tabs[^1] && _tab != third, "");
        TabKey(Key.PageDown, true, false, false);
        check("tabs: Ctrl+Page Down goes round to the first", _tab == _tabs[0], "");
        CloseTab(third); Activate(left); SetSplit(true, right); pump(150);

        var tb = Descend2<TapBorder>(TabStrip).FirstOrDefault(x => x.DataContext == left);
        var peer = tb == null ? null : System.Windows.Automation.Peers.UIElementAutomationPeer.CreatePeerForElement(tb);
        var rb = Descend2<TapBorder>(TabStrip).FirstOrDefault(x => x.DataContext == right);
        var rpeer = rb == null ? null : System.Windows.Automation.Peers.UIElementAutomationPeer.CreatePeerForElement(rb);
        check("tabs: a screen reader hears each tab's name and whether it is open or on the other side, and the keyboard can open it",
            peer != null && peer.GetName() == "left" && peer.GetItemStatus() == "open" && rpeer?.GetItemStatus() == "on the other side" && tb!.Focusable && tb.Activate != null,
            $"\"{peer?.GetName()}\" \"{peer?.GetItemStatus()}\" \"{rpeer?.GetItemStatus()}\"");

        CloseTab(right); pump(100);
        check("split view: closing the other side's tab ends split view", !SplitOn && PeerPane.Visibility == Visibility.Collapsed && Grid.GetColumn(MainPane) == 0 && !_tabs.Any(x => x.IsPartner) && _tab == left, "");

        OpenOnOtherSide(b); until(() => SplitOn && _partner?.Entries != null, 5000); pump(100);
        check("split view: \"Open on the other side\" opens that folder next to this one", SplitOn && _partner!.Current == b && _tab == left && PeerList.Items.Count == 2, $"peer {PeerList.Items.Count}");
        SetSplit(false);

        var picker = new MainWindow(new PickRequest(a, "", "Open", Array.Empty<string>(), -1, "", 0, 0, 0, 0));
        picker._tab = new TabState(); picker._tabs.Add(picker._tab);
        picker.SetSplit(true);
        check("split view: never in an Open / Save picker", !picker.SplitOn && picker.PeerPane.Visibility != Visibility.Visible, "");
    }
}
