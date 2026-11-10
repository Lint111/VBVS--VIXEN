# Samples machine CPU busy %, cl.exe count / peak and summed working set, and free memory once a second
# until the stop file appears. Usage: sample-build.ps1 -Out file.csv -Stop stopfile
param([string]$Out, [string]$Stop, [int]$IntervalS = 1)
"t,cpu_pct,cl_count,cl_max_ws_mb,cl_sum_ws_mb,free_mb,link_count,link_sum_ws_mb,avail_mb,cl_max_priv_mb,cl_sum_priv_mb" | Set-Content $Out
$t0 = Get-Date
while (-not (Test-Path $Stop)) {
    $ctr = (Get-Counter '\Processor(_Total)\% Processor Time','\Memory\Available MBytes' -ErrorAction SilentlyContinue).CounterSamples
    $cpu = $ctr[0].CookedValue; $avail = $ctr[1].CookedValue
    $cl = @(Get-Process cl -ErrorAction SilentlyContinue)
    $ln = @(Get-Process link,lld-link -ErrorAction SilentlyContinue)
    $free = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1024
    $max = 0; $sum = 0; $pmax = 0; $psum = 0
    foreach ($p in $cl) { $w = $p.WorkingSet64 / 1MB; $sum += $w; if ($w -gt $max) { $max = $w }; $v = $p.PrivateMemorySize64 / 1MB; $psum += $v; if ($v -gt $pmax) { $pmax = $v } }
    $lsum = 0; foreach ($p in $ln) { $lsum += $p.WorkingSet64 / 1MB }
    ("{0:F0},{1:F1},{2},{3:F0},{4:F0},{5:F0},{6},{7:F0},{8:F0},{9:F0},{10:F0}" -f ((Get-Date) - $t0).TotalSeconds, $cpu, $cl.Count, $max, $sum, $free, $ln.Count, $lsum, $avail, $pmax, $psum) | Add-Content $Out
    Start-Sleep -Seconds $IntervalS
}
