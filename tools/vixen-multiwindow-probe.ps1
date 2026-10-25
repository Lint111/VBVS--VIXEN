# Multi-process / resize probe for the native VIXEN window path.
# Starts N VIXEN.exe processes against ONE shared cache dir, then moves and resizes their windows
# and records, per window, the OS client size next to a screenshot of the client area.
# The log lines "[SwapChainNode] swapchain (re)created WxH" in each process log give the swapchain
# extent to compare with the client size reported here.
# String parameters only (a comma list passed through bash becomes one integer).
param(
    [string]$ExeDir,
    [string]$OutDir,
    [string]$Count = "3",
    [string]$Sizes = "900x700;400x300;700x500;1100x800;520x520",
    [string]$ExtraEnv = ""
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W32 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hh, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
}
"@
[void][W32]::SetProcessDPIAware()
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$cache = Join-Path $OutDir "cache"
New-Item -ItemType Directory -Force -Path $cache | Out-Null
$report = Join-Path $OutDir "probe.txt"
function Log($m) { $line = "{0:HH:mm:ss.fff} {1}" -f (Get-Date), $m; Add-Content -Path $report -Value $line; Write-Host $line }

function Snap($tag, $idx, $proc) {
    $h = $proc.MainWindowHandle
    # raise it so the capture is this window, not whatever overlaps it
    [void][W32]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x4000)
    Start-Sleep -Milliseconds 700
    $c = New-Object W32+RECT; $w = New-Object W32+RECT
    [void][W32]::GetClientRect($h, [ref]$c); [void][W32]::GetWindowRect($h, [ref]$w)
    $p = New-Object W32+POINT; [void][W32]::ClientToScreen($h, [ref]$p)
    $cw = $c.R - $c.L; $ch = $c.B - $c.T
    $proc.Refresh()
    Log ("{0} proc{1} pid={2} responding={9} client={3}x{4} window={5}x{6} at ({7},{8})" -f $tag, $idx, $proc.Id, $cw, $ch, ($w.R-$w.L), ($w.B-$w.T), $p.X, $p.Y, $proc.Responding)
    if ($cw -gt 0 -and $ch -gt 0) {
        $bmp = New-Object System.Drawing.Bitmap $cw, $ch
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($p.X, $p.Y, 0, 0, (New-Object System.Drawing.Size $cw, $ch))
        $g.Dispose()
        $bmp.Save((Join-Path $OutDir ("{0}_proc{1}.png" -f $tag, $idx)))
        $bmp.Dispose()
    }
    [void][W32]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x4000)
}

$n = [int]$Count
$procs = @()
for ($i = 0; $i -lt $n; $i++) {
    $env:VIXEN_CACHE_DIR = $cache
    $env:VIXEN_EXIT_AFTER_FRAMES = "200000"
    $env:VIXEN_LOG_LEVEL = "INFO"
    foreach ($kv in ($ExtraEnv -split ";")) { if ($kv -match "^(.+?)=(.*)$") { Set-Item -Path ("Env:" + $Matches[1]) -Value $Matches[2] } }
    # Output goes straight to files: pipes drained by the probe stalled behind a blocked window call.
    $p = Start-Process -FilePath (Join-Path $ExeDir "VIXEN.exe") -WorkingDirectory $ExeDir -PassThru `
        -RedirectStandardOutput (Join-Path $OutDir ("proc{0}.log" -f $i)) -RedirectStandardError (Join-Path $OutDir ("proc{0}.err" -f $i))
    $procs += $p
    Log ("started proc{0} pid={1}" -f $i, $p.Id)
    Start-Sleep -Seconds 2
}
Start-Sleep -Seconds 12
foreach ($p in $procs) { $p.Refresh() }
for ($i = 0; $i -lt $n; $i++) {
    if ($procs[$i].HasExited) { Log ("proc{0} EXITED code={1}" -f $i, $procs[$i].ExitCode); continue }
    [void][W32]::SetWindowPos($procs[$i].MainWindowHandle, [IntPtr]::Zero, 20 + 360 * $i, 40, 0, 0, 0x0001 -bor 0x0004 -bor 0x4000)
}
Start-Sleep -Seconds 2
for ($i = 0; $i -lt $n; $i++) { if (-not $procs[$i].HasExited) { Snap "A_initial" $i $procs[$i] } }

$sizeList = $Sizes -split ";"
$round = 0
foreach ($s in $sizeList) {
    $round++
    $wh = $s -split "x"
    for ($i = 0; $i -lt $n; $i++) {
        if ($procs[$i].HasExited) { continue }
        $sz = $sizeList[($round + $i - 1) % $sizeList.Count] -split "x"
        [void][W32]::SetWindowPos($procs[$i].MainWindowHandle, [IntPtr]::Zero, 0, 0, [int]$sz[0], [int]$sz[1], 0x0002 -bor 0x0004 -bor 0x4000)
    }
    Start-Sleep -Seconds 4
    for ($i = 0; $i -lt $n; $i++) { if (-not $procs[$i].HasExited) { Snap ("B_resize{0}" -f $round) $i $procs[$i] } }
}
# maximise the last process, then restore
if (-not $procs[$n-1].HasExited) {
    [void][W32]::ShowWindow($procs[$n-1].MainWindowHandle, 3); Start-Sleep -Seconds 4
    Snap "C_maximised" ($n-1) $procs[$n-1]
    [void][W32]::ShowWindow($procs[$n-1].MainWindowHandle, 9); Start-Sleep -Seconds 4
    Snap "D_restored" ($n-1) $procs[$n-1]
}
foreach ($p in $procs) {
    if (-not $p.HasExited) { [void]$p.CloseMainWindow() }
}
Start-Sleep -Seconds 6
foreach ($p in $procs) {
    if (-not $p.HasExited) { Log ("force-stopping pid={0}" -f $p.Id); Stop-Process -Id $p.Id -Force }
}
Log "probe done"
