# Reads capture-routes.tsv, the declaration shared with native-capture.sh (see native-common.sh).
# Get-NativeCaptureRoute returns Regex, StateEnv and Cleanup (paths relative to runtime-captures).
function Get-NativeCaptureRoutes {
  param([string]$RoutesFile = (Join-Path $PSScriptRoot 'capture-routes.tsv'))
  foreach ($line in [IO.File]::ReadAllLines($RoutesFile)) {
    if (-not $line -or $line.StartsWith('#')) { continue }
    $f = $line.Split("`t")
    [pscustomobject]@{ Capture = $f[0]; State = $f[1]; Regex = $f[2]; StateEnv = $(if ($f[3] -eq '-') { '' } else { $f[3] }); CleanupTemplate = $f[4] }
  }
}
function Get-NativeCaptureRoute {
  param([Parameter(Mandatory=$true)][string]$Capture, [Parameter(Mandatory=$true)][string]$State,
        [string]$RoutesFile = (Join-Path $PSScriptRoot 'capture-routes.tsv'))
  $routes = @(Get-NativeCaptureRoutes -RoutesFile $RoutesFile)
  $route = $routes | Where-Object { $_.Capture -ceq $Capture -and $_.State -ceq $State } | Select-Object -First 1
  if (-not $route) {
    $declared = ($routes | Where-Object { $_.Capture -ceq $Capture } | ForEach-Object { $_.State }) -join ' '
    throw "no route for capture '$Capture' state '$State' in ${RoutesFile}; declared: $declared"
  }
  [pscustomobject]@{
    Regex = $route.Regex
    StateEnv = $route.StateEnv
    Cleanup = @($route.CleanupTemplate.Replace('{state}', $State).Split(';'))
  }
}
