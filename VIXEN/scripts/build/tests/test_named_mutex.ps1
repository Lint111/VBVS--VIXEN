# Purpose: prove a mutex abandoned by a killed holder (queue cancel -> taskkill /T /F) is taken over by
# Wait-VixenMutex instead of aborting the next build (T-4910), and that Get-NativeRunRootPid names a
# live process. Windows only. Usage: powershell -ExecutionPolicy Bypass -File test_named_mutex.ps1
$ErrorActionPreference = 'Stop'
$buildDir = Split-Path -Parent $PSScriptRoot
. "$buildDir\NamedMutex.ps1"
$env:VIXEN_NATIVE_PIDFILE = ""
. "$buildDir\Register-NativeRunRoot.ps1"

$name = "Global\VixenTestAbandon-$PID"
$failures = 0
function Check($label, $ok) { if ($ok) { Write-Host "PASS $label" } else { Write-Host "FAIL $label"; $script:failures++ } }

# A child process takes the mutex and blocks; killing it abandons the mutex.
$holder = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @('-NoProfile', '-Command',
    "`$m = New-Object System.Threading.Mutex(`$false, '$name'); `$null = `$m.WaitOne(); Start-Sleep -Seconds 600")
$probe = New-Object System.Threading.Mutex($false, $name)
$deadline = (Get-Date).AddSeconds(30)
$held = $false
while ((Get-Date) -lt $deadline -and -not $held) {
    Start-Sleep -Milliseconds 200
    try { $got = $probe.WaitOne(0); if ($got) { $probe.ReleaseMutex() } else { $held = $true } }
    catch [System.Threading.AbandonedMutexException] { $probe.ReleaseMutex() }
}
Check "holder process owns the mutex" $held
& taskkill.exe /PID $holder.Id /T /F | Out-Null
$holder.WaitForExit(10000) | Out-Null

$acquired = Wait-VixenMutex -Mutex $probe -TimeoutSeconds 10 -Name $name -Tag '[test]'
Check "abandoned mutex is taken over (not a timeout)" ($acquired -eq $true)
if ($acquired) { $probe.ReleaseMutex() }
$again = Wait-VixenMutex -Mutex $probe -TimeoutSeconds 10 -Name $name -Tag '[test]'
Check "mutex is clean after the takeover is released" ($again -eq $true)
if ($again) { $probe.ReleaseMutex() }
$probe.Dispose()

$root = Get-NativeRunRootPid
Check "run-root PID is a live process" ([bool](Get-Process -Id $root -ErrorAction SilentlyContinue))
$env:VIXEN_NATIVE_PIDFILE = "$env:TEMP\vixen_native_root_test_$PID.pid"
Register-NativeRunRoot | Out-Null
Check "Register-NativeRunRoot writes the PID file" ((Get-Content $env:VIXEN_NATIVE_PIDFILE).Trim() -eq "$root")
Remove-Item $env:VIXEN_NATIVE_PIDFILE -ErrorAction SilentlyContinue
# The queue's CPU-attribution registration (sn-wincpu1): the same root PID goes to UT_QUEUE_WINROOT_FILE, with or without a cancel pidfile.
$env:VIXEN_NATIVE_PIDFILE = ""
$env:UT_QUEUE_WINROOT_FILE = "$env:TEMP\ut_queue_winroot_test_$PID"
Register-NativeRunRoot | Out-Null
Check "Register-NativeRunRoot writes the queue registration file" ((Get-Content $env:UT_QUEUE_WINROOT_FILE).Trim() -eq "$root")
Remove-Item $env:UT_QUEUE_WINROOT_FILE -ErrorAction SilentlyContinue
$env:UT_QUEUE_WINROOT_FILE = ""
exit $failures
