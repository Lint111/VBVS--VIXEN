param(
  [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-fA-F]{40}$')][string]$Commit,
  [Parameter(Mandatory=$true)][ValidateSet('state', 'label', 'all-tests', 'missing-dll')][string]$Mode,
  [ValidateSet('lookdev', 'planet', 'glyphsdf')][string]$Capture,
  [string]$State,
  [string]$CtestLabel,
  [string]$Root = 'C:\vixen-native\nativerunner-textshader'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $Root 'VIXEN\scripts\build\SetWindowsChildErrorMode.ps1')
$buildDir = Join-Path $Root "build\native\$Commit"
$readyFile = Join-Path $buildDir '.vixendll-build-ready'
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
} elseif ($Mode -eq 'label' -and -not $CtestLabel) {
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

$temp = Join-Path $Root '.tmp\native-temp'
$logDir = Join-Path $Root '.tmp\native-temp'
New-Item -ItemType Directory -Force $temp | Out-Null
$logKey = if ($Mode -eq 'state') { "$Capture-$State" } elseif ($Mode -eq 'label') { "label-$CtestLabel" } else { $Mode }
$logKey = $logKey -replace '[^A-Za-z0-9_.-]', '-'
$log = Join-Path $logDir "ctest-$Commit-$logKey.log"
if (Test-Path $log) { Remove-Item -Force $log }

if ($Mode -eq 'missing-dll') {
  $captureExe = Get-ChildItem -Path $buildDir -Filter 'vixen_lookdev_capture.exe' -File -Recurse |
    Select-Object -First 1
  if (-not $captureExe) {
    throw "vixen_lookdev_capture.exe was not found under the native build directory: $buildDir"
  }

  $tbbRuntimePath = Join-Path $captureExe.DirectoryName 'tbb12.dll'
  if (-not (Test-Path -LiteralPath $tbbRuntimePath)) {
    throw "The staged TBB runtime DLL was not next to the test executable: $tbbRuntimePath"
  }
  $disabledTbbPath = "$tbbRuntimePath.vixendll-negative-check"
  if (Test-Path -LiteralPath $disabledTbbPath) {
    throw "A prior negative-check backup already exists: $disabledTbbPath"
  }

  $originalPath = $env:PATH
  $env:PATH = (($env:PATH -split ';') | Where-Object {
    $_ -and $_ -ne $captureExe.DirectoryName -and -not (Test-Path -LiteralPath (Join-Path $_ 'tbb12.dll'))
  }) -join ';'
  Move-Item -LiteralPath $tbbRuntimePath -Destination $disabledTbbPath
  $process = $null
  try {
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $captureExe.FullName
    $startInfo.Arguments = '--gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically'
    $startInfo.WorkingDirectory = $captureExe.DirectoryName
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo

    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    if (-not $process.Start()) {
      throw 'Windows did not start the native missing-DLL probe process'
    }
    if (-not $process.WaitForExit(10000)) {
      $process.Kill()
      throw 'The missing-DLL probe did not exit within 10 seconds; a loader dialog may be blocking it'
    }
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $stopwatch.Stop()
    $exitBytes = [BitConverter]::GetBytes([int]$process.ExitCode)
    $exitHex = [BitConverter]::ToUInt32($exitBytes, 0).ToString('X8')
    $result = "[NATIVE_MISSING_DLL_RESULT] exe=$($captureExe.FullName) missing=tbb12.dll exit=0x$exitHex wall_ms=$($stopwatch.ElapsedMilliseconds)"
    if ($stdout) { Add-Content -LiteralPath $log -Value $stdout }
    if ($stderr) { Add-Content -LiteralPath $log -Value $stderr }
    Add-Content -LiteralPath $log -Value $result
    Write-Output $result

    if ($exitHex -ne 'C0000135') {
      throw "Expected Windows missing-DLL exit 0xC0000135, received 0x$exitHex"
    }
  } finally {
    if ($process) { $process.Dispose() }
    if (Test-Path -LiteralPath $disabledTbbPath) {
      Move-Item -LiteralPath $disabledTbbPath -Destination $tbbRuntimePath
    }
    $env:PATH = $originalPath
  }
  exit 0
}

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
} elseif ($Mode -eq 'label') {
  $ctestArgs += @('-L', $CtestLabel)
} elseif ($Mode -ne 'all-tests') {
  throw "Unsupported native capture mode: $Mode"
}
$sw = [Diagnostics.Stopwatch]::StartNew()
& $ctest @ctestArgs 2>&1 | Tee-Object -FilePath $log
$exitCode = $LASTEXITCODE
$sw.Stop()
$result = "[NATIVE_CAPTURE_RESULT] commit=$Commit mode=$Mode capture=$Capture state=$State label=$CtestLabel exit=$exitCode wall_s=$([math]::Round($sw.Elapsed.TotalSeconds,2)) artifacts=$captureRoot"
$result | Tee-Object -FilePath "$log.result.txt"

if ($Mode -eq 'all-tests') {
  $inventoryJson = & $ctest --test-dir $buildDir --show-only=json-v1
  if ($LASTEXITCODE -ne 0) { throw "CTest executable inventory failed with exit $LASTEXITCODE" }
  $inventory = ($inventoryJson -join "`n") | ConvertFrom-Json
  $testsByExecutable = @{}
  foreach ($test in $inventory.tests) {
    if (-not $test.command -or -not ([string]$test.command[0]).EndsWith('.exe', [System.StringComparison]::OrdinalIgnoreCase)) {
      continue
    }
    $executablePath = [System.IO.Path]::GetFullPath([string]$test.command[0])
    $key = $executablePath.ToLowerInvariant()
    if (-not $testsByExecutable.ContainsKey($key)) { $testsByExecutable[$key] = [System.Collections.Generic.List[string]]::new() }
    $testsByExecutable[$key].Add([string]$test.name)
  }

  $failedTestNames = @()
  if (Test-Path -LiteralPath $log) {
    $logText = Get-Content -LiteralPath $log -Raw
    $failedMatches = [regex]::Matches($logText, '(?m)^\s*\d+\s+-\s+(.+?)\s+\(Failed\)\s*$')
    foreach ($match in $failedMatches) { $failedTestNames += $match.Groups[1].Value }
  }
  $manifest = Join-Path $logDir "native-executable-smokes-$Commit.tsv"
  $manifestLines = @("executable`tregistered_ctest_cases`tresult")
  foreach ($executable in (Get-ChildItem -Path $buildDir -Filter '*.exe' -File -Recurse | Sort-Object FullName)) {
    $key = $executable.FullName.ToLowerInvariant()
    $cases = @()
    if ($testsByExecutable.ContainsKey($key)) { $cases = @($testsByExecutable[$key]) }
    if ($exitCode -eq 0) {
      $status = if ($cases.Count) { 'PASS' } else { 'NO_REGISTERED_CTEST_SMOKE' }
    } elseif (@($cases | Where-Object { $_ -in $failedTestNames }).Count -gt 0) {
      $status = 'FAIL'
    } elseif ($cases.Count) {
      $status = 'PASS; suite has failures in other executables'
    } else {
      $status = 'NO_REGISTERED_CTEST_SMOKE; suite failed'
    }
    $manifestLines += "$($executable.FullName)`t$($cases.Count)`t$status"
  }
  Set-Content -LiteralPath $manifest -Value $manifestLines -Encoding utf8
  Write-Output "[NATIVE_EXECUTABLE_SMOKE_MANIFEST] $manifest"
}
exit $exitCode
