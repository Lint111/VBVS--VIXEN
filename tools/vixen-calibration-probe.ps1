# Calibration persistence probe: starts N native VIXEN.exe processes that share ONE cache directory
# and each exit by themselves after -Frames frames (a graceful shutdown, so CalibrationStore saves).
# Records every process's stdout/stderr and a copy of the calibration directory when all are done.
# Run it once with -Count 1 (a clean calibration) and then with -Count 3 (contended) against the same
# -CacheDir to see whether the contended runs replaced the clean file.
# String parameters only (see vixen-multiwindow-probe.ps1).
param(
    [string]$ExeDir,
    [string]$OutDir,
    [string]$CacheDir,
    [string]$Count = "1",
    [string]$Frames = "900",
    [string]$TimeoutSeconds = "420",
    [string]$ExtraEnv = "VIXEN_HIDDEN_WINDOW=1"
)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null
$report = Join-Path $OutDir "calibration-probe.txt"
function Log($m) { $line = "{0:HH:mm:ss.fff} {1}" -f (Get-Date), $m; Add-Content -Path $report -Value $line; Write-Host $line }

$env:VIXEN_CACHE_DIR = $CacheDir
$env:VIXEN_EXIT_AFTER_FRAMES = $Frames
$env:VIXEN_LOG_LEVEL = "INFO"
foreach ($kv in ($ExtraEnv -split ";")) { if ($kv -match "^(.+?)=(.*)$") { Set-Item -Path ("Env:" + $Matches[1]) -Value $Matches[2] } }

$calDir = Join-Path $CacheDir "calibration"
$before = if (Test-Path $calDir) { (Get-ChildItem $calDir -Filter *.json | ForEach-Object { "{0} {1}" -f $_.Name, $_.LastWriteTime.ToString("HH:mm:ss.fff") }) -join "; " } else { "(none)" }
Log "calibration before: $before"

$procs = @()
for ($i = 0; $i -lt [int]$Count; $i++) {
    $p = Start-Process -FilePath (Join-Path $ExeDir "VIXEN.exe") -WorkingDirectory $ExeDir -PassThru `
        -RedirectStandardOutput (Join-Path $OutDir ("proc{0}.log" -f $i)) -RedirectStandardError (Join-Path $OutDir ("proc{0}.err" -f $i))
    $procs += $p
    Log ("started proc{0} pid={1}" -f $i, $p.Id)
}
$deadline = (Get-Date).AddSeconds([int]$TimeoutSeconds)
foreach ($p in $procs) {
    $left = [int](($deadline - (Get-Date)).TotalMilliseconds)
    if ($left -lt 1) { $left = 1 }
    if (-not $p.WaitForExit($left)) { Log ("TIMEOUT pid={0}; force-stopping" -f $p.Id); Stop-Process -Id $p.Id -Force }
    else { Log ("proc pid={0} exited code={1}" -f $p.Id, $p.ExitCode) }
}
$after = if (Test-Path $calDir) { (Get-ChildItem $calDir -Filter *.json | ForEach-Object { "{0} {1}" -f $_.Name, $_.LastWriteTime.ToString("HH:mm:ss.fff") }) -join "; " } else { "(none)" }
Log "calibration after: $after"
if (Test-Path $calDir) { Copy-Item -Recurse -Force $calDir (Join-Path $OutDir "calibration") }
Log "probe done"
