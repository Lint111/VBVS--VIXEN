$ErrorActionPreference='Stop'
$root='C:\vixen-native\gpuyield'
$env:VIXEN_RECIPE_BUCKETED_DISPATCH=$null
$env:VIXEN_BUCKETED_SHADE=$null
$env:VIXEN_PROCEDURAL_UBER_DEMO=$null
foreach($run in @(@{label='native-pacing-500';width=500;height=500},@{label='native-pacing-1080';width=1920;height=1080})) {
 & "$root\.tmp\run-native-producer.ps1" -Binary "$root\build\gpuyield-native\binaries\vixen_editor.exe" -Output "$root\reports\gpuyield-run4\$($run.label)" -Width $run.width -Height $run.height
 Get-Content "$root\reports\gpuyield-run4\$($run.label)\status.json"
}
