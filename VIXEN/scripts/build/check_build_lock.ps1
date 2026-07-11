# Purpose: check whether the machine-wide VIXEN build lock (Fix 9 — see Worktree-Build-
# Artifact-Accumulation-Audit-2026-07.md) is currently held, WITHOUT acquiring it or blocking.
# For an agent deciding whether to start a build now or go do other work (docs, review,
# planning) and check back later instead of dispatching a build that will just queue and
# contend for CPU/IO with whatever's already running.
#
# The actual acquire/release happens inside run_build_with_summary.ps1 (in-process, via
# try/finally on the same "Global\VixenBuildLock" Mutex this script only peeks at) — kept
# separate from that script rather than folded in as a flag, so a status check never has to
# spin up cmake/ninja machinery just to answer "is it busy right now".
#
# Usage: powershell -ExecutionPolicy Bypass -File check_build_lock.ps1
#   Exit 0 + "FREE"  if no build is currently running.
#   Exit 1 + "HELD"  if a build is currently running.

$mutex = New-Object System.Threading.Mutex($false, "Global\VixenBuildLock")
$acquired = $mutex.WaitOne(0)
if ($acquired) {
    Write-Host "[build-lock] FREE (no build currently running)"
    $mutex.ReleaseMutex()
    $mutex.Dispose()
    exit 0
} else {
    Write-Host "[build-lock] HELD (another build is in progress on this machine)"
    $mutex.Dispose()
    exit 1
}
