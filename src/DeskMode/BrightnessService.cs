using System.Management;
using static DeskMode.Native;

namespace DeskMode;

/// <summary>
/// Brightness for one screen. UI calls <see cref="Set"/> as often as it likes (every slider tick);
/// a single background worker only ever sends the newest value, spaced out so slow DDC/CI
/// monitors are never flooded, and re-sends the final value once things go quiet.
/// </summary>
public abstract class BrightnessChannel : IDisposable
{
    private readonly object _lock = new();
    private readonly AutoResetEvent _signal = new(false);
    private readonly Thread _worker;
    private volatile bool _disposed;
    private int _target = -1;
    private int _written = -1;

    public int Value { get; protected set; } = -1;
    public bool Available { get; protected set; }

    protected abstract int MinGapMs { get; }
    protected abstract int SettleMs { get; }

    protected BrightnessChannel(string name)
    {
        _worker = new Thread(Loop) { IsBackground = true, Name = name };
        _worker.Start();
    }

    public void Set(int percent)
    {
        percent = Math.Clamp(percent, 0, 100);
        lock (_lock) _target = percent;
        Value = percent;
        _signal.Set();
    }

    /// Refreshes <see cref="Value"/> from the hardware (skipped while a write is in flight).
    public void Refresh()
    {
        lock (_lock) if (_target != _written) return;
        var v = ReadHardware();
        if (v >= 0) { Value = v; lock (_lock) { _target = _written = v; } }
    }

    private void Loop()
    {
        while (!_disposed)
        {
            _signal.WaitOne();
            bool wroteSomething = false;
            while (!_disposed)
            {
                int t;
                lock (_lock) t = _target;
                if (t < 0 || t == _written)
                {
                    // Idle: wait for more slider movement, else do one final confirm write.
                    if (_signal.WaitOne(SettleMs)) continue;
                    if (wroteSomething && t >= 0) WriteWithRetry(t);
                    break;
                }
                if (WriteWithRetry(t)) { lock (_lock) _written = t; wroteSomething = true; }
                else { lock (_lock) _written = t; } // give up on this value; a newer one may follow
                Thread.Sleep(MinGapMs);
            }
        }
    }

    private bool WriteWithRetry(int v)
    {
        for (int i = 0; i < 3; i++)
        {
            if (WriteHardware(v)) return true;
            Thread.Sleep(40 + i * 40);
            if (i == 1) Reacquire();
        }
        return false;
    }

    protected abstract bool WriteHardware(int percent);
    protected abstract int ReadHardware();
    protected virtual void Reacquire() { }

    public virtual void Dispose()
    {
        _disposed = true;
        _signal.Set();
    }
}

/// Built-in Surface panel via WMI.
public sealed class InternalBrightness : BrightnessChannel
{
    private ManagementEventWatcher? _watcher;
    public event Action<int>? ChangedExternally;
    private int _lastSelfWrite = -1;
    private DateTime _lastSelfWriteAt;

    protected override int MinGapMs => 15;
    protected override int SettleMs => 600;

    public InternalBrightness() : base("internal-brightness")
    {
        Value = ReadHardware();
        Available = Value >= 0;
        try
        {
            _watcher = new ManagementEventWatcher(new ManagementScope(@"root\wmi"),
                new EventQuery("SELECT * FROM WmiMonitorBrightnessEvent"));
            _watcher.EventArrived += (_, e) =>
            {
                var v = Convert.ToInt32(e.NewEvent["Brightness"]);
                // Ignore echoes of our own writes.
                if (v == _lastSelfWrite && DateTime.UtcNow - _lastSelfWriteAt < TimeSpan.FromSeconds(2)) return;
                if ((DateTime.UtcNow - _lastSelfWriteAt).TotalMilliseconds < 700) return;
                Value = v;
                ChangedExternally?.Invoke(v);
            };
            _watcher.Start();
        }
        catch { _watcher = null; }
    }

    protected override bool WriteHardware(int percent)
    {
        try
        {
            _lastSelfWrite = percent;
            _lastSelfWriteAt = DateTime.UtcNow;
            using var s = new ManagementObjectSearcher(@"root\wmi", "SELECT * FROM WmiMonitorBrightnessMethods");
            foreach (ManagementObject o in s.Get())
            {
                o.InvokeMethod("WmiSetBrightness", new object[] { (uint)1, (byte)percent });
                return true;
            }
        }
        catch { }
        return false;
    }

    protected override int ReadHardware()
    {
        try
        {
            using var s = new ManagementObjectSearcher(@"root\wmi", "SELECT CurrentBrightness FROM WmiMonitorBrightness");
            foreach (ManagementObject o in s.Get()) return Convert.ToInt32(o["CurrentBrightness"]);
        }
        catch { }
        return -1;
    }

    public override void Dispose()
    {
        try { _watcher?.Stop(); _watcher?.Dispose(); } catch { }
        base.Dispose();
    }
}

/// External monitor via DDC/CI (VCP 0x10). Tuned for slow/finicky LG firmware.
public sealed class DdcBrightness : BrightnessChannel
{
    private const byte VcpBrightness = 0x10;
    private readonly string _deviceName;
    private readonly object _handleLock = new();
    private IntPtr _handle;
    private uint _max = 100;

    protected override int MinGapMs => 70;
    protected override int SettleMs => 450;

    public DdcBrightness(string deviceName) : base("ddc-brightness")
    {
        _deviceName = deviceName;
        Reacquire();
        // LG monitors often don't answer DDC right after hot-plug; try a few times.
        for (int i = 0; i < 6 && Value < 0; i++)
        {
            Value = ReadHardware();
            if (Value < 0) { Thread.Sleep(250); if (i == 2) Reacquire(); }
        }
        Available = _handle != IntPtr.Zero;
    }

    protected override void Reacquire()
    {
        lock (_handleLock)
        {
            if (_handle != IntPtr.Zero) { DestroyPhysicalMonitor(_handle); _handle = IntPtr.Zero; }
            IntPtr hMon = IntPtr.Zero;
            EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, (IntPtr h, IntPtr _, ref RECT _, IntPtr _) =>
            {
                var mi = new MONITORINFOEX { cbSize = System.Runtime.InteropServices.Marshal.SizeOf<MONITORINFOEX>() };
                if (GetMonitorInfo(h, ref mi) && mi.szDevice == _deviceName) { hMon = h; return false; }
                return true;
            }, IntPtr.Zero);
            if (hMon == IntPtr.Zero || !GetNumberOfPhysicalMonitorsFromHMONITOR(hMon, out var n) || n == 0) return;
            var arr = new PHYSICAL_MONITOR[n];
            if (!GetPhysicalMonitorsFromHMONITOR(hMon, n, arr)) return;
            _handle = arr[0].hPhysicalMonitor;
            for (int i = 1; i < arr.Length; i++) DestroyPhysicalMonitor(arr[i].hPhysicalMonitor);
        }
    }

    protected override bool WriteHardware(int percent)
    {
        lock (_handleLock)
        {
            if (_handle == IntPtr.Zero) return false;
            return SetVCPFeature(_handle, VcpBrightness, (uint)Math.Round(percent * _max / 100.0));
        }
    }

    protected override int ReadHardware()
    {
        lock (_handleLock)
        {
            if (_handle == IntPtr.Zero) return -1;
            if (!GetVCPFeatureAndVCPReply(_handle, VcpBrightness, IntPtr.Zero, out var cur, out var max) || max == 0) return -1;
            _max = max;
            return (int)Math.Round(cur * 100.0 / max);
        }
    }

    public override void Dispose()
    {
        base.Dispose();
        lock (_handleLock) { if (_handle != IntPtr.Zero) DestroyPhysicalMonitor(_handle); _handle = IntPtr.Zero; }
    }
}
