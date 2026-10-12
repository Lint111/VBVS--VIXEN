param(
  [Parameter(Mandatory=$true)][string]$Tag,
  [string]$State = 'midday',
  [string]$ExtraEnv = '',
  [string]$Root = 'C:\vixen-native\capturespeed',
  [int]$TimeoutSeconds = 900
)
$ErrorActionPreference = 'Stop'
$exe = (Get-ChildItem -Path "$Root\build\capturespeed-native" -Recurse -Filter vixen_lookdev_capture.exe | Select-Object -First 1).FullName
if (-not $exe) { throw 'vixen_lookdev_capture.exe was not found' }
$tbb = (Get-ChildItem -Path "$Root\build\capturespeed-native" -Recurse -Filter tbb12.dll | Select-Object -First 1).DirectoryName
$dir = "$Root\reports\capturespeed\native\$Tag"
if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
New-Item -ItemType Directory -Force $dir | Out-Null
$tmp = "$Root\.tmp\native-runtime-temp"
New-Item -ItemType Directory -Force $tmp | Out-Null
$psi = [Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $env:ComSpec
$psi.Arguments = '/d /s /c ""' + $exe + '" --gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically >"' + $dir + '\stdout.log" 2>"' + $dir + '\stderr.log""'
$psi.WorkingDirectory = Split-Path $exe
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
foreach ($key in @($psi.Environment.Keys)) { if ($key -like 'VIXEN_*') { [void]$psi.Environment.Remove($key) } }
if ($tbb) { $psi.Environment['PATH'] = "$tbb;" + $psi.Environment['PATH'] }
$psi.Environment['TEMP'] = $tmp
$psi.Environment['TMP'] = $tmp
$psi.Environment['VIXEN_HDR_EXPOSURE'] = '1'
$psi.Environment['VIXEN_HUD_SUPPRESS'] = '1'
$psi.Environment['VIXEN_PHASE_TIMING'] = '1'
$psi.Environment['VIXEN_LOOKDEV_CAPTURE_STATE'] = $State
$psi.Environment['VIXEN_LOOKDEV_CAPTURE_DIR'] = "$dir\png"
foreach ($pair in $ExtraEnv.Split(';')) {
  if ($pair -match '^([^=]+)=(.*)$') { $psi.Environment[$Matches[1]] = $Matches[2] }
}
$sw = [Diagnostics.Stopwatch]::StartNew()
$proc = [Diagnostics.Process]::Start($psi)
if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) { & taskkill /F /T /PID $proc.Id | Out-Null; throw "timeout after $TimeoutSeconds s" }
$sw.Stop()
"exit=$($proc.ExitCode) wall_s=$([math]::Round($sw.Elapsed.TotalSeconds,2))" | Tee-Object -FilePath "$dir\result.txt"
exit $proc.ExitCode
