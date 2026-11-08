# Derives the per-executable native smoke manifest from CTest's own result records.
# CTest writes one JUnit file per run (--output-junit); a run that was stopped and resumed
# (-I start,end) leaves several files. Every file for the commit is merged, the newest record of
# a test wins, and a later "notrun" never overwrites an earlier real result.
param(
  [Parameter(Mandatory=$true)][string]$InventoryJsonPath,   # ctest --show-only=json-v1 output
  [Parameter(Mandatory=$true)][string[]]$JunitPath,         # one or more ctest --output-junit files
  [Parameter(Mandatory=$true)][string]$BuildDir,
  [Parameter(Mandatory=$true)][string]$ManifestPath
)
$ErrorActionPreference = 'Stop'

function Read-CtestJunitResults([string[]]$Paths) {
  $results = @{}
  foreach ($path in ($Paths | Sort-Object { (Get-Item -LiteralPath $_).LastWriteTimeUtc })) {
    try { [xml]$doc = Get-Content -LiteralPath $path -Raw }
    catch { throw "CTest JUnit file is not valid XML: $path ($($_.Exception.Message)). Rerun the slice that wrote it." }
    foreach ($case in $doc.SelectNodes('//testcase')) {
      $name = [string]$case.GetAttribute('name')
      if ($case.SelectSingleNode('failure') -or $case.SelectSingleNode('error')) { $state = 'FAILED' }
      elseif ($case.SelectSingleNode('skipped') -or $case.GetAttribute('status') -eq 'notrun') { $state = 'NOTRUN' }
      else { $state = 'PASSED' }
      if ($state -eq 'NOTRUN' -and $results.ContainsKey($name)) { continue }
      $results[$name] = $state
    }
  }
  return $results
}

if (-not (Test-Path -LiteralPath $InventoryJsonPath)) { throw "CTest inventory file not found: $InventoryJsonPath" }
if (-not $JunitPath -or @($JunitPath | Where-Object { -not (Test-Path -LiteralPath $_) }).Count -gt 0) {
  throw "CTest JUnit result file missing in: $($JunitPath -join ', '). Run ctest with --output-junit before deriving the manifest."
}
$inventory = (Get-Content -LiteralPath $InventoryJsonPath -Raw) | ConvertFrom-Json
$results = Read-CtestJunitResults $JunitPath

$testsByExecutable = @{}
foreach ($test in $inventory.tests) {
  if (-not $test.command -or -not ([string]$test.command[0]).EndsWith('.exe', [System.StringComparison]::OrdinalIgnoreCase)) { continue }
  $key = [System.IO.Path]::GetFullPath([string]$test.command[0]).ToLowerInvariant()
  if (-not $testsByExecutable.ContainsKey($key)) { $testsByExecutable[$key] = [System.Collections.Generic.List[string]]::new() }
  $testsByExecutable[$key].Add([string]$test.name)
}

$lines = @("executable`tregistered_ctest_cases`tpassed`tfailed`tnot_run`tresult")
foreach ($executable in (Get-ChildItem -Path $BuildDir -Filter '*.exe' -File -Recurse | Sort-Object FullName)) {
  $cases = @()
  $key = $executable.FullName.ToLowerInvariant()
  if ($testsByExecutable.ContainsKey($key)) { $cases = @($testsByExecutable[$key]) }
  $passed = 0; $failed = 0; $notRun = 0
  foreach ($case in $cases) {
    $state = if ($results.ContainsKey($case)) { $results[$case] } else { 'NOTRUN' }
    switch ($state) { 'PASSED' { $passed++ } 'FAILED' { $failed++ } default { $notRun++ } }
  }
  $status = if (-not $cases.Count) { 'NO_REGISTERED_CTEST_SMOKE' }
            elseif ($failed) { 'FAIL' }
            elseif ($notRun) { 'INCOMPLETE' }
            else { 'PASS' }
  $lines += "$($executable.FullName)`t$($cases.Count)`t$passed`t$failed`t$notRun`t$status"
}
Set-Content -LiteralPath $ManifestPath -Value $lines -Encoding utf8
