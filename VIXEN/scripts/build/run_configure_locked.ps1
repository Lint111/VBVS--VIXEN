# Purpose: run `cmake --preset <preset>` (the CONFIGURE step) under a machine-wide lock,
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
# Usage: powershell -ExecutionPolicy Bypass -File run_configure_locked.ps1 -CMakeExe <path> -Preset <name> [-LockTimeoutSeconds N] [-SkipLock]
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
    [int]$LockTimeoutSeconds = 1800,
    [switch]$SkipLock
)

$mutex = $null
if (-not $SkipLock) {
    $mutex = New-Object System.Threading.Mutex($false, "Global\VixenConfigureLock")
    Write-Host "[configure] Waiting for the machine-wide configure lock (another worktree may be configuring)..."
    $lockWaitStart = Get-Date
    $acquired = $mutex.WaitOne([TimeSpan]::FromSeconds($LockTimeoutSeconds))
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
    & $CMakeExe --preset $Preset
    exit $LASTEXITCODE
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
