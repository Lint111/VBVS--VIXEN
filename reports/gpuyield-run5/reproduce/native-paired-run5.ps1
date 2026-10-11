$ErrorActionPreference='Stop'
$root='C:\vixen-native\gpuyield'
$probe="$root\.tmp\run-native-producer.ps1"
$base="$root\.tmp\unbounded-base\build\gpuyield-native\binaries\vixen_editor.exe"
$final="$root\build\gpuyield-native\binaries\vixen_editor.exe"
$calibration="$root\build\gpuyield-native\runtime-cache\calibration"
$out="$root\reports\gpuyield-run5"
New-Item -ItemType Directory -Force $out | Out-Null
$failures=@()
$env:VIXEN_LOG_LEVEL=$null
foreach($run in @(
 @{label='native-unbounded';binary=$base;width=500;height=500},
 @{label='native-final-cold';binary=$final;width=500;height=500;cold=$true},
 @{label='native-final-warm';binary=$final;width=500;height=500},
 @{label='native-final-warm-no-trace';binary=$final;width=500;height=500;noTrace=$true},
 @{label='native-unbounded-1080';binary=$base;width=1920;height=1080},
 @{label='native-final-1080-cold';binary=$final;width=1920;height=1080;cold=$true},
 @{label='native-final-1080-warm';binary=$final;width=1920;height=1080},
 @{label='native-final-1080-warm-no-trace';binary=$final;width=1920;height=1080;noTrace=$true},
 @{label='native-unbounded-1080-return';binary=$base;width=1920;height=1080},
 @{label='native-hardware-query';binary=$final;width=500;height=500;hardware=$true}
)) {
 $env:VIXEN_RECIPE_BUCKETED_DISPATCH=$null
 $env:VIXEN_BUCKETED_SHADE=$null
 $env:VIXEN_PROCEDURAL_UBER_DEMO=$null
 $extra=@{}
 if($run.noTrace){$extra.DisableTrace=$true}
 if($run.hardware){$extra.HardwareQuery=$true}
 if($run.cold){ Remove-Item -LiteralPath "$calibration\*.json" -ErrorAction SilentlyContinue; Write-Output "Cleared calibration for $($run.label)" }
 Write-Output "Native paired full-producer: $($run.label)"
 try {
  & $probe -Binary $run.binary -Output "$out\$($run.label)" -Width $run.width -Height $run.height @extra
  Get-Content "$out\$($run.label)\status.json"
  if (Test-Path "$calibration\*.json") { Copy-Item "$calibration\*.json" "$out\$($run.label)\" -Force }
 } catch {$failures += "$($run.label): $_"; Write-Output $failures[-1]}
}
$rt="$root\build\gpuyield-native\libraries\SVO\tests\test_rayquery_feasibility.exe"
& $rt --gtest_filter='RayQueryFeasibilityTest.*' 2>&1 | Out-File -Encoding utf8 "$out\native-hardware-ray-tests.log"
if($LASTEXITCODE -ne 0){$failures += "Native hardware fixture exit $LASTEXITCODE"}
Get-Content "$out\native-hardware-ray-tests.log" | Select-Object -Last 3
$failures | ConvertTo-Json | Set-Content "$out\native-matrix-failures.json"
if($failures.Count){throw ($failures -join '; ')}
