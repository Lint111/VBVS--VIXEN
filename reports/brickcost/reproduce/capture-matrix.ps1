param(
  [Parameter(Mandatory=$true)][string]$ConfigIds,
  [Parameter(Mandatory=$true)][int]$Rep,
  [Parameter(Mandatory=$true)][string]$Tag,
  [string]$Root = 'C:\vixen-native\brickcost',
  [string]$States = 'midday,late-afternoon,overcast,night-service',
  [string]$ExtraEnv = 'VIXEN_WINDOW_WIDTH=500;VIXEN_WINDOW_HEIGHT=500',
  [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$cfg = Get-Content "$Root\reports\brickcost\reproduce\configs.json" -Raw | ConvertFrom-Json
$exe = (Get-ChildItem -Path "$Root\build\brickcost-native" -Recurse -Filter vixen_lookdev_capture.exe | Select-Object -First 1).FullName
if (-not $exe) { throw 'vixen_lookdev_capture.exe was not found under build\brickcost-native' }
$tbb = (Get-ChildItem -Path "$Root\build\brickcost-native" -Recurse -Filter tbb12.dll | Select-Object -First 1).DirectoryName
if (-not $tbb) { throw 'tbb12.dll was not found under build\brickcost-native' }
$outRoot = "$Root\reports\brickcost\native\$Tag"
$tmp = "$Root\.tmp\native-runtime-temp"
New-Item -ItemType Directory -Force $tmp | Out-Null
$failures = @()
foreach ($id in $ConfigIds.Split(',')) {
  $config = $cfg.configs | Where-Object { $_.id -eq $id }
  if (-not $config) { throw "unknown config $id" }
  foreach ($state in $States.Split(',')) {
    $dir = "$outRoot\$id\r$Rep\$state"
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    New-Item -ItemType Directory -Force $dir | Out-Null
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $env:ComSpec
    $psi.Arguments = '/d /s /c ""' + $exe + '" --gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically >"' + $dir + '\stdout.log" 2>"' + $dir + '\stderr.log""'
    $psi.WorkingDirectory = Split-Path $exe
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    foreach ($key in @($psi.Environment.Keys)) { if ($key -like 'VIXEN_*') { [void]$psi.Environment.Remove($key) } }
    $psi.Environment['PATH'] = "$tbb;" + $psi.Environment['PATH']
    $psi.Environment['TEMP'] = $tmp
    $psi.Environment['TMP'] = $tmp
    $psi.Environment['VIXEN_HUD_SUPPRESS'] = '1'
    $psi.Environment['VIXEN_PERF_CSV'] = "$dir\perf.csv"
    $psi.Environment['VIXEN_LOOKDEV_CAPTURE_STATE'] = $state
    $psi.Environment['VIXEN_LOOKDEV_CAPTURE_DIR'] = $dir
    foreach ($prop in $config.env.PSObject.Properties) { $psi.Environment[$prop.Name] = [string]$prop.Value }
    foreach ($pair in $ExtraEnv.Split(';')) {
      if (-not $pair) { continue }
      $i = $pair.IndexOf('=')
      if ($i -lt 1) { throw "invalid environment pair '$pair'" }
      $psi.Environment[$pair.Substring(0, $i)] = $pair.Substring($i + 1)
    }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $proc = [Diagnostics.Process]::Start($psi)
    $null = $proc.Handle
    $timedOut = $false
    if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
      $timedOut = $true
      & taskkill /PID $proc.Id /T /F | Out-Null
    }
    $exit = if ($timedOut) { -1 } else { $proc.ExitCode }
    @{ config=$id; rep=$Rep; state=$state; exit=$exit; timed_out=$timedOut;
       wall_seconds=$sw.Elapsed.TotalSeconds; exe=$exe; env=$config.env;
       extra_env=$ExtraEnv; started=(Get-Date).ToString('o') } |
      ConvertTo-Json -Depth 4 | Set-Content "$dir\status.json"
    Write-Output ("{0} r{1} {2}: exit={3} wall={4:N1}s" -f $id, $Rep, $state, $exit, $sw.Elapsed.TotalSeconds)
    if ($exit -ne 0) { $failures += "$id r$Rep ${state}: exit $exit" }
  }
}
if ($failures.Count) {
  Write-Output ('FAILURES: ' + ($failures -join '; '))
  exit 1
}
