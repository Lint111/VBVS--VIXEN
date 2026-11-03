# Purpose: run `cmake -S <source> --preset <preset>` (the CONFIGURE step) under a machine-wide lock,
# closing a real race: CMake's FetchContent drives its clone/update/"recompaction" (internal
# stamp-file rewrite) during CONFIGURE, not build — and FETCHCONTENT_BASE_DIR is deliberately
# ONE shared directory across every worktree on this machine (VIXEN/CMakeLists.txt's
# "share FetchContent's clone+build output across all worktrees" block, Worktree-Build-
# Artifact-Accumulation-Audit-2026-07.md Fix 1). The existing Global\VixenBuildLock Mutex
# (run_build_with_summary.ps1) only wraps the BUILD step -- build.bat's `:do_all`/`:do_configure`
# actions called `cmake --preset` directly, completely unlocked, so two worktrees configuring at
# the same moment could both write into the SAME shared FetchContent subbuild (e.g. glm-build,
# nlohmann_json-build) unserialized. CMake's own stamp-file rewrite isn't safe against two
# concurrent writers -- whichever process lost the race got a raw Windows
# "Permission denied" (the other process still had the file open/mid-rename), and it appeared
# to move between different dependencies build-to-build because it was whichever two configures
# happened to overlap on that PARTICULAR sub-project at that moment, not a defect in any one
# library. Observed live 2026-07-12: 3 concurrent agents (view-binding-inc-c, ki-020-017-fix,
# lazy-baseline-inc0) each hit this on different FetchContent subbuilds during the same
# ~15-minute window.
#
# Usage: powershell -ExecutionPolicy Bypass -File run_configure_locked.ps1 -CMakeExe <path> -Preset <name> [-SourceDir <path>] [-BinaryDir <path>] [-LockTimeoutSeconds N] [-SkipLock]
#
# Deliberately a SEPARATE, narrower Mutex (Global\VixenConfigureLock) from the build lock
# (Global\VixenBuildLock), not the same one: configure and build serialize independently here
# because a worktree's configure (fast, seconds when FetchContent is already populated) should
# not have to wait behind another worktree's multi-minute BUILD, and vice versa -- the two
# phases contend on different resources (FetchContent's shared _deps directory vs. CPU/IO
# during compile/link) and gain nothing by sharing one lock. A configure that also needs to
# build still acquires each lock in turn (configure lock only during `cmake --preset`, released
# before the build lock is acquired inside run_build_with_summary.ps1) -- never held
# simultaneously, so this cannot deadlock against the build lock.

param(
    [Parameter(Mandatory=$true)][string]$CMakeExe,
    [Parameter(Mandatory=$true)][string]$Preset,
    [string]$SourceDir = "",
    [string]$BinaryDir = "",
    [int]$LockTimeoutSeconds = 1800,
    [switch]$SkipLock
)

$mutex = $null
if (-not $SkipLock) {
    $mutex = New-Object System.Threading.Mutex($false, "Global\VixenConfigureLock")
    Write-Host "[configure] Waiting for the machine-wide configure lock (another worktree may be configuring)..."
    $lockWaitStart = Get-Date
    try {
        $acquired = $mutex.WaitOne([TimeSpan]::FromSeconds($LockTimeoutSeconds))
    } catch [System.Threading.AbandonedMutexException] {
        # WaitOne throws after transferring ownership to this process. The old
        # Windows-native wrapper could be cancelled from WSL while cmake.exe
        # survived, leaving the named mutex abandoned for the next lane.
        $acquired = $true
        Write-Host "[configure] Recovered an abandoned configure lock; the previous owner exited without releasing it."
        Write-Host "[configure] Continuing with this configure; if it is stale, delete only this lane's build tree and retry."
    }
    if (-not $acquired) {
        $waited = [int]((Get-Date) - $lockWaitStart).TotalSeconds
        Write-Host "[configure] TIMEOUT after $($waited)s waiting for the configure lock - another configure is still running."
        Write-Host "[configure] Not starting this configure. Retry later, or increase -LockTimeoutSeconds."
        $mutex.Dispose()
        exit 2
    }
    $waited = [int]((Get-Date) - $lockWaitStart).TotalSeconds
    if ($waited -gt 1) {
        Write-Host "[configure] Configure lock acquired after waiting $($waited)s."
    } else {
        Write-Host "[configure] Configure lock acquired (was free)."
    }
}

try {
    $cmakeArgs = @()
    if ($SourceDir) { $cmakeArgs += @('-S', $SourceDir) }
    $cmakeArgs += @('--preset', $Preset)
    if ($BinaryDir) { $cmakeArgs += @('-B', $BinaryDir) }
    $configureLog = Join-Path $env:TEMP "vixen_configure_$([Guid]::NewGuid().ToString('N')).log"
    $startTime = Get-Date
    $job = Start-Job -ScriptBlock {
        param($cmakeExe, $args, $logPath)
        & $cmakeExe @args *>&1 | Out-File -FilePath $logPath -Encoding utf8
        $LASTEXITCODE
    } -ArgumentList $CMakeExe, (,$cmakeArgs), $configureLog
    try {
        while ($job.State -eq 'Running') {
            Start-Sleep -Seconds 5
            if ($job.State -eq 'Running') {
                $elapsed = [int]((Get-Date) - $startTime).TotalSeconds
                $lastLine = (Get-Content -Path $configureLog -Tail 1 -ErrorAction SilentlyContinue)
                if ($lastLine) {
                    Write-Host "[configure] still running (${elapsed}s): $lastLine"
                } else {
                    Write-Host "[configure] still running (${elapsed}s); CMake has not emitted a new line yet."
                }
            }
        }
        $jobOutput = Receive-Job -Job $job
        $configureExit = $jobOutput | Select-Object -Last 1
        if ($configureExit -is [int]) { exit $configureExit }
        exit 1
    } finally {
        Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
        if (Test-Path $configureLog) {
            Write-Host "[configure] CMake log: $configureLog"
        }
    }
} finally {
    # Released even if cmake throws or this script is Ctrl+C'd -- the Mutex also auto-releases
    # if THIS process itself dies, so a killed configure can never wedge the lock (same
    # guarantee the build lock already has).
    if ($mutex) {
        $mutex.ReleaseMutex()
        $mutex.Dispose()
        Write-Host "[configure] Configure lock released."
    }
}
