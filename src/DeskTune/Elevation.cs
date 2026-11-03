using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Principal;

namespace DeskTune;

/// Administrator rights (services and machine-wide settings need them) and the Windows restore point made before the first such change.
public static class Elevation
{
    public static bool IsAdmin { get; } = new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);

    /// Starts DeskTune again with the Windows administrator prompt. False if the user said no.
    public static bool RestartAsAdmin()
    {
        try { Process.Start(new ProcessStartInfo(Environment.ProcessPath!) { UseShellExecute = true, Verb = "runas" }); return true; }
        catch { return false; }   // the UAC prompt was declined
    }

    private static bool _pointMade;

    /// One restore point per DeskTune session, before the first administrator change. Windows itself skips it if one was made in the last 24 h.
    /// Returns a message for the status line.
    public static string EnsureRestorePoint()
    {
        if (_pointMade || !IsAdmin) return "";
        _pointMade = true;
        try
        {
            var info = new RESTOREPOINTINFOW { dwEventType = 100 /*BEGIN_SYSTEM_CHANGE*/, dwRestorePtType = 12 /*MODIFY_SETTINGS*/, szDescription = "Before DeskTune changes" };
            if (!SRSetRestorePointW(ref info, out var st)) return st.nStatus == 1058 ? "System Restore is off on this PC, so no restore point was made." : "Windows didn't make a restore point (it allows one a day).";
            var end = new RESTOREPOINTINFOW { dwEventType = 101 /*END_SYSTEM_CHANGE*/, llSequenceNumber = st.llSequenceNumber };
            SRSetRestorePointW(ref end, out _);
            return "Restore point made.";
        }
        catch { return "Windows didn't make a restore point."; }
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Unicode)]
    private struct RESTOREPOINTINFOW { public int dwEventType; public int dwRestorePtType; public long llSequenceNumber; [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string szDescription; }
    [StructLayout(LayoutKind.Sequential, Pack = 1)] private struct STATEMGRSTATUS { public int nStatus; public long llSequenceNumber; }
    [DllImport("srclient.dll", CharSet = CharSet.Unicode)] private static extern bool SRSetRestorePointW(ref RESTOREPOINTINFOW info, out STATEMGRSTATUS status);
}
