using System.Runtime.InteropServices;

namespace DeskMode;

/// <summary>
/// Turns Windows 11 Energy Saver on/off instantly, the same way the Settings app does:
/// through the "SystemSettings_BatterySaver_LandingPage_OverrideControl" setting handler.
/// Undocumented Windows internals, so every call fails soft (returns null/false) if a future update changes it.
/// Call from a background thread (not the WPF UI thread).
/// </summary>
public static class EnergySaver
{
    private static readonly string HandlerDll = System.IO.Path.Combine(Environment.SystemDirectory, "SettingsHandlers_OneCore_BatterySaver.dll");
    private const string SettingId = "SystemSettings_BatterySaver_LandingPage_OverrideControl";

    // ISettingItem vtable slots (after IUnknown 0-2 and IInspectable 3-5)
    private const int SlotGetValue = 13, SlotSetValue = 14;
    // IPropertyValue.GetBoolean / IPropertyValueStatics.CreateBoolean
    private const int SlotGetBoolean = 18, SlotCreateBoolean = 17;
    private static Guid IID_IPropertyValue = new("4BD682DD-7554-40E9-9A9B-82654EDE7E62");
    private static Guid IID_IPropertyValueStatics = new("629BDBC8-D932-4FF4-96B9-8D96C5C1E858");

    [DllImport("combase.dll")] private static extern int WindowsCreateString([MarshalAs(UnmanagedType.LPWStr)] string s, int len, out IntPtr h);
    [DllImport("combase.dll")] private static extern int WindowsDeleteString(IntPtr h);
    [DllImport("combase.dll")] private static extern int RoInitialize(int type);
    [DllImport("combase.dll")] private static extern int RoGetActivationFactory(IntPtr classId, ref Guid iid, out IntPtr factory);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr LoadLibrary(string path);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)] private static extern IntPtr GetProcAddress(IntPtr module, string name);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int GetSettingFn(IntPtr id, out IntPtr item, IntPtr reserved);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int GetValueFn(IntPtr self, IntPtr name, out IntPtr value);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int SetValueFn(IntPtr self, IntPtr name, IntPtr value);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int GetBoolFn(IntPtr self, [MarshalAs(UnmanagedType.U1)] out bool value);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate int CreateBoolFn(IntPtr self, [MarshalAs(UnmanagedType.U1)] bool value, out IntPtr boxed);

    private static readonly object Lock = new();
    private static GetSettingFn? _getSetting;

    private static T Slot<T>(IntPtr obj, int slot) where T : Delegate =>
        Marshal.GetDelegateForFunctionPointer<T>(Marshal.ReadIntPtr(Marshal.ReadIntPtr(obj), slot * IntPtr.Size));

    private static T WithHString<T>(string s, Func<IntPtr, T> use)
    {
        WindowsCreateString(s, s.Length, out var h);
        try { return use(h); } finally { WindowsDeleteString(h); }
    }

    private static IntPtr GetItem()
    {
        RoInitialize(1); // MTA; harmless if already initialized
        if (_getSetting == null)
        {
            var module = LoadLibrary(HandlerDll);
            var fn = module == IntPtr.Zero ? IntPtr.Zero : GetProcAddress(module, "GetSetting");
            if (fn == IntPtr.Zero) return IntPtr.Zero;
            _getSetting = Marshal.GetDelegateForFunctionPointer<GetSettingFn>(fn);
        }
        return WithHString(SettingId, id => _getSetting(id, out var item, IntPtr.Zero) == 0 ? item : IntPtr.Zero);
    }

    /// Whether Energy Saver is on right now, or null if it couldn't be read.
    public static bool? IsOn()
    {
        lock (Lock)
        {
            try
            {
                var item = GetItem();
                if (item == IntPtr.Zero) return null;
                try
                {
                    var value = WithHString("Value", n => Slot<GetValueFn>(item, SlotGetValue)(item, n, out var v) == 0 ? v : IntPtr.Zero);
                    if (value == IntPtr.Zero) return null;
                    try
                    {
                        if (Marshal.QueryInterface(value, ref IID_IPropertyValue, out var pv) != 0) return null;
                        try { return Slot<GetBoolFn>(pv, SlotGetBoolean)(pv, out var on) == 0 ? on : null; }
                        finally { Marshal.Release(pv); }
                    }
                    finally { Marshal.Release(value); }
                }
                finally { Marshal.Release(item); }
            }
            catch { return null; }
        }
    }

    /// Turns Energy Saver on or off immediately. Returns false if Windows refused.
    public static bool Set(bool on)
    {
        lock (Lock)
        {
            try
            {
                var item = GetItem();
                if (item == IntPtr.Zero) return false;
                try
                {
                    var factory = WithHString("Windows.Foundation.PropertyValue",
                        c => RoGetActivationFactory(c, ref IID_IPropertyValueStatics, out var f) == 0 ? f : IntPtr.Zero);
                    if (factory == IntPtr.Zero) return false;
                    try
                    {
                        if (Slot<CreateBoolFn>(factory, SlotCreateBoolean)(factory, on, out var boxed) != 0) return false;
                        try { return WithHString("Value", n => Slot<SetValueFn>(item, SlotSetValue)(item, n, boxed)) == 0; }
                        finally { Marshal.Release(boxed); }
                    }
                    finally { Marshal.Release(factory); }
                }
                finally { Marshal.Release(item); }
            }
            catch { return false; }
        }
    }
}
