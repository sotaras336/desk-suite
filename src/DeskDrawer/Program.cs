namespace DeskDrawer;

/// Our own entry point, so opening a folder while DeskDrawer is already running just sends it over and exits
/// before WPF (about 0.5 s of loading) even starts.
public static class Program
{
    [STAThread]
    public static int Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--dialog-guard") return DialogGuard.Run(args);   // first, before anything loads: see DialogGuard
        if (args.Contains("--uninstall")) return Setup.Uninstall();
        if (Setup.IsSetupName() || args.Contains("--install")) return Setup.Install();   // "DeskDrawer-Setup.exe" from the website: install, then start the installed copy
        if (args.Contains("--background") && Instance.IsRunning()) return 0;   // the sign-in start, but a copy is already up
        // --set (the Desk settings window, when DeskDrawer's pipe was busy): App handles it; it used to be handed over here as a folder called "KeepReady=1"
        bool plain = args.Any(a => a is "--selftest" or "--makeicon" or "--shortcuts" or "--snapshot" or "--dialogtest" or "--picktest" or "--pickshot" or "--settingsshot" or "--default" or "--background" or "--uitest" or "--set");   // a test run never hands its arguments to the copy you are using
        if (!plain && !args.Contains("--new-window") && Instance.HandOff(args.Where(a => !a.StartsWith("--")).Select(App.CleanPath).ToList())) return 0;
        var app = new App(); app.InitializeComponent(); return app.Run();
    }
}
