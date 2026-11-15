# cx-gb1 — tile candidates

STATUS: STOPPED: the required kernel input merge stops on two Roslyn analyzer file conflicts. GB1 was not implemented and no product witness was run.

## SHAs

- VIXEN `lane-gb1`: `8c71234d4` — merged GB0 (`lane-gb0`, `e4f3db1e4`) and committed the conflict resolution.
- Yeroket-Fantasy `lane-gb1k`: `a2614dee4` — clean after aborting the unresolved input merge.
- Required kernel input: `lane-gbbin1`, `6a9c72a59084aef154214ca45b87832cee62b0fa`.

## What landed

The VIXEN worktree contains the supplied GB0 baseline: looped instance-count reporting, the located capacity warning, overview/tier scenario groundwork, B1 timers/counters, and the associated API conflict resolution. This is a prerequisite merge only; it is not claimed as a fresh build or capture result.

## Measurement table (design §4 row 1)

| Check | Before (GB0 base) | After (GB1) | Result |
|---|---|---|---|
| Native 1080p/720p, tiers 1–3, flythrough/fastpan/closeup/overview orbit; traversal and binning p50/p99 | Not run | Not run | Blocked before GB1 implementation |
| Load scenario looped instance count | GB0 source reports the looped count, including the 2,197-instance field | Not run | No executable witness |
| 192-cap removal and per-tile candidate-list traversal | Existing capped shader only | Not implemented | STOP |
| Look-dev goldens byte-identical | Not run | Not run | No captures |
| HUD frames 5/45/75 byte-identical | Not run | Not run | No captures |
| CPU/GPU CTest | Not run | Not run | No build |
| Image inspection | No new images generated | No new images generated | Nothing to open or describe |

## STOPs and recovery record

Required scope was the VIXEN march/shader and generated GPU-system consumer, the kernel GPU-system generator, fresh codegen, native build, CPU/GPU CTest, and the native Windows measurement/capture set. The kernel merge is a prerequisite for that scope.

The blocking command was:

```text
git merge --no-edit lane-gbbin1
```

It exited 1 in `Yeroket-Fantasy/.claude-worktrees/gb1k` with modify/delete conflicts in:

- `Packages/com.yeroket.utility.kernel-framework/RoslynAnalyzers/.SDFNodeGenerator.sources.sha256`
- `Packages/com.yeroket.utility.kernel-framework/RoslynAnalyzers/SDFNodeGenerator.dll`

`SourceGenerator~/Transpiler/GlslAstVisitor.cs` auto-merged and was not left unmerged. Per the brief, kernel conflicts are a STOP; the merge was aborted, leaving `lane-gb1k` clean at `a2614dee4`. The owner must resolve/regenerate the analyzer artifacts or provide a conflict-free merged kernel tip, then re-dispatch GB1.

Recovery A: located the actual kernel repository (`Yeroket-Fantasy`), ran `git fetch --all --prune` successfully, verified both supplied full SHAs, created the correct `gb1k` worktree from `lane-d11row`, and retried the required merge. Recovery B is not applicable: this is an input merge conflict, not stale generated output. Recovery C is not applicable: no supported narrower scope can validate a generated binning system without the required kernel merge.

No VIXEN build, codegen check, CTest, native capture, or image inspection was run after the STOP. CodeGraph was attempted first but no `.codegraph` index exists in the VIXEN worktree.

## LANDABLE NOW

- VIXEN `8c71234d4`: GB0 prerequisite merge and conflict resolution, committed cleanly. No green witness is claimed because the required kernel merge stopped before GB1 verification.

## SPT DISPOSITION

No SPT task id was supplied for this lane.

## CONSOLIDATION ISSUES

- proposed: Lane bootstrap should resolve the Yeroket kernel checkout for paired worktrees (`cfe110080ab4`)
