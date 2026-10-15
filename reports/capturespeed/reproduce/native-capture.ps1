param(
  [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-fA-F]{40}$')][string]$Commit,
  [Parameter(Mandatory=$true)][ValidateSet('state', 'label')][string]$Mode,
  [ValidateSet('lookdev', 'planet', 'glyphsdf')][string]$Capture,
  [string]$State,
  [string]$CtestLabel,
  [string]$Root = 'C:\vixen-native\nativerunner-textshader'
)
$ErrorActionPreference = 'Stop'
$buildDir = Join-Path $Root "build\native\$Commit"
$readyFile = Join-Path $buildDir '.nativerunner-build-ready'
if (-not (Test-Path $readyFile) -or (Get-Content -Raw $readyFile).Trim() -ne $Commit) {
  throw "native capture build is not ready for VIXEN commit $Commit"
}
$ctest = 'C:\Program Files\CMake\bin\ctest.exe'
if (-not (Test-Path $ctest)) { throw "CTest was not found: $ctest" }

$testRegex = ''
if ($Mode -eq 'state') {
  if (-not $State) { throw 'State is required in state mode' }
  if ($Capture -eq 'lookdev') {
    if ($State -notin @('midday', 'late-afternoon', 'overcast', 'night-service')) {
      throw "unsupported look-dev capture state: $State"
    }
    $testRegex = '^Application\.LookdevCapture\.RendersFourStatesAtFourAnglesDeterministically$'
  } elseif ($Capture -eq 'planet') {
    if ($State -notin @('C1', 'C1-epic', 'C2', 'C2-term', 'C4a', 'C4b', 'C4c', 'C5a-moon', 'C7', 'C7-60', 'C7-60-1p5au', 'C9')) {
      throw "unsupported planet capture state: $State"
    }
    $testRegex = '^Application\.PlanetCapture\.' + [regex]::Escape($State) + '$'
  } elseif ($Capture -eq 'glyphsdf') {
    if ($State -in @('latin-carved', 'latin-embossed', 'qps-rtl-carved', 'qps-rtl-embossed')) {
      $testRegex = '^Application\.LookdevCapture\.GlyphSdf\.' + [regex]::Escape($State) + '$'
    } elseif ($State -eq 'analytic-vs-voxel') {
      $testRegex = '^Application\.LookdevCapture\.GlyphSdf\.AnalyticVsVoxel$'
    } else {
      throw "unsupported glyph SDF capture state: $State"
    }
  } else {
    throw 'Capture must be lookdev, planet, or glyphsdf in state mode'
  }
} elseif (-not $CtestLabel) {
  throw 'CtestLabel is required in label mode'
}

$captureRoot = Join-Path $buildDir 'runtime-captures'
if ($Mode -eq 'state') {
  if ($Capture -eq 'lookdev') {
    foreach ($path in @((Join-Path $captureRoot "lookdev\$State"), (Join-Path $captureRoot "lookdev\.repeat\$State"))) {
      if (Test-Path $path) { Remove-Item -Recurse -Force $path }
    }
  } elseif ($Capture -eq 'glyphsdf') {
    foreach ($path in @((Join-Path $captureRoot "glyphsdf\$State"),
                        (Join-Path $captureRoot "glyphsdf\.repeat\$State"))) {
      if (Test-Path $path) { Remove-Item -Recurse -Force $path }
    }
  } else {
    foreach ($suffix in @('.png', '.hits.bin', '.lighting.bin', '.meter.bin', '.scene.json')) {
      foreach ($base in @((Join-Path $captureRoot 'planet'), (Join-Path $captureRoot 'planet\.repeat'))) {
        $path = Join-Path $base "$State$suffix"
        if (Test-Path $path) { Remove-Item -Force $path }
      }
    }
  }
}

# MSVC's CMake package layout keeps the dynamically linked oneTBB runtime under a
# compiler-keyed directory in the build tree. The capture executables import tbb12.dll,
# so expose that runtime directory when CTest launches them from application/main.
$tbbRuntime = Get-ChildItem -Path $buildDir -Filter 'tbb12.dll' -File -Recurse -ErrorAction SilentlyContinue |
  Select-Object -First 1
if (-not $tbbRuntime) {
  throw "tbb12.dll was not found under the native build directory: $buildDir"
}
$env:PATH = "$($tbbRuntime.DirectoryName);$env:PATH"

$temp = Join-Path $Root '.tmp\native-temp'
$logDir = Join-Path $Root '.tmp\native-temp'
New-Item -ItemType Directory -Force $temp | Out-Null
$logKey = if ($Mode -eq 'state') { "$Capture-$State" } else { "label-$CtestLabel" }
$logKey = $logKey -replace '[^A-Za-z0-9_.-]', '-'
$log = Join-Path $logDir "ctest-$Commit-$logKey.log"
if (Test-Path $log) { Remove-Item -Force $log }

# CTest's test properties provide the capture environment. Strip inherited VIXEN and Dozen
# overrides so the Windows run uses the native Vulkan driver and only the registered settings.
Get-ChildItem Env: | Where-Object { $_.Name -like 'VIXEN_*' -or $_.Name -in @('VK_ICD_FILENAMES', 'VK_LAYER_PATH') } |
  ForEach-Object { Remove-Item "Env:$($_.Name)" }
$env:VIXEN_PHASE_TIMING = '1'
if ($Capture -eq 'lookdev') { $env:VIXEN_LOOKDEV_CAPTURE_STATE = $State }
$env:TEMP = $temp
$env:TMP = $temp
$env:VULKAN_SDK = 'C:\VulkanSDK\1.4.350.0'

$ctestArgs = @('--test-dir', $buildDir, '--output-on-failure', '-V')
if ($Mode -eq 'state') {
  $ctestArgs += @('-R', $testRegex)
} else {
  $ctestArgs += @('-L', $CtestLabel)
}
$sw = [Diagnostics.Stopwatch]::StartNew()
& $ctest @ctestArgs 2>&1 | Tee-Object -FilePath $log
$exitCode = $LASTEXITCODE
$sw.Stop()
$result = "[NATIVE_CAPTURE_RESULT] commit=$Commit mode=$Mode capture=$Capture state=$State label=$CtestLabel exit=$exitCode wall_s=$([math]::Round($sw.Elapsed.TotalSeconds,2)) artifacts=$captureRoot"
$result | Tee-Object -FilePath "$log.result.txt"
exit $exitCode
