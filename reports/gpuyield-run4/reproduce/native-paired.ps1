$ErrorActionPreference='Stop'
$root='C:\vixen-native\gpuyield'
$probe="$root\.tmp\run-native-producer.ps1"
$base="$root\.tmp\unbounded-base\build\gpuyield-native\binaries\vixen_editor.exe"
$final="$root\build\gpuyield-native\binaries\vixen_editor.exe"
$failures=@()
$env:VIXEN_LOG_LEVEL=$null
foreach($run in @(
 @{label='native-unbounded';binary=$base;width=500;height=500},
 @{label='native-final';binary=$final;width=500;height=500},
 @{label='native-final-no-trace';binary=$final;width=500;height=500;noTrace=$true},
 @{label='native-unbounded-1080';binary=$base;width=1920;height=1080},
 @{label='native-final-1080';binary=$final;width=1920;height=1080},
 @{label='native-final-1080-no-trace';binary=$final;width=1920;height=1080;noTrace=$true},
 @{label='native-unbounded-1080-return';binary=$base;width=1920;height=1080},
 @{label='native-hardware-query';binary=$final;width=500;height=500;hardware=$true}
)) {
 $env:VIXEN_RECIPE_BUCKETED_DISPATCH=$null
 $env:VIXEN_BUCKETED_SHADE=$null
 $env:VIXEN_PROCEDURAL_UBER_DEMO=$null
 $extra=@{}
 if($run.noTrace){$extra.DisableTrace=$true}
 if($run.hardware){$extra.HardwareQuery=$true}
 Write-Output "Native paired full-producer: $($run.label)"
 try {
  & $probe -Binary $run.binary -Output "$root\reports\gpuyield-run4\$($run.label)" -Width $run.width -Height $run.height @extra
  Get-Content "$root\reports\gpuyield-run4\$($run.label)\status.json"
 } catch {$failures += "$($run.label): $_"; Write-Output $failures[-1]}
}
$rt="$root\build\gpuyield-native\libraries\SVO\tests\test_rayquery_feasibility.exe"
& $rt --gtest_filter='RayQueryFeasibilityTest.*' 2>&1 | Out-File -Encoding utf8 "$root\reports\gpuyield-run4\native-hardware-ray-tests.log"
if($LASTEXITCODE -ne 0){$failures += "Native hardware fixture exit $LASTEXITCODE"}
Get-Content "$root\reports\gpuyield-run4\native-hardware-ray-tests.log"
$env:VIXEN_LOG_LEVEL='DEBUG'
try {
 & $probe -Binary $final -Output "$root\reports\gpuyield-run4\native-extent-debug" -Width 1920 -Height 1080 -Frames 4
} catch {$failures += "Native extent diagnostic: $_"}
$env:VIXEN_LOG_LEVEL=$null
$failures | ConvertTo-Json | Set-Content "$root\reports\gpuyield-run4\native-matrix-failures.json"
if($failures.Count){throw ($failures -join '; ')}
