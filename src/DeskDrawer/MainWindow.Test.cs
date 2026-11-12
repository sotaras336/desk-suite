using System.Diagnostics;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace DeskDrawer;

/// DeskDrawer.exe --new-window --uitest report.txt : drives the real window on a throw-away folder (delete, undo, click-to-rename), then exits.
/// The first Delete bug lived in the window, not in ShellOps, so the self-test that only calls ShellOps could not see it.
public partial class MainWindow
{
    internal async void RunUiTest(string report)
    {
        var sb = new StringBuilder(); int fail = 0;
        void Check(string name, bool ok, string extra = "") { sb.AppendLine((ok ? "PASS " : "FAIL ") + name + (extra.Length > 0 ? "  " + extra : "")); if (!ok) fail++; }
        string? only = Environment.GetEnvironmentVariable("DD_UITEST_ONLY");   // e.g. "box": run just that part (edit, search, box, bench)
        bool Want(string part) => only == null || only.Split(',').Contains(part);
        Left = -32000; Top = 0; ShowInTaskbar = false;   // the test window stays out of your way
        string root = Path.Combine(Path.GetTempPath(), "deskdrawer-ui-" + Guid.NewGuid().ToString("N")[..8]);
        try
        {
            ShellOps.Silent = true;
            Directory.CreateDirectory(root); string a = Path.Combine(root, "victim.txt"), b = Path.Combine(root, "zz-next.txt"); File.WriteAllText(a, "x"); File.WriteAllText(b, "y");
            Navigate(root); await Task.Delay(1500);
            FileEntry? Find(string p) => _tab.Entries?.FirstOrDefault(x => string.Equals(x.FullPath, p, StringComparison.OrdinalIgnoreCase));
            void SelectOnly(FileEntry e) { List.SelectedItems.Clear(); List.SelectedItems.Add(e); List.Focus(); }

            // click on the name of the item that is already selected -> rename; the same click on an unselected item -> nothing
            var va = Find(a); Check("folder listed", va != null && Find(b) != null);
            Check("WPF refuses WindowInteropHelper.Handle off the UI thread (why Hwnd is cached)", await Task.Run(() => { try { _ = new System.Windows.Interop.WindowInteropHelper(this).Handle; return false; } catch (InvalidOperationException) { return true; } }));
            Check("owner window can be read from a worker thread (the Delete bug)", await Task.Run(() => { try { return Hwnd != IntPtr.Zero; } catch { return false; } }));
            if (Want("wheel"))   // a touchpad sends many small wheel messages: the folder must move by their total, not a whole notch each
            {
                var many = Path.Combine(root, "many"); Directory.CreateDirectory(many); for (int i = 0; i < 300; i++) File.WriteAllText(Path.Combine(many, $"f{i:000}.txt"), "");
                Navigate(many); await Task.Delay(2000);
                if (FindScroll(List) is { } sv)
                {
                    double Swipe(int msgs, int delta) { sv.ScrollToTop(); UpdateLayout(); for (int i = 0; i < msgs; i++) List.RaiseEvent(new MouseWheelEventArgs(Mouse.PrimaryDevice, Environment.TickCount, -delta) { RoutedEvent = UIElement.PreviewMouseWheelEvent }); UpdateLayout(); return sv.VerticalOffset; }
                    double notch = Wheel.Px(120), touch = Swipe(12, 10);   // 12 messages of 10 = one notch in total
                    Check("12 small touchpad messages scroll one notch, not twelve", Math.Abs(touch - notch) < 2, $"(moved {touch:0} px, a notch is {notch:0} px)");
                    double keep = Wheel.Speed; Wheel.Speed = 0.4; double slow = Swipe(1, 120); Wheel.Speed = keep;
                    Check("the Slower setting moves less", Math.Abs(slow - notch * 0.4) < 2, $"({slow:0} px)");
                }
                else Check("the list's scroll viewer found", false);
                Navigate(root); await Task.Delay(800);
            }
            if (Want("address"))   // the path bar: a click on its blank end opens the text box,, and Enter goes to the typed folder
            {
                foreach (var n in new[] { "alpha", "alps", "beta" }) Directory.CreateDirectory(Path.Combine(root, n)); await Task.Delay(1800);   // the folder watcher refreshes the list after this: let it finish before clicking
                // what a press on each part of the bar does (the press is raised from that part and tunnels through the bar's handler)
                bool PressOn(UIElement part) { Address.Visibility = Visibility.Hidden; CrumbBar.Visibility = Visibility.Visible; CrumbBar.RaiseEvent(new MouseButtonEventArgs(Mouse.PrimaryDevice, Environment.TickCount, MouseButton.Left) { RoutedEvent = UIElement.PreviewMouseLeftButtonDownEvent, Source = part }); return Address.Visibility == Visibility.Visible; }
                Check("a press on the blank scroll area of the bar opens the text box", PressOn(CrumbScroll));
                Check("a press on the blank end after the last folder opens it", PressOn(Crumbs.Children[^1]));
                Check("a press on the bar's own padding opens it", PressOn(CrumbBar));
                Check("a press on a folder name or its arrow does not (they do their own thing)", !PressOn(Crumbs.Children[0]) && !PressOn(Crumbs.Children[1]));
                CrumbBar.RaiseEvent(new MouseButtonEventArgs(Mouse.PrimaryDevice, Environment.TickCount, MouseButton.Left) { RoutedEvent = UIElement.PreviewMouseLeftButtonDownEvent, Source = CrumbBar }); await Task.Delay(250);
                Address.Text = root + "\\alpha"; Address_KeyDown(Address, new KeyEventArgs(Keyboard.PrimaryDevice, PresentationSource.FromVisual(this), Environment.TickCount, Key.Enter) { RoutedEvent = Keyboard.KeyDownEvent }); await Task.Delay(1200);
                Check("Enter goes to the typed folder", string.Equals(_tab.Current, Path.Combine(root, "alpha"), StringComparison.OrdinalIgnoreCase), _tab.Current ?? "");
                Navigate(root); await Task.Delay(1200); va = Find(a);   // back in the folder: the entries are new objects now
            }
            if (va != null && Want("edit"))
            {
                SelectOnly(va); await Task.Delay(300);
                TextBlock? NameBlock(FileEntry e) { var c = List.ItemContainerGenerator.ContainerFromItem(e) as DependencyObject; return c == null ? null : Descend(c).FirstOrDefault(t => t.Text == e.Name); }
                var tb = NameBlock(va); Check("name text block found", tb != null);
                if (tb != null)
                {
                    void Click(FrameworkElement src)
                    {
                        List.RaiseEvent(new MouseButtonEventArgs(Mouse.PrimaryDevice, Environment.TickCount, MouseButton.Left) { RoutedEvent = UIElement.PreviewMouseLeftButtonDownEvent, Source = src });
                        RaiseEvent(new MouseButtonEventArgs(Mouse.PrimaryDevice, Environment.TickCount, MouseButton.Left) { RoutedEvent = UIElement.PreviewMouseLeftButtonUpEvent, Source = src });
                    }
                    Click(tb); await Task.Delay(1200);
                    Check("click on a selected name starts rename", va.IsRenaming);
                    va.IsRenaming = false;
                    var vb = Find(b)!; SelectOnly(va); await Task.Delay(100);
                    var tbB = NameBlock(vb); if (tbB != null) { Click(tbB); await Task.Delay(1200); Check("click on another (unselected) name does not rename", !vb.IsRenaming); }
                    SelectOnly(va); await Task.Delay(100); Click(tb); List_DoubleClick(List, new MouseButtonEventArgs(Mouse.PrimaryDevice, Environment.TickCount, MouseButton.Left)); await Task.Delay(1200);
                    Check("a double click does not rename", !va.IsRenaming);
                    foreach (var x in _tab.Entries!) x.IsRenaming = false;
                }
                // Delete from the window (the path that was broken): gone, the next item selected, Ctrl+Z puts it back
                SelectOnly(va); await Task.Delay(200);
                DeleteSelection(false); await Task.Delay(3000);
                Check("Delete removes the file", !File.Exists(a));
                Check("the next item is selected afterwards", List.SelectedItems.Count == 1 && ((FileEntry)List.SelectedItems[0]!).FullPath == b);
                Undo(); await Task.Delay(3500);
                Check("Ctrl+Z puts it back from the Recycle Bin", File.Exists(a), "(" + Status.Text + ")");
            }

            // ---- search ----
            async Task<List<FileEntry>> Search(string where, string q)
            {
                Navigate(SearchTag + where + "?" + q); long t0 = Environment.TickCount64; await Task.Delay(400);
                while (_tab.Searching && Environment.TickCount64 - t0 < 25000) await Task.Delay(100);
                await Task.Delay(500); return _tab.View?.Cast<FileEntry>().ToList() ?? new();   // as shown: in the order the list sorts them
            }
            var r = new List<FileEntry>();
            if (Want("search")) {
            string sr = Path.Combine(root, "find"); Directory.CreateDirectory(Path.Combine(sr, "a")); Directory.CreateDirectory(Path.Combine(sr, "b", "deep")); Directory.CreateDirectory(Path.Combine(sr, "Εργασία")); Directory.CreateDirectory(Path.Combine(sr, "c", "2024"));
            File.WriteAllText(Path.Combine(sr, "a", "report.txt"), "1"); File.WriteAllText(Path.Combine(sr, "a", "other.txt"), "1"); File.WriteAllText(Path.Combine(sr, "b", "deep", "REPORT_final.docx"), "1");
            File.WriteAllText(Path.Combine(sr, "Εργασία", "Έκθεση τελική.docx"), "1"); File.WriteAllText(Path.Combine(sr, "c", "2024", "summary.txt"), "1");
            string hid = Path.Combine(sr, "a", "report-hidden.txt"); File.WriteAllText(hid, "1"); File.SetAttributes(hid, FileAttributes.Hidden);
            string Names(List<FileEntry> l) => string.Join(", ", l.Select(x => x.Name));
            r = await Search(sr, "report");
            Check("search finds names in every subfolder, hidden ones too, best match first", r.Count == 3 && r[0].Name == "report.txt" && r.Any(x => x.Name == "REPORT_final.docx") && r.Any(x => x.Name == "report-hidden.txt"), "(" + Names(r) + ")");
            Check("the banner says what was searched and how many were found", SearchTitle.Text.Contains("3 results") && SearchDetail.Text.Contains("Looked at"), "(" + SearchTitle.Text + " | " + SearchDetail.Text + ")");
            r = await Search(sr, "εκθεση"); Check("search ignores accents (Greek)", r.Count == 1 && r[0].Name == "Έκθεση τελική.docx", "(" + Names(r) + ")");
            r = await Search(sr, "ΕΚΘΕΣΗ ΤΕΛΙΚΗ"); Check("search ignores case and the final sigma, and takes several words", r.Count == 1, "(" + Names(r) + ")");
            r = await Search(sr, "summary 2024"); Check("a word may be in a folder above the file", r.Count == 1 && r[0].Name == "summary.txt", "(" + Names(r) + ")");
            r = await Search(sr, "nothing-called-this"); Check("a search with no results says so", r.Count == 0 && Message.Visibility == Visibility.Visible && Message.Text.Contains("Nothing called"), "(" + Message.Text + ")");
            r = await Search(sr, "report"); List_KeyDown(List, new KeyEventArgs(Keyboard.PrimaryDevice, PresentationSource.FromVisual(this), 0, Key.Escape) { RoutedEvent = Keyboard.KeyDownEvent }); await Task.Delay(500);
            Check("Esc leaves the results and goes back to where the search started", string.Equals(_tab.Current, sr, StringComparison.OrdinalIgnoreCase), "(" + _tab.Current + ")");
            }
            // ---- the drag-select box ----
            string mq = Path.Combine(root, "mq"); Directory.CreateDirectory(mq); for (int i = 1; i <= 60; i++) File.WriteAllText(Path.Combine(mq, $"f{i:00}.txt"), "x");
            if (Want("box")) { Navigate(mq); await Task.Delay(1500); SetViewMode("Details"); await Task.Delay(300); }
            var fr = Want("box") ? Frame() : null; var scroller = Want("box") ? FindScroll(List) : null; if (Want("box")) Check("the list has a scroll viewport", fr != null && scroller != null);
            if (fr != null && scroller != null)
            {
                scroller.ScrollToVerticalOffset(0); List.UpdateLayout();
                Check("the list scrolls in pixels (items would give a few dozen units)", scroller.ScrollableHeight > 500, $"({scroller.ScrollableHeight:0})");
                List<(ListViewItem Row, Rect Box)> Rows() { var l = new List<ListViewItem>(); Realized(List, l); return l.Select(x => (x, x.TransformToAncestor(fr).TransformBounds(new Rect(0, 0, x.ActualWidth, x.ActualHeight)))).OrderBy(x => x.Item2.Top).ThenBy(x => x.Item2.Left).ToList(); }
                var rows = Rows(); var ra = rows[3].Box; var rb = rows[7].Box; double x0 = fr.ActualWidth - 30;
                Check("the blank part of a row starts a box, its text does not", !OnContent(rows[3].Row) && Descend(rows[3].Row).Any(t => OnContent(t)));
                MarqueeArmAt(new Point(x0, ra.Top + 3), false, false); MarqueeBegin(); MarqueeMoveTo(new Point(x0 - 500, rb.Bottom - 3));
                var got = Sel().Select(x => x.Name).OrderBy(x => x).ToList();
                Check("the box over five rows selects exactly those", got.SequenceEqual(new[] { "f04.txt", "f05.txt", "f06.txt", "f07.txt", "f08.txt" }), "(" + string.Join(",", got) + ")");
                await Task.Delay(400); try { Snapshot(report + ".box.png"); } catch { }   // a picture of the box while it is open
                scroller.ScrollToVerticalOffset(scroller.VerticalOffset + 400); List.UpdateLayout(); MarqueeMoveTo(new Point(x0 - 500, 60));   // scrolled down, pointer near the top of the view
                got = Sel().Select(x => x.Name).OrderBy(x => x).ToList(); var nums = got.Select(n => int.Parse(n[1..3])).ToList();
                Check("after scrolling the box has grown with it, from the same corner", got.Count > 10 && nums[0] == 4 && nums.Zip(nums.Skip(1), (p, q) => q - p).All(d => d == 1), "(" + string.Join(",", got) + ")");
                scroller.ScrollToVerticalOffset(0); List.UpdateLayout(); MarqueeMoveTo(new Point(x0 - 500, rb.Bottom - 3));
                Check("and shrinks again when the pointer comes back", Sel().Count == 5, "(" + Sel().Count + ")");
                MarqueeEnd();
                Check("letting go removes the box and keeps the selection", !_mqActive && _mqRect!.Visibility != Visibility.Visible && Sel().Count == 5);
                List.SelectedItems.Clear(); var first = _tab.Entries!.First(x => x.Name == "f01.txt"); List.SelectedItems.Add(first); var rows2 = Rows();
                MarqueeArmAt(new Point(x0, rows2[9].Box.Top + 3), false, true); MarqueeBegin(); MarqueeMoveTo(new Point(x0 - 500, rows2[11].Box.Bottom - 3)); got = Sel().Select(x => x.Name).OrderBy(x => x).ToList(); MarqueeEnd();
                Check("with Ctrl or Shift the box adds to what was selected", got.SequenceEqual(new[] { "f01.txt", "f10.txt", "f11.txt", "f12.txt" }), "(" + string.Join(",", got) + ")");
                MarqueeArmAt(new Point(x0, 5), true, false); MarqueeEnd();
                Check("a plain click on empty space clears the selection", Sel().Count == 0);
                SetViewMode("Icons", 64); await Task.Delay(1000); fr = Frame(); scroller = FindScroll(List)!; scroller.ScrollToVerticalOffset(0); List.UpdateLayout(); rows = Rows();   // a new view re-applies the list template: look the scroll parts up again
                if (rows.Count > 6)
                {
                    var i1 = rows[1].Box; var i4 = rows[4].Box; MarqueeArmAt(new Point(i1.Left + 1, i1.Top + 1), true, false); MarqueeBegin(); MarqueeMoveTo(new Point(i4.Right - 1, i4.Bottom - 1)); got = Sel().Select(x => x.Name).ToList(); MarqueeEnd();
                    var n1 = ((FileEntry)rows[1].Row.Content).Name; var n4 = ((FileEntry)rows[4].Row.Content).Name; var far = ((FileEntry)rows[^1].Row.Content).Name;
                    Check("in the icon view the box selects the icons it touches", got.Contains(n1) && got.Contains(n4) && !got.Contains(far) && got.Count < rows.Count, "(" + got.Count + " of " + rows.Count + ")");
                }
                SetViewMode("Details"); await Task.Delay(300);
            }
            if (only != null && only.Split(',').Contains("thispc"))   // only when asked for: it clicks a real drive tile
            {   // This PC: a click on a drive opens it in the SAME tab (the user saw a new window open)
                Navigate(ThisPcTag); await Task.Delay(1800);
                IEnumerable<DependencyObject> All(DependencyObject d) { for (int i = 0; i < VisualTreeHelper.GetChildrenCount(d); i++) { var c = VisualTreeHelper.GetChild(d, i); yield return c; foreach (var x in All(c)) yield return x; } }
                var tiles = All(HomePane).OfType<Border>().Where(x => x.ToolTip is string t && t.Length == 3 && t[1] == ':').ToList();
                Check("This PC shows drive tiles", tiles.Count > 0, $"({tiles.Count})");
                foreach (var tile in tiles)
                {
                    Navigate(ThisPcTag); await Task.Delay(1200);
                    string drive = (string)tile.ToolTip; var known = Process.GetProcessesByName("DeskDrawer").Select(p => p.Id).ToHashSet();
                    int windows = Application.Current.Windows.OfType<MainWindow>().Count(), tabs = _tabs.Count;
                    var t2 = All(HomePane).OfType<Border>().First(x => x.ToolTip is string t && t == drive);   // the page was rebuilt: the tile is a new object
                    t2.RaiseEvent(new MouseButtonEventArgs(Mouse.PrimaryDevice, Environment.TickCount, MouseButton.Left) { RoutedEvent = UIElement.MouseLeftButtonUpEvent, Source = t2 });
                    var seen = new HashSet<int>(); for (int i = 0; i < 60; i++) { await Task.Delay(50); foreach (var p in Process.GetProcessesByName("DeskDrawer")) if (!known.Contains(p.Id)) seen.Add(p.Id); }
                    int w2 = Application.Current.Windows.OfType<MainWindow>().Count();
                    Check($"tile {drive}: opens in the same tab, no new window / tab / process", w2 == windows && _tabs.Count == tabs && seen.Count == 0 && (string.Equals(_tab.Current, drive, StringComparison.OrdinalIgnoreCase) || !Directory.Exists(drive)), $"(now {_tab.Current}; windows {windows}->{w2}, tabs {tabs}->{_tabs.Count}, new processes {seen.Count})");

                    // the same drive picked in the folder tree (This PC > the drive)
                    Navigate(ThisPcTag); await Task.Delay(800); known = Process.GetProcessesByName("DeskDrawer").Select(p => p.Id).ToHashSet(); windows = Application.Current.Windows.OfType<MainWindow>().Count(); tabs = _tabs.Count;
                    TreeViewItem? FindNode(ItemsControl parent) { foreach (var o in parent.Items) if (o is TreeViewItem ti) { if (ti.Tag is string tg && string.Equals(tg, drive, StringComparison.OrdinalIgnoreCase)) return ti; ti.IsExpanded = true; ti.UpdateLayout(); if (FindNode(ti) is { } f) return f; } return null; }
                    var node = FindNode(Tree);
                    if (node == null) sb.AppendLine($"INFO tree: no node found for {drive}");
                    else
                    {
                        node.IsSelected = true; node.BringIntoView(); seen.Clear(); for (int i = 0; i < 60; i++) { await Task.Delay(50); foreach (var p in Process.GetProcessesByName("DeskDrawer")) if (!known.Contains(p.Id)) seen.Add(p.Id); }
                        w2 = Application.Current.Windows.OfType<MainWindow>().Count();
                        Check($"tree {drive}: selecting it opens it in the same tab, no new window / tab / process", w2 == windows && _tabs.Count == tabs && seen.Count == 0 && (string.Equals(_tab.Current, drive, StringComparison.OrdinalIgnoreCase) || !Directory.Exists(drive)), $"(now {_tab.Current}; windows {windows}->{w2}, tabs {tabs}->{_tabs.Count}, new processes {seen.Count})");
                    }
                }
            }
            if (Want("address") || only != null && only.Split(',').Contains("thispc"))
            {   // the second click of a double-click that began on a card must not act on the folder's list that replaced the page
                Check("a plain double-click in a folder is never swallowed (no card was clicked)", !SwallowCardContinuation(2));
                _cardClickAt = Environment.TickCount64;
                Check("a double-click's second click right after a card click is swallowed", SwallowCardContinuation(2) && !SwallowCardContinuation(1));
                _cardClickAt = Environment.TickCount64 - (long)Native.GetDoubleClickTime() - 200;
                Check("...but an ordinary double-click later is not", !SwallowCardContinuation(2));
                _cardClickAt = -10_000_000;
            }
            if (Want("bench") && Environment.GetEnvironmentVariable("DD_SEARCHBENCH") is { Length: > 0 } bench && bench.Split('|') is { Length: 2 } bp)   // DD_SEARCHBENCH="folder|words": how long a live search takes there
            {
                var sw = Stopwatch.StartNew(); r = await Search(bp[0], bp[1]); sw.Stop();
                sb.AppendLine($"BENCH live search for \"{bp[1]}\" in {bp[0]}: {r.Count} found, {sw.ElapsedMilliseconds - 900} ms (minus the test's own waits); {SearchDetail.Text}");
            }
            if (Want("search") && SharedIndex.Acquire() is { } shared)
            {
                int items = shared.Count; shared.Done();
                r = await Search(ThisPcTag, "lecture7 stamatina");
                Check("the whole-PC search uses DeskBar's list when it is there", SearchDetail.Text.Contains("From DeskBar's list") && r.Count >= 1 && r.All(x => x.Name.Contains("Lecture7", StringComparison.OrdinalIgnoreCase)), $"({items:N0} items; {r.Count} found; {SearchDetail.Text})");
                r = await Search(ThisPcTag, "εργασια"); Check("the whole-PC search folds Greek accents too", r.Count >= 1, "(" + r.Count + ")");
            }
        }
        catch (Exception ex) { Check("no exception", false, ex.ToString()); }
        finally { try { Directory.Delete(root, true); } catch { } }
        sb.AppendLine($"{fail} failure(s)"); File.WriteAllText(report, sb.ToString());
        App.Quitting = true; Application.Current.Shutdown(fail);
    }

    private static IEnumerable<TextBlock> Descend(DependencyObject d)
    {
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(d); i++)
        {
            var c = VisualTreeHelper.GetChild(d, i); if (c is TextBlock t) yield return t;
            foreach (var x in Descend(c)) yield return x;
        }
    }
}
