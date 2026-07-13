# Per-Dispatch-Call Fixed Overhead — Scoping Note (2026-07)

**Status:** NOT a plan — an unconfirmed hypothesis captured precisely, surfaced by Inc6's own honest result, for a future investigation if further DDGI frame-budget headroom is wanted.

## The finding that motivates this note

Inc6 (sparse-dispatch amortization, merged `origin/main` `1ff7deb4`) fixed the specific sub-linearity Inc5's own mechanism exhibited — dispatching only the active `probeCount/amortizationFactor` workgroups instead of dispatching all of them and early-out'ing inside most. This measurably helped (direction confirmed across three independent measurement sessions per Inc6 M2's own Progress Log) but did NOT fully close the gap to the naive 1/F cost prediction — the shortfall was roughly halved, not eliminated, per the implementer's own session's numbers (validator's own re-measurement confirmed direction, not the precise magnitude — see Inc6 M2's own Progress Log and gate artifact for the full honest accounting).

## The hypothesis, precisely stated (not yet confirmed)

**Claim**: some cost is fixed per `vkCmdDispatch` CALL — independent of how many workgroups that call launches — and this residual cost is what Inc6's mechanism could not touch, since it reduces workgroup COUNT but still issues exactly one dispatch call per frame regardless of `amortizationFactor`.

**Candidate contributors to this fixed per-call cost** (plausible, not individually measured or ranked):
- Descriptor-set binding (`ProbeGridConfig` SSBO, the light-tree buffer, the atlas images) — bound once per dispatch call, not scaled by workgroup count.
- Push-constant upload into the command buffer — once per call.
- Pipeline barrier/sync2 dependency-wait bookkeeping attached to this pass's entry — once per call, not per workgroup.
- GPU command-processor (front-end) fixed cost to decode and begin executing a dispatch command — largely workgroup-count-independent by GPU architecture.

**What this note does NOT claim**: that per-dispatch overhead is confirmed to be THE dominant remaining cost, or that its magnitude is known. It is the most plausible explanation for Inc6's own measured residual gap, consistent with Inc6's own mechanism (which specifically eliminated per-*workgroup* overhead and got a real, measured, partial win) — but no experiment has yet isolated per-dispatch-call cost from other candidate explanations (e.g. non-linear traversal-cost scaling per surviving workgroup due to cache locality or GPU occupancy/scheduling granularity effects unrelated to dispatch call overhead).

## Why this hasn't been (and can't easily be) directly measured yet

This is exactly the gap **KI-019** already names: no isolated-GPU-dispatch timing mechanism currently works in this codebase (`GPUQueryManager::ReadAllResults` never unblocks in the live render graph — confirmed non-functional since Inc1, still open as of this writing). Every cost measurement in this program to date (Inc1 through Inc6) has used the CPU FrameTimer full-frame-delta substitute — real, useful for A/B direction/magnitude at the whole-pass level, but structurally incapable of isolating "cost of issuing the dispatch call itself" from "cost of everything that runs inside it." Confirming or refuting this hypothesis needs either:
1. **KI-019 actually resolved** — real isolated GPU dispatch-boundary timestamps (`vkCmdWriteTimestamp` around just the dispatch call, or `VkPerformanceQueryKHR`-class GPU counters if the target hardware/driver supports them) would directly measure per-call fixed cost vs. per-workgroup scaling cost as separate terms.
2. **A synthetic isolating experiment** even without KI-019 resolved: dispatch a MINIMAL no-op shader (empty `main()`, no shading logic) at varying workgroup counts and varying dispatch-call counts independently — e.g. compare "1 dispatch of N workgroups" vs. "N dispatches of 1 workgroup each" at the same total workgroup count, using the SAME CPU FrameTimer methodology already established. If the fixed-per-call hypothesis is real, the "N separate dispatch calls" case should cost measurably more than "1 dispatch of N workgroups" even though total workgroup count is identical — a clean, achievable A/B without needing GPU-side timestamps at all.

Option 2 is the more immediately actionable path if this is worth investigating — it reuses entirely existing measurement infrastructure (the same FrameTimer A/B harness Inc1-6 already built and validated) and doesn't need KI-019 resolved first.

## Sequencing / when this matters

This is NOT scheduled and is explicitly optional — Inc4/Inc5/Inc6 together already moved DDGI's cost meaningfully in the right direction; whether to chase this further residual gap is a real prioritization question, not an obviously-correct next step. Worth investigating if:
- A future increment's own frame-budget accounting shows DDGI is still the binding constraint after Inc5+Inc6's combined improvement, AND
- The synthetic isolating experiment (option 2 above) is judged cheap enough to be worth running before committing to a specific fix (e.g. batching multiple probe-update-shaped dispatches into fewer calls, if the hypothesis is confirmed and a batching strategy is viable for this specific one-workgroup-per-probe shape).

## Related

- `Sampled-Lighting-Inc6-Sparse-Dispatch-Plan-2026-07.md` — the increment whose own honest M2 result motivated this note; full numbers and the validator's independent re-measurement caveat live there.
- `Sampled-Lighting-Inc5-Plan-2026-07.md` — KI-019's standing status (still open, CPU FrameTimer substitute used throughout).
- `DDGI-HWRT-Acceleration-And-MultiQueue-Direction-2026-07.md` — a separate, larger-scoped future direction (HW ray-query acceleration) that would also reduce DDGI cost, via a different mechanism (accelerating the ray-traversal work itself, not dispatch-call overhead) — worth knowing both exist as independent, non-competing levers for the same underlying problem.
