---
title: Measurement Discipline — evidence rules earned during the wavefront epoch
status: active
created: 2026-08-08
tags: [measurement, evidence, methodology, perf, gpu, wavefront]
---

# Measurement Discipline — evidence rules earned during the wavefront epoch

**Source:** every rule below is distilled from a specific dated finding on the undertow
ledger, `docs/plans/2026-08-04-wavefront-recipe-shading.md` (batches 35-41v2). This is a
methodology digest, not a new claim — see the ledger for the evidence behind each rule and
[[Deep-Field-Mip-Accessor-Policy-2026-08]] / `04-Development/Known-Issues.md` for where
each rule's originating defect landed.

## Seeded probes: an unseeded field cannot prove it fired

A counter/probe that defaults to 0 cannot distinguish "never fired" from "fired and
counted zero". The batch-38 `recordEntryGateLhs` probe used a `-nan` seed instead — 12 of
15 boots printed the untouched seed, which is what PROVED the three real readings were
measurement and not a default. Where an unsigned accumulator forces a 0 seed (e.g.
`atomicAdd` arithmetic — a `0xFFFFFFFF` seed wraps the first increment to 0), disambiguate
unfired-vs-fired-zero from a separate boot-log line (an "ENGAGED" flag print) instead of
the counter itself.

## State-set hashing, not single-reference equality

Two "identical" boots of the same config are not guaranteed to produce the same frame —
the two-state boot alternation (KI-046) means a raw px-delta or single-hash-equality check
against one reference frame can show hundreds of false-positive pixels. Hash every boot's
frame and check **membership in the known state set**, not equality to one reference. This
is how the batch-41 sparse-body work correctly proved its flag-off gate clean ("frame ∈
known state set") despite the alternation being live and unexplained.

## Pool bytes are the bake-sparsity truth instrument

`[BrickDataHash] sizes:` (unconditional, no `VIXEN_NODE_LOG` needed) reports actual GPU
pool byte counts. When batch 41's sparse-body bake silently re-densified to 98% via a
one-brick dilation trap (KI-045), pool bytes caught it in one line — cross-checked exactly
against the 502/512 brick count. Prefer pool bytes over any derived percentage or
node-count print when the question is "how much data did the bake actually keep."

## Count vs. mean/max: gate on the coarse-but-stable statistic

Census **count** was boot-stable (414/420 held 3/3 across multiple batches); **mean/max
were not** — the same config showed 149.93/206 on one boot and 244.31/332 on another, a
swing large enough to invalidate any single-boot mean/max citation. **Gate on count; never
gate on single-boot mean/max.** If a finer statistic is needed, report it across ALL
boots in the leg, never cherry-picked from the boot that happens to pass.

## Box-lock dispatch pattern: one atomic acquisition per matrix, resource-scoped

Multi-boot perf matrices ran under `tools/with-test-lock.sh`, acquiring the `gpu` resource
for the WHOLE matrix as one atomic hold (not per-boot), while a separate `build` resource
let compilation proceed concurrently under a different lock. Validator-confirmed sanction:
different resources held concurrently by different streams at the same time is the correct
pattern, not a violation — e.g. batch 40's `16:03:56 build streamC exit=0` and `16:30:20
gpu streamA exit=0` overlapping was compliant. This is what let regime-3's mid-matrix code
edits build without ever touching the exe a live perf matrix had open (the LNK1104 in
batch 40 was the lock working as intended, not a bug).

## The brief-bar rule: a threshold must be checkable, and demanded evidence must match what the code can produce

Two brief defects cost real cycles this epoch:
- **Vacuous thresholds:** a "< 0.9936" criterion was vacuous because the real baseline
  (0.993562) rounds INTO the threshold at normal precision — state thresholds at full
  precision or as an explicit strict-vs-baseline comparison, never a rounded number that
  can silently swallow the baseline itself.
- **Evidence the code cannot produce:** batch 41's brief demanded cross-instance
  bleed-through (body4 magenta) as regime-3 slice 1's success bar, but slice 1's own code
  banner explicitly scopes cross-instance compositing OUT (`SceneBindings.glsl:2583-2587`
  — residual transmittance is discarded, not composited). The "null result" that followed
  was not a code defect; it was a bar the implementation was never designed to clear.
  **Before writing a success bar, confirm the slice's own scope note doesn't already rule
  it out.**

## Instrument-erases-signal is a recurring defect class — validate the instrument before trusting a null result

Three separate times this epoch, an instrument that read "no effect" was itself the
defect, not the code under test (batch 8's flake saga; batch 13's third occurrence). Before
accepting a null/negative measurement as characterization, check whether the same counter
has a known conflation, an out-of-scope call site, or an unproven seed — the pattern is
common enough that "the number didn't move" should default to suspicion of the instrument,
not confirmation of no-effect, until independently re-derived.

## Environment-read traps: filtered queries can return silent absence-of-match, not absence-of-process

`tasklist /FI "IMAGENAME eq steam.exe"` returned blank output (not the documented "no
tasks" string) on this machine while Steam WAS running — a controller briefing built on
that blank read stood as ground truth for a full batch (see Known-Issues.md's `tasklist
/FI` entry). **Standing rule: use the unfiltered form and grep client-side; never trust a
filtered system query's silence as a negative.** More generally — a verified-state claim
in a briefing still deserves a cheap independent check before a whole batch's figures rest
on it, especially when the check is nearly free (one unfiltered `tasklist | grep`).

## Distinguish inequality from equality, and UNMEASURED from disproven

Recurring epoch-wide discipline, restated here because it governs every rule above: ruling
out one cause (concurrent GPU load, then CPU/pacing) is not the same as identifying the
real one, and "we didn't observe a win" is not the same as "there is no win." The cost
figure for the entry-dispatch inversion has been **UNMEASURED, not disproven** across five
batches of narrowing (35→40); each batch correctly reported the narrower-but-still-open
state rather than forcing a number or declaring the question closed. See
[[Deep-Field-Mip-Accessor-Policy-2026-08]]'s Cost section for the full chain.

## THE UNIFICATION (batches 42-43, 2026-08-08 evening): the two-state pixel alternation and the per-boot cost variance are ONE bistable boot regime

Across 30 matrix legs, `esvo_traverse_shade` and `shadow_visibility_wave` are **anti-correlated
r = −0.88 and bimodal** — two clean per-boot bands (esvo 2.74 vs 4.17 ms; shadow 1.99 vs 1.18;
whole-frame only 1.10× apart) with every other stage flat to ≤2%. The same bistability shows in
pixels as the two-state frame alternation (all diffs in the y∈[240,259] band). The bistable
input itself is UNMEASURED; classification makes it harmless. Protocol that follows from it:

1. **Discard frames 1–4 of every boot** — GPU query timers are ZERO-FILLED for the first four
   frames in every leg ever measured (30/30). Frame-1-only discard was never sufficient.
2. **Metric = the conserved sum `esvo + shadow_visibility_wave`** (or whole-frame). NEVER the
   esvo/whole ratio — it divides the anti-correlated pair and amplifies the regime split.
3. **Classify each boot's regime FIRST (esvo/whole ratio, threshold 0.49), compare
   within-regime.** This collapsed 28–54% cross-boot spreads to 0.1–5.0% on existing data and
   surfaced a consistent ~6–15% mip-policy cost win on both backends (strong-provisional at
   the time; the batch-44 run was voided, and the later round-robin/resumable sweep closed
   certification — see the Cost closure section below).
4. **Masked-hash boot-stability instrument:** zeroing rows 240–259 collapses all 24 historical
   legs across 3 batches × 5 configs to ONE hash (`c76867f9ba34defd`). Use it to separate
   "alternation-band flip" from a real out-of-band regression — its first live use caught a
   misconfigured gate leg (missing `VIXEN_BRICKMAP_SCENE=1`) within minutes.

## Build-and-boot serialization rules (earned via LNK1104 + a false green, batch 42)

- **Anything that WRITES `VIXEN.exe` (the rtquery build's link step) must hold the machine's
  `gpu` box-lock** — linking collides with running boots, not with compiles. A rebuild+smoke
  sequence is one `gpu` acquisition.
- **`rtquery_build.bat` printed `=== BUILD OK ===` after a failed ninja step** (`if errorlevel`
  checks silently not tripping); the box done-log recorded `exit=0` for a failed build. The
  bat's checks are now hardened to `if %ERRORLEVEL% NEQ 0`, but the standing rule remains:
  never accept BUILD OK without grepping the log for `ninja: build stopped` AND confirming the
  binary mtime advanced past the newest source edit.
- **Agents can lose track of their own long-running matrices** (batch 42: an implementer
  launched a matrix, then reported it never ran, then collided its own relink with its own
  boot). Before diagnosing a mystery `VIXEN.exe`, check the box lock's holder label and the
  Windows-side bat drivers (`Win32_Process` command lines) — the orphan is usually ours.

## Sweeps must be RESUMABLE — never all-or-nothing (user directive, 2026-08-08)

A mid-sweep kill (game starting, priority change, controller misjudgment) must cost only the
in-flight boot, not the whole run. Paid for once: a valid 5-boot partial was discarded and
restarted from scratch because the driver couldn't credit persisted work. The contract every
sweep driver must satisfy (reference implementation: `~/scripts/sweep-cert-resumable.sh`):
1. **Per-boot artifacts persist** (CSV/log/frames per boot — not one aggregate at the end).
2. **Resume-scan on start**: enumerate existing per-boot artifacts, classify each, seed the
   tallies, continue numbering after the highest existing index; skip already-full cells.
3. **Incremental results file** (`results.tsv` appended per boot) so the analysis input
   survives any kill.
4. **Binary-mix guard**: stamp the binary (mtime+size) on first run; a resume under a
   different binary ABORTS rather than silently mixing builds (per-row stamps in results.tsv
   make any mix auditable).
5. Keep the flag-integrity guard (an -off leg logging its flag ENGAGED aborts — the cmd
   `set VAR= &&` defines-to-space trap).

## Composable jobs take the lock PER STEP, not per run (user directive, 2026-08-08)

A long job made of independent steps (a 25-48-boot sweep) must not hold the `gpu` queue
end-to-end — that starves every sibling agent behind a task that was never atomic. The
discipline (reference: `~/scripts/sweep-cert-resumable.sh`):
- **`gpu` = per-step**: each boot acquires/releases individually; other agents' boots and
  smokes interleave between steps (FIFO fairness).
- **`binary` = per-run, only for what is genuinely atomic**: a cert sweep's real invariant
  is VIXEN.exe stability, so the RUN holds the `binary` resource; only binary-WRITING jobs
  (relinks) queue on it. Non-conflicting work never waits behind the sweep.
- **Ordering rule (deadlock-free): acquire `binary` before `gpu`, never the reverse** —
  builds take binary, then gpu for the link step; sweeps take binary for the run, gpu per boot.
- The per-boot binary-mix stamp remains the detection backstop for anyone bypassing this.
Generalization: when wrapping any multi-step job, ask "what is the ATOMIC invariant?" and
hold a lock exactly as wide as that invariant — the queue-visible resource stays per-step.

## Cost closure — six attempts, and what each failure bought (2026-08-09)

The six-attempt lineage is now closed: **Steam misattribution → GPU-warming dead end →
ratio-metric dead end → voided flag run → streak-starved cells → within-regime +
round-robin closure**. Each failure bought a durable instrument or protocol correction:

| attempt / failure | instrument or correction earned |
|---|---|
| Steam misattribution | independent process/GPU-state verification; filtered `tasklist` silence is not a negative |
| GPU-warming dead end | clock/pacing hypotheses were ruled out; classify the actual boot regime before comparing cost |
| ratio-metric dead end | use the conserved row-wise sum `esvo + shadow_visibility_wave` |
| voided flag run | flag-integrity guard; quoted `set "VAR="` clears are mandatory because presence-only gates treat whitespace as ON |
| streak-starved cells | round-robin sampling, which disambiguated streaks from config-dependent regime bias |
| resumed top-up | resume-scan, incremental artifacts, per-boot locks, and a run-wide binary hold |
| Sol validation | a metric is its implementation: compare against `regime_of.py`, not a hand-rederived proxy |

The final protocol discarded frames 1–4, classified LOW/HIGH first, used the conserved sum,
guarded every OFF leg, interleaved configs round-robin, resumed persisted work, and kept
the binary stable. It certified all four cost cells; see
[[Deep-Field-Mip-Accessor-Policy-2026-08]]'s Cost section.

## Config-dependent boot-regime bias (round-robin finding, 2026-08-09)

Round-robin sampling disambiguated the streak question: in the same time window,
`dda-off` drew **4L/3H** while `dda-on` drew **1L/11H**. The bias also drifted across
nights: `dda-on` was **11L/1H** yesterday and **1L/11H** tonight. This is a characterized
phenomenon, not a causal explanation for the boot regime.

Fingerprint calibration is COMPLETE: **22 boots, 21 distinct fingerprints**. The only repeat
is `af3ba039199ec6b6`, occurring **2×**, both LOW. DDA-off frame↔regime association is
**2/2 LOW and 2/2 HIGH**; composed-on produces one frame state in both regimes. The
sharpened hypothesis is a pairwise precedence between the ESVO/march dispatch and
`shadow_visibility_wave`. Sol found no dependency path in either direction, statically and
across the logged boots: the intended march→shadow semantic ordering is **not encoded in
graph topology**. BATCH-48 OUTCOME: the probe ran (8 boots) and FALSIFIED the pairwise-
precedence hypothesis — shadow precedes march in all 8 while regimes split 4/4. The regime's
input remains unidentified (next candidates: frame-sync group assignment, pipeline-cache
state). The missing semantic edge remains a design-hygiene gap, no longer the bistability
kill. (Edge COUNT was already a proven non-predictor; batch 48 killed the ORDER hypothesis
too — no schedule-derived predictor of the regime survives.)

## EFFORT POLICY (adopted 2026-08-09)

Use HIGH for GPU/Windows-boundary work, builds, driver watches, boot matrices,
synchronization, and artifact production. Use MEDIUM for bounded static analysis,
simulate-first modeling, formula derivation, and narrow code inspection. Promote MEDIUM to
HIGH—or move the mechanics to the controller—after the first process-launch or watch
failure. HIGH is a persistence/mechanical-execution tier, not an analysis-quality upgrade.
This policy is part of dispatch discipline for the measurement workflow.

## `cmd.exe` UNC-cwd stall fingerprint

Launching `cmd.exe /c` from a WSL-path current directory can enter the UNC fallback and
**hang silently**. The diagnostic fingerprint is: the invocation originates from a WSL
cwd rather than `/mnt/c`, the Windows-side command produces no useful progress, and the
same command proceeds when the caller first executes `cd /mnt/c`. This stalled the same
invocation three times in the batch-46 execution work.

For Windows-side builds and boots, change to `/mnt/c` before `cmd.exe /c`; do not accept a
silent wait or a superficial success line as evidence of execution. This is separate from
the build-log false-green trap: a build still requires checking for `ninja: build stopped`
and confirming the binary mtime advanced.

## Three-axis measurement tables (user directive, 2026-08-09)

Every certification/measurement table reports **performance** (the conserved-sum metric),
**bandwidth** (boot pool upload bytes + steady-state bytes/frame), and **efficiency**
(performance per byte moved) — or explicitly labels an axis UNINSTRUMENTED. Known gap at
adoption: the perf-CSV `boot_bytes_uploaded`/`steady_state_bytes_uploaded` columns read ZERO
on the brickmap benchmark path (its wholesale upload bypasses the instrumented
BatchedUploader/residency uploader); until that scene routes through residency, boot
bandwidth comes from the `[BrickDataHash] sizes: pool=` log line. The sweep driver's
results.tsv carries bootUploadB / ssBytesPerFrame per boot.
