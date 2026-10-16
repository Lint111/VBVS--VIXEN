param(
    [string]$RepoRoot = 'C:\vixen-native\upscalestage1',
    [string]$OutputRoot = 'C:\vixen-native\upscalestage1\reports\upscalestage1\stage-cost-final-20261017',
    [int]$FramesPerYaw = 64,
    [int]$TimeoutSeconds = 1200
)

$ErrorActionPreference = 'Stop'
$exe = Join-Path $RepoRoot 'build\stage1-merged-native\application\main\vixen_lookdev_capture.exe'
if (-not (Test-Path $exe)) { throw "Capture executable not found: $exe" }

$vulkanSdk = 'C:\VulkanSDK\1.4.350.0'
if (Test-Path $vulkanSdk) {
    $env:VULKAN_SDK = $vulkanSdk
    $env:PATH = (Join-Path $vulkanSdk 'Bin') + ';' + $env:PATH
}
$runtimeRoot = Join-Path $RepoRoot 'build\stage1-merged-native'
$runtimeDirs = @(Get-ChildItem $runtimeRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName 'tbb12.dll') } |
    Select-Object -ExpandProperty FullName)
if ($runtimeDirs.Count) {
    $env:PATH = ($runtimeDirs -join ';') + ';' + $env:PATH
}

$cases = @(
    [pscustomobject]@{ Name = 'fixed-500-scale1'; Width = 500; Height = 500; Scale = '1.0'; LodMode = 'fixed' },
    [pscustomobject]@{ Name = 'fixed-500-scale-2-3'; Width = 500; Height = 500; Scale = '0.6666667'; LodMode = 'fixed' },
    [pscustomobject]@{ Name = 'fixed-500-scale-1-2'; Width = 500; Height = 500; Scale = '0.5'; LodMode = 'fixed' },
    [pscustomobject]@{ Name = 'coupled-500-scale-2-3'; Width = 500; Height = 500; Scale = '0.6666667'; LodMode = 'coupled' },
    [pscustomobject]@{ Name = 'coupled-500-scale-1-2'; Width = 500; Height = 500; Scale = '0.5'; LodMode = 'coupled' },
    [pscustomobject]@{ Name = 'fixed-1080-scale1'; Width = 1920; Height = 1080; Scale = '1.0'; LodMode = 'fixed' },
    [pscustomobject]@{ Name = 'fixed-1080-scale-2-3'; Width = 1920; Height = 1080; Scale = '0.6666667'; LodMode = 'fixed' },
    [pscustomobject]@{ Name = 'fixed-1080-scale-1-2'; Width = 1920; Height = 1080; Scale = '0.5'; LodMode = 'fixed' }
)

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
@(
    "repo=$RepoRoot"
    "executable=$exe"
    "frames_per_yaw=$FramesPerYaw"
    'sequence=one FSR 1 capture per profile; four midday yaws; this sweep reports stage costs, not A/B/A gate results'
    'camera=orbit center (5,5,5); distance 14; FOV 45 degrees; pitch 0.45 radians; yaws 0/90/180/270 degrees'
    'fixed_exposure=VIXEN_HDR_EXPOSURE=1; VIXEN_HDR_EXPOSURE_FIXED=1'
    'recipe=static authored LookdevSceneDefinition; no per-run RNG; sky_seed=0x5a17f13d'
    'steady_cost_samples=frames 33-64 per yaw (128 samples)'
) | Set-Content -Path (Join-Path $OutputRoot 'run-manifest.txt') -Encoding UTF8

$testFilter = '--gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically'
foreach ($case in $cases) {
    $runDir = Join-Path $OutputRoot $case.Name
    $captureDir = Join-Path $runDir 'captures'
    New-Item -ItemType Directory -Path $captureDir -Force | Out-Null
    $csv = Join-Path $runDir 'perf.csv'
    $stdout = Join-Path $runDir 'stdout.log'
    $stderr = Join-Path $runDir 'stderr.log'

    $env:VIXEN_WINDOW_WIDTH = [string]$case.Width
    $env:VIXEN_WINDOW_HEIGHT = [string]$case.Height
    $env:VIXEN_LOOKDEV_CAPTURE_STATE = 'midday'
    $env:VIXEN_LOOKDEV_CAPTURE_FRAMES = [string]$FramesPerYaw
    $env:VIXEN_LOOKDEV_CAPTURE_DIR = $captureDir
    $env:VIXEN_PERF_CSV = $csv
    $env:VIXEN_HDR_EXPOSURE = '1'
    $env:VIXEN_HDR_EXPOSURE_FIXED = '1'
    $env:VIXEN_HUD_SUPPRESS = '1'
    Remove-Item Env:VIXEN_LOOKDEV_CAPTURE_PAN, Env:VIXEN_LOOKDEV_CAPTURE_ANGLE -ErrorAction SilentlyContinue
    if ($case.LodMode -eq 'fixed') {
        $env:VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE = '0.00165685425'
    } else {
        Remove-Item Env:VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE -ErrorAction SilentlyContinue
    }

    $startedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    Write-Host "START at_utc=$startedAtUtc $($case.Name) upscaler=fsr1"
    $process = Start-Process -FilePath $exe `
        -ArgumentList @($testFilter, "--render-scale=$($case.Scale)", '--upscaler=fsr1') `
        -WorkingDirectory (Split-Path $exe) -NoNewWindow -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        throw "Timed out after $TimeoutSeconds seconds: $($case.Name)"
    }
    $process.Refresh()
    $exitCode = $process.ExitCode
    $passMarker = Select-String -Path $stdout -Pattern '^\[  PASSED  \] 1 test\.$' -Quiet
    if (($null -ne $exitCode -and $exitCode -ne 0) -or
        ($null -eq $exitCode -and -not $passMarker)) {
        throw "Capture failed with exit code ${exitCode}: $($case.Name); see $stdout and $stderr"
    }
    if (-not (Test-Path $csv)) { throw "Performance CSV was not written: $($case.Name)" }
    $captureCount = @(Get-ChildItem $captureDir -Filter '*.png' -ErrorAction SilentlyContinue).Count
    if ($captureCount -ne 4) { throw "Expected four stills, got ${captureCount}: $($case.Name)" }

    $stopwatch.Stop()
    $completedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    $elapsedMs = [string]::Format(
        [System.Globalization.CultureInfo]::InvariantCulture, '{0:F1}', $stopwatch.Elapsed.TotalMilliseconds)
    @(
        "profile=$($case.Name)"
        "width=$($case.Width)"
        "height=$($case.Height)"
        "scale=$($case.Scale)"
        "lod_mode=$($case.LodMode)"
        "started_at_utc=$startedAtUtc"
        "completed_at_utc=$completedAtUtc"
        "elapsed_ms=$elapsedMs"
        "capture_count=$captureCount"
    ) | Set-Content -Path (Join-Path $runDir 'run-timing.txt') -Encoding UTF8
    Write-Host "DONE at_utc=$completedAtUtc elapsed_ms=$elapsedMs $($case.Name)"
}

Add-Content -Path (Join-Path $OutputRoot 'run-manifest.txt') -Value 'completed=all-eight-fsr-stage-cost-profiles'
