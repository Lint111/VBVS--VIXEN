---
title: Graph-Derived Node Linkage — Spec
aliases: [Selective Node Linking Spec]
tags: [architecture, rendergraph, nodes, build, spec]
created: 2026-07-11
status: 📐 SPEC (approved approach, not yet planned into increments)
related:
  - "[[Graph-Derived-Node-Linkage-Direction-2026-07]]"
  - "[[RenderGraph-System]]"
  - "[[RT-Core-Optional-Acceleration-Spec-2026-07]]"
---

# Graph-Derived Node Linkage — Spec

Downstream of [[Graph-Derived-Node-Linkage-Direction-2026-07]]. That doc laid out the problem,
4 unevaluated candidate approaches, and open questions. This spec resolves those into one
chosen approach with concrete design, based on measurement (below) and the constraints already
established.

## 1. Measurement (resolves Direction §5 open question 2)

Checked 2026-07-11 by grepping `#include "Nodes/X.h"` across every `application/*/source/graph/Build*.cpp`
(the proxy for `AddNode<XNodeType>` usage, since a node type must be included to be named as a
template argument):

| Graph builder | Node headers included |
|---|---|
| `BuildRenderGraph.cpp` (main scene graph) | 41 |
| `BuildInstancingDemoGraph.cpp` | 19 |
| `BuildAutoSyncDemoGraph.cpp` | 16 |
| `BuildFanInDemoGraph.cpp` | 13 |
| `BuildUIGraph.cpp` | 10 |
| **Union across all of `VixenApp`'s graph builders** | **45 / 53** |
| **Total registered node types** | **53** |

`VixenApp` is currently the **only** real graph-building consumer in the tree (`application/editor`
has no graph builders of its own; the benchmark app was deleted this cycle — see
[[Worktree-Build-Artifact-Accumulation-Audit-2026-07]]). `RegisterAllNodes()` is also called
from `libraries/RenderGraph/tests/test_node_self_registration.cpp` and
`test_pass_group_node_smoke.cpp`, which legitimately want every node type registered (that's
what they're testing) — not a linkage-scoping target.

**Nodes never referenced by any current `VixenApp` graph builder (8):**
`AccelerationStructureNode`, `BoolOpNode`, `ConstantNode`, `InstanceBufferNode`,
`MultiDispatchNode`, `RayTracingPipelineNode`, `ShellRevalidateNode`, `TraceRaysNode`,
`VoxelAABBConverterNode`.

**Implication:** the *current* achievable win is modest (~15% of node object code, concentrated
in the RT-core cluster) because there is only one real consumer and it already uses most nodes.
The win is not primarily about today's binary — it is about **arresting future creep**: per
[[RT-Core-Optional-Acceleration-Spec-2026-07]], the RT-core cluster (`AccelerationStructureNode`,
`RayTracingPipelineNode`, `TraceRaysNode`) is deliberately dormant scaffolding for a scheduled
future epic, and every future node family that lands unused-by-default (exactly this pattern)
compounds the same way. **This spec must not delete or gate those nodes out of existence** —
they are intentional future-facing code, not dead code — it must make their *linkage* scoped to
whichever binary's graph actually instantiates them, once RT-Core-Optional-Acceleration wires
them into `BuildRenderGraph.cpp`.

This resolves Direction §5 Q2 in favor of a granularity that scales with *future* consumers
(headless tools, a lighter editor binary, family-scoped test binaries), not one over-engineered
for today's single-consumer reality.

## 2. Chosen approach: 4a-lite — per-node OBJECT libraries + generated per-app link manifest

Selects Direction §4a (build-time scan → generated per-app node set), scoped down to the
smallest version that satisfies every constraint in Direction §3, and explicitly **defers**
per-node object-file splitting's full generality in favor of reusing the *existing* codegen
trust already established by [[kernel-codegen-framework-direction]] rather than inventing a new
C++ AST scanner.

### 2.1 Why not 4b/4c/4d

- **4b (two-pass build + object-file diff):** adds a real second build pass. Direct tension
  with [[vixen-build-policy]]'s standing concern about build time/traffic; the information it
  extracts (which `AddNode<T>` instantiations exist) is available far more cheaply at the
  *source* level (constraint below) — no reason to pay for a second compile+link to learn it.
- **4c (per-app-scoped registration trait):** still needs per-node OBJECT libraries underneath
  to do anything with the trait once collected (shared prerequisite with 4a/4b per Direction §4
  closing paragraph) — so it's strictly more moving parts than 4a for no granularity gain.
- **4d (family-level static libs, hand-grouped):** rejected as the *primary* mechanism because
  it reintroduces a hand-maintained classification (which family does node X belong to?) that
  can drift exactly like the "central list" §9.2 eliminated — a node's family membership is not
  derivable from anything in the node's own declaration today. Kept as a **structural stepping
  stone** inside 4a below (see 2.3) because CMake's practical unit of "thing you can
  conditionally link" is more naturally a library than a single `.obj`, but the *grouping* used
  is generated, not hand-classified.

### 2.2 The manifest source: parse `AddNode<T>` call sites, not registration

A lightweight scan (regex/tokenizer — this does **not** need a full AST, see 2.4) over each
app's `source/graph/Build*.cpp` files for the pattern:

```
AddNode<XNodeType>(
```

extracts the exact set of node type names that TU references. This is strictly simpler than
parsing `#include` lines (which is what the measurement in §1 used as a proxy) because it reads
the actual call site, not an include that could in principle be unused. Run once per app target
at configure time (a `execute_process`-driven CMake script, or a tiny standalone tool — see
2.4), producing a generated file:

```cmake
# generated: build/<app>/generated/node_manifest.cmake
set(VIXEN_APP_USED_NODE_TYPES
    CameraNodeType DeviceNodeType WindowNodeType SwapChainNodeType ... # (N entries)
)
```

This is the **single generated artifact** — everything else (linkage, the build-time
verification check in §4) is derived from it, never hand-edited, matching the constraint from
the direction doc ("one source of intent, multiple tools consuming a manifest generated from
it").

### 2.3 Linkage: per-node OBJECT libraries, family-grouped at CMake configure time only

`libraries/RenderGraph/CMakeLists.txt` changes from one `RENDERGRAPH_NODE_SOURCES` list feeding
a single `RenderGraphNodes` STATIC target, to:

- Each node's `.cpp`/`.h`/config-header trio compiles into its own tiny OBJECT library target
  (`RenderGraphNode_Camera`, `RenderGraphNode_AccelerationStructure`, ... — 53 targets,
  generated by a `foreach` over the existing per-node source list, **not hand-authored** — this
  is a mechanical CMake loop over data that already exists in the current
  `RENDERGRAPH_NODE_SOURCES` list).
- `RenderGraphNodes` (the existing facade target apps link) becomes a thin wrapper that, per
  consuming app, links only the OBJECT libraries named in that app's generated
  `node_manifest.cmake` — via a new `vixen_link_used_nodes(<app-target>)` CMake function that
  reads the manifest and calls `target_link_libraries(<app-target> PRIVATE RenderGraphNode_<X> ...)`
  for each entry.
- **No whole-archive needed for the scoped set**, because each OBJECT library's single
  translation unit *is* what gets pulled in when named directly — whole-archive was only ever
  needed to defeat *static-library* stripping of an unreferenced member; naming an OBJECT
  library directly has no such stripping to defeat.
- Tooling/tests that need every node type (`test_node_self_registration.cpp`,
  `test_pass_group_node_smoke.cpp`) keep linking **all 53** OBJECT libraries directly — trivial,
  since the generated per-node target list already exists for this purpose; no manifest
  involved (they are not "an app with a graph," they are the guarantee that global registration
  still works, per Direction §3 constraint #6).

This keeps `RegisterAllNodes()` and the global self-registration manifest **completely
unchanged** — constraint #3 from the direction doc. The registry still describes "every node
type this process's linked object code contains"; what changes is which object code is
*present* in a given binary, decided by the CMake link step, not by the registration mechanism.

### 2.4 Manifest generation: reuse or standalone?

Direction §5 Q1 asked whether the existing kernel-codegen tool already has C++-source-scanning.
**Resolution needed at Inc-1 kickoff, not here** — but the scan needed is trivial enough
(single regex over a small, fixed set of `Build*.cpp` files, no template/macro expansion, no
type resolution) that a ~30-line CMake script (`file(STRINGS ...)` + regex) is very plausibly
sufficient and should be tried FIRST before reaching for the Yeroket kernel-codegen core. Only
escalate to a real tool if the regex approach proves fragile (e.g. multi-line `AddNode<...>`
calls, node types instantiated indirectly through a helper template) — record whichever is
chosen and why in the Inc-1 progress log, not here.

## 3. What "graph-scoped" means precisely (avoiding a foot-gun)

A node type is "used by app X" if and only if `AddNode<ThatNodeType>(...)` is a literal
call-site token sequence somewhere in one of app X's `source/graph/Build*.cpp` files. This is
**deliberately conservative and syntactic**, not semantic:

- A node type reachable only via a runtime string (`graph->AddNode("CameraNode", ...)`, the
  legacy overload) will NOT be detected — confirmed unused by any current app (§1), so this is
  currently a non-issue, but **must be a documented limitation**, not a silent gap: if that
  overload is ever reintroduced for a real app, its node types must ALSO get an explicit
  `AddNode<T>` call-site somewhere (even a throwaway one solely for manifest-detection purposes)
  or they will silently fail to link at runtime (`typeRegistry->Get<TNodeType>()` will find the
  type registered globally-but-not-linked-into-this-binary... actually: if the OBJECT library
  isn't linked, the node's `.cpp` isn't in the binary at all, so `Get<TNodeType>()` returns
  `nullptr` and `AddNode` throws `"Node type not registered"` at graph-build time — a **loud,
  immediate runtime failure**, not silent corruption. Still worth flagging in the Inc-1 plan as
  a "how would a developer debug this" scenario).
- Node types referenced only inside test files, benchmarks, or future/dormant call sites
  wrapped behind a not-yet-enabled `#ifdef`/runtime flag: **still detected and linked**, since
  detection is syntactic over the TU's compiled content, not over what code path executes at
  runtime. This is intentionally conservative in the safe direction — better to over-link a
  guarded-off node than under-link a live one.

## 4. Build-time verification (Direction §3 constraint #5)

Once the manifest exists and drives linkage, verification is nearly free: add a CMake/CTest
check (or a link-time assertion) that fails the build if a graph-builder TU is compiled into an
app target but that TU's own `AddNode<T>` usage isn't a subset of what got linked — in practice
this is automatically true by construction (§2.3 links exactly what §2.2 extracted from the
same files), so the "verification" is really just: **the manifest generation step and the link
step must read the exact same file list** (a single CMake variable,
`VIXEN_APP_GRAPH_SOURCES`, feeding both) — a structural guarantee, not a separate check to
maintain. Document this as the actual mechanism in the Inc-1 plan rather than building a
redundant verifier.

## 5. Scope boundaries for Inc-1 (not the full epic)

Per Direction §4 closing paragraph, the shared prerequisite across every candidate approach is
splitting node object code to finer-than-single-static-lib granularity. **Inc-1 should
implement exactly that prerequisite plus the simplest possible manifest/linkage loop for
`VixenApp` only** — proving the mechanism end-to-end on the one real consumer — and leave
`vixen_editor` (currently graph-builder-less) and any future headless/tools binary for a later
increment once one exists to prove against. See the companion plan doc for the concrete task
breakdown.

## 6. Non-goals (unchanged from direction doc)

- No node deletion. The RT-core cluster and any other currently-unused-by-`VixenApp` node stays
  fully buildable and testable; it simply won't be linked into `VixenApp`'s binary until
  `BuildRenderGraph.cpp` actually calls `AddNode<That>(...)`.
- No runtime/dynamic node loading (plugin DLLs) — still out of scope, per direction doc §6.
- No change to `RegisterAllNodes()`/the global self-registration manifest semantics.
