namespace DeskDrawer;

/// Our own entry point, so opening a folder while DeskDrawer is already running just sends it over and exits
/// before WPF (about 0.5 s of loading) even starts.
public static class Program
{
    [STAThread]
    public static int Main(string[] args)
    {
        if (args.Contains("--uninstall")) return Setup.Uninstall();
        if (Setup.IsSetupName() || args.Contains("--install")) return Setup.Install();   // "DeskDrawer-Setup.exe" from the website: install, then start the installed copy
        if (args.Contains("--background") && Instance.IsRunning()) return 0;   // the sign-in start, but a copy is already up
        bool plain = args.Any(a => a is "--selftest" or "--makeicon" or "--shortcuts" or "--snapshot" or "--dialogtest" or "--picktest" or "--pickshot" or "--default" or "--background");
        if (!plain && !args.Contains("--new-window") && Instance.HandOff(args.Where(a => !a.StartsWith("--")).Select(App.CleanPath).ToList())) return 0;
        var app = new App(); app.InitializeComponent(); return app.Run();
    }
}
