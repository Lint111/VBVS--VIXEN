param([string]$Binary,[string]$Output,[int]$Frames=500,[int]$Width=500,[int]$Height=500,[switch]$HardwareQuery,[switch]$DisableTrace)
$ErrorActionPreference='Stop'
$env:TEMP='C:\vixen-native\gpuyield\.tmp\native-runtime-temp'
$env:TMP=$env:TEMP
New-Item -ItemType Directory -Force $env:TEMP | Out-Null
New-Item -ItemType Directory -Force $Output | Out-Null
$env:VIXEN_RENDER_SCALE='1.0'
$env:VIXEN_WINDOW_WIDTH="$Width"
$env:VIXEN_WINDOW_HEIGHT="$Height"
$env:VIXEN_GPUYIELD_TRACE=if($DisableTrace){$null}else{'1'}
$env:VIXEN_EDITOR_TEST_CAMERA='top-down'
$env:VIXEN_EXIT_AFTER_FRAMES="$Frames"
$env:VIXEN_PERF_CSV="$Output\vixen-perf.csv"
if ($HardwareQuery) {$env:VIXEN_RTQUERY_TRAVERSAL='1'; $env:VIXEN_RT_LIGHTING='force'}
$start=[Diagnostics.ProcessStartInfo]::new()
$start.FileName=$env:ComSpec
$start.Arguments='/d /s /c ""'+$Binary+'" >"'+$Output+'\vixen.log" 2>"'+$Output+'\host.log""'
$start.WorkingDirectory=Split-Path $Binary
$start.UseShellExecute=$false
$start.CreateNoWindow=$true
$p=[Diagnostics.Process]::Start($start)
$null=$p.Handle
$sw=[Diagnostics.Stopwatch]::StartNew()
try {
 if (!$p.WaitForExit(240000)) {throw 'Native full-producer timed out after 240 seconds'}
 @{exit=$p.ExitCode;pid=$p.Id;wall_seconds=$sw.Elapsed.TotalSeconds;frames=$Frames;binary=$Binary;scale=1.0;width=$Width;height=$Height;hardware_query=$HardwareQuery.IsPresent;binary_sha256=(Get-FileHash $Binary -Algorithm SHA256).Hash} | ConvertTo-Json | Set-Content "$Output\status.json"
 if($p.ExitCode -ne 0){throw "Native full-producer exit $($p.ExitCode)"}
} finally {if(!$p.HasExited){& taskkill /PID $p.Id /T /F}}
