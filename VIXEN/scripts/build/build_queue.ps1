# Purpose: FIFO registration queue layered on top of the Fix-9 machine-wide build lock, so an
# agent can register intent to build, go do OTHER WORK, and check back (or be woken) only when
# it's actually their turn — instead of a whole turn/process blocking synchronously on
# WaitOne() with zero visibility into position or ETA. See Worktree-Build-Artifact-
# Accumulation-Audit-2026-07.md and the vixen-build-policy skill for the full design.
#
# This does NOT replace the Mutex in run_build_with_summary.ps1 — that still holds the actual
# OS-level lock for the duration of a real build. This queue is a layer ABOVE it: fair FIFO
# ordering + a status an agent can poll (or drive a ScheduleWakeup loop from) BEFORE deciding
# to call build.bat at all. A raw WaitOne() gives no such visibility — every waiter races
# blind. Ticket files live in %TEMP%\vixen_build_queue\ as small JSON files; the queue is
# derived from the directory listing (creation order = FIFO), not a separate index, so there
# is no separate structure that can drift out of sync with reality.
#
# Subcommands:
#   -Register -AgentId <id> [-Source <worktree-or-repo-path>] [-BuildTarget <cmake-target>] [-Note <text>]
#       Create a ticket, print its TicketId, exit 0. Call this FIRST, then go do other work.
#       De-duplicated by the COMBINATION of (AgentId, Source, BuildTarget) — not AgentId alone:
#       if a non-stale ticket already exists with the exact same AgentId + Source + BuildTarget,
#       returns that EXISTING ticket instead of creating a new one (idempotent — safe to call
#       again if you're not sure whether you already registered, e.g. after a retry/resume).
#       This prevents the same source/agent from queueing repeated redundant requests for the
#       SAME build (bug, retry, duplicate dispatch piling up tickets for itself, pushing back
#       everyone else's position for no reason) — while still treating the same agent building
#       a DIFFERENT target, or from a DIFFERENT worktree/source, as a distinct, valid, separate
#       queue entry (e.g. one agent legitimately queuing both a "VixenApp" build and a
#       "test_node_self_registration" build back-to-back is two real requests, not a dup).
#       -Source/-BuildTarget default to "" (matches other ""-valued tickets — omitting both
#       falls back to AgentId-only dedup, the prior behavior). Pass -Source as something stable
#       per requester (e.g. your worktree name) and -BuildTarget as whatever you'll pass to
#       build.bat's target arg (empty string for a full/default build).
#   -Status -TicketId <id>
#       Print this ticket's queue position (1 = next), whether the BUILD LOCK is currently
#       free, and derived "YOUR_TURN" (position 1 AND lock free) / "WAITING" / "UNKNOWN_TICKET".
#       Cheap, non-blocking — safe to poll every few minutes or drive a ScheduleWakeup loop.
#   -Release -TicketId <id>
#       Remove a ticket (call after your build.bat invocation returns, whether it built or was
#       skipped — a ticket left behind after the agent moved on blocks everyone behind it).
#   -ListQueue
#       Print all current tickets in FIFO order, for a human/agent to see the whole queue.
#   -Reap
#       Delete tickets older than -StaleMinutes (default 60) — a crashed/killed agent leaves
#       its ticket behind forever otherwise, since (unlike the Mutex) a ticket file has no OS-
#       level auto-release. Safe to call opportunistically from -Status/-ListQueue callers.

param(
    [switch]$Register,
    [switch]$Status,
    [switch]$Release,
    [switch]$ListQueue,
    [switch]$Reap,
    [string]$AgentId = "",
    [string]$TicketId = "",
    [string]$Note = "",
    [string]$Source = "",
    [string]$BuildTarget = "",
    [int]$StaleMinutes = 60
)

$queueDir = "$env:TEMP\vixen_build_queue"
if (-not (Test-Path $queueDir)) {
    New-Item -ItemType Directory -Path $queueDir -Force | Out-Null
}

function Get-QueueTickets {
    # FIFO = sorted by the ticket file's own creation timestamp (stored inside the JSON, not
    # filesystem mtime, since filesystem timestamp resolution/copy semantics aren't reliable
    # enough to trust for ordering across processes).
    $tickets = @()
    Get-ChildItem -Path $queueDir -Filter "*.json" -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            $t = Get-Content -Path $_.FullName -Raw | ConvertFrom-Json
            $tickets += $t
        } catch { }
    }
    return $tickets | Sort-Object -Property CreatedUtc
}

function Invoke-Reap {
    $cutoff = (Get-Date).ToUniversalTime().AddMinutes(-$StaleMinutes)
    Get-ChildItem -Path $queueDir -Filter "*.json" -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            $t = Get-Content -Path $_.FullName -Raw | ConvertFrom-Json
            $created = [DateTime]::Parse($t.CreatedUtc).ToUniversalTime()
            if ($created -lt $cutoff) {
                Remove-Item -Path $_.FullName -Force -ErrorAction SilentlyContinue
                Write-Host "[build-queue] Reaped stale ticket $($t.TicketId) (agent $($t.AgentId), registered $($t.CreatedUtc))"
            }
        } catch {
            # Unparseable ticket file — remove it too, it's not usable by anyone.
            Remove-Item -Path $_.FullName -Force -ErrorAction SilentlyContinue
        }
    }
}

function Test-BuildLockFree {
    $mutex = New-Object System.Threading.Mutex($false, "Global\VixenBuildLock")
    $acquired = $mutex.WaitOne(0)
    if ($acquired) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
    return $acquired
}

if ($Reap) {
    Invoke-Reap
    exit 0
}

if ($Register) {
    if (-not $AgentId) {
        Write-Host "[build-queue] ERROR: -Register requires -AgentId <id>"
        exit 1
    }
    Invoke-Reap

    # De-dup key = (AgentId, Source, BuildTarget), NOT AgentId alone — the same agent building a
    # DIFFERENT target or from a DIFFERENT worktree/source is a distinct, valid request (e.g.
    # queuing a full build then separately a scoped test-target build), not a duplicate to
    # collapse. Only an EXACT match on all three means "this is the same request again" (retry,
    # duplicate dispatch, confused re-invocation) — hand back that same ticket instead of piling
    # up a second one behind it, which would just push back everyone else's position for no
    # reason. Older tickets (registered before Source/BuildTarget existed) have $null for both
    # via ConvertFrom-Json on a JSON object missing those keys — coerce to "" so they compare
    # correctly against a caller who also didn't pass -Source/-BuildTarget (both default "").
    $existingTickets = @(Get-QueueTickets | Where-Object {
        $_.AgentId -eq $AgentId -and
        [string]$_.Source -eq $Source -and
        [string]$_.BuildTarget -eq $BuildTarget
    })
    if ($existingTickets.Count -gt 0) {
        $existing = $existingTickets[0]
        $tickets = @(Get-QueueTickets)
        $position = ($tickets | ForEach-Object { $_.TicketId }).IndexOf($existing.TicketId) + 1
        Write-Host "[build-queue] Already registered - reusing existing ticket for AgentId '$AgentId' (same Source/BuildTarget - de-dup, not creating a new one)."
        Write-Host "[build-queue] TicketId: $($existing.TicketId)"
        Write-Host "[build-queue] Queue position: $position of $($tickets.Count)"
        Write-Host "[build-queue] Go do other work. Check back with: -Status -TicketId $($existing.TicketId)"
        exit 0
    }

    $ticketId = [System.Guid]::NewGuid().ToString('N').Substring(0, 12)
    $ticket = [PSCustomObject]@{
        TicketId    = $ticketId
        AgentId     = $AgentId
        Source      = $Source
        BuildTarget = $BuildTarget
        Note        = $Note
        CreatedUtc  = (Get-Date).ToUniversalTime().ToString('o')
    }
    $ticketPath = "$queueDir\$ticketId.json"
    $ticket | ConvertTo-Json | Set-Content -Path $ticketPath -Encoding utf8
    $tickets = @(Get-QueueTickets)
    $position = ($tickets | ForEach-Object { $_.TicketId }).IndexOf($ticketId) + 1
    Write-Host "[build-queue] Registered. TicketId: $ticketId"
    Write-Host "[build-queue] Queue position: $position of $($tickets.Count)"
    Write-Host "[build-queue] Go do other work. Check back with: -Status -TicketId $ticketId"
    exit 0
}

if ($Status) {
    if (-not $TicketId) {
        Write-Host "[build-queue] ERROR: -Status requires -TicketId <id>"
        exit 1
    }
    Invoke-Reap
    $tickets = @(Get-QueueTickets)
    $ids = $tickets | ForEach-Object { $_.TicketId }
    $idx = $ids.IndexOf($TicketId)
    if ($idx -lt 0) {
        Write-Host "[build-queue] UNKNOWN_TICKET (expired/reaped, released, or never registered)"
        exit 3
    }
    $position = $idx + 1
    $lockFree = Test-BuildLockFree
    if ($position -eq 1 -and $lockFree) {
        Write-Host "[build-queue] YOUR_TURN - position 1, build lock is free. Call build.bat now."
        exit 0
    } elseif ($position -eq 1) {
        Write-Host "[build-queue] WAITING - position 1, but build lock still held by an in-flight build."
        exit 1
    } else {
        Write-Host "[build-queue] WAITING - position $position of $($tickets.Count)."
        exit 1
    }
}

if ($Release) {
    if (-not $TicketId) {
        Write-Host "[build-queue] ERROR: -Release requires -TicketId <id>"
        exit 1
    }
    $ticketPath = "$queueDir\$TicketId.json"
    if (Test-Path $ticketPath) {
        Remove-Item -Path $ticketPath -Force
        Write-Host "[build-queue] Released ticket $TicketId."
    } else {
        Write-Host "[build-queue] Ticket $TicketId not found (already released or expired) - nothing to do."
    }
    exit 0
}

if ($ListQueue) {
    Invoke-Reap
    $tickets = @(Get-QueueTickets)
    if ($tickets.Count -eq 0) {
        Write-Host "[build-queue] Queue is empty."
        exit 0
    }
    $lockFree = Test-BuildLockFree
    Write-Host "[build-queue] Build lock: $(if ($lockFree) { 'FREE' } else { 'HELD' })"
    $pos = 1
    foreach ($t in $tickets) {
        $src = if ($t.Source) { $t.Source } else { "-" }
        $tgt = if ($t.BuildTarget) { $t.BuildTarget } else { "(full build)" }
        Write-Host "[build-queue] $pos. $($t.TicketId)  agent=$($t.AgentId)  source=$src  target=$tgt  registered=$($t.CreatedUtc)  note=$($t.Note)"
        $pos++
    }
    exit 0
}

Write-Host "[build-queue] No action given. Use -Register, -Status, -Release, -ListQueue, or -Reap."
exit 1
