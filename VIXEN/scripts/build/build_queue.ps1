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
#   -Register -AgentId <id> [-Source <worktree-or-repo-path>] [-BuildTarget <cmake-target>]
#             [-BuildScript <absolute-path-to-build.bat>] [-BuildAction configure|build|all]
#             [-BuildPreset <preset-name>] [-Note <text>]
#       ALWAYS PASS -BuildScript. Omitting it (manual dispatch) is a narrow opt-out for one case
#       only — reserving a position without ever intending to build (e.g. a validator just
#       checking whether the lock will free up soon) — not a normal registration style. A manual
#       ticket that finishes its build but forgets to -Release has NO self-healing path the way
#       an auto-dispatch ticket does (see AUTO-DISPATCH below); it just blocks every real waiter
#       behind it for up to -StaleMinutes even after its build visibly finished. This has
#       happened in practice (2026-07-12) — an agent built successfully, moved on, and never
#       called -Release. The queue now ALSO opportunistically reaps a manual ticket once its own
#       build is provably DONE (see Test-ManualTicketAlreadyFinished), but that is a safety net
#       for the mistake, not a reason to keep making it.
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
#
#       AUTO-DISPATCH (opt-in via -BuildScript): if you pass -BuildScript, this ticket carries
#       everything needed to actually RUN your build — the queue itself will dispatch it when
#       its turn comes, even if YOU (the registering agent) have stalled or gone away by then.
#       See "Auto-dispatch" below for the full design and why this exists. Omit -BuildScript for
#       the old behavior: the ticket just reserves your position, and only YOU are expected to
#       actually call build.bat when -Status reports YOUR_TURN.
#   -Status -TicketId <id>
#       Print this ticket's queue position (1 = next), whether the BUILD LOCK is currently
#       free, and derived "YOUR_TURN" (position 1 AND lock free) / "WAITING" / "UNKNOWN_TICKET"
#       / "AUTO_DISPATCHED" (this call itself just ran your build — see below) /
#       "AUTO_DISPATCH_FAILED" (your build.bat exited non-zero; ticket is still released, check
#       your own build log). Cheap, non-blocking — safe to poll every few minutes or drive a
#       ScheduleWakeup loop. Refreshes THIS ticket's LastSeenUtc (proof of an active waiter — see
#       liveness dedup below) and, if this ticket is blocked behind another one, opportunistically
#       reaps that BLOCKING ticket if it's gone liveness-stale — so a genuinely active waiter is
#       never kept stuck behind an abandoned ticket for the full -StaleMinutes window; the check
#       happens at the exact moment it matters (someone is actually blocked on it), not just
#       periodically.
#   -Release -TicketId <id>
#       Remove a ticket (call after your build.bat invocation returns, whether it built or was
#       skipped — a ticket left behind after the agent moved on blocks everyone behind it). Not
#       needed for auto-dispatch tickets (see below) — the auto-dispatch itself releases them.
#   -ListQueue
#       Print all current tickets in FIFO order, for a human/agent to see the whole queue. Also
#       opportunistically auto-dispatches position-1 tickets, same as -Status (see below) — so
#       even a passive "what's in the queue" check can be the thing that unsticks a stalled build.
#   -Reap
#       Delete tickets whose LastSeenUtc is older than -StaleMinutes (default 15 — see liveness
#       dedup below for why this is short, not the original 60). A crashed/killed agent leaves
#       its ticket behind forever otherwise, since (unlike the Mutex) a ticket file has no OS-
#       level auto-release. Safe to call opportunistically from -Status/-ListQueue callers.
#
# Liveness, not just age (the queue-stall fix): every ticket has a LastSeenUtc field, refreshed
# on every -Register (re-registration/dedup hit) and -Status call for that ticket — i.e. every
# time its owner actually checks in. Reaping compares LastSeenUtc (last proof of life), NOT
# CreatedUtc (age since creation) — so a genuinely active waiter polling -Status every ~20s per
# vixen-build-policy's active-polling rule NEVER goes stale, no matter how long it legitimately
# waits, while a ticket registered and then abandoned (agent crashed, shut down without
# releasing, or a one-off ad-hoc registration that never got followed by a build or any -Status
# poll) goes stale within -StaleMinutes of its LAST actual check-in — which for an abandoned
# ticket is its registration moment. The queue is shared, contended, single-point-of-failure
# infrastructure for every agent on this machine; a stuck ticket blocks EVERYONE behind it, so
# the default window is short (15 min, down from an earlier 60 min default) and -Status
# opportunistically reaps its own blocker inline rather than waiting for a periodic sweep.
#
# Auto-dispatch (2026-07-12): even with liveness reaping, an agent that stalls (context
# exhaustion, a stuck subagent, anything short of a clean shutdown) AFTER its ticket reaches
# position 1 but BEFORE it personally calls build.bat means a perfectly valid, wanted build
# never runs — worse, the stalled agent's own -Status polling is what would normally refresh its
# ticket's liveness, so a stalled requester's ticket goes stale and gets reaped WITHOUT the build
# ever happening either way. The fix: let the ticket carry the actual build command (-BuildScript
# / -BuildAction / -BuildPreset / -BuildTarget), and let ANY agent's routine -Status/-ListQueue
# call — not just the ticket owner's — be the thing that notices "this ticket is at position 1,
# the lock is free, and it declared a real command" and just runs it inline right there, before
# returning. This piggybacks on polling traffic that's already happening across the whole agent
# fleet (per the active-~20s-polling rule), so a stalled requester's build still fires as soon as
# ANY other agent's normal, unrelated queue check touches the queue — no new persistent
# watcher/daemon process needed, nothing new to babysit or that can itself silently die. Safe
# under concurrency: the actual build lock is a real OS Mutex (run_build_with_summary.ps1's
# WaitOne()), so if two agents' -Status calls both observe "position 1, lock free" and both try
# to auto-dispatch at nearly the same instant, only one genuinely acquires the lock and executes
# — the design does not depend on this script's own logic being race-free, only on the Mutex
# being the single source of truth for "is a build actually running," which it already was.
# Auto-dispatch is opt-in per ticket (only fires if -BuildScript was passed at -Register time) —
# a ticket registered without it behaves exactly as before, so validators/agents that only want
# to reserve a queue position without handing off unattended build execution are unaffected.

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
    [string]$BuildScript = "",
    [string]$BuildAction = "all",
    [string]$BuildPreset = "vixen-ninja",
    [int]$StaleMinutes = 15
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

function Get-TicketLastSeenUtc($ticket) {
    # LastSeenUtc is the liveness field (set on -Register and refreshed on every -Status call
    # for that ticket). Tickets written before this field existed have no LastSeenUtc — fall
    # back to CreatedUtc for those so old-format tickets still reap on a sane schedule instead
    # of erroring or never expiring.
    if ($ticket.LastSeenUtc) {
        return [DateTime]::Parse($ticket.LastSeenUtc).ToUniversalTime()
    }
    return [DateTime]::Parse($ticket.CreatedUtc).ToUniversalTime()
}

function Invoke-Reap {
    $cutoff = (Get-Date).ToUniversalTime().AddMinutes(-$StaleMinutes)
    Get-ChildItem -Path $queueDir -Filter "*.json" -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            $t = Get-Content -Path $_.FullName -Raw | ConvertFrom-Json
            $lastSeen = Get-TicketLastSeenUtc $t
            if ($lastSeen -lt $cutoff) {
                Remove-Item -Path $_.FullName -Force -ErrorAction SilentlyContinue
                Write-Host "[build-queue] Reaped stale ticket $($t.TicketId) (agent $($t.AgentId), last seen $($lastSeen.ToString('o')))"
            }
        } catch {
            # Unparseable ticket file — remove it too, it's not usable by anyone.
            Remove-Item -Path $_.FullName -Force -ErrorAction SilentlyContinue
        }
    }
}

# Detect a MANUAL-dispatch ticket (no -BuildScript, so Invoke-AutoDispatchIfReady never touches
# it) whose owner already ran and finished a build but never called -Release afterward — a real,
# observed gap (2026-07-12): auto-dispatch tickets self-release the instant ANY agent's routine
# poll finds them at position 1 (Invoke-AutoDispatchIfReady runs it and releases inline), but a
# manual ticket sits there until either its owner personally calls -Release or the FULL
# -StaleMinutes liveness window elapses — even once its build has visibly, provably already
# finished. An agent that built successfully via build.bat WITHOUT threading -QueueTicketId
# through (the self-release path in run_build_with_summary.ps1 is opt-in via that param) leaves
# exactly this kind of orphaned-but-satisfied ticket behind, blocking every real waiter behind it
# for up to 15 minutes even though the lock has been free the whole time.
#
# Heuristic: the shared build-status file (%TEMP%\vixen_build_status.txt) records the build_id of
# the MOST RECENT build on this machine. If that build_id contains this ticket's own AgentId (the
# convention every worker in this fleet uses: pass -BuildId <something containing your agent
# name>) AND that build's status is terminal (DONE — run_build_with_summary.ps1 only ever writes
# RUNNING/WAITING_FOR_LOCK/DONE, never a separate FAILED state; a failed build still ends in DONE
# with targets_failed > 0) AND the status file was last written AFTER this ticket was registered
# (so a stale status file from a build that predates the ticket can't false-positive), the
# ticket's own build already ran to completion and the ticket is safe to reap. Deliberately
# requires AgentId length >=4 to avoid short-substring collisions (e.g. "m5" matching
# "vixen-m4-isolate").
function Test-ManualTicketAlreadyFinished($ticket) {
    if ($ticket.BuildScript) { return $false }  # auto-dispatch tickets are handled elsewhere
    if (-not $ticket.AgentId) { return $false }
    if ($ticket.AgentId.Length -lt 4) { return $false }
    $statusPath = "$env:TEMP\vixen_build_status.txt"
    if (-not (Test-Path $statusPath)) { return $false }
    try {
        $content = Get-Content -Path $statusPath -Raw
        if ($content -notmatch 'VIXEN_BUILD_STATUS:\s*DONE') { return $false }
        if ($content -notmatch 'build_id:\s*(.+)') { return $false }
        $buildId = $Matches[1].Trim()
        if (-not $buildId) { return $false }
        if ($buildId -notlike "*$($ticket.AgentId)*") { return $false }
        $statusFileTime = (Get-Item $statusPath).LastWriteTimeUtc
        $ticketCreated = [DateTime]::Parse($ticket.CreatedUtc).ToUniversalTime()
        if ($statusFileTime -lt $ticketCreated) { return $false }
        return $true
    } catch {
        return $false
    }
}

# Reap a SPECIFIC ticket right now if it's liveness-stale OR (new, 2026-07-12) if it's a manual
# ticket whose own build already finished without releasing — regardless of the periodic sweep.
# Used by -Status when it discovers it's blocked behind another ticket — checks that ticket's
# own staleness/completion inline instead of waiting for the next full -Reap. Returns $true if
# reaped.
function Test-ReapSingleTicket($ticketId) {
    $path = "$queueDir\$ticketId.json"
    if (-not (Test-Path $path)) { return $false }
    try {
        $t = Get-Content -Path $path -Raw | ConvertFrom-Json
        $cutoff = (Get-Date).ToUniversalTime().AddMinutes(-$StaleMinutes)
        $lastSeen = Get-TicketLastSeenUtc $t
        if ($lastSeen -lt $cutoff) {
            Remove-Item -Path $path -Force -ErrorAction SilentlyContinue
            Write-Host "[build-queue] Reaped stale BLOCKING ticket $ticketId (agent $($t.AgentId), last seen $($lastSeen.ToString('o'))) - it was stalling your queue position."
            return $true
        }
        if (Test-ManualTicketAlreadyFinished $t) {
            Remove-Item -Path $path -Force -ErrorAction SilentlyContinue
            Write-Host "[build-queue] Reaped BLOCKING ticket $ticketId (agent $($t.AgentId)) - its own build already completed (status file shows DONE matching this AgentId) but it never called -Release. It was stalling your queue position for no reason."
            return $true
        }
    } catch {
        Remove-Item -Path $path -Force -ErrorAction SilentlyContinue
        return $true
    }
    return $false
}

# Refresh a ticket's LastSeenUtc in place (proof of an active waiter). Best-effort — a failure
# here must never block the caller's actual -Register/-Status result.
function Update-TicketLastSeen($ticketId) {
    $path = "$queueDir\$ticketId.json"
    if (-not (Test-Path $path)) { return }
    try {
        $t = Get-Content -Path $path -Raw | ConvertFrom-Json
        $t | Add-Member -NotePropertyName LastSeenUtc -NotePropertyValue ((Get-Date).ToUniversalTime().ToString('o')) -Force
        $t | ConvertTo-Json | Set-Content -Path $path -Encoding utf8
    } catch { }
}

function Test-BuildLockFree {
    $mutex = New-Object System.Threading.Mutex($false, "Global\VixenBuildLock")
    $acquired = $mutex.WaitOne(0)
    if ($acquired) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
    return $acquired
}

# Auto-dispatch: if the ticket at position 1 declared a real build command (-BuildScript at
# -Register time) and the lock LOOKS free, actually run it right here, inline, before this
# -Status/-ListQueue call returns — regardless of whether the CALLER is the ticket's own owner.
# This is what lets a stalled requester's build still happen: whichever agent's routine queue
# check touches this ticket first while it's sitting at position 1 is the one that runs it.
#
# Concurrency note: the Test-BuildLockFree check here is only an optimistic pre-check to avoid
# obviously-pointless dispatch attempts (e.g. skip if a build is clearly already running) - the
# REAL safety is run_build_with_summary.ps1's own Mutex WaitOne(), which every dispatch still
# goes through. If two agents' calls both see "free" and both reach this function at nearly the
# same instant, both invoke build.bat, but only one actually acquires the Mutex and builds; the
# other blocks briefly inside its own WaitOne() and then (per that script's own lock-wait logic)
# proceeds only if it still has something to do - in practice this races harmlessly because the
# ticket file itself is removed by whichever dispatch attempt finishes first reaching the Release
# step below, and Remove-Item on an already-removed ticket is a silent no-op (see -Release).
#
# Returns one of: $null (nothing to auto-dispatch, e.g. not position 1 / no BuildScript / lock
# held), or a hashtable @{ Dispatched = $true; ExitCode = <int> } once a dispatch was attempted.
function Invoke-AutoDispatchIfReady {
    $tickets = @(Get-QueueTickets)
    if ($tickets.Count -eq 0) { return $null }
    $head = $tickets[0]
    if (-not $head.BuildScript) {
        # Manual-dispatch head ticket — auto-dispatch has nothing to run, but it may still be
        # sitting here abandoned-after-completion (see Test-ManualTicketAlreadyFinished). Reap it
        # inline so it doesn't block everyone behind it for the full staleness window just
        # because its owner forgot to -Release after a successful manual build. This is the SAME
        # kind of self-healing auto-dispatch tickets already get via the Remove-Item below — a
        # manual ticket just can't be RUN on someone else's behalf, only cleaned up once its own
        # build is provably already done.
        if (Test-ManualTicketAlreadyFinished $head) {
            Remove-Item -Path "$queueDir\$($head.TicketId).json" -Force -ErrorAction SilentlyContinue
            Write-Host "[build-queue] Reaped HEAD ticket $($head.TicketId) (agent $($head.AgentId)) - its own build already completed (status file shows DONE matching this AgentId) but it never called -Release. Re-checking queue for the new head."
            return Invoke-AutoDispatchIfReady  # re-check: the new head may itself be ready
        }
        return $null
    }
    if (-not (Test-BuildLockFree)) { return $null }
    if (-not (Test-Path $head.BuildScript)) {
        Write-Host "[build-queue] AUTO-DISPATCH SKIPPED: ticket $($head.TicketId)'s BuildScript '$($head.BuildScript)' no longer exists (worktree removed?) - releasing the ticket so it doesn't block the queue forever."
        Remove-Item -Path "$queueDir\$($head.TicketId).json" -Force -ErrorAction SilentlyContinue
        return $null
    }

    Write-Host "[build-queue] AUTO-DISPATCH: ticket $($head.TicketId) (agent $($head.AgentId)) is at position 1 with the lock free - running its build now on its behalf."
    Write-Host "[build-queue] AUTO-DISPATCH command: `"$($head.BuildScript)`" $($head.BuildAction) $($head.BuildPreset) $($head.BuildTarget)"

    $argList = @($head.BuildAction, $head.BuildPreset)
    if ($head.BuildTarget) { $argList += $head.BuildTarget }
    $proc = Start-Process -FilePath $head.BuildScript -ArgumentList $argList -NoNewWindow -Wait -PassThru
    $exitCode = $proc.ExitCode

    # Always release the ticket after dispatch, success or failure - a failed build is still a
    # COMPLETED turn, not a reason to keep blocking everyone behind it. The caller's own build
    # log (build.bat/run_build_with_summary.ps1 print their own log path) is the place to
    # investigate a failure, same as a manually-dispatched build always was.
    Remove-Item -Path "$queueDir\$($head.TicketId).json" -Force -ErrorAction SilentlyContinue
    if ($exitCode -eq 0) {
        Write-Host "[build-queue] AUTO-DISPATCH complete: ticket $($head.TicketId) build SUCCEEDED (exit 0). Ticket released."
    } else {
        Write-Host "[build-queue] AUTO-DISPATCH complete: ticket $($head.TicketId) build FAILED (exit $exitCode). Ticket released anyway - a failed turn is still a completed turn. Agent $($head.AgentId) should check its own build log."
    }
    return @{ Dispatched = $true; TicketId = $head.TicketId; AgentId = $head.AgentId; ExitCode = $exitCode }
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
        Update-TicketLastSeen $existing.TicketId
        $tickets = @(Get-QueueTickets)
        $position = (@($tickets | ForEach-Object { $_.TicketId })).IndexOf($existing.TicketId) + 1
        Write-Host "[build-queue] Already registered - reusing existing ticket for AgentId '$AgentId' (same Source/BuildTarget - de-dup, not creating a new one)."
        Write-Host "[build-queue] TicketId: $($existing.TicketId)"
        Write-Host "[build-queue] Queue position: $position of $($tickets.Count)"
        Write-Host "[build-queue] Go do other work. Check back with: -Status -TicketId $($existing.TicketId)"
        exit 0
    }

    if ($BuildScript -and -not (Test-Path $BuildScript)) {
        Write-Host "[build-queue] ERROR: -BuildScript '$BuildScript' does not exist. Pass the FULL ABSOLUTE Windows path to your build.bat (get it via wslpath -w if calling from WSL bash)."
        exit 1
    }

    $ticketId = [System.Guid]::NewGuid().ToString('N').Substring(0, 12)
    $nowUtc = (Get-Date).ToUniversalTime().ToString('o')
    $ticket = [PSCustomObject]@{
        TicketId    = $ticketId
        AgentId     = $AgentId
        Source      = $Source
        BuildTarget = $BuildTarget
        BuildScript = $BuildScript
        BuildAction = $BuildAction
        BuildPreset = $BuildPreset
        Note        = $Note
        CreatedUtc  = $nowUtc
        LastSeenUtc = $nowUtc
    }
    $ticketPath = "$queueDir\$ticketId.json"
    $ticket | ConvertTo-Json | Set-Content -Path $ticketPath -Encoding utf8
    $tickets = @(Get-QueueTickets)
    $position = (@($tickets | ForEach-Object { $_.TicketId })).IndexOf($ticketId) + 1
    Write-Host "[build-queue] Registered. TicketId: $ticketId"
    Write-Host "[build-queue] Queue position: $position of $($tickets.Count)"
    if ($BuildScript) {
        Write-Host "[build-queue] Auto-dispatch ENABLED: your build.bat ($BuildAction $BuildPreset $(if($BuildTarget){$BuildTarget}else{'<full>'})) will run automatically when it's your turn, even if you stall - no need to personally call build.bat."
    }
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
    $ids = @($tickets | ForEach-Object { $_.TicketId })
    $idx = $ids.IndexOf($TicketId)
    if ($idx -lt 0) {
        Write-Host "[build-queue] UNKNOWN_TICKET (expired/reaped, released, or never registered)"
        exit 3
    }

    # This ticket is alive and checking in — refresh its liveness so it never goes stale while
    # its owner is genuinely still polling (per vixen-build-policy's active ~20s polling rule).
    Update-TicketLastSeen $TicketId

    $position = $idx + 1

    # Opportunistic inline reap: if something is queued ahead of us, check whether THAT specific
    # ticket has gone liveness-stale right now, rather than waiting for the next periodic sweep
    # or the full -StaleMinutes window to elapse. This is the fix for the queue-stall class of
    # bug — an abandoned ticket (agent crashed/shut down without releasing) would otherwise block
    # every real waiter behind it for up to -StaleMinutes even though nothing is actually using
    # the lock. Re-fetch position/tickets afterward since a reap may have changed them.
    if ($position -gt 1) {
        $blockingTicket = $tickets[$position - 2]  # 0-indexed; the ticket one ahead of us
        if (Test-ReapSingleTicket $blockingTicket.TicketId) {
            $tickets = @(Get-QueueTickets)
            $ids = @($tickets | ForEach-Object { $_.TicketId })
            $idx = $ids.IndexOf($TicketId)
            if ($idx -lt 0) {
                # Should not happen (we didn't touch our own ticket) but guard anyway.
                Write-Host "[build-queue] UNKNOWN_TICKET (expired/reaped, released, or never registered)"
                exit 3
            }
            $position = $idx + 1
        }
    }

    # Auto-dispatch: whoever's -Status call finds a position-1, auto-dispatch-enabled ticket
    # with the lock free runs it right here — regardless of whether it's THIS caller's own
    # ticket. This is what makes a stalled requester's build still happen: any other agent's
    # routine poll can be the one that unsticks it. If the ticket that got dispatched was OUR
    # own ticket, report that explicitly instead of falling through to a stale WAITING/YOUR_TURN
    # message about a ticket that no longer exists (it was just released by the dispatch).
    $dispatchResult = Invoke-AutoDispatchIfReady
    if ($dispatchResult) {
        if ($dispatchResult.TicketId -eq $TicketId) {
            if ($dispatchResult.ExitCode -eq 0) {
                Write-Host "[build-queue] AUTO_DISPATCHED - your build just ran (as part of this -Status call) and SUCCEEDED. Ticket released, nothing more to do."
                exit 0
            } else {
                Write-Host "[build-queue] AUTO_DISPATCH_FAILED - your build just ran (as part of this -Status call) and FAILED (exit $($dispatchResult.ExitCode)). Ticket released. Check your build log."
                exit 4
            }
        }
        # A DIFFERENT ticket (ahead of ours) was the one auto-dispatched — re-check our own
        # position now that it's gone, rather than reporting stale numbers.
        $tickets = @(Get-QueueTickets)
        $ids = @($tickets | ForEach-Object { $_.TicketId })
        $idx = $ids.IndexOf($TicketId)
        if ($idx -lt 0) {
            Write-Host "[build-queue] UNKNOWN_TICKET (expired/reaped, released, or never registered)"
            exit 3
        }
        $position = $idx + 1
    }

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

    # Same opportunistic auto-dispatch as -Status — a passive "what's in the queue" check can be
    # the thing that unsticks a stalled requester's build. Loop (bounded) rather than a single
    # attempt: dispatching one ticket can immediately free the lock for the NEXT position-1
    # auto-dispatch ticket, and a caller listing the queue benefits from seeing it settle rather
    # than reporting a stale snapshot from mid-drain. Bounded to avoid this one command chewing
    # through an unbounded backlog of auto-dispatch tickets on a machine that's been quiet a while.
    $autoDispatchRounds = 0
    while ($autoDispatchRounds -lt 5) {
        $result = Invoke-AutoDispatchIfReady
        if (-not $result) { break }
        $autoDispatchRounds++
    }

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
        $auto = if ($t.BuildScript) { "auto" } else { "manual" }
        Write-Host "[build-queue] $pos. $($t.TicketId)  agent=$($t.AgentId)  source=$src  target=$tgt  dispatch=$auto  registered=$($t.CreatedUtc)  note=$($t.Note)"
        $pos++
    }
    exit 0
}

Write-Host "[build-queue] No action given. Use -Register, -Status, -Release, -ListQueue, or -Reap."
exit 1
