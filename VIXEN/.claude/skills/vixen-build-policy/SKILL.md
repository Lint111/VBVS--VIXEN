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

**While a build is queued or running — go do other work.** Docs, code review, planning the
next phase, reading a previous test run's output. A blocked build is not a blocked agent. This
is the same principle as the `multi-worktree-sync` skill's build-traffic-control section — this
skill is the VIXEN-specific implementation of it.

## Queue and notification: register, do other work, get pinged on your turn

Use this instead of directly calling `build.bat` when the lock is held, or whenever you want
fair ordering + visibility instead of a blind synchronous wait.

```powershell
# 1. Register — cheap, instant, non-blocking. Prints your TicketId and queue position.
powershell -ExecutionPolicy Bypass -File VIXEN\scripts\build\build_queue.ps1 -Register -AgentId "<your-agent-id>" -Note "<why you're building>"

# 2. Go do other work. Do NOT sit in a tight poll loop — that defeats the purpose.
#    Use ScheduleWakeup (a few minutes out, or longer for a known-busy machine) to come back
#    and check, rather than blocking this turn.

# 3. Check status (repeat via ScheduleWakeup until YOUR_TURN):
powershell -ExecutionPolicy Bypass -File VIXEN\scripts\build\build_queue.ps1 -Status -TicketId <id>
#   YOUR_TURN        -> position 1 AND the lock is free. Call build.bat now.
#   WAITING          -> not your turn yet (either not position 1, or position 1 but the lock
#                        is still held by an in-flight build — these are reported distinctly).
#   UNKNOWN_TICKET    -> your ticket expired (see Reap below), was released, or never existed.

# 4. ALWAYS release your ticket after build.bat returns — whether it built, failed, or you
#    decided not to build after all. A ticket left behind blocks everyone queued behind you.
powershell -ExecutionPolicy Bypass -File VIXEN\scripts\build\build_queue.ps1 -Release -TicketId <id>
```

`-ListQueue` shows the whole queue (position, agent, note, registration time) plus current
lock state — useful for a human or an orchestrating agent to see who's waiting.

**Notification mechanism, concretely**: there is no push/interrupt — "notification" here means
structuring the wait as `ScheduleWakeup` (fire in N minutes, re-check `-Status`, reschedule if
still `WAITING`) rather than either (a) a blocking synchronous wait that ties up a whole turn,
or (b) a tight poll loop that wastes cycles. Pick the `ScheduleWakeup` delay based on what
you're actually waiting on — a build that's been running 2 minutes probably has more to go;
check back on an interval proportional to how long builds on this project typically take (see
Fix 8's measurements: real builds run 1-3+ minutes even mostly-cached, longer from scratch),
not a fixed short interval.

**Ticket hygiene**: tickets are files under `%TEMP%\vixen_build_queue\`, not tied to a process
lifetime the way the Mutex is — a crashed/killed agent's ticket does NOT auto-release. Every
queue command opportunistically reaps tickets older than 60 minutes
(`-StaleMinutes`), so an abandoned ticket doesn't block the queue forever, but always call
`-Release` yourself rather than relying on the timeout.

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

## Known gotcha: the shared status file is machine-wide, not per-build

`%TEMP%\vixen_build_status.txt` (`run_build_with_summary.ps1`'s live-status file) is a SINGLE
file shared by every build on the machine — it is overwritten by whichever build last touched it,
regardless of which worktree/agent started it. If two agents build around the same time, checking
this file for "is MY build done" can show a stale or entirely different build's `last_target`
(e.g. a path under a DIFFERENT worktree). Observed 2026-07-11: after my own build finished, the
status file's `last_target` pointed at `tiered-esvo-inc2` — a sibling agent's build that happened
to finish around the same moment. **This file is only reliable for "is the lock currently busy"
(paired with `check_build_lock.ps1`), never for "did MY specific build finish/succeed."** For your
own build's outcome, always read the log file YOU redirected stdout to (and confirm its `source:`
line matches your worktree, per the gotcha above) — never the shared status file.

## What NOT to do

- Don't call `cmake --build` directly, bypassing `build.bat` — you skip the lock, the
  parallelism caps, AND the `-k 0` keep-going behavior, reintroducing exactly the problems
  Fix 7-10 fixed. If you need a scoped/target-specific build, still go through
  `run_build_with_summary.ps1` (it accepts the same preset-based `cmake --build` underneath;
  don't hand-roll a separate invocation).
- Don't poll `-Status`/`check_build_lock.ps1` in a tight loop — use `ScheduleWakeup` at a
  sensible interval instead.
- Don't assume the lock/queue coordinates with a WSL-side build (undertow or otherwise) — it
  doesn't, by design (see Platform scope above).
- Don't leave a queue ticket unreleased "because it'll expire eventually" — release it
  immediately when you're done, even on failure/abort.
