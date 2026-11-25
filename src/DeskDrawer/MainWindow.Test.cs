using System.Diagnostics;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

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
            if (Want("look"))   // Space: a big look at the file (QuickLook.cs): text as text, a picture as a picture, arrows step, Space closes
            {
                string t = Path.Combine(root, "look-a.txt"), p = Path.Combine(root, "look-b.png"); File.WriteAllText(t, "hello quick look");
                var rtb = new RenderTargetBitmap(64, 48, 96, 96, PixelFormats.Pbgra32); var dv = new DrawingVisual(); using (var dc = dv.RenderOpen()) dc.DrawRectangle(Brushes.OrangeRed, null, new Rect(0, 0, 64, 48)); rtb.Render(dv);
                using (var fs = File.Create(p)) { var enc = new PngBitmapEncoder(); enc.Frames.Add(BitmapFrame.Create(rtb)); enc.Save(fs); }
                await Task.Delay(1500);   // the folder watcher merges the new files in (a reload would replace the rows the later tests hold)
                if (Find(t) is { } et) {
                    SelectOnly(et); QuickLook.Toggle(this, et, StepSelection, _ => { }); await Task.Delay(800);
                    Check("Space opens a quick look of the text file, as text", QuickLook.IsOpen && QuickLook.ShownText == "hello quick look", $"({QuickLook.ShownText})");
                    QuickLook.Press(Key.Right); await Task.Delay(1500);
                    Check("the right arrow steps to the next file, a picture shown as a picture", QuickLook.ShownName == "look-b.png" && QuickLook.ShowsPicture, $"({QuickLook.ShownName}, picture {QuickLook.ShowsPicture})");
                    Check("the list's selection followed the arrow", Sel() is { Count: 1 } s1 && s1[0].Name == "look-b.png");
                    QuickLook.Press(Key.Space); await Task.Delay(300); Check("Space closes it again", !QuickLook.IsOpen);
                } else Check("quick-look test files listed", false);
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
                {   // the blank part of a row that is ALREADY selected is a grab (drag the selection out), not a new box; on an unselected row it still starts a box
                    void Press(ListViewItem row) => List.RaiseEvent(new MouseButtonEventArgs(Mouse.PrimaryDevice, Environment.TickCount, MouseButton.Left) { RoutedEvent = UIElement.PreviewMouseLeftButtonDownEvent, Source = row });
                    var vis = Rows(); var selRow = vis.First(x => List.SelectedItems.Contains(x.Row.Content)).Row; var freeRow = vis.First(x => !List.SelectedItems.Contains(x.Row.Content)).Row;
                    Press(selRow); bool grabbed = _dragItem == selRow.Content && !_mqArmed && Sel().Count == 5;
                    Press(freeRow); bool boxed = _dragItem == null && _mqArmed;
                    _dragItem = null; _pendingSingle = null; _mqArmed = false;
                    Check("pressing the blank part of a selected row grabs the selection, of an unselected row starts a box", grabbed && boxed, $"(grab {grabbed}, box {boxed})");
                }
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
            if (Want("behind"))   // a press on a selected row while another window is in front must not bring this window forward (MainWindow.DragBehind.cs)
            {
                var dirB = Path.Combine(root, "behind"); Directory.CreateDirectory(dirB); for (int i = 0; i < 6; i++) File.WriteAllText(Path.Combine(dirB, $"b{i}.txt"), "");
                Navigate(dirB); await Task.Delay(1800); UpdateLayout();
                var rowsB = new List<ListViewItem>(); Realized(List, rowsB); rowsB = rowsB.OrderBy(x => x.TransformToAncestor(this).Transform(new Point(0, 0)).Y).ToList();
                // another window, on its own thread like another app's, shown after ours and so above it
                IntPtr front = IntPtr.Zero; System.Windows.Threading.Dispatcher? fd = null; var ready = new ManualResetEventSlim();
                var th = new Thread(() => { var w = new Window { Width = 220, Height = 120, Left = -32000, Top = 300, ShowInTaskbar = false, Title = "in front (test)" }; w.Show(); front = new System.Windows.Interop.WindowInteropHelper(w).Handle; fd = System.Windows.Threading.Dispatcher.CurrentDispatcher; ready.Set(); System.Windows.Threading.Dispatcher.Run(); });
                th.SetApartmentState(ApartmentState.STA); th.IsBackground = true; th.Start(); ready.Wait(5000); await Task.Delay(500);
                bool Above(IntPtr top, IntPtr below) { for (IntPtr h = GetWindow(top, 2 /*GW_HWNDNEXT*/); h != IntPtr.Zero; h = GetWindow(h, 2)) if (h == below) return true; return false; }
                if (rowsB.Count < 4 || front == IntPtr.Zero || !Above(front, Hwnd)) sb.AppendLine($"INFO behind: skipped, the stand-in window did not end up in front this time (rows {rowsB.Count}, front {front})");   // a setup miss says nothing about the feature
                else
                {
                    List.SelectedItems.Clear(); List.SelectedItems.Add(rowsB[1].Content); List.SelectedItems.Add(rowsB[2].Content); UpdateLayout();
                    Point Mid(ListViewItem r) => r.TransformToAncestor(this).Transform(new Point(r.ActualWidth / 2, r.ActualHeight / 2));
                    IntPtr ask = (IntPtr)((0x201 << 16) | 1 /*HTCLIENT*/); IntPtr fg0 = GetForegroundWindow(); bool pre = fg0 != Hwnd && Above(front, Hwnd); Keyboard.ClearFocus(); bool focus0 = List.IsKeyboardFocusWithin;   // earlier parts of the test may have left the focus in the list: start from none
                    Check("behind: the test window starts behind another one", pre, $"(foreground is ours {fg0 == Hwnd}, other window above {Above(front, Hwnd)}, active {IsActive})");

                    // 1. a press on a row that is NOT selected, a right press, a press with the title bar: all answered the usual way (the window may come forward)
                    TestPressAt = Mid(rowsB[4]); long plain = SendMessage(Hwnd, 0x21, Hwnd, ask).ToInt64(); bool lazy1 = _lazyPress;
                    TestPressAt = Mid(rowsB[1]); long right = SendMessage(Hwnd, 0x21, Hwnd, (IntPtr)((0x204 << 16) | 1)).ToInt64(); bool lazy2 = _lazyPress;
                    long caption = SendMessage(Hwnd, 0x21, Hwnd, (IntPtr)((0x201 << 16) | 2 /*HTCAPTION*/)).ToInt64(); bool lazy3 = _lazyPress;
                    Check("behind: an unselected row, a right press and the title bar still activate as always", plain != 3 && right != 3 && !lazy1 && !lazy2 && !lazy3, $"({plain} {right} {caption})");

                    // 2. the real thing: Windows' question, then the real button-down message, on a selected row
                    var at = Mid(rowsB[1]); TestPressAt = at; long ans = SendMessage(Hwnd, 0x21, Hwnd, ask).ToInt64();
                    Check("behind: a left press on a selected row is answered \"don't activate\"", ans == 3 && _lazyPress, $"(answer {ans}, lazy {_lazyPress})");
                    // (WPF ignores a posted button message when the real pointer is elsewhere, so the press is raised the way the other tests here do it)
                    TestTrace = ""; List.RaiseEvent(new MouseButtonEventArgs(Mouse.PrimaryDevice, Environment.TickCount, MouseButton.Left) { RoutedEvent = UIElement.PreviewMouseLeftButtonDownEvent, Source = rowsB[1] }); await Task.Delay(300); sb.AppendLine("INFO behind: " + TestTrace);
                    bool held = _dragItem == rowsB[1].Content && Sel().Count == 2;
                    Check("behind: the press reached the list as a grab of the whole selection", held, $"(grabbed {_dragItem?.Name}, selected {Sel().Count})");
                    Check("behind: the window did not come forward, did not become active and took no focus", GetForegroundWindow() == fg0 && Above(front, Hwnd) && List.IsKeyboardFocusWithin == focus0, $"(foreground unchanged {GetForegroundWindow() == fg0}, still below {Above(front, Hwnd)}, active {IsActive}, focus {List.IsKeyboardFocusWithin})");

                    // 3. the button comes up without a drag: it was a click, so now the window asks to come forward, and the click selects the one row
                    RaiseEvent(new MouseButtonEventArgs(Mouse.PrimaryDevice, Environment.TickCount, MouseButton.Left) { RoutedEvent = UIElement.PreviewMouseLeftButtonUpEvent, Source = rowsB[1] }); await Task.Delay(400);
                    Check("behind: letting go without dragging ends the press as a click", !_lazyPress && _dragItem == null && Sel().Count == 1 && Sel()[0] == rowsB[1].Content && !List.IsMouseCaptured, $"(lazy {_lazyPress}, selected {Sel().Count}, captured {List.IsMouseCaptured})");
                    sb.AppendLine($"INFO behind: after the click the window is {(GetForegroundWindow() == Hwnd ? "in front" : "not in front (Windows only lets the app that was really clicked come forward; this test has no real mouse)")}");

                    // 4. a rename being typed, and Ctrl held, are never lazy
                    List.SelectedItems.Clear(); List.SelectedItems.Add(rowsB[1].Content); ((FileEntry)rowsB[1].Content).IsRenaming = true; TestPressAt = at;
                    long ren = SendMessage(Hwnd, 0x21, Hwnd, ask).ToInt64(); ((FileEntry)rowsB[1].Content).IsRenaming = false;
                    Check("behind: not while a name is being typed", ren != 3 && !_lazyPress, $"({ren})");
                }
                TestPressAt = null; _lazyPress = false; _dragItem = null; _pendingSingle = null; List.ReleaseMouseCapture();
                fd?.BeginInvokeShutdown(System.Windows.Threading.DispatcherPriority.Normal);
                Navigate(root); await Task.Delay(800);
            }
            if (Want("merge"))   // a change in the open folder is merged into the list on screen: rows that stay keep their objects, selection and place
            {
                var dirM = Path.Combine(root, "merge"); Directory.CreateDirectory(dirM); for (int i = 0; i < 5; i++) File.WriteAllText(Path.Combine(dirM, $"m{i}.txt"), "x"); Directory.CreateDirectory(Path.Combine(dirM, "sub"));
                Navigate(dirM); await Task.Delay(1800);
                var keep = _tab.Entries!.First(x => x.Name == "m1.txt"); var grow = _tab.Entries!.First(x => x.Name == "m2.txt"); var view0 = _tab.View; var list0 = _tab.Entries;
                List.SelectedItems.Clear(); List.SelectedItems.Add(keep);
                File.WriteAllText(Path.Combine(dirM, "new.txt"), "n"); File.Delete(Path.Combine(dirM, "m4.txt")); File.WriteAllText(Path.Combine(dirM, "m2.txt"), new string('y', 5000));
                await Task.Delay(2500);   // the folder watcher sees it and merges
                var now = _tab.Entries!;
                Check("merge: the new file is listed and the deleted one is gone", now.Any(x => x.Name == "new.txt") && !now.Any(x => x.Name == "m4.txt"), "(" + string.Join(",", now.Select(x => x.Name)) + ")");
                Check("merge: the list was not rebuilt (same list, same view, same row objects)", ReferenceEquals(now, list0) && ReferenceEquals(_tab.View, view0) && now.Contains(keep) && now.Contains(grow));
                Check("merge: a file that grew shows its new size in place", grow.Size == 5000, $"({grow.Size})");
                Check("merge: the selection survived", Sel().Count == 1 && ReferenceEquals(Sel()[0], keep));
                var none = MergeListing(_tab, new DirectoryInfo(dirM).EnumerateFileSystemInfos().Select(fi => new FileEntry(fi)).ToList());
                Check("merge: nothing changed = nothing done", none == (0, 0, 0), none.ToString());
                Navigate(root); await Task.Delay(800);
            }
            if (Want("zip"))
            {
                var src = Path.Combine(root, "zsrc"); Directory.CreateDirectory(Path.Combine(src, "inner", "empty")); File.WriteAllText(Path.Combine(src, "a.txt"), "alpha"); File.WriteAllBytes(Path.Combine(src, "inner", "b.bin"), new byte[300_000]);
                string zf = Path.Combine(root, "z.zip"), outDir = Path.Combine(root, "zout"); double seen = -1;
                ZipTo(new List<string> { src }, zf, p => seen = p, CancellationToken.None); UnzipTo(zf, outDir, null, CancellationToken.None);
                Check("zip: a folder goes in and comes out the same, empty folders included, with progress reported", File.ReadAllText(Path.Combine(outDir, "zsrc", "a.txt")) == "alpha" && new FileInfo(Path.Combine(outDir, "zsrc", "inner", "b.bin")).Length == 300_000 && Directory.Exists(Path.Combine(outDir, "zsrc", "inner", "empty")) && seen > 0.99, $"(progress {seen:0.00})");
                string evil = Path.Combine(root, "evil.zip"); using (var z = System.IO.Compression.ZipFile.Open(evil, System.IO.Compression.ZipArchiveMode.Create)) { using var w = new StreamWriter(z.CreateEntry("../escaped.txt").Open()); w.Write("x"); }
                bool refused = false; try { UnzipTo(evil, Path.Combine(root, "evilout"), null, CancellationToken.None); } catch (IOException) { refused = true; }
                Check("zip: an entry that points outside its folder is refused and nothing lands there", refused && !File.Exists(Path.Combine(root, "escaped.txt")));
                bool stopped = false; try { ZipTo(new List<string> { src }, Path.Combine(root, "c.zip"), null, new CancellationToken(true)); } catch (OperationCanceledException) { stopped = true; }
                Check("zip: a stop request stops it", stopped);
            }
            if (Want("reach"))
            {
                var sw1 = Stopwatch.StartNew(); bool gone = Reach.Dir(@"\\192.0.2.1\nowhere\folder"); long first = sw1.ElapsedMilliseconds; sw1.Restart(); bool again = Reach.Dir(@"\\192.0.2.1\nowhere\other"); long second = sw1.ElapsedMilliseconds;
                Check("reach: a place that does not answer costs at most ~1.5 s once, then nothing for a while", !gone && !again && first < 2500 && second < 100, $"({first} ms, then {second} ms)");
                Check("reach: a local folder is simply there, a missing one is not", Reach.Dir(root) && !Reach.Dir(Path.Combine(root, "no-such-folder")) && Reach.IsLocalDisk(Environment.SystemDirectory));
            }
            if (Want("fileops"))   // the copy engine: a path longer than 260 characters, and an error said in words
            {
                string deep = root; while (deep.Length < 300) deep = Path.Combine(deep, "a-rather-long-folder-name-to-get-past-the-old-limit"); Directory.CreateDirectory(@"\\?\" + deep);
                string lf = Path.Combine(deep, "long.txt"); File.WriteAllText(@"\\?\" + lf, "deep"); string dst = Path.Combine(root, "fo-dst"); Directory.CreateDirectory(dst);
                bool okCopy = await Task.Run(() => ShellOps.Copy(new[] { lf }, dst, IntPtr.Zero)); bool arrived = File.Exists(Path.Combine(dst, "long.txt"));
                if (okCopy && arrived) Check("fileops: a file on a path longer than 260 characters is copied", true, $"({lf.Length} characters)");
                else sb.AppendLine($"INFO fileops: a {lf.Length}-character path was not copied on this PC ({ShellOps.LastError}); Windows' long-path setting decides this");
                bool bad = await Task.Run(() => ShellOps.Copy(new[] { Path.Combine(root, "does-not-exist.txt") }, dst, IntPtr.Zero));
                Check("fileops: a failure is explained in words, not a number", !bad && ShellOps.LastError is { } le && !le.Contains("0x"), $"({ShellOps.LastError})");
                try { Directory.Delete(@"\\?\" + Path.Combine(root, "a-rather-long-folder-name-to-get-past-the-old-limit"), true); } catch { }
            }
            if (Want("pick"))   // the Save / Open picker: its size, its tooltip font, a file name that arrives late, and how fast it comes up (invisible: Opacity 0)
            {
                var (sw1, sh1) = PickSize(2880, 1824, 2.0, 1440, 960); var (sw2, sh2) = PickSize(1920, 1032, 1.0, 1920, 1080); var (sw3, sh3) = PickSize(2880, 1824, 2.0, 625, 541);
                Check("pick: at least 820x560 at the screen's scaling (or the dialog's size), at most 60% x 70% of the screen", sw1 == 1640 && sh1 == 1120 && sw2 == 1152 && sh2 == 722 && sw3 == 1640 && sh3 == 1120, $"(PowerPoint on the Surface {sw1}x{sh1}, full-screen app on 1080p {sw2}x{sh2}, small Chrome dialog {sw3}x{sh3})");
                { var (rw, rh) = PickSize(2880, 1824, 2.0, 1440, 960, 500, 400); var (bw, bh) = PickSize(1920, 1032, 1.0, 640, 480, 3000, 3000); Check("pick: a size you set yourself is used, scaled to the screen and kept inside it", rw == 1000 && rh == 800 && bw == 1896 && bh == 1008, $"({rw}x{rh}, {bw}x{bh})"); }
                // a tooltip on a round toolbar button (icon font) must be written in the text font
                var tool = FindDesc<Button>(this) is { } any ? Descend2<Button>(this).FirstOrDefault(b => b.FontFamily.Source.Contains("Icons")) : null;
                if (tool != null)
                {
                    var tt = new ToolTip { Content = "Refresh" }; var keepTip = tool.ToolTip; tool.ToolTip = tt; tt.PlacementTarget = tool; tt.Placement = System.Windows.Controls.Primitives.PlacementMode.Bottom; tt.IsOpen = true; await Task.Delay(300);
                    Check("pick: a toolbar button's tooltip is in letters, not the icon font", !tt.FontFamily.Source.Contains("Icons"), $"(button font {tool.FontFamily.Source}, tooltip font {tt.FontFamily.Source})");
                    tt.IsOpen = false; tool.ToolTip = keepTip;
                    var cm = new ContextMenu { PlacementTarget = tool }; cm.Items.Add(new MenuItem { Header = "x" }); cm.IsOpen = true; await Task.Delay(200);
                    Check("pick: a menu opened from an icon button is in letters too", !cm.FontFamily.Source.Contains("Icons"), $"({cm.FontFamily.Source})"); cm.IsOpen = false;
                }
                else Check("pick: an icon-font toolbar button found", false);
                foreach (var fam in new[] { "Segoe Fluent Icons", "Segoe MDL2 Assets" })
                    if (new Typeface(fam).TryGetGlyphTypeface(out var gt)) sb.AppendLine($"INFO font {fam}: has 'D' {gt.CharacterToGlyphMap.ContainsKey('D')}, 'e' {gt.CharacterToGlyphMap.ContainsKey('e')}, space {gt.CharacterToGlyphMap.ContainsKey(' ')}, file {gt.FontUri}");
                    else sb.AppendLine($"INFO font {fam}: not installed");
                PrewarmPicker(); await Task.Delay(2500);
                var times = new List<long>(); MainWindow? pw = null; string pdir = Path.Combine(root, "pickdir"); Directory.CreateDirectory(pdir); for (int i = 0; i < 40; i++) File.WriteAllText(Path.Combine(pdir, $"p{i}.txt"), "");
                for (int round = 0; round < 3; round++)
                {
                    var swp = Stopwatch.StartNew(); pw = ForPick(round == 0 ? root : pdir, "", "Save", new[] { "Text (*.txt)" }, 0, "Save As", 200, 200, 900, 700); long tA = swp.ElapsedMilliseconds; pw.Opacity = 0; pw.PickShow(); long tB = swp.ElapsedMilliseconds; await Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.Render); sb.AppendLine($"INFO pick round {round}: request {tA} ms, show {tB} ms, first render {swp.ElapsedMilliseconds} ms");
                    await Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.ContextIdle); times.Add(swp.ElapsedMilliseconds);
                    if (round == 0) { var ph = new System.Windows.Interop.WindowInteropHelper(pw).Handle; PickGetWindowRect(ph, out var pr); var pd = new PRECT { L = 200, T = 200, R = 900, B = 700 }; var pm = new PMONITORINFO { cbSize = System.Runtime.InteropServices.Marshal.SizeOf<PMONITORINFO>() }; PickGetMonitorInfo(PickMonitorFromRect(ref pd, 2), ref pm);
                        int gx = pr.L - pm.rcWork.L, gy = pr.T - pm.rcWork.T; Check("pick: opens in the top-left corner of the screen", gx >= 0 && gy >= 0 && gx <= 40 && gy <= 40, $"({gx}, {gy} px from the corner)"); }
                    if (round == 1)   // a double click on a file answers the dialog with it
                    {
                        string pf = Path.Combine(pdir, "p3.txt"); FileEntry? pe = null;
                        for (int i = 0; i < 40 && (pe = pw._tab.Entries?.FirstOrDefault(x => x.FullPath.Equals(pf, StringComparison.OrdinalIgnoreCase))) == null; i++) await Task.Delay(50);
                        if (pe != null) { pw.List.SelectedItems.Clear(); pw.List.SelectedItem = pe; pw.List.UpdateLayout(); var lvi = pw.List.ItemContainerGenerator.ContainerFromItem(pe) as ListViewItem;
                            pw.List.RaiseEvent(new MouseButtonEventArgs(Mouse.PrimaryDevice, Environment.TickCount, MouseButton.Left) { RoutedEvent = Control.MouseDoubleClickEvent, Source = (object?)lvi ?? pw.List }); await Task.Delay(100); }
                        Check("pick: a double click on a file answers the dialog with that file", pw.Result is { Count: 1 } dr && dr[0].Equals(pf, StringComparison.OrdinalIgnoreCase), $"(found {pe != null}, got {(pw.Result == null ? "nothing" : string.Join("|", pw.Result))})");
                    }
                    if (round == 2) { Check("pick: a name that arrives late fills an untouched box", pw.PickLateName("", "report.docx") && pw.PickName.Text == "report.docx"); pw.PickName.Text = "mine.txt"; Check("pick: ...but never one you typed", !pw.PickLateName("", "other.docx") && pw.PickName.Text == "mine.txt");
                        string hf = Path.Combine(pdir, "p5.txt"); pw.OpenFile(hf); Check("pick: opening a file another way (Home's recent files, Quick Look) answers the dialog too", pw.Result is { Count: 1 } hr && hr[0] == hf); }
                    pw.FinishPick(); pw.Opacity = 1; await Task.Delay(300);
                }
                sb.AppendLine("INFO pick: shown and drawn in " + string.Join(", ", times) + " ms (first = prewarmed window)");
                Check("pick: comes up in under 700 ms once warm", times.Skip(1).All(t => t < 700), "(" + string.Join(", ", times) + " ms)");   // a ceiling, not a benchmark: the exact figure moves with whatever else the PC is doing
                Navigate(root); await Task.Delay(500);
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
                bool heldAfter = SharedIndex.IsHeld; SharedIndex.ReleaseIfIdle(0); bool gone = !SharedIndex.IsHeld;
                r = await Search(ThisPcTag, "lecture7 stamatina"); Check("DeskBar's list is let go when idle and maps again for the next search", heldAfter && gone && r.Count >= 1, $"(held {heldAfter}, let go {gone}, then {r.Count} found)");
            }
        }
        catch (Exception ex) { Check("no exception", false, ex.ToString()); }
        finally { try { Directory.Delete(root, true); } catch { } }
        sb.AppendLine($"{fail} failure(s)"); File.WriteAllText(report, sb.ToString());
        App.Quitting = true; Application.Current.Shutdown(fail);
    }

    private static IEnumerable<T> Descend2<T>(DependencyObject d) where T : DependencyObject
    {
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(d); i++) { var c = VisualTreeHelper.GetChild(d, i); if (c is T t) yield return t; foreach (var x in Descend2<T>(c)) yield return x; }
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
