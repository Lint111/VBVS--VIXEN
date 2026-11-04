# Self-check for native-process.ps1 (Windows PowerShell, run through the queue as a light job):
#   powershell -NoProfile -ExecutionPolicy Bypass -File test-native-process.ps1
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'native-process.ps1')
$cmd = Join-Path $env:SystemRoot 'System32\cmd.exe'
$log = Join-Path $env:TEMP "vixen-native-process-$PID.log"
$failures = 0
function Check([string]$name, [bool]$ok) {
  if ($ok) { Write-Output "PASS $name" } else { Write-Output "FAIL $name"; $script:failures++ }
}

$rc = Invoke-NativeJobProcess -FilePath $cmd -ArgumentList @('/c', 'echo first&&ping -n 6 127.0.0.1 >nul&&echo last&&exit 3') `
  -LogPath $log -Description 'stub' -HeartbeatSeconds 2
$text = Get-Content -Raw -LiteralPath $log
Check 'exit code is passed through' ($rc -eq 3)
Check 'process output is logged' ($text -match 'first' -and $text -match 'last')
Check 'a silent stretch produces heartbeat lines' (([regex]::Matches($text, 'NATIVE_HEARTBEAT')).Count -ge 1)

$before = @(Get-Process ping -ErrorAction SilentlyContinue).Count
$watch = [Diagnostics.Stopwatch]::StartNew()
$rc = Invoke-NativeJobProcess -FilePath $cmd -ArgumentList @('/c', 'start /b ping -n 120 127.0.0.1 >nul&&echo started') `
  -LogPath $log -Description 'orphaning stub' -HeartbeatSeconds 2
Check 'a descendant holding the output pipes does not block the wrapper' ($watch.Elapsed.TotalSeconds -lt 30)
# The wrapper's own exit closes the job; stand in for it by checking the job-owned child is reaped when
# this script ends. Report the count now and let the caller compare it after the script exits.
Write-Output "ping processes still running inside the job: $(@(Get-Process ping -ErrorAction SilentlyContinue).Count - $before)"
Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
exit $failures
