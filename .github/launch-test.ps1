# The real launch paths, clicked through on the CI machine right after Desk-Setup.exe installed the suite (DeskBar is then the running taskbar).
# A self-test can check each app on its own; only this sees what a person sees: a menu row that a click doesn't reach, a Settings button that
# finds no DeskBar, an update that can't replace an open app. Each line is PASS or FAIL; the exit code is the number of failures.
#   powershell -File .github/launch-test.ps1 -Report <file>      (Windows PowerShell: it uses UI Automation)
# It moves the mouse, opens and closes windows and ends the Desk apps it started: run it on a test machine, not on a PC in use.
param([string]$Report = "$env:TEMP\launch-test.txt")
$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Windows.Forms
Add-Type @"
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text; using System.Threading;
public static class W {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; public POINT(int x, int y) { X = x; Y = y; } }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr after, string cls, string title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern uint RegisterWindowMessageW(string name);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeoutW(IntPtr h, uint m, IntPtr w, IntPtr l, uint flags, uint ms, out IntPtr result);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
    [DllImport("user32.dll")] public static extern bool RegisterHotKey(IntPtr h, int id, uint mods, uint vk);
    [DllImport("user32.dll")] public static extern bool UnregisterHotKey(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    public static string Class(IntPtr h) { var s = new StringBuilder(128); GetClassNameW(h, s, 128); return s.ToString(); }
    public static string Title(IntPtr h) { var s = new StringBuilder(256); GetWindowTextW(h, s, 256); return s.ToString(); }
    public static List<IntPtr> All(string cls) { var l = new List<IntPtr>(); IntPtr h = IntPtr.Zero; while ((h = FindWindowExW(IntPtr.Zero, h, cls, null)) != IntPtr.Zero) l.Add(h); return l; }
    public static void Click(int x, int y, bool right) {   // a real click, through Windows' own hit testing, as a hand on the mouse would make it
        SetCursorPos(x, y); Thread.Sleep(120);
        mouse_event(right ? 0x0008u : 0x0002u, 0, 0, 0, IntPtr.Zero); Thread.Sleep(70); mouse_event(right ? 0x0010u : 0x0004u, 0, 0, 0, IntPtr.Zero);
    }
    public static void Keys(byte[] vks) { foreach (var k in vks) keybd_event(k, 0, 0, IntPtr.Zero); Thread.Sleep(60); for (int i = vks.Length - 1; i >= 0; i--) keybd_event(vks[i], 0, 2, IntPtr.Zero); }
    public static string Under(int x, int y) { IntPtr h = WindowFromPoint(new POINT(x, y)); return h == IntPtr.Zero ? "nothing" : Class(h) + " \"" + Title(h) + "\""; }
}
"@

$results = New-Object System.Collections.Generic.List[string]; $script:fails = 0
function Check([string]$name, [bool]$ok, [string]$detail = '') {
    $line = $(if ($ok) { 'PASS ' } else { 'FAIL ' }) + $name + $(if ($detail) { "  [$detail]" } else { '' })
    $results.Add($line); Write-Host $line; if (-not $ok) { $script:fails++; if ($env:GITHUB_ACTIONS) { Write-Host "::error::launch test: $name $detail" } }
}
function WaitFor([scriptblock]$cond, [int]$ms = 8000) { $end = [DateTime]::Now.AddMilliseconds($ms); do { $v = & $cond; if ($v) { return $v }; Start-Sleep -Milliseconds 150 } while ([DateTime]::Now -lt $end); return $null }
$la = $env:LOCALAPPDATA
$exe = @{ DeskBar = "$la\Programs\DeskBar3\bin\DeskBar.exe"; DeskDrawer = "$la\Programs\DeskDrawer\DeskDrawer.exe"; DeskTune = "$la\Programs\DeskTune\DeskTune.exe" }
function Running([string]$app) { @(Get-Process -Name $app -ErrorAction SilentlyContinue | Where-Object { try { $_.Path -eq $exe[$app] } catch { $false } }) }
function SettingsWindow { [W]::All('DB3.Wnd') | Where-Object { [W]::IsWindowVisible($_) -and [W]::Title($_) -eq 'DeskBar settings' } | Select-Object -First 1 }
function CloseSettings { $s = SettingsWindow; if ($s) { [void][W]::PostMessageW($s, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero); [void](WaitFor { -not (SettingsWindow) } 4000) } }
function Visible { @([W]::All('DB3.Wnd') | Where-Object { [W]::IsWindowVisible($_) }) }
function RectOf($h) { $r = New-Object W+RECT; [void][W]::GetWindowRect($h, [ref]$r); $r }
function FindByName($hwnd, [string]$name) {   # UI Automation (ui_access.cpp): the element a screen reader would announce with this name
    try {
        $root = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
        $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $name)
        return $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
    } catch { return $null }
}
function ClickElement($el) { $b = $el.Current.BoundingRectangle; $x = [int]($b.X + $b.Width / 2); $y = [int]($b.Y + $b.Height / 2); $under = [W]::Under($x, $y); [W]::Click($x, $y, $false); return $under }

# ---- 0. DeskBar is installed and is the taskbar ----
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bar = WaitFor { [W]::All('DB3.Wnd') | Where-Object { $r = RectOf $_; [W]::IsWindowVisible($_) -and $r.B -eq $screen.Bottom -and ($r.R - $r.L) -ge $screen.Width - 2 -and ($r.B - $r.T) -lt 200 } | Select-Object -First 1 } 60000
Check 'DeskBar is running as the taskbar after Desk-Setup.exe' ([bool]$bar) "$(@(Running 'DeskBar').Count) DeskBar process(es)"
$br = if ($bar) { RectOf $bar } else { $null }
$scale = if ($br) { ($br.B - $br.T) / 48.0 } else { 1.0 }

# ---- 1. Start menu and Desktop shortcuts point at the installed apps ----
$sh = New-Object -ComObject WScript.Shell
$programs = [Environment]::GetFolderPath('Programs'); $desktop = [Environment]::GetFolderPath('DesktopDirectory')
foreach ($s in @(@{ L = "$programs\DeskDrawer.lnk"; T = $exe.DeskDrawer; A = '' }, @{ L = "$desktop\DeskDrawer.lnk"; T = $exe.DeskDrawer; A = '' }, @{ L = "$programs\Uninstall DeskDrawer.lnk"; T = $exe.DeskDrawer; A = '--uninstall' },
                 @{ L = "$programs\DeskTune.lnk"; T = $exe.DeskTune; A = '' }, @{ L = "$desktop\DeskTune.lnk"; T = $exe.DeskTune; A = '' }, @{ L = "$programs\Uninstall DeskTune.lnk"; T = $exe.DeskTune; A = '--uninstall' },
                 @{ L = "$programs\DeskBar\DeskBar.lnk"; T = $exe.DeskBar; A = '--supervise' }, @{ L = "$programs\DeskBar\Uninstall DeskBar.lnk"; T = $exe.DeskBar; A = '--uninstall' })) {
    $ok = Test-Path $s.L; $d = 'missing'
    if ($ok) { $l = $sh.CreateShortcut($s.L); $ok = ($l.TargetPath -eq $s.T) -and ($l.Arguments -eq $s.A) -and (Test-Path $l.TargetPath); $d = "$($l.TargetPath) $($l.Arguments)" }
    Check "shortcut $(Split-Path $s.L -Leaf) ($(Split-Path (Split-Path $s.L) -Leaf)) starts the installed app" $ok $d
}

# ---- 2. DeskDrawer's and DeskTune's Settings button: the message they send opens DeskBar's settings window ----
$msg = [W]::RegisterWindowMessageW('DeskOpenSettings')
foreach ($code in 1, 2, 3, 0) {
    CloseSettings; $acked = $false
    foreach ($h in ([W]::All('DB3.Wnd'))) { $r = [IntPtr]::Zero; if ([W]::SendMessageTimeoutW($h, $msg, [IntPtr]$code, [IntPtr]::Zero, 2, 1000, [ref]$r) -ne [IntPtr]::Zero -and $r.ToInt64() -eq 0x44534B) { $acked = $true; break } }
    $win = WaitFor { SettingsWindow } 6000
    Check "Settings from another Desk app (code $code): DeskBar answers and opens its settings window" ($acked -and [bool]$win) "answered $acked, window $([bool]$win)"
}
CloseSettings

# ---- 3. the taskbar's right-click menu: a real click on each row reaches it ----
function BarMenu {   # right-clicks an empty part of the bar; returns the menu window, or $null
    if (-not $br) { return $null }
    $y = [int](($br.T + $br.B) / 2)
    foreach ($f in 0.55, 0.45, 0.62, 0.38) {
        $before = Visible; $x = [int]($br.L + ($br.R - $br.L) * $f)
        [W]::Click($x, $y, $true)
        $m = WaitFor { Visible | Where-Object { $before -notcontains $_ } | Select-Object -First 1 } 2000
        if ($m -and (FindByName $m 'DeskBar settings')) { return $m }
        if ($m) { [W]::Keys([byte[]]@(0x1B)); Start-Sleep -Milliseconds 300 }   # a pin's or a window's menu: Esc, try another spot
    }
    return $null
}
function RowSpot($menu, [string]$name, [int]$fromBottom) {   # the row's centre: from UI Automation, else from the menu's layout (rows 36 high, 4 + 1 padding at the bottom)
    $el = FindByName $menu $name
    if ($el) { $b = $el.Current.BoundingRectangle; if ($b.Width -gt 0) { return @([int]($b.X + $b.Width / 2), [int]($b.Y + $b.Height / 2), 'ui automation') } }
    $r = RectOf $menu; return @([int](($r.L + $r.R) / 2), [int]($r.B - (5 + 1 + 18 + 38 * $fromBottom) * $scale), 'layout')
}
$menu = BarMenu
Check 'right-click on the taskbar shows its menu (with DeskBar settings)' ([bool]$menu)
if ($menu) {
    $x, $y, $how = RowSpot $menu 'DeskBar settings' 0; $under = [W]::Under($x, $y)
    [W]::Click($x, $y, $false); $win = WaitFor { SettingsWindow } 6000
    Check 'taskbar menu > DeskBar settings: a click on the row opens the settings window' ([bool]$win) "row at $x,$y by $how; under the pointer: $under; menu $($(RectOf $menu).T)..$($(RectOf $menu).B), bar top $($br.T)"
    CloseSettings
}
$menu = BarMenu
if ($menu) {
    $x, $y, $how = RowSpot $menu 'Task Manager' 1; $under = [W]::Under($x, $y)
    $tmBefore = @(Get-Process Taskmgr -ErrorAction SilentlyContinue).Count
    [W]::Click($x, $y, $false); $tm = WaitFor { @(Get-Process Taskmgr -ErrorAction SilentlyContinue).Count -gt $tmBefore } 8000
    Check 'taskbar menu > Task Manager: a click on the row starts it' ([bool]$tm) "row at $x,$y by $how; under the pointer: $under"
    Get-Process Taskmgr -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

# ---- 4. Desk settings > DeskDrawer / DeskTune: "Open ..." starts the installed app ----
foreach ($t in @(@{ App = 'DeskDrawer'; Code = 1; Row = 'Open DeskDrawer' }, @{ App = 'DeskTune'; Code = 2; Row = 'Open DeskTune' })) {
    Running $t.App | Stop-Process -Force -ErrorAction SilentlyContinue; CloseSettings
    foreach ($h in ([W]::All('DB3.Wnd'))) { $r = [IntPtr]::Zero; if ([W]::SendMessageTimeoutW($h, $msg, [IntPtr]$t.Code, [IntPtr]::Zero, 2, 1000, [ref]$r) -ne [IntPtr]::Zero -and $r.ToInt64() -eq 0x44534B) { break } }
    $win = WaitFor { SettingsWindow } 6000; $el = if ($win) { WaitFor { FindByName $win $t.Row } 4000 } else { $null }; $under = ''
    if ($el) { $under = ClickElement $el }
    $p = WaitFor { Running $t.App } 15000
    Check "Desk settings > $($t.App) > $($t.Row) starts the installed $($t.App)" ([bool]$p) "settings window $([bool]$win), row found $([bool]$el), under the pointer: $under"
    CloseSettings
}

# ---- 5. DeskMode's shortcuts are DeskBar's, and Ctrl+Alt+D opens its panel ----
$i = 0
foreach ($k in 'D', 'M', 'B', 'F') {
    $free = [W]::RegisterHotKey([IntPtr]::Zero, 0x7100 + $i, 0x4003, [uint32][char]$k); if ($free) { [void][W]::UnregisterHotKey([IntPtr]::Zero, 0x7100 + $i) }
    Check "Ctrl+Alt+$k belongs to DeskBar (nothing else can take it)" (-not $free); $i++
}
$before = Visible; [W]::Keys([byte[]]@(0x11, 0x12, 0x44))   # Ctrl+Alt+D
$panel = WaitFor { Visible | Where-Object { $before -notcontains $_ } | Select-Object -First 1 } 4000
Check 'Ctrl+Alt+D opens the DeskMode panel' ([bool]$panel)
if ($panel) { [W]::Keys([byte[]]@(0x11, 0x12, 0x44)); Start-Sleep -Milliseconds 500; [W]::Keys([byte[]]@(0x1B)) }

# ---- 6. an update or a reinstall while the app is open (what its updater, Settings > Desk apps and Desk-Setup.exe do: run <app>-Setup.exe) ----
foreach ($app in 'DeskDrawer', 'DeskTune') {
    if (-not (Running $app)) { Start-Process $exe[$app]; [void](WaitFor { Running $app } 15000) }
    Start-Sleep -Seconds 3; $old = @(Running $app | ForEach-Object { $_.Id })
    $dir = Join-Path $env:TEMP "DeskUpdate-$app"; New-Item -ItemType Directory -Force $dir | Out-Null; $setup = Join-Path $dir "$app-Setup.exe"; Copy-Item $exe[$app] $setup -Force
    $p = Start-Process $setup -PassThru; $done = $p.WaitForExit(60000); if (-not $done) { $p.Kill() }
    $new = WaitFor { Running $app | Where-Object { $old -notcontains $_.Id } } 15000
    Check "$app updates while it is open: the setup replaces it and starts the new copy" ($done -and $p.ExitCode -eq 0 -and [bool]$new -and -not (Running $app | Where-Object { $old -contains $_.Id })) "open before: $($old -join ','); setup finished $done, exit $(if ($done) { $p.ExitCode }); running now: $(@(Running $app | ForEach-Object { $_.Id }) -join ',')"
    Running $app | Stop-Process -Force -ErrorAction SilentlyContinue
}

$results.Add("$($results.Count - $script:fails) passed, $($script:fails) failed")
New-Item -ItemType Directory -Force (Split-Path $Report) | Out-Null; $results | Set-Content -Path $Report -Encoding UTF8
Write-Host "$($results.Count - 1 - $script:fails) passed, $($script:fails) failed"
exit $script:fails
