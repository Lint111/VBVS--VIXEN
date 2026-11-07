# Environment helpers for native-capture.ps1. Dot-source this file; it defines functions only.

# Parse the ';'-separated VIXEN_NAME=value list that native-capture.sh passes as -EnvOverrides.
function ConvertTo-NativeCaptureOverrides {
  param([string]$Spec)
  $overrides = [ordered]@{}
  if (-not $Spec) { return $overrides }
  foreach ($entry in ($Spec -split ';' | Where-Object { $_ })) {
    if ($entry -notmatch '^(VIXEN_[A-Za-z0-9_]+)=(.*)$') {
      throw "invalid environment override '$entry': expected VIXEN_NAME=value"
    }
    if ($overrides.Contains($Matches[1])) { throw "environment override repeated: $($Matches[1])" }
    $overrides[$Matches[1]] = $Matches[2]
  }
  return $overrides
}

# Rebuild the environment CTest gives one test: inherited process env, then the ENVIRONMENT property,
# then ENVIRONMENT_MODIFICATION, then the per-run overrides. Returns name -> value, or $null to unset.
function Resolve-CTestEnvironment {
  param(
    [Parameter(Mandatory=$true)][hashtable]$Base,
    [string[]]$Environment = @(),
    [string[]]$Modification = @(),
    [System.Collections.IDictionary]$Overrides = @{}
  )
  $result = @{}
  foreach ($key in $Base.Keys) { $result[$key] = $Base[$key] }
  foreach ($entry in $Environment) {
    $split = $entry.IndexOf('=')
    if ($split -lt 1) { throw "malformed ENVIRONMENT entry: $entry" }
    $result[$entry.Substring(0, $split)] = $entry.Substring($split + 1)
  }
  foreach ($entry in $Modification) {
    if ($entry -notmatch '^([^=]+)=([a-z_]+):(.*)$') { throw "malformed ENVIRONMENT_MODIFICATION entry: $entry" }
    $name = $Matches[1]; $op = $Matches[2]; $value = $Matches[3]
    $current = if ($result.ContainsKey($name)) { $result[$name] } else { $null }
    $joined = { param($sep, $first, $second) if ($first) { "$first$sep$second" } else { $second } }
    switch ($op) {
      'set'                 { $result[$name] = $value }
      'unset'               { $result[$name] = $null }
      'reset'               { $result[$name] = if ($Base.ContainsKey($name)) { $Base[$name] } else { $null } }
      'string_append'       { $result[$name] = "$current$value" }
      'string_prepend'      { $result[$name] = "$value$current" }
      'path_list_append'    { $result[$name] = & $joined ([IO.Path]::PathSeparator) $current $value }
      'path_list_prepend'   { $result[$name] = & $joined ([IO.Path]::PathSeparator) $value $current }
      'cmake_list_append'   { $result[$name] = & $joined ';' $current $value }
      'cmake_list_prepend'  { $result[$name] = & $joined ';' $value $current }
      default               { throw "unsupported ENVIRONMENT_MODIFICATION operation '$op' for $name" }
    }
  }
  foreach ($name in $Overrides.Keys) { $result[$name] = $Overrides[$name] }
  return $result
}

function Get-NativeCaptureProcessEnvironment {
  $base = @{}
  foreach ($item in Get-ChildItem Env:) { $base[$item.Name] = $item.Value }
  return $base
}

function Get-CTestProperty {
  param($Test, [string]$Name)
  $property = @($Test.properties | Where-Object { $_.name -eq $Name }) | Select-Object -First 1
  if (-not $property) { return @() }
  return @($property.value)
}

# Run the selected tests' commands directly (CTest cannot override its own ENVIRONMENT properties),
# with the resolved environment applied to this process around each run. Returns the worst exit code.
function Invoke-CTestDirect {
  param(
    [Parameter(Mandatory=$true)][string]$Ctest,
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string[]]$Selector,
    [Parameter(Mandatory=$true)][System.Collections.IDictionary]$Overrides,
    [Parameter(Mandatory=$true)][string]$Log
  )
  $inventory = (& $Ctest --test-dir $BuildDir --show-only=json-v1 @Selector) -join "`n" | ConvertFrom-Json
  if ($LASTEXITCODE -ne 0) { throw "CTest inventory failed with exit $LASTEXITCODE" }
  $tests = @($inventory.tests)
  if (-not $tests.Count) { throw "no registered CTest case matches: $($Selector -join ' ')" }
  $base = Get-NativeCaptureProcessEnvironment
  $worst = 0
  foreach ($test in $tests) {
    $resolved = Resolve-CTestEnvironment -Base $base -Environment (Get-CTestProperty $test 'ENVIRONMENT') `
      -Modification (Get-CTestProperty $test 'ENVIRONMENT_MODIFICATION') -Overrides $Overrides
    $workDir = @(Get-CTestProperty $test 'WORKING_DIRECTORY') | Select-Object -First 1
    if (-not $workDir) { $workDir = $BuildDir }
    $command = @($test.command)
    $header = "[NATIVE_DIRECT_TEST] name=$($test.name) overrides=$(($Overrides.Keys | ForEach-Object { "$_=$($Overrides[$_])" }) -join ';')"
    $header | Tee-Object -FilePath $Log -Append | Out-Host
    foreach ($name in $resolved.Keys) {
      if ($resolved[$name] -ne $base[$name]) { [Environment]::SetEnvironmentVariable($name, $resolved[$name], 'Process') }
    }
    Push-Location $workDir
    try {
      & $command[0] @($command | Select-Object -Skip 1) 2>&1 | Tee-Object -FilePath $Log -Append | Out-Host
      $code = $LASTEXITCODE
    } finally {
      Pop-Location
      foreach ($name in $resolved.Keys) {
        if ($resolved[$name] -ne $base[$name]) { [Environment]::SetEnvironmentVariable($name, $base[$name], 'Process') }
      }
    }
    "[NATIVE_DIRECT_TEST_RESULT] name=$($test.name) exit=$code" | Tee-Object -FilePath $Log -Append | Out-Host
    if ($code -ne 0) { $worst = $code }
  }
  return [int]$worst
}
