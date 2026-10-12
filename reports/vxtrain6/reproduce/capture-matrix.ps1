param(
  [Parameter(Mandatory=$true)][string]$Tag,
  [string]$ConfigIds = 'default,esvo',
  [string]$States = 'midday,late-afternoon,overcast,night-service',
  [int]$Width = 1920,
  [int]$Height = 1080,
  [int]$Rep = 1,
  [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$Root = 'C:\vixen-native\vxtrain6'
$harness = "$Root\.tmp\vxtrain6-harness"
$cfg = Get-Content "$harness\configs.json" -Raw | ConvertFrom-Json
$exe = (Get-ChildItem -Path "$Root\build\vxtrain6-native" -Recurse -Filter vixen_lookdev_capture.exe | Select-Object -First 1).FullName
if (-not $exe) { throw 'vixen_lookdev_capture.exe was not found under build\vxtrain6-native' }
$tbb = (Get-ChildItem -Path "$Root\build\vxtrain6-native" -Recurse -Filter tbb12.dll | Select-Object -First 1).DirectoryName
if (-not $tbb) { throw 'tbb12.dll was not found under build\vxtrain6-native' }
$outRoot = "$Root\.tmp\native-matrix\$Tag\${Width}x${Height}"
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
    foreach ($key in @($psi.EnvironmentVariables.Keys)) {
      if ($key -like 'VIXEN_*') { [void]$psi.EnvironmentVariables.Remove($key) }
    }
    $psi.EnvironmentVariables['PATH'] = "$tbb;" + $psi.EnvironmentVariables['PATH']
    $psi.EnvironmentVariables['TEMP'] = $tmp
    $psi.EnvironmentVariables['TMP'] = $tmp
    $psi.EnvironmentVariables['VIXEN_HUD_SUPPRESS'] = '1'
    $psi.EnvironmentVariables['VIXEN_PERF_CSV'] = "$dir\perf.csv"
    $psi.EnvironmentVariables['VIXEN_LOOKDEV_CAPTURE_STATE'] = $state
    $psi.EnvironmentVariables['VIXEN_LOOKDEV_CAPTURE_DIR'] = $dir
    $psi.EnvironmentVariables['VIXEN_WINDOW_WIDTH'] = "$Width"
    $psi.EnvironmentVariables['VIXEN_WINDOW_HEIGHT'] = "$Height"
    foreach ($property in $config.env.PSObject.Properties) {
      $psi.EnvironmentVariables[$property.Name] = [string]$property.Value
    }

    $sw = [Diagnostics.Stopwatch]::StartNew()
    $started = (Get-Date).ToString('o')
    $proc = [Diagnostics.Process]::Start($psi)
    $null = $proc.Handle
    $timedOut = $false
    if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
      $timedOut = $true
      & taskkill /PID $proc.Id /T /F | Out-Null
    }
    $exit = if ($timedOut) { -1 } else { $proc.ExitCode }
    @{ config=$id; rep=$Rep; scene='lookdev'; width=$Width; height=$Height; state=$state;
       exit=$exit; timed_out=$timedOut; wall_seconds=$sw.Elapsed.TotalSeconds;
       env=$config.env; started=$started } |
      ConvertTo-Json -Depth 4 | Set-Content "$dir\status.json"
    Write-Output ("{0} r{1} {2} {3}x{4}: exit={5} wall={6:N1}s" -f $id, $Rep, $state, $Width, $Height, $exit, $sw.Elapsed.TotalSeconds)
    if ($exit -ne 0) { $failures += "$id r$Rep ${state}: exit $exit" }
  }
}
if ($failures.Count) {
  Write-Output ('FAILURES: ' + ($failures -join '; '))
  exit 1
}
exit 0
