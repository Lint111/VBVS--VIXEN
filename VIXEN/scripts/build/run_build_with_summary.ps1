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
# Usage: powershell -ExecutionPolicy Bypass -File run_build_with_summary.ps1 -CMakeExe <path> -Preset <name> [-StatusFile <path>] [-LockTimeoutSeconds N] [-SkipLock] [-MaxParallelJobs N] [-Target <cmake-target-name>] [-BuildId <id>]
#
# -Target scopes the build to a single CMake target (`cmake --build ... --target <name>`)
# instead of the full default graph - e.g. just `VixenApp` or a single test binary, so an
# agent iterating on one library doesn't pay to rebuild+relink everything else in the graph.
#
# -BuildId: every build gets an ID, either caller-supplied (pass something greppable, e.g.
# your worktree name — "graph-node-linkage-inc1") or auto-generated (an 8-char hex suffix,
# same as the log filename's suffix, so they always match even when auto-generated). This
# solves a real multi-worktree confusion: dispatching builds from different worktrees in
# succession/parallel makes it hard to tell which log/status/binary belongs to which request.
# The ID is printed as the FIRST line of output (before anything else, so it's visible even if
# you only capture the tail of a long build), embedded in the log filename
# (vixen_build_<BuildId>.log — no separate random suffix), written into the status file's
# build_id field, and repeated in the BUILD SUMMARY footer. Always note the BuildId when you
# dispatch a build so you can find its log later without guessing which vixen_build_*.log is
# yours among several concurrent ones.
#
# Status file format (plain text, overwritten in place — safe to `cat`/`Get-Content` anytime,
# never partially-written since each update is a single atomic file write):
#   VIXEN_BUILD_STATUS: <WAITING_FOR_LOCK|RUNNING|DONE>
#   build_id: <this build's BuildId>
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
    [int]$MaxParallelJobs = 0,
    [string]$Target = "",
    [string]$QueueTicketId = "",
    [string]$BuildId = ""
)

# Auto-generate if the caller didn't supply one. Sanitize a caller-supplied ID to safe
# filename/log characters so it can't break the log path or the status-file format.
if (-not $BuildId) {
    $BuildId = [System.Guid]::NewGuid().ToString('N').Substring(0, 8)
} else {
    $BuildId = ($BuildId -replace '[^A-Za-z0-9_.-]', '-')
}
Write-Host "[build] BuildId   : $BuildId  (note this - it's how you find this build's log/status among concurrent builds)"

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
$buildLog = "$env:TEMP\vixen_build_$BuildId.log"
$startTime = Get-Date

function Write-StatusFile($state, $elapsed, $total, $done, $failed, $lastTarget, $failedTargets) {
    $lines = @(
        "VIXEN_BUILD_STATUS: $state",
        "build_id: $BuildId",
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
if ($Target) {
    Write-Host "[build] Target    : $Target (scoped build, not the full 'all' graph)"
} else {
    Write-Host "[build] Target    : (default - full build graph)"
}

$workDir = $PWD.Path
$job = Start-Job -ScriptBlock {
    param($cmakeExe, $preset, $logPath, $workDir, $maxJobs, $target)
    Set-Location $workDir
    if ($target) {
        & $cmakeExe --build --preset $preset --target $target -- -k 0 -j $maxJobs *>&1 | Out-File -FilePath $logPath -Encoding utf8
    } else {
        & $cmakeExe --build --preset $preset -- -k 0 -j $maxJobs *>&1 | Out-File -FilePath $logPath -Encoding utf8
    }
    # Capture the REAL exit code of the cmake/ninja invocation and return it as the job's
    # result. $job.ChildJobs[0].JobStateInfo.State only reports 'Failed' for a terminating
    # PowerShell/script error inside the job -- an external process (cmake/ninja) exiting
    # non-zero for its OWN reasons (e.g. "ninja: error: unknown target") still leaves the job
    # State as 'Completed', so relying on State alone silently treats that as success. This bit
    # a real case: an invalid -Target name made ninja exit 1 immediately with zero targets
    # attempted, but the old logic reported "All targets built successfully" (exit 0).
    $LASTEXITCODE
} -ArgumentList $CMakeExe, $Preset, $buildLog, $workDir, $MaxParallelJobs, $Target

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

# Drain the job's output. Its LAST result object is the real cmake/ninja exit code we
# explicitly returned as the job scriptblock's final expression (see Start-Job above) -- NOT
# $job.ChildJobs[0].JobStateInfo.State, which only reflects PowerShell-level job failure and
# stays 'Completed' even when the external process it ran exited non-zero on its own terms.
$jobOutput = Receive-Job -Job $job
$jobExitCode = $jobOutput | Select-Object -Last 1
$buildExitCode = if ($null -ne $jobExitCode -and $jobExitCode -is [int] -and $jobExitCode -ne 0) { 1 } else { 0 }
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

# A non-zero exit with NO per-target FAILED: lines means cmake/ninja rejected the invocation
# itself before attempting any target (e.g. "ninja: error: unknown target 'foo'", a bad
# preset, a CMake configure error surfaced mid-build) -- surface this distinctly instead of
# silently reporting "All targets built successfully" just because $failedTargets is empty.
$topLevelFailure = ($buildExitCode -ne 0 -and $failedTargets.Count -eq 0)

$elapsed = [int]((Get-Date) - $startTime).TotalSeconds
Write-StatusFile "DONE" $elapsed $lastTotal $lastDone $failedTargets.Count $lastTarget $failedTargets

Write-Host ""
Write-Host "[build] ============================== BUILD SUMMARY =============================="
Write-Host "[build] BuildId  : $BuildId"
if ($topLevelFailure) {
    Write-Host "[build] BUILD INVOCATION FAILED (exit $jobExitCode) BEFORE any target's compile/link was attempted."
    Write-Host "[build] This is NOT a compile/link failure -- likely a bad -Target name, bad preset, or a"
    Write-Host "[build] CMake configure error. See `"$buildLog`" for cmake/ninja's own error message."
} elseif ($failedTargets.Count -eq 0) {
    Write-Host "[build] All targets built successfully."
} else {
    Write-Host "[build] $($failedTargets.Count) target(s) FAILED:"
    foreach ($t in $failedTargets) { Write-Host "[build]   $t" }
    Write-Host "[build] Everything else in this build succeeded (kept going past the"
    Write-Host "[build] failures above via ninja -k 0) - see `"$buildLog`" for full output,"
    Write-Host "[build] including each failure's compiler error just under its FAILED: line."
}
Write-Host "[build] =============================================================================="
Write-Host "[build] Log file (this build's own, unambiguous by BuildId): $buildLog"
Write-Host "[build] Status file (shared across builds - match build_id: $BuildId to find yours): $StatusFile"

exit $buildExitCode

} finally {
    # Released even if the build throws, is killed, or this script is Ctrl+C'd — the Mutex
    # also auto-releases if THIS process itself dies, so a killed build never wedges the lock.
    if ($mutex) {
        $mutex.ReleaseMutex()
        $mutex.Dispose()
        Write-Host "[build] Build lock released."
    }

    # Auto-release the caller's queue ticket (if any) HERE, not left to the caller to remember
    # after this script returns. This is the fix for a real gap: build_queue.ps1's -Release was
    # previously only ever called by the dispatching agent itself, after build.bat returned -
    # if that agent's turn ends abnormally (killed, crashed, context-cleared) before it gets to
    # -Release, the ticket sat there blocking everyone behind it until the 60-minute stale reap.
    # Releasing in THIS finally block ties ticket lifetime to the build process's own lifetime
    # (same guarantee the Mutex release above already has), independent of whether the
    # dispatching agent is still around to call anything afterward.
    if ($QueueTicketId) {
        $queueScript = Join-Path $PSScriptRoot "build_queue.ps1"
        if (Test-Path $queueScript) {
            try {
                & powershell -ExecutionPolicy Bypass -File $queueScript -Release -TicketId $QueueTicketId | Out-Null
                Write-Host "[build] Queue ticket $QueueTicketId auto-released."
            } catch {
                Write-Host "[build] WARNING: failed to auto-release queue ticket $QueueTicketId : $_"
            }
        }

        # Post the result to a per-ticket log so an agent that isn't still attached (or a
        # different agent entirely) can read the outcome later without needing to have polled
        # -Status through to completion. Best-effort - never let a logging failure mask the
        # real build exit code.
        try {
            $resultLogDir = "$env:TEMP\vixen_build_queue_results"
            if (-not (Test-Path $resultLogDir)) { New-Item -ItemType Directory -Path $resultLogDir -Force | Out-Null }
            $resultLines = @(
                "TicketId: $QueueTicketId",
                "BuildId: $BuildId",
                "CompletedUtc: $((Get-Date).ToUniversalTime().ToString('o'))",
                "ExitCode: $buildExitCode",
                "TargetsFailed: $($failedTargets.Count)",
                "FailedTargets: $($failedTargets -join ', ')",
                "BuildLog: $buildLog"
            )
            Set-Content -Path "$resultLogDir\$QueueTicketId.log" -Value $resultLines -Encoding utf8
        } catch {
            Write-Host "[build] WARNING: failed to write queue result log for ticket $QueueTicketId : $_"
        }
    }
}
