param(
    [string]$RepoRoot = 'C:\vixen-native\upscalestage1',
    [string]$OutputRoot = '',
    [int]$Rounds = 3,
    [int]$TimeoutSeconds = 1200,
    [ValidateSet('fixed', 'coupled')]
    [string]$LodMode = 'fixed',
    [ValidateSet('all', '500', '1080')]
    [string]$Extent = 'all',
    [int]$Round = 0,
    # One FSR 1 capture per profile (500/1080 extents, fixed and coupled LOD) for per-stage
    # input-AA/EASU/RCAS cost; steady frames are 33-64 of each yaw. No A/B/A controls.
    [switch]$StageCost,
    [string]$BuildDir = 'build\stage1-merged-native'
)

$ErrorActionPreference = 'Stop'
$exe = Join-Path $RepoRoot (Join-Path $BuildDir 'application\main\vixen_lookdev_capture.exe')
if (-not (Test-Path $exe)) { throw "Capture executable not found: $exe" }
$vulkanSdk = 'C:\VulkanSDK\1.4.350.0'
if (Test-Path $vulkanSdk) {
    $env:VULKAN_SDK = $vulkanSdk
    $env:PATH = (Join-Path $vulkanSdk 'Bin') + ';' + $env:PATH
}
$runtimeRoot = Join-Path $RepoRoot $BuildDir
$runtimeDirs = @(Get-ChildItem $runtimeRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName 'tbb12.dll') } |
    Select-Object -ExpandProperty FullName)
if ($runtimeDirs.Count) {
    $env:PATH = ($runtimeDirs -join ';') + ';' + $env:PATH
}
if (-not $StageCost -and $Rounds -lt 3) { throw 'At least three A/B/A rounds are required.' }
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $leaf = if ($StageCost) { 'stage-cost' } else { $LodMode }
    $OutputRoot = Join-Path $RepoRoot ('reports\upscalestage1\paired-native\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $leaf)
}
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

$configs = @(
    [pscustomobject]@{ Name = 'linear-scale1'; Scale = '1.0'; Upscaler = 'linear' },
    [pscustomobject]@{ Name = 'linear-scale-2-3'; Scale = '0.6666667'; Upscaler = 'linear' },
    [pscustomobject]@{ Name = 'linear-scale-1-2'; Scale = '0.5'; Upscaler = 'linear' },
    [pscustomobject]@{ Name = 'fsr1-scale1'; Scale = '1.0'; Upscaler = 'fsr1' },
    [pscustomobject]@{ Name = 'fsr1-scale-2-3'; Scale = '0.6666667'; Upscaler = 'fsr1' },
    [pscustomobject]@{ Name = 'fsr1-scale-1-2'; Scale = '0.5'; Upscaler = 'fsr1' }
)
if ($LodMode -eq 'coupled') {
    $configs = @($configs | Where-Object { $_.Scale -ne '1.0' })
}
$stageCostCases = @(
    [pscustomobject]@{ Name = 'fixed-500-scale1'; Width = 500; Height = 500; Scale = '1.0'; Lod = 'fixed' },
    [pscustomobject]@{ Name = 'fixed-500-scale-2-3'; Width = 500; Height = 500; Scale = '0.6666667'; Lod = 'fixed' },
    [pscustomobject]@{ Name = 'fixed-500-scale-1-2'; Width = 500; Height = 500; Scale = '0.5'; Lod = 'fixed' },
    [pscustomobject]@{ Name = 'coupled-500-scale-2-3'; Width = 500; Height = 500; Scale = '0.6666667'; Lod = 'coupled' },
    [pscustomobject]@{ Name = 'coupled-500-scale-1-2'; Width = 500; Height = 500; Scale = '0.5'; Lod = 'coupled' },
    [pscustomobject]@{ Name = 'fixed-1080-scale1'; Width = 1920; Height = 1080; Scale = '1.0'; Lod = 'fixed' },
    [pscustomobject]@{ Name = 'fixed-1080-scale-2-3'; Width = 1920; Height = 1080; Scale = '0.6666667'; Lod = 'fixed' },
    [pscustomobject]@{ Name = 'fixed-1080-scale-1-2'; Width = 1920; Height = 1080; Scale = '0.5'; Lod = 'fixed' }
)
if ($StageCost -and $Extent -ne 'all') {
    $stageCostCases = @($stageCostCases | Where-Object { ($Extent -eq '500') -eq ($_.Width -eq 500) })
}
$testFilter = '--gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically'
$telemetryPath = Join-Path $OutputRoot 'gpu-telemetry.csv'
$telemetryNote = Join-Path $OutputRoot 'gpu-telemetry-availability.txt'
$manifestPath = Join-Path $OutputRoot 'run-manifest.txt'
$counterPaths = @()

if ($StageCost) {
    @(
        "repo=$RepoRoot"
        "executable=$exe"
        'sequence=one FSR 1 capture per profile; four midday yaws; this sweep reports stage costs, not A/B/A gate results'
        'camera=orbit center (5,5,5); distance 14; FOV 45 degrees; pitch 0.45 radians; yaws 0/90/180/270 degrees'
        'fixed_exposure=VIXEN_HDR_EXPOSURE=1; VIXEN_HDR_EXPOSURE_FIXED=1'
        'recipe=static authored LookdevSceneDefinition; no per-run RNG; sky_seed=0x5a17f13d'
        'steady_cost_samples=frames 33-64 per yaw (128 samples)'
        "profiles=$(($stageCostCases | ForEach-Object { $_.Name }) -join ',')"
    ) | Set-Content -Path $manifestPath -Encoding UTF8
} elseif (-not (Test-Path $manifestPath)) {
    @(
        "repo=$RepoRoot"
        "executable=$exe"
        "vulkan_sdk=$($env:VULKAN_SDK)"
        "runtime_dll_dirs=$($runtimeDirs -join ';')"
        "rounds=$Rounds"
        "lod_mode=$LodMode"
        'sequence=midday; four fixed yaws; 64 frames per yaw; A/B/A per candidate and round'
        'camera=orbit center (5,5,5); distance 14; FOV 45 degrees; pitch 0.45 radians; yaws 0/90/180/270 degrees'
        'recipe=static authored LookdevSceneDefinition; no per-run RNG; sky_seed=0x5a17f13d'
        'fixed_exposure=VIXEN_HDR_EXPOSURE=1; VIXEN_HDR_EXPOSURE_FIXED=1'
        $(if ($LodMode -eq 'fixed') { 'lod_policy=fixed; VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE=0.00165685425' } else { 'lod_policy=coupled; VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE unset' })
        'scale/upscaler supplied as RenderPathConfig test args, not environment variables'
    ) | Set-Content -Path $manifestPath -Encoding UTF8
} elseif (
    -not ((Get-Content $manifestPath -Raw) -match [regex]::Escape("rounds=$Rounds")) -or
    -not ((Get-Content $manifestPath -Raw) -match [regex]::Escape("lod_mode=$LodMode"))
) {
    throw "Output root belongs to a different round configuration: $OutputRoot"
}

try {
    $gpuSets = @(Get-Counter -ListSet * -ErrorAction Stop | Where-Object {
        $_.CounterSetName -match '(?i)GPU|AMD|Radeon'
    })
    $counterPaths = @($gpuSets | ForEach-Object { $_.PathsWithInstances } |
        Where-Object { $_ -match '(?i)(clock|frequency|power)' } | Sort-Object -Unique)
    $driver = @(Get-CimInstance Win32_VideoController -ErrorAction Stop |
        Where-Object { $_.Name -match '(?i)Radeon|AMD' } |
        Select-Object Name, DriverVersion, VideoModeDescription)
    @(
        "Adapter and driver:"
        ($driver | Format-Table -AutoSize | Out-String).TrimEnd()
        "GPU clock/power counter paths:"
        $(if ($counterPaths.Count) { $counterPaths -join "`r`n" } else { 'none exposed by Windows performance counters' })
        "AMD/ROCm management commands:"
        $(@(Get-Command 'amd-smi*','rocm-smi*' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source) -join "`r`n")
    ) | Set-Content -Path $telemetryNote -Encoding UTF8
} catch {
    "GPU telemetry discovery unavailable: $($_.Exception.Message)" | Set-Content -Path $telemetryNote -Encoding UTF8
    $counterPaths = @()
}

function Record-GpuTelemetry([string]$RunLabel, [string]$Phase) {
    if (-not $counterPaths.Count) { return }
    try {
        $sample = Get-Counter -Counter $counterPaths -ErrorAction Stop
        foreach ($counter in $sample.CounterSamples) {
            [pscustomobject]@{
                run = $RunLabel
                phase = $Phase
                sampled_at = (Get-Date).ToUniversalTime().ToString('o')
                counter = $counter.Path
                value = $counter.CookedValue
            } | Export-Csv -Path $telemetryPath -NoTypeInformation -Append
        }
    } catch {
        "Telemetry sample failed for ${RunLabel}/${Phase}: $($_.Exception.Message)" |
            Add-Content -Path $telemetryNote -Encoding UTF8
    }
}

function Invoke-Capture(
    [int]$Width,
    [int]$Height,
    [int]$Round,
    [string]$ConfigName,
    [string]$Role,
    [string]$Scale,
    [string]$Upscaler,
    [string]$Lod = $LodMode,
    [string]$RunDirOverride = ''
) {
    $runLabel = "${Width}x${Height}-round-$Round-$ConfigName-$Role"
    $runDir = Join-Path $OutputRoot (Join-Path "${Width}x${Height}" (Join-Path "round-$Round" (Join-Path $ConfigName $Role)))
    if ($RunDirOverride) {
        $runLabel = $ConfigName
        $runDir = $RunDirOverride
    }
    New-Item -ItemType Directory -Path $runDir -Force | Out-Null
    $csv = Join-Path $runDir 'perf.csv'
    $captureDir = Join-Path $runDir 'captures'

    $env:VIXEN_WINDOW_WIDTH = [string]$Width
    $env:VIXEN_WINDOW_HEIGHT = [string]$Height
    $env:VIXEN_LOOKDEV_CAPTURE_STATE = 'midday'
    $env:VIXEN_LOOKDEV_CAPTURE_FRAMES = '64'
    $env:VIXEN_LOOKDEV_CAPTURE_DIR = $captureDir
    $env:VIXEN_PERF_CSV = $csv
    $env:VIXEN_HDR_EXPOSURE = '1'
    $env:VIXEN_HDR_EXPOSURE_FIXED = '1'
    $env:VIXEN_HUD_SUPPRESS = '1'
    if ($Lod -eq 'fixed') {
        $env:VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE = '0.00165685425'
    } else {
        Remove-Item Env:VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE -ErrorAction SilentlyContinue
    }

    $stdout = Join-Path $runDir 'stdout.log'
    $stderr = Join-Path $runDir 'stderr.log'
    $runStartedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    $runStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $processArgs = @($testFilter, "--render-scale=$Scale", "--upscaler=$Upscaler")
    Record-GpuTelemetry $runLabel 'before'
    Write-Host "START at_utc=$runStartedAtUtc $runLabel scale=$Scale upscaler=$Upscaler"
    $process = Start-Process -FilePath $exe -ArgumentList $processArgs -WorkingDirectory (Split-Path $exe) `
        -NoNewWindow -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        throw "Timed out after $TimeoutSeconds seconds: $runLabel"
    }
    Record-GpuTelemetry $runLabel 'after'
    $process.Refresh()
    $exitCode = $process.ExitCode
    $passMarker = Select-String -Path $stdout -Pattern '^\[  PASSED  \] 1 test\.$' -Quiet
    if (($null -ne $exitCode -and $exitCode -ne 0) -or
        ($null -eq $exitCode -and -not $passMarker)) {
        throw "Capture failed with exit code ${exitCode}: $runLabel; see $stdout and $stderr"
    }
    if (-not (Test-Path $csv)) { throw "Performance CSV was not written: $runLabel" }
    $captureCount = @(Get-ChildItem $captureDir -Filter '*.png' -ErrorAction SilentlyContinue).Count
    if ($captureCount -ne 4) { throw "Expected four stills, got ${captureCount}: $runLabel" }
    $runStopwatch.Stop()
    $runCompletedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    $elapsedMs = [string]::Format(
        [System.Globalization.CultureInfo]::InvariantCulture,
        '{0:F1}', $runStopwatch.Elapsed.TotalMilliseconds)
    @(
        "run=$runLabel"
        "scale=$Scale"
        "upscaler=$Upscaler"
        "started_at_utc=$runStartedAtUtc"
        "completed_at_utc=$runCompletedAtUtc"
        "elapsed_ms=$elapsedMs"
    ) | Set-Content -Path (Join-Path $runDir 'run-timing.txt') -Encoding UTF8
    Write-Host "DONE at_utc=$runCompletedAtUtc elapsed_ms=$elapsedMs $runLabel"
}

if ($StageCost) {
    foreach ($case in $stageCostCases) {
        Invoke-Capture $case.Width $case.Height 1 $case.Name 'stage-cost' $case.Scale 'fsr1' $case.Lod (Join-Path $OutputRoot $case.Name)
    }
    Add-Content -Path $manifestPath -Value "completed=$($stageCostCases.Count)-fsr-stage-cost-profiles" -Encoding UTF8
    Write-Host "COMPLETE stage-cost extent=$Extent output=$OutputRoot"
    return
}
if ($Round -lt 0 -or $Round -gt $Rounds) { throw "Round must be 0 (all) or 1..$Rounds." }
$extents = @(
    [pscustomobject]@{ Width = 500; Height = 500 },
    [pscustomobject]@{ Width = 1920; Height = 1080 }
)
if ($Extent -eq '500') { $extents = @($extents | Where-Object { $_.Width -eq 500 }) }
if ($Extent -eq '1080') { $extents = @($extents | Where-Object { $_.Width -eq 1920 }) }
$roundsToRun = if ($Round -eq 0) { 1..$Rounds } else { @($Round) }

foreach ($selectedExtent in $extents) {
    foreach ($roundNumber in $roundsToRun) {
        foreach ($config in $configs) {
            # Each candidate is bracketed by native, scale-1, empty-slot controls.
            Invoke-Capture $selectedExtent.Width $selectedExtent.Height $roundNumber $config.Name 'A-before' '1.0' 'none'
            Invoke-Capture $selectedExtent.Width $selectedExtent.Height $roundNumber $config.Name 'B-candidate' $config.Scale $config.Upscaler
            Invoke-Capture $selectedExtent.Width $selectedExtent.Height $roundNumber $config.Name 'A-after' '1.0' 'none'
        }
        "completed=$($selectedExtent.Width)x$($selectedExtent.Height)-round-$roundNumber" |
            Add-Content -Path $manifestPath -Encoding UTF8
    }
}

Write-Host "COMPLETE extent=$Extent round=$Round lod=$LodMode output=$OutputRoot"
