using System.IO;
using System.Text;
using System.Text.Json;

namespace DeskMode;

/// DeskMode.exe --selftest report.txt --instance t --data <dir> : checks the logic and builds both windows without showing them. It only READS the screens and
/// power state: nothing here changes a refresh rate, a power mode or Energy saver.
public static class SelfTest
{
    public static int Run(string reportFile)
    {
        var sb = new StringBuilder(); int fail = 0;
        void Check(string name, bool ok, string extra = "") { sb.AppendLine((ok ? "PASS " : "FAIL ") + name + (extra.Length > 0 ? "  " + extra : "")); if (!ok) fail++; }
        {   // the update check: reading the checksum list, and (only when a test points DESK_UPDATE_BASE at a local release folder) the whole check and a verified download that is not run
            string sums = new string('a', 64) + " *" + Updater.Asset + "\r\n" + new string('b', 64) + " *Other-Setup.exe\n";
            Check("update: reads the checksum for this app's file", Updater.HashFor(sums, Updater.Asset) == new string('a', 64) && Updater.HashFor(sums, "Nope.exe") == null);
            if (Environment.GetEnvironmentVariable("DESK_UPDATE_BASE") != null)
            {
                bool got = Task.Run(() => Updater.UpdateNowAsync()).GetAwaiter().GetResult();   // no earlier check: Update now must look at the release list itself (a checksum remembered from an older check once made it reject the newest file)
                Check("update: looks again, downloads, verifies and (in a test) does not run it", got && Updater.LastDownload != null && System.IO.File.Exists(Updater.LastDownload), Updater.Status + " " + Updater.LastDownload);
                Task.Run(() => Updater.CheckAsync()).GetAwaiter().GetResult();
                Check("update: sees a different build in the release", Updater.Newer, Updater.Status);
            }
        }
        {   // ten tiny wheel messages (one touchpad swipe) move one notch in total, not ten
            var sv = new System.Windows.Controls.ScrollViewer { Width = 100, Height = 100, Content = new System.Windows.Controls.Border { Height = 5000 } };
            Wheel.Attach(sv); sv.Measure(new System.Windows.Size(100, 100)); sv.Arrange(new System.Windows.Rect(0, 0, 100, 100)); sv.UpdateLayout();
            for (int i = 0; i < 10; i++) sv.RaiseEvent(new System.Windows.Input.MouseWheelEventArgs(System.Windows.Input.Mouse.PrimaryDevice, Environment.TickCount, -12) { RoutedEvent = System.Windows.UIElement.PreviewMouseWheelEvent });
            sv.UpdateLayout();
            Check("wheel: a touchpad swipe moves one notch, not ten", Math.Abs(sv.VerticalOffset - Wheel.Px(120)) < 0.5 && Wheel.Px(120) >= 16, $"{sv.VerticalOffset} px");
        }
        {   // the fade windows sit exactly over each screen (built invisible and closed again at once: nothing shows)
            ScreenFade.Begin(null, instant: true);
            var shown = ScreenFade.Rects(); var screens = System.Windows.Forms.Screen.AllScreens.Select(s => s.Bounds).ToList();
            Check("fade: one window per screen, exactly over it", shown.Count == screens.Count && screens.All(b => shown.Contains(b)), $"{shown.Count} windows, {screens.Count} screens");
            ScreenFade.CloseNow(); Check("fade: closes completely", ScreenFade.Count == 0);
            { var named = DisplayService.GetDisplays(); Check("fade: screens are matched by the same device names the display list uses", named.Count > 0 && named.All(d => screens.Count == 0 || System.Windows.Forms.Screen.AllScreens.Any(s => string.Equals(s.DeviceName, d.DeviceName, StringComparison.OrdinalIgnoreCase))), string.Join(",", named.Select(d => d.DeviceName)) + " vs " + string.Join(",", System.Windows.Forms.Screen.AllScreens.Select(s => s.DeviceName))); }
        }
        try
        {
            Controller.ReadOnly = true;
            var d = new Settings();
            Check("defaults suit any PC", d.MonitorMatch == "" && d.LaptopMaxHz == 0 && d.MonitorMaxHz == 0 && d.EasyHz == 60 && d.MaxPowerMode == PowerMode.BestPerformance && d.RestoreOnBattery && d.Hotkeys);

            // a settings file written by the old, one-monitor DeskMode keeps its choices and gets the new defaults
            var old = JsonSerializer.Deserialize<Settings>("{\"MonitorMatch\":\"GSM5C8B\",\"LaptopMaxHz\":120,\"MonitorMaxHz\":180,\"PreferLaptop60\":true,\"Snapshot\":{\"LaptopHz\":60,\"MonitorHz\":60,\"EnergySaverOn\":false,\"AcPowerMode\":0}}")!;
            Check("an old settings file keeps what it said", old.MonitorMatch == "GSM5C8B" && old.LaptopMaxHz == 120 && old.MonitorMaxHz == 180 && old.PreferLaptop60 && old.Snapshot?.LaptopHz == 60);
            Check("...and gets the new settings as defaults", old.EasyHz == 60 && old.RestoreOnBattery && old.MaxEnergySaverOff && old.BorderlessMatch.Contains("GLFW30"));
            var back = JsonSerializer.Deserialize<Settings>(JsonSerializer.Serialize(old))!;
            Check("settings survive a save and a load", back.MonitorMatch == "GSM5C8B" && back.EasyPowerMode == old.EasyPowerMode && back.BorderlessMatch.SequenceEqual(old.BorderlessMatch));
            old.EasyHz = 90; old.Hotkeys = false; old.ResetToDefaults();
            Check("reset restores the defaults but keeps the remembered 'before' state", old.EasyHz == 60 && old.Hotkeys && old.MonitorMatch == "" && old.Snapshot?.LaptopHz == 60);

            var screen = new DisplayInfo(@"\\.\DISPLAY9", "TST0001", "A Monitor With A Long Name", false, 2560, 1440, 60, new[] { 60, 120, 180 });
            Check("highest refresh rate when asked for 0", DisplayService.BestRate(screen, 0) == 180);
            Check("a rate the screen has is used as is", DisplayService.BestRate(screen, 120) == 120 && DisplayService.BestRate(screen, 60) == 60);
            Check("a rate it doesn't have gives the next one down, or its lowest", DisplayService.BestRate(screen, 144) == 120 && DisplayService.BestRate(screen, 30) == 60);
            Check("a screen that lists no rates stays where it is", DisplayService.BestRate(screen with { AvailableRates = Array.Empty<int>() }, 144) == 60);
            Check("long monitor names are shortened for labels", screen.ShortName.Length <= 14 && screen.ShortName.EndsWith("…"));

            var displays = DisplayService.GetDisplays();
            Check("the screens of this PC can be read", displays.Count >= 1, "(" + string.Join(" | ", displays.Select(x => $"{x.FriendlyName} {x.Width}x{x.Height} {x.RefreshRate}Hz [{string.Join(",", x.AvailableRates)}]{(x.IsInternal ? " internal" : "")}")) + ")");
            Check("every screen has a real name", displays.All(x => x.FriendlyName.Length > 0 && !x.FriendlyName.Contains("UltraGear") || x.HardwareId.StartsWith("GSM", StringComparison.OrdinalIgnoreCase)));
            Check("the monitor choice 'any' finds an external screen when there is one", (DisplayService.External(displays, "") != null) == displays.Any(x => !x.IsInternal));

            PowerService.StylePref = 2; var std = PowerService.Choices(); PowerService.StylePref = 1; var srf = PowerService.Choices(); var extra = PowerService.Choices(PowerMode.BestEfficiency); PowerService.StylePref = 0;
            Check("Windows offers three power modes: the usual three", std.Select(x => x.Label).SequenceEqual(new[] { "Efficient", "Balanced", "Best" }));
            Check("...or the Surface three", srf.Select(x => x.Label).SequenceEqual(new[] { "Recommended", "Better", "Best" }));
            Check("a mode outside the three is still shown while the PC is in it", extra.Count == 4 && extra[0].Mode == PowerMode.BestEfficiency);
            Check("power mode names", PowerService.ModeName(PowerMode.BestPerformance) == "Best" && PowerService.ModeName(PowerMode.BestEfficiency) == "Efficient" && PowerService.ModeName(PowerMode.BetterPerformance) == "Better");
            sb.AppendLine($"INFO power mode supported={PowerService.IsSupported} now={PowerService.GetPowerMode()} plugged={PowerService.IsPluggedIn} battery={PowerService.HasBattery} {PowerService.BatteryPercent}%");

            // the 'look but never touch' guard of a test copy: running Go max and Go easy must change nothing
            var c = new Controller();
            var before = DisplayService.GetDisplays().Select(x => x.RefreshRate).ToList(); var modeBefore = PowerService.GetPowerMode(); bool? saverBefore = EnergySaver.IsOn();
            c.ApplyMax(); c.ApplyMonitorFirst(); c.ApplySaver(); c.SetLaptopHz(30); c.SetMonitorHz(30);
            Check("a test copy changes nothing on this PC", before.SequenceEqual(DisplayService.GetDisplays().Select(x => x.RefreshRate)) && modeBefore == PowerService.GetPowerMode() && saverBefore == EnergySaver.IsOn());
            Check("the status line names the real screens", c.StatusLine().StartsWith("DeskMode") && (displays.All(x => x.IsInternal) || !c.StatusLine().Contains("LG ") || displays.Any(x => x.FriendlyName.StartsWith("LG"))));

            var panel = new PanelWindow(c);
            Check("the panel builds", panel.Content != null);
            panel.ShowSettings(true); var page = panel.SettingsPageForSnapshot; int n = page == null ? 0 : ((System.Windows.Controls.StackPanel)page.Content).Children.Count;
            Check("the Settings page builds inside the panel", n >= 16 && panel.SettingsOpen, $"({n} parts)");
            Check("a monitor is named by its maker in short labels", new DisplayInfo("x", "GSM5C8B", "LG ULTRAGEAR", false, 1920, 1080, 180, new[] { 60, 180 }).Brand == "LG" && new DisplayInfo("x", "y", "External monitor", false, 1, 1, 60, new[] { 60 }).Brand == "Monitor" && new DisplayInfo("x", "y", "DELL U2720Q", false, 1, 1, 60, new[] { 60 }).Brand == "DELL");
            c.Dispose();
        }
        catch (Exception ex) { Check("no exception", false, ex.ToString()); }
        sb.AppendLine($"{fail} failure(s)");
        File.WriteAllText(reportFile, sb.ToString());
        return fail;
    }
}
