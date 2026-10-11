$ErrorActionPreference = 'Stop'
$owned = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'gpu-cadence-probe.exe' -and $_.CommandLine -and $_.CommandLine.Contains('gpuyield') -and $_.CommandLine.Contains('native-under-editor') })
foreach ($process in $owned) { Write-Output "Stopping owned native peer PID $($process.ProcessId)"; Stop-Process -Id $process.ProcessId -Force }
