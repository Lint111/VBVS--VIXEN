# Purpose: prove the Job Object that tools/vixen-native.sh puts the native build tree in (box-winjob.exe, box-queue
# managed/BoxQueue.WinJob) ends cl.exe/ninja.exe when the process that holds it is killed abruptly, the way a SIGKILL of the WSL
# launcher kills the interop relay and then box-winjob.exe (sn-winjob1). Also proves the harness can fail: the same tree run WITHOUT the
# job is orphaned, not killed. Windows only. Run it as a native job through the queue.
# Usage: powershell -ExecutionPolicy Bypass -File test_winjob_kill.ps1 -WinJobExe <C:\path\box-winjob.exe> [-WaitSeconds N]
param(
    [Parameter(Mandatory = $true)][string]$WinJobExe,
    [int]$WaitSeconds = 15
)
$ErrorActionPreference = 'Stop'
$failures = 0
function Check($label, $ok) { if ($ok) { Write-Host "PASS $label" } else { Write-Host "FAIL $label"; $script:failures++ } }

# The stand-in tree is cmd.exe -> cl.exe + ninja.exe (copies of ping.exe, which sleeps for -n seconds) in a throwaway lane directory,
# so "a descendant under the lane directory" is decided by image path, whoever the parent was.
$lane = Join-Path $env:TEMP "winjob-lane-$PID"
New-Item -ItemType Directory -Path $lane | Out-Null
foreach ($name in 'cl.exe', 'ninja.exe') { Copy-Item "$env:SystemRoot\System32\ping.exe" (Join-Path $lane $name) }
Set-Content -Path (Join-Path $lane 'empty.txt') -Value '' -Encoding ascii
Set-Content -Path (Join-Path $lane 'tree.cmd') -Encoding ascii -Value @(
    '@echo off',
    'start "" /b "%~dp0cl.exe" -n 600 127.0.0.1 >nul',
    '"%~dp0ninja.exe" -n 600 127.0.0.1 >nul')
Set-Content -Path (Join-Path $lane 'orphan.cmd') -Encoding ascii -Value @(
    '@echo off',
    'start "" /b "%~dp0cl.exe" -n 600 127.0.0.1 >nul',
    'exit /b 7')

function Get-LaneProcesses {
    @(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($lane, [StringComparison]::OrdinalIgnoreCase) })
}
function Wait-LaneProcesses($count, $seconds) {  # polls until at least $count lane processes exist (-ge) or, for 0, none do
    $deadline = (Get-Date).AddSeconds($seconds)
    do {
        $n = (Get-LaneProcesses).Count
        if (($count -eq 0 -and $n -eq 0) -or ($count -gt 0 -and $n -ge $count)) { return $true }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return $false
}
function Stop-LaneProcesses { foreach ($p in Get-LaneProcesses) { Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue } }
function Start-Quiet($file, $argumentList, $errFile) {
    Start-Process -FilePath $file -ArgumentList $argumentList -PassThru -WindowStyle Hidden `
        -RedirectStandardInput (Join-Path $lane 'empty.txt') -RedirectStandardOutput (Join-Path $lane "out-$errFile") -RedirectStandardError (Join-Path $lane $errFile)
}

try {
    # Control: no job. Killing the root leaves the tree running (the leak this lane exists for).
    $root = Start-Quiet 'cmd.exe' @('/c', "`"$lane\tree.cmd`"") 'control.err'
    Check 'control: the tree started (cl.exe and ninja.exe under the lane)' (Wait-LaneProcesses 2 $WaitSeconds)
    Stop-Process -Id $root.Id -Force
    Start-Sleep -Seconds 3
    Check 'control: without a job the descendants outlive their killed root' ((Get-LaneProcesses).Count -ge 2)
    Stop-LaneProcesses
    Check 'control: cleaned up by explicit PIDs' (Wait-LaneProcesses 0 $WaitSeconds)

    # Abrupt death of the job holder: TerminateProcess, no cleanup code runs, exactly what the relay's death does to it.
    $holder = Start-Quiet $WinJobExe @('run', '--', 'cmd.exe', '/c', "`"$lane\tree.cmd`"") 'holder.err'
    Check 'job: the tree started under box-winjob.exe' (Wait-LaneProcesses 2 $WaitSeconds)
    $started = Get-Date
    Stop-Process -Id $holder.Id -Force
    $gone = Wait-LaneProcesses 0 $WaitSeconds
    Check "job: no cl.exe/ninja.exe under the lane $([int]((Get-Date) - $started).TotalSeconds)s after the holder was killed (limit ${WaitSeconds}s)" $gone
    if (-not $gone) { Get-LaneProcesses | ForEach-Object { Write-Host "  left: $($_.ProcessId) $($_.ExecutablePath)" }; Stop-LaneProcesses }

    # Normal end: the child's exit code comes back and a process it left running is reaped, and said so.
    $normal = Start-Quiet $WinJobExe @('run', '--', 'cmd.exe', '/c', "`"$lane\orphan.cmd`"") 'normal.err'
    $null = $normal.Handle   # Start-Process -PassThru reports a null ExitCode unless the handle was opened before the exit
    $normal.WaitForExit(30000) | Out-Null
    Check "normal: the child exit code (7) is the holder exit code (got '$($normal.ExitCode)')" ($normal.ExitCode -eq 7)
    Check 'normal: a process left running at the end is gone' (Wait-LaneProcesses 0 $WaitSeconds)
    Check 'normal: the reaping is reported on stderr' ((Get-Content (Join-Path $lane 'normal.err') -Raw) -match 'reaped 1 process')
    Stop-LaneProcesses
}
finally {
    Stop-LaneProcesses
    Start-Sleep -Milliseconds 500
    Remove-Item -Recurse -Force $lane -ErrorAction SilentlyContinue
}
if ($failures -gt 0) { Write-Host "$failures check(s) failed"; exit 1 }
Write-Host 'all checks passed'
