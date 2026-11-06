using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows;
using Microsoft.Win32;
using Forms = System.Windows.Forms;

namespace DeskMode;

public partial class App : System.Windows.Application
{
    /// A test copy (--instance name): its own single-instance names and settings, no keyboard shortcuts, and it never changes a refresh rate or power mode.
    public static bool TestInstance;

    private Mutex? _mutex;
    private EventWaitHandle? _showSignal, _quitSignal;
    private Controller? _controller;
    private Forms.NotifyIcon? _tray;
    public PanelWindow? Panel { get; private set; }

    private static string Arg(string[] args, string name) { int i = Array.IndexOf(args, name); return i >= 0 && i + 1 < args.Length ? args[i + 1] : ""; }

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        string instance = Arg(e.Args, "--instance"), data = Arg(e.Args, "--data");
        TestInstance = instance.Length > 0 || e.Args.Contains("--readonly");
        if (data.Length > 0) Environment.SetEnvironmentVariable("DESKMODE_DATA", data);
        string suffix = instance.Length > 0 ? "." + instance : "";

        if (Setup.IsSetupName() || e.Args.Contains("--install")) { Shutdown(Setup.Install()); return; }   // "DeskMode-Setup.exe" from the website: install, then start the installed copy
        if (e.Args.Contains("--uninstall")) { Shutdown(Setup.Uninstall()); return; }
        if (Arg(e.Args, "--selftest") is { Length: > 0 } report) { Theme.Apply(); Shutdown(SelfTest.Run(report)); return; }

        // DeskMode.exe --energy-saver on|off : switch Energy Saver and exit (handy for shortcuts/scripts).
        int es = Array.IndexOf(e.Args, "--energy-saver");
        if (es >= 0 && es + 1 < e.Args.Length)
        {
            bool on = e.Args[es + 1].Equals("on", StringComparison.OrdinalIgnoreCase);
            bool ok = Task.Run(() => EnergySaver.Set(on)).Result;
            Shutdown(ok ? 0 : 1);
            return;
        }

        // Single instance: a second launch just opens the panel of the running one.
        _mutex = new Mutex(true, "DeskMode.SingleInstance" + suffix, out bool first);
        _showSignal = new EventWaitHandle(false, EventResetMode.AutoReset, "DeskMode.Show" + suffix);
        if (!first)
        {
            _showSignal.Set();
            Shutdown();
            return;
        }
        _quitSignal = new EventWaitHandle(false, EventResetMode.AutoReset, "DeskMode.Quit" + suffix);   // Setup asks a running copy to leave before it replaces the file

        Controller.ReadOnly = TestInstance;
        _controller = new Controller();
        Panel = new PanelWindow(_controller);

        _tray = new Forms.NotifyIcon { Text = "DeskMode", Icon = MakeIcon(), Visible = true };
        _tray.MouseClick += (_, a) =>
        {
            if (a.Button != Forms.MouseButtons.Left) return;
            if (Panel.IsVisible) Panel.HidePanel();
            else if ((DateTime.UtcNow - Panel.HiddenAt).TotalMilliseconds > 300) Panel.ShowPanel();
        };
        var menu = new Forms.ContextMenuStrip();
        menu.Items.Add("Open", null, (_, _) => Panel.ShowPanel());
        menu.Items.Add("Go max", null, (_, _) => Task.Run(_controller.ApplyMax));
        menu.Items.Add("Go max, built-in screen easy", null, (_, _) => Task.Run(_controller.ApplyMonitorFirst));
        menu.Items.Add("Go easy", null, (_, _) => Task.Run(_controller.ApplySaver));
        menu.Items.Add(new Forms.ToolStripSeparator());
        var lockItem = new Forms.ToolStripMenuItem("Keep screen modes steady during games") { Checked = _controller.Settings.GameLock, CheckOnClick = true };
        lockItem.CheckedChanged += (_, _) => { _controller.Settings.GameLock = lockItem.Checked; _controller.Settings.Save(); };
        var borderlessItem = new Forms.ToolStripMenuItem("Borderless for games on my list") { Checked = _controller.Settings.AutoBorderless, CheckOnClick = true };
        borderlessItem.CheckedChanged += (_, _) => { _controller.Settings.AutoBorderless = borderlessItem.Checked; _controller.Settings.Save(); };
        menu.Items.Add(lockItem);
        menu.Items.Add(borderlessItem);
        menu.Items.Add(new Forms.ToolStripSeparator());
        menu.Items.Add("Settings…", null, (_, _) => OpenSettings());
        menu.Items.Add("Quit DeskMode", null, (_, _) => Shutdown());
        menu.Opening += (_, _) => { lockItem.Checked = _controller.Settings.GameLock; borderlessItem.Checked = _controller.Settings.AutoBorderless; };   // the Settings window may have changed them
        _tray.ContextMenuStrip = menu;

        _controller.Notify += msg => Dispatcher.BeginInvoke(() => _tray?.ShowBalloonTip(2500, "DeskMode", msg, Forms.ToolTipIcon.None));
        _controller.StateChanged += UpdateTrayTooltip;
        _controller.RequestPanel += () => Dispatcher.BeginInvoke(() => Panel.ShowPanel());
        SystemEvents.UserPreferenceChanged += (_, _) => Dispatcher.BeginInvoke(() => { if (_tray != null) _tray.Icon = MakeIcon(); });

        new Thread(() => { while (_showSignal.WaitOne()) Dispatcher.BeginInvoke(() => Panel.ShowPanel()); }) { IsBackground = true }.Start();
        new Thread(() => { if (_quitSignal.WaitOne()) Dispatcher.BeginInvoke(() => Shutdown()); }) { IsBackground = true }.Start();

        _controller.Start();

        // Debug: --snapshot out.png renders the panel, --snapshot-settings out.png the Settings window (both off-screen), then exit.
        string snap = Arg(e.Args, "--snapshot"), snapSettings = Arg(e.Args, "--snapshot-settings");
        bool snapRun = snap.Length > 0 || snapSettings.Length > 0;
        if ((!TestInstance && !snapRun) || Environment.GetEnvironmentVariable("DESK_UPDATE_BASE") != null) Updater.Start();   // a test copy or a snapshot never asks GitHub (unless a test points it at a local server)
        if (snapRun)
        {
            Theme.Apply();
            FrameworkElement root; Window host = Panel;
            if (snap.Length > 0) { Panel.ShowPanel(); Panel.Left = -20000; root = (FrameworkElement)Panel.Content; }
            else { Panel.ShowPanel(settings: true); Panel.Left = -20000; if (Panel.SettingsPageForSnapshot is { } sp) sp.MaxHeight = double.PositiveInfinity; root = (FrameworkElement)Panel.Content; }
            var timer = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(4) };
            timer.Tick += (_, _) =>
            {
                timer.Stop();
                var dpi = System.Windows.Media.VisualTreeHelper.GetDpi(root);
                int w = (int)(root.ActualWidth * dpi.DpiScaleX) + 48, h = (int)(root.ActualHeight * dpi.DpiScaleY) + 48;
                var rtb = new System.Windows.Media.Imaging.RenderTargetBitmap(w, h, dpi.PixelsPerInchX, dpi.PixelsPerInchY, System.Windows.Media.PixelFormats.Pbgra32);
                var shot = new System.Windows.Media.DrawingVisual();   // drawn through a brush: rendering the element itself would include its margin as an offset and crop the right edge
                using (var dc = shot.RenderOpen()) dc.DrawRectangle(new System.Windows.Media.VisualBrush(root), null, new Rect(0, 0, root.ActualWidth, root.ActualHeight));
                rtb.Render(shot);
                var enc = new System.Windows.Media.Imaging.PngBitmapEncoder();
                enc.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(rtb));
                string path = snap.Length > 0 ? snap : snapSettings;
                using (var fs = System.IO.File.Create(path)) enc.Save(fs);
                System.IO.File.WriteAllText(path + ".txt", $"visible={host.IsVisible} size={root.ActualWidth}x{root.ActualHeight}");
                Shutdown();
            };
            timer.Start();
            return;
        }

        if (!e.Args.Contains("--background")) Panel.ShowPanel();
        else Task.Delay(8000).ContinueWith(_ => Native.TrimWorkingSet());   // nothing is on screen at logon: give the startup pages back
    }

    /// Settings are a page of the panel itself: this opens the panel on it.
    public void OpenSettings() => Panel?.ShowPanel(settings: true);

    private DateTime _lastTooltip;

    /// Hovering the tray icon shows the current state. Throttled: it enumerates displays.
    private void UpdateTrayTooltip()
    {
        if (DateTime.UtcNow - _lastTooltip < TimeSpan.FromSeconds(2)) return;
        _lastTooltip = DateTime.UtcNow;
        Task.Run(() =>
        {
            var text = _controller!.StatusLine();
            Dispatcher.BeginInvoke(() => { if (_tray != null) _tray.Text = text.Length > 127 ? text[..127] : text; });
        });
    }

    protected override void OnExit(ExitEventArgs e)
    {
        if (_tray != null) { _tray.Visible = false; _tray.Dispose(); }
        _controller?.Dispose();
        base.OnExit(e);
    }

    /// The app icon at tray size, falling back to a drawn glyph if the resource is missing.
    private static Icon MakeIcon()
    {
        try
        {
            using var stream = typeof(App).Assembly.GetManifestResourceStream("DeskMode.DeskMode.ico");
            if (stream != null) return new Icon(stream, Forms.SystemInformation.SmallIconSize);
        }
        catch { }
        return DrawIcon();
    }

    /// Fallback: small monitor + bolt glyph, drawn to match the taskbar theme.
    private static Icon DrawIcon()
    {
        var light = Theme.TaskbarIsLight();
        var fg = light ? Color.FromArgb(30, 30, 30) : Color.White;
        using var bmp = new Bitmap(32, 32);
        using (var g = Graphics.FromImage(bmp))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            using var pen = new Pen(fg, 2.6f);
            using var path = RoundedRect(new RectangleF(3, 5, 26, 17), 3);
            g.DrawPath(pen, path);
            g.DrawLine(pen, 16, 22, 16, 27);
            g.DrawLine(pen, 10, 27.5f, 22, 27.5f);
            using var brush = new SolidBrush(fg);
            g.FillPolygon(brush, new PointF[] { new(17.5f, 7.5f), new(11.5f, 14.5f), new(15.5f, 14.5f), new(14.5f, 19.5f), new(20.5f, 12.5f), new(16.5f, 12.5f) });
        }
        return Icon.FromHandle(bmp.GetHicon());
    }

    private static GraphicsPath RoundedRect(RectangleF r, float rad)
    {
        var p = new GraphicsPath();
        p.AddArc(r.X, r.Y, rad * 2, rad * 2, 180, 90);
        p.AddArc(r.Right - rad * 2, r.Y, rad * 2, rad * 2, 270, 90);
        p.AddArc(r.Right - rad * 2, r.Bottom - rad * 2, rad * 2, rad * 2, 0, 90);
        p.AddArc(r.X, r.Bottom - rad * 2, rad * 2, rad * 2, 90, 90);
        p.CloseFigure();
        return p;
    }
}
