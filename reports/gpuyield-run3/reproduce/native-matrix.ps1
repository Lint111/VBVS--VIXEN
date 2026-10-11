$ErrorActionPreference='Stop'
$failures=@()
$root='C:\vixen-native\gpuyield'
$probe="$root\.tmp\run-native-producer.ps1"
$base="$root\.tmp\unbounded-base\build\gpuyield-native\binaries\vixen_editor.exe"
$before="$root\.tmp\bounded-before-runtime\vixen_editor.exe"
$final="$root\build\gpuyield-native\binaries\vixen_editor.exe"
foreach ($run in @(
 @{label='native-unbounded';binary=$base;width=500;height=500},
 @{label='native-before';binary=$before;width=500;height=500},
 @{label='native-final';binary=$final;width=500;height=500},
 @{label='native-unbounded-1080';binary=$base;width=1920;height=1080},
 @{label='native-final-1080';binary=$final;width=1920;height=1080},
 @{label='native-bucketed-unbounded';binary=$base;width=500;height=500;bucketed=$true},
 @{label='native-bucketed';binary=$final;width=500;height=500;bucketed=$true},
 @{label='native-hardware-query';binary=$final;width=500;height=500;hardware=$true}
)) {
 Write-Output "Native full producer: $($run.label)"
 $extra=@{}
 $env:VIXEN_RECIPE_BUCKETED_DISPATCH=if($run.bucketed){'1'}else{$null}
 $env:VIXEN_BUCKETED_SHADE=if($run.bucketed){'1'}else{$null}
 $env:VIXEN_PROCEDURAL_UBER_DEMO=if($run.bucketed){'1'}else{$null}
 if ($run.hardware) {$extra.HardwareQuery=$true}
 try {
  & $probe -Binary $run.binary -Output "$root\reports\gpuyield-run3\$($run.label)" -Width $run.width -Height $run.height @extra
  Get-Content "$root\reports\gpuyield-run3\$($run.label)\status.json"
 } catch { $failures += "$($run.label): $_"; Write-Output $failures[-1] }
}
$rt="$root\build\gpuyield-native\libraries\SVO\tests\test_rayquery_feasibility.exe"
& $rt --gtest_filter='RayQueryFeasibilityTest.*' 2>&1 | Out-File -Encoding utf8 "$root\reports\gpuyield-run3\native-hardware-ray-tests.log"
$rc=$LASTEXITCODE
Get-Content "$root\reports\gpuyield-run3\native-hardware-ray-tests.log"
if ($rc -ne 0) {$failures += "Native hardware ray witness exit $rc"}
$failures | ConvertTo-Json | Set-Content "$root\reports\gpuyield-run3\native-matrix-failures.json"
if ($failures.Count -gt 0) {throw ($failures -join "; ")}
