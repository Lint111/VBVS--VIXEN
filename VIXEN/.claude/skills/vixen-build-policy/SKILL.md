---
name: vixen-build-policy
description: Use before dispatching a VIXEN build (build.bat, cmake --build) on Windows, or when deciding whether to wait/queue/do other work while a build is in flight. Covers the machine-wide build lock, the FIFO registration/notification queue, and why a slower-but-throttled build beats maximum parallelism. Triggers on "run the build", "let's build", "is a build running", "build.bat", "cmake --build vixen-ninja".
---

# VIXEN Build Policy

## Overview

VIXEN builds on Windows go through `build.bat` at the repo root
(`/mnt/c/cpp/VBVS--VIXEN/build.bat`), which drives three layered pieces of tooling under
`VIXEN/scripts/build/` — a machine-wide **lock** (only one build runs at a time), a
**parallelism cap** (a running build doesn't peg every core), and a **FIFO queue with
notification** (an agent can register intent to build, go do other work, and check back only
when it's actually their turn). All three exist because of one incident: concurrent/unthrottled
builds pegged every core and stalled every agent's work on this machine — see
`Vixen-Docs/04-Development/Worktree-Build-Artifact-Accumulation-Audit-2026-07.md` Fix 7-10 for
the full incident history, measurements, and the bugs found while building this.

**Platform scope**: this whole policy is Windows-PowerShell-only (the lock is a .NET
`System.Threading.Mutex`, a Win32 kernel object with no equivalent semantics on Linux/WSL). A
WSL-side build (e.g. undertow's `cmake --build vixen/build`, invoked directly with no wrapper)
is NOT coordinated by any of this — it's a separate, unlocked build lane by design, not an
oversight. Don't assume cross-platform safety.

## Building a single target instead of everything

`build.bat` takes an optional third argument: a CMake target name. Omit it to build the full
default graph (as before); pass it to scope the build to one target (and its dependencies) —
e.g. iterating on one library or test binary without paying to rebuild+relink the rest of the
graph:

```bash
cmd.exe /c "C:\cpp\VBVS--VIXEN\build.bat build vixen-ninja VixenApp"
```

This threads through as `cmake --build --preset <preset> --target <name> -- -k 0 -j <N>` —
same lock, same parallelism caps, same `-k 0`/FAILED-summary behavior, just scoped to that
target's subgraph. Calling `run_build_with_summary.ps1` directly, the flag is `-Target <name>`.

## The three layers

1. **`run_build_with_summary.ps1`** (called by `build.bat build`/`build.bat all`) — acquires
   the lock (`Global\VixenBuildLock`), runs `cmake --build --preset <preset> -- -k 0 -j
   <MaxParallelJobs>`, releases the lock in a `finally` (so a killed build can never wedge it —
   unlike a lockfile, a Mutex is auto-released by the OS if its owning process dies). Writes a
   live status file (`%TEMP%\vixen_build_status.txt`) every 5s: `VIXEN_BUILD_STATUS`
   (`WAITING_FOR_LOCK`/`RUNNING`/`DONE`), `targets_done`/`targets_total`/`targets_failed`,
   `last_target`, `elapsed_seconds`. Prints a `BUILD SUMMARY` at the end listing every `FAILED:`
   target by path — `-k 0` means ONE broken target never masks whether everything else built.
2. **`check_build_lock.ps1`** — a non-blocking peek (`WaitOne(0)`) at whether the lock is
   currently held. Exit 0 + "FREE", or exit 1 + "HELD". Use this BEFORE deciding to register or
   dispatch, so you don't queue behind a build you didn't know was running.
3. **`build_queue.ps1`** — FIFO registration on top of the lock (see "Queue and notification"
   below). This is the layer that lets you avoid blocking a whole turn on a synchronous wait.

## The core policy: don't dispatch blind, don't wait blind

**Before dispatching a build you expect to take more than a minute or two:**

```powershell
powershell -ExecutionPolicy Bypass -File VIXEN\scripts\build\check_build_lock.ps1
```

- **FREE** → just call `build.bat build` (or `all`). It will acquire the lock itself; no
  further action needed.
- **HELD** → **don't dispatch a competing build.** It will just block synchronously inside
  `run_build_with_summary.ps1`'s `WaitOne()`, holding your whole turn/process idle with zero
  visibility into how long that wait will be. Register in the queue instead (below), then go
  do something else.

**While a build is queued or running, stay ACTIVE — do not go idle.** If you have other useful
work to interleave (docs, code review, planning the next phase), do it between status checks.
But the wait itself must be driven by an active foreground poll loop on the ~20s cadence below,
never a background wakeup/idle wait — see "Queue and notification" for why. This is the same
principle as the `multi-worktree-sync` skill's build-traffic-control section — this skill is
the VIXEN-specific implementation of it.

## Queue and notification: register, poll actively, go on your turn

Use this instead of directly calling `build.bat` when the lock is held, or whenever you want
fair ordering + visibility instead of a blind synchronous wait.

```powershell
# 1. Register — cheap, instant, non-blocking. Prints your TicketId and queue position.
# De-duplicated by the COMBINATION of (AgentId, Source, BuildTarget) — not AgentId alone. A
# retry/duplicate dispatch of the SAME agent registering the SAME source+target again gets
# back the SAME existing ticket, not a second one. But the same agent building a DIFFERENT
# target, or from a DIFFERENT worktree/source, is a distinct, valid request and gets its own
# new ticket — appended at the BACK of the queue behind everyone already waiting, never ahead.
# Pass -Source as something stable per requester (e.g. your worktree name) and -BuildTarget as
# whatever you'll pass to build.bat's target arg (omit for a full/default build) so de-dup can
# actually distinguish your requests correctly.
powershell -ExecutionPolicy Bypass -File VIXEN\scripts\build\build_queue.ps1 -Register -AgentId "<your-agent-id>" -Source "<your-worktree-name>" -BuildTarget "<target-or-omit-for-full-build>" -Note "<why you're building>"

# 2. Poll -Status actively every ~20s (see below) until YOUR_TURN. Do NOT hand the wait off to
#    ScheduleWakeup or a background Monitor task and go idle — those have repeatedly failed to
#    reliably wake the agent back up on this machine, silently stalling the whole turn.

# 3. Check status (repeat in the active ~20s loop until YOUR_TURN):
powershell -ExecutionPolicy Bypass -File VIXEN\scripts\build\build_queue.ps1 -Status -TicketId <id>
#   YOUR_TURN        -> position 1 AND the lock is free. Call build.bat now.
#   WAITING          -> not your turn yet (either not position 1, or position 1 but the lock
#                        is still held by an in-flight build — these are reported distinctly).
#   UNKNOWN_TICKET    -> your ticket expired (see Reap below), was released, or never existed.

# 4. Release is AUTOMATIC once build.bat actually starts the build (see below) — you do not
#    need to call -Release yourself in that case. Only call it explicitly if you registered a
#    ticket and then decided NOT to build after all (so nothing else will ever release it):
powershell -ExecutionPolicy Bypass -File VIXEN\scripts\build\build_queue.ps1 -Release -TicketId <id>
```

`-ListQueue` shows the whole queue (position, agent, note, registration time) plus current
lock state — useful for a human or an orchestrating agent to see who's waiting.

**Ticket release is automatic, not dependent on the dispatching agent staying alive.** Pass
your ticket through to the actual build via the `VIXEN_QUEUE_TICKET_ID` env var:

```bash
VIXEN_QUEUE_TICKET_ID=<id> cmd.exe /c "C:\...\build.bat build vixen-ninja"
```

`run_build_with_summary.ps1` releases that ticket itself, in the same `finally` block that
releases the build-lock Mutex — so release is tied to the **build process's own lifetime**, not
to whether the agent that registered it is still around afterward to call `-Release`. This
closes a real gap: previously a ticket was only ever released by the dispatching agent calling
`-Release` after `build.bat` returned, so an agent that got killed, crashed, or had its context
cleared mid-build left its ticket blocking the queue until the 60-minute stale reap. Now the
build itself — success, failure, or crash — always clears it. `run_build_with_summary.ps1` also
writes the outcome to `%TEMP%\vixen_build_queue_results\<TicketId>.log` (exit code, failed
targets, path to the full build log) so a *different* agent, or the same agent resumed later,
can read what happened without having stayed attached to watch it happen.

You should still call `-Release` explicitly in the one case auto-release doesn't cover:
registering a ticket and then deciding not to build at all.

**Notification mechanism, concretely — active polling, NOT `ScheduleWakeup`/`Monitor`.**
`ScheduleWakeup` and background-task notifications have repeatedly failed to reliably wake an
agent back up on this machine — an agent that goes idle waiting on one is liable to just stay
idle, silently stalling the whole turn with no one watching. Do not rely on them for a build
wait. Instead, poll `-Status` from an **active foreground loop directly in the same turn**, on
a ~20 second interval, per the standing rule in CLAUDE.md for any long-running
build/configure/render/deploy: never a silent `sleep`/blind wait, always a loop that prints a
readable status line each iteration so both the user and the agent's own process stay live and
attentive. Concretely, something like:

```bash
while true; do
  out=$(powershell -ExecutionPolicy Bypass -File VIXEN\scripts\build\build_queue.ps1 -Status -TicketId <id>)
  echo "[queue] $out"
  echo "$out" | grep -q YOUR_TURN && break
  sleep 20
done
```

This is a real foreground command (or the harness's Monitor/until-loop equivalent driving the
same query), not a background task — the agent stays active and re-checks every ~20s rather
than parking on a wakeup that may never fire. Only reach for a longer interval if a build is
already known to run long (Fix 8: real builds run 1-3+ minutes even mostly-cached) AND the
agent is deliberately doing other useful work in parallel between checks — the default,
un-supervised wait is always the ~20s active loop.

**Ticket hygiene — liveness-based reaping (fixed 2026-07-11), not just age.** Tickets are files
under `%TEMP%\vixen_build_queue\`, not tied to a process lifetime the way the Mutex is — a
crashed/killed agent's ticket does NOT auto-release. Every ticket has a `LastSeenUtc` field,
refreshed on every `-Register` (dedup-hit) and `-Status` call for that ticket — i.e. every time
its owner actually checks in. Reaping compares `LastSeenUtc`, NOT registration age: a
genuinely active waiter polling `-Status` every ~20s (per the active-polling rule above) never
goes stale no matter how long it legitimately waits, while an abandoned ticket (agent crashed,
shut down without releasing, or a one-off registration that was never followed by a build or
any `-Status` poll) goes stale within `-StaleMinutes` (default **15**, down from an earlier 60)
of its last real check-in. **`-Status` also opportunistically reaps its own blocking ticket
inline** the moment it discovers it's stuck behind a stale one — not just on the next periodic
sweep — so a real waiter is never stuck behind an abandoned ticket for the full stale window.
This closes a real incident (2026-07-11): a validator agent self-registered a ticket for an
ad-hoc verification build (outside the `build.bat`-auto-release path — see below), then shut
down without releasing it, stranding a sibling agent's real build behind it until manually
released. **Still always call `-Release` yourself when you're truly done** rather than relying
on staleness reaping — reaping is the backstop for abandonment, not the primary release path.

## Why parallelism is capped, not maximized (Fix 10)

`run_build_with_summary.ps1` passes `-j <N>` to ninja, defaulting to ~75% of logical cores
(override: `-MaxParallelJobs` or `VIXEN_MAX_BUILD_JOBS` env var). Separately, `CMakeLists.txt`
caps concurrent **link** jobs specifically, lower still (`(cores+3)/4` — 4 on a 16-core
machine, override: `VIXEN_MAX_PARALLEL_LINKS`), via a Ninja job pool
(`CMAKE_JOB_POOL_LINK`). Two different caps because compile and link have different resource
profiles here: link.exe for this project's debug binaries (with `/Z7` embedded debug info)
runs 400-500MB RSS each — running as many links in parallel as cores is what actually made the
whole machine stutter under load, not the compile step. A build that leaves the machine
usable is worth more than a build that finishes 20% faster while everything else on the
machine (including other agents) grinds to a halt.

## Env vars (all optional, all have conservative defaults)

| Var | Default | Effect |
|---|---|---|
| `VIXEN_SKIP_BUILD_LOCK` | unset | Set to `1` to bypass the lock entirely (e.g. a machine known to be otherwise idle) |
| `VIXEN_BUILD_LOCK_TIMEOUT` | 1800 (30 min) | Seconds to wait for the lock before giving up |
| `VIXEN_MAX_BUILD_JOBS` | ~75% of logical cores | Overall ninja `-j` cap |
| `VIXEN_MAX_PARALLEL_LINKS` | `(cores+3)/4` | Concurrent link-job cap (separate, lower) |

## Known worktree gotcha: always invoke `build.bat` by absolute Windows path

`build.bat` derives its source directory from its own location (`REPO_ROOT=%~dp0`,
`SRC_DIR=%REPO_ROOT%\VIXEN`) — this is correct BY DESIGN so the same script works from any
clone/worktree. The failure mode is upstream of that: if you invoke it from a WSL bash shell via
`cmd.exe /c "build.bat build vixen-ninja"` (a bare relative name, no explicit path) while your
bash `cwd` is inside a worktree, `cmd.exe`'s OWN starting directory is not guaranteed to be your
bash `cwd` — cross-shell invocation can silently resolve `build.bat` against a DIFFERENT
`build.bat` on `PATH`/a stale default directory (observed: it resolved to the main checkout's
`C:\cpp\VBVS--VIXEN\build.bat` while running from a worktree at
`.claude\worktrees\<name>\`). The build then runs, acquires the lock, and reports success/failure
— all against the WRONG tree, with no error, because `build.bat` has no way to know it was asked
to build somewhere other than intended. This burned a full build-lock turn (and a second one
finding the fix "already applied" on the wrong checkout) on 2026-07-11 during Sampled-Lighting
Inc3 M1.

**Always call `build.bat` with its full Windows absolute path** when driving it from WSL bash,
even though the script itself is path-agnostic:

```bash
cmd.exe /c "C:\cpp\VBVS--VIXEN\.claude\worktrees\<your-worktree>\build.bat build vixen-ninja" > log 2>&1
```

not:

```bash
cmd.exe /c "build.bat build vixen-ninja" > log 2>&1   # DON'T — cwd/PATH resolution is not guaranteed
```

Get the absolute Windows path from bash with `wslpath -w "$(pwd)/build.bat"` if unsure. **After
any build, sanity-check the logged `source:` line** (`run_build_with_summary.ps1` prints
`[build] source   : <path>` near the top of every run) — if it doesn't match the worktree you
meant to build, the whole result (including a "target now builds!" fix-verification) is
meaningless, silently.

## Known gotcha: the shared status file is machine-wide, not per-build (mitigated by BuildId)

`%TEMP%\vixen_build_status.txt` (`run_build_with_summary.ps1`'s live-status file) is a SINGLE
file shared by every build on the machine — it is overwritten by whichever build last touched it,
regardless of which worktree/agent started it. If two agents build around the same time, checking
this file for "is MY build done" can show a stale or entirely different build's `last_target`
(e.g. a path under a DIFFERENT worktree). Observed 2026-07-11: after my own build finished, the
status file's `last_target` pointed at `tiered-esvo-inc2` — a sibling agent's build that happened
to finish around the same moment.

**Fixed (2026-07-11): every build now has a `BuildId`**, printed as the FIRST line of console
output, written into the status file as a `build_id:` field, embedded in the log filename
itself (`%TEMP%\vixen_build_<BuildId>.log` — no more anonymous random-GUID logs), and repeated
in the BUILD SUMMARY footer. `build.bat` auto-derives a sensible default from the checkout's own
directory name (a worktree's builds are self-identifying with zero setup — e.g. `build.bat` run
from `.claude\worktrees\graph-node-linkage-inc1\` gets `BuildId=graph-node-linkage-inc1`), or set
`VIXEN_BUILD_ID=<something>` explicitly for a more specific label (e.g. a task/ticket name).
**Always check the status file's `build_id:` field against the BuildId you noted at dispatch
time before trusting `last_target`/`targets_done` as "my build's" progress** — if it doesn't
match, you're looking at a different, possibly-concurrent build's status, not yours. This makes
the status file usable for "is MY build done" now, not just "is the lock currently busy" — but
your own build's log (`%TEMP%\vixen_build_<YourBuildId>.log`, printed at both start and end of
output) remains the authoritative source for full output/failures, same as before.

## What NOT to do

- Don't call `cmake --build` directly, bypassing `build.bat` — you skip the lock, the
  parallelism caps, AND the `-k 0` keep-going behavior, reintroducing exactly the problems
  Fix 7-10 fixed. If you need a scoped/target-specific build, still go through
  `run_build_with_summary.ps1` (it accepts the same preset-based `cmake --build` underneath;
  don't hand-roll a separate invocation).
- Don't poll `-Status`/`check_build_lock.ps1` in a sub-second tight loop that wastes cycles —
  but DO poll actively on a ~20s cadence (see "Queue and notification" above). Don't hand the
  wait off to `ScheduleWakeup` or a background `Monitor` task and go idle instead — those have
  repeatedly failed to reliably resume the agent on this machine, which stalls the whole turn
  with nothing watching it. An active 20s foreground loop is the correct middle ground.
- Don't assume the lock/queue coordinates with a WSL-side build (undertow or otherwise) — it
  doesn't, by design (see Platform scope above).
- Don't manually call `-Release` after a build you dispatched via `VIXEN_QUEUE_TICKET_ID` —
  `run_build_with_summary.ps1` already released it in its `finally` block; calling `-Release`
  again is harmless (idempotent — "already released") but unnecessary. DO still call `-Release`
  yourself if you registered a ticket and decided not to build at all — nothing else will ever
  release that one.
- **`build.bat build` does NOT reconfigure — it never re-runs `cmake --preset`.** Only
  `build.bat all`/`configure` does. If you ADD A NEW SOURCE FILE to a `CMakeLists.txt` (a new
  `.cpp`/`.h` registered in a target's source list) and then call `build.bat build`, ninja's
  existing `build.ninja` has no idea the new file exists — it silently rebuilds/relinks whatever
  it already knew about and calls it done, exit 0, no error. The resulting binary does NOT
  contain your new file, and any gate/capture run against it is a **false pass** — it "succeeds"
  precisely because none of your new code is in the binary being tested. This bit a real gate
  (Sampled-Lighting Inc3 M2, 2026-07-11): an Opus validator caught it via build-graph forensics —
  the new file's `.obj` didn't exist, the target `.lib`/`.exe` timestamps predated the source
  edit by tens of minutes, and a `grep` for the new symbol found zero occurrences in the built
  artifacts, even though the gate had reported byte-identical/clean results. **Rule: whenever
  your milestone adds or removes a source file from any `CMakeLists.txt`, run `build.bat all`
  (or `configure` then `build`) at least once before trusting any gate capture — `build.bat
  build` alone is only safe for iterating on EXISTING files.** When validating someone else's
  gate, spot-check this yourself: compare the new/changed source file's mtime against the
  target binary's mtime, and grep the binary (or its `.lib`) for a symbol/string unique to the
  new code — don't just trust that "the build succeeded."
- **Always invoke the WORKTREE's own `build.bat`, not the repo-root/main-checkout's.** Each git
  worktree has its own copy of `build.bat` at its own root — if you `cd` or path-reference the
  wrong one (e.g. accidentally the main checkout's `/mnt/c/cpp/VBVS--VIXEN/build.bat` while
  meaning to build a worktree's source), you silently build/gate the WRONG tree's code. This
  also bit the same M2 validation session. Double-check the absolute path you invoke resolves
  inside the worktree you actually intend to build.
