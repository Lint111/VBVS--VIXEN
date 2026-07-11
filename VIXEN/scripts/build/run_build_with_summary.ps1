# Purpose: run `cmake --build --preset <preset> -- -k 0` (keep going past compile failures
# instead of stopping at the first one) with a PERIODIC status file an agent/human can poll
# mid-build, plus the same end-of-build FAILED-target summary as the old
# run_build_with_summary.bat. Rewritten in PowerShell (see Worktree-Build-Artifact-
# Accumulation-Audit-2026-07.md Fix 8) because streaming+parsing a live log while also
# updating a status file on a timer is exactly the kind of stateful logic that kept breaking
# in cmd.exe batch (Fix 7's CRLF/for-loop fragility) — PowerShell's job/event primitives are
# a much better fit than another batch script.
#
# Also acquires the machine-wide build lock (Fix 9) around the actual build — held IN THIS
# SAME PROCESS via a named Mutex, not by shelling out to a separate acquire_build_lock.ps1 and
# passing the build command through as a string. An earlier version tried that split and hung
# indefinitely: three layers of nested process/argument quoting (build.bat -> lock script's
# -InnerArgs string -> Start-Process -ArgumentList) silently failed to launch the real inner
# build. Doing the acquire/release in-process avoids that whole class of bug. Concurrent
# builds on one machine contend for the same CPU/IO and make ALL of them slower, not faster —
# observed directly this session running concurrent verification builds.
#
# Usage: powershell -ExecutionPolicy Bypass -File run_build_with_summary.ps1 -CMakeExe <path> -Preset <name> [-StatusFile <path>] [-LockTimeoutSeconds N] [-SkipLock]
#
# Status file format (plain text, overwritten in place — safe to `cat`/`Get-Content` anytime,
# never partially-written since each update is a single atomic file write):
#   VIXEN_BUILD_STATUS: <WAITING_FOR_LOCK|RUNNING|DONE>
#   elapsed_seconds: <N>
#   targets_total: <N or ?>
#   targets_done: <N>
#   targets_failed: <N>
#   last_target: <most recent ninja [x/y] line's target name>
#   failed_targets: <comma-separated list, empty if none yet>

param(
    [Parameter(Mandatory=$true)][string]$CMakeExe,
    [Parameter(Mandatory=$true)][string]$Preset,
    [string]$StatusFile = "$env:TEMP\vixen_build_status.txt",
    [int]$LockTimeoutSeconds = 1800,
    [switch]$SkipLock,
    [int]$MaxParallelJobs = 0
)

# Fix 10: cap ninja's overall concurrent job count below full core count by default, so a
# build leaves the machine usable instead of pegging every logical core (observed: multiple
# cl.exe processes at 400-500MB RSS each under the prior unthrottled -j16 default). 0 (the
# param default) means "not explicitly set" -> fall back to VIXEN_MAX_BUILD_JOBS env var, then
# a conservative 75% of logical cores. Link-job concurrency is capped separately and lower, in
# CMakeLists.txt's JOB_POOLS (link is the RSS-heavy step, not compile) - this cap is the
# overall ninja -j ceiling on top of that.
if ($MaxParallelJobs -le 0) {
    if ($env:VIXEN_MAX_BUILD_JOBS) {
        $MaxParallelJobs = [int]$env:VIXEN_MAX_BUILD_JOBS
    } else {
        $logicalCores = [Environment]::ProcessorCount
        $MaxParallelJobs = [Math]::Max(1, [Math]::Floor($logicalCores * 0.75))
    }
}

$ErrorActionPreference = 'Continue'
$buildLog = "$env:TEMP\vixen_build_$([System.Guid]::NewGuid().ToString('N').Substring(0,8)).log"
$startTime = Get-Date

function Write-StatusFile($state, $elapsed, $total, $done, $failed, $lastTarget, $failedTargets) {
    $lines = @(
        "VIXEN_BUILD_STATUS: $state",
        "elapsed_seconds: $elapsed",
        "targets_total: $total",
        "targets_done: $done",
        "targets_failed: $failed",
        "last_target: $lastTarget",
        "failed_targets: $($failedTargets -join ', ')"
    )
    # Write to a temp path then move — avoids a reader ever seeing a half-written file.
    $tmp = "$StatusFile.tmp"
    Set-Content -Path $tmp -Value $lines -Encoding utf8
    Move-Item -Path $tmp -Destination $StatusFile -Force
}

# One mutex name for the whole machine — intentionally NOT scoped per-worktree or per-repo,
# because the contention this prevents is CPU/IO on the shared machine, not a per-repo git
# concern. A .NET Mutex (not a lockfile) is released automatically by the OS if this process
# dies or is killed, so a killed build can never wedge the lock for everyone else.
$mutex = $null
if (-not $SkipLock) {
    Write-StatusFile "WAITING_FOR_LOCK" 0 "?" 0 0 "" @()
    $mutex = New-Object System.Threading.Mutex($false, "Global\VixenBuildLock")
    Write-Host "[build] Waiting for the machine-wide build lock (another build may be running)..."
    $lockWaitStart = Get-Date
    $acquired = $mutex.WaitOne([TimeSpan]::FromSeconds($LockTimeoutSeconds))
    if (-not $acquired) {
        $waited = [int]((Get-Date) - $lockWaitStart).TotalSeconds
        Write-Host "[build] TIMEOUT after $($waited)s waiting for the build lock - another build is still running."
        Write-Host "[build] Not starting this build. Retry later, or increase -LockTimeoutSeconds."
        $mutex.Dispose()
        exit 2
    }
    $waited = [int]((Get-Date) - $lockWaitStart).TotalSeconds
    if ($waited -gt 1) {
        Write-Host "[build] Build lock acquired after waiting $($waited)s."
    } else {
        Write-Host "[build] Build lock acquired (was free)."
    }
}

try {

Write-Host "[build] Max parallel jobs: $MaxParallelJobs (of $([Environment]::ProcessorCount) logical cores; override with -MaxParallelJobs or VIXEN_MAX_BUILD_JOBS)"
Write-StatusFile "RUNNING" 0 "?" 0 0 "" @()

# Launch the build, streaming stdout+stderr to both the console (so a foreground caller still
# sees live output) and the log file, via a background job so this script can poll the log on
# a timer without blocking on the build's own completion. Start-Job spawns a fresh PowerShell
# process that does NOT inherit the caller's current directory (it uses its own profile
# default, e.g. OneDrive\Documents) — pass $PWD explicitly and Set-Location inside the job, or
# `cmake --build --preset` silently resolves CMakePresets.json from the wrong place and fails.
# Tee-Object writes UTF-16 by default in Windows PowerShell 5.1, which breaks line-based regex
# parsing against the log — force UTF-8 by piping through Out-File -Encoding utf8 instead.
$workDir = $PWD.Path
$job = Start-Job -ScriptBlock {
    param($cmakeExe, $preset, $logPath, $workDir, $maxJobs)
    Set-Location $workDir
    & $cmakeExe --build --preset $preset -- -k 0 -j $maxJobs *>&1 | Out-File -FilePath $logPath -Encoding utf8
} -ArgumentList $CMakeExe, $Preset, $buildLog, $workDir, $MaxParallelJobs

$progressPattern = '^\[(\d+)/(\d+)\]\s+(.+)$'
$failedPattern = '^FAILED:\s+\[code=\d+\]\s+(\S+)'

$lastDone = 0
$lastTotal = "?"
$lastTarget = ""
$failedTargets = @()

while ($job.State -eq 'Running') {
    Start-Sleep -Seconds 5
    if (Test-Path $buildLog) {
        $content = Get-Content -Path $buildLog -ErrorAction SilentlyContinue
        if ($content) {
            foreach ($line in $content) {
                if ($line -match $progressPattern) {
                    $lastDone = [int]$Matches[1]
                    $lastTotal = $Matches[2]
                    $lastTarget = $Matches[3]
                } elseif ($line -match $failedPattern) {
                    $t = $Matches[1]
                    if ($failedTargets -notcontains $t) { $failedTargets += $t }
                }
            }
        }
    }
    $elapsed = [int]((Get-Date) - $startTime).TotalSeconds
    Write-StatusFile "RUNNING" $elapsed $lastTotal $lastDone $failedTargets.Count $lastTarget $failedTargets
}

# Drain any output the job produced after our last poll, then get its exit code.
Receive-Job -Job $job | Out-Null
$buildExitCode = if ($job.ChildJobs[0].JobStateInfo.State -eq 'Failed') { 1 } else { 0 }
Remove-Job -Job $job -Force

# Final pass over the complete log for an authoritative count (the polling loop can miss the
# last few lines written right as the job exits).
$content = Get-Content -Path $buildLog -ErrorAction SilentlyContinue
$failedTargets = @()
foreach ($line in $content) {
    if ($line -match $progressPattern) {
        $lastDone = [int]$Matches[1]
        $lastTotal = $Matches[2]
        $lastTarget = $Matches[3]
    } elseif ($line -match $failedPattern) {
        $t = $Matches[1]
        if ($failedTargets -notcontains $t) { $failedTargets += $t }
    }
}
if ($failedTargets.Count -gt 0) { $buildExitCode = 1 }

$elapsed = [int]((Get-Date) - $startTime).TotalSeconds
Write-StatusFile "DONE" $elapsed $lastTotal $lastDone $failedTargets.Count $lastTarget $failedTargets

Write-Host ""
Write-Host "[build] ============================== BUILD SUMMARY =============================="
if ($failedTargets.Count -eq 0) {
    Write-Host "[build] All targets built successfully."
} else {
    Write-Host "[build] $($failedTargets.Count) target(s) FAILED:"
    foreach ($t in $failedTargets) { Write-Host "[build]   $t" }
    Write-Host "[build] Everything else in this build succeeded (kept going past the"
    Write-Host "[build] failures above via ninja -k 0) - see `"$buildLog`" for full output,"
    Write-Host "[build] including each failure's compiler error just under its FAILED: line."
}
Write-Host "[build] =============================================================================="
Write-Host "[build] Status file (pollable mid-build next time): $StatusFile"

exit $buildExitCode

} finally {
    # Released even if the build throws, is killed, or this script is Ctrl+C'd — the Mutex
    # also auto-releases if THIS process itself dies, so a killed build never wedges the lock.
    if ($mutex) {
        $mutex.ReleaseMutex()
        $mutex.Dispose()
        Write-Host "[build] Build lock released."
    }
}
