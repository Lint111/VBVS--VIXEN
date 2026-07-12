---
title: Graph-Derived Node Linkage — Direction
aliases: [Selective Node Linking, Graph-Scoped Build]
tags: [architecture, rendergraph, nodes, build, direction, FUTURE]
created: 2026-07-11
status: ✅ INC1 SHIPPED (2026-07-12, branch `feat/graph-derived-node-linkage-inc1`, merged to main `09dafd3f`) — see [[Graph-Derived-Node-Linkage-Spec-2026-07]] and [[Graph-Derived-Node-Linkage-Inc1-Plan-2026-07]] for the chosen approach (4a-lite) and full milestone record. Epic continues: `vixen_editor`/headless-binary scoping deferred until such a binary has its own graph builder to prove against (see Spec §5).
related:
  - "[[RenderGraph-System]]"
  - "[[../04-Development/Worktree-Build-Artifact-Accumulation-Audit-2026-07]]"
---

# Graph-Derived Node Linkage — Direction

## 1. Problem

Every VIXEN app currently links **every** RenderGraph node type, regardless of what its own
graph-construction code (`BuildRenderGraph.cpp`, `BuildUIGraph.cpp`, the demo-graph builders)
actually uses. This is not an oversight — it is a deliberate consequence of the node
self-registration design (`RenderGraph-System.md` §9.2/§9.3):

- Each node `.cpp` self-registers via `VIXEN_REGISTER_NODE(...)`, a file-scope static
  initializer that appends to a global Meyers-singleton manifest at dynamic-init
  (`NodeRegistration.h`).
- Nothing in the TU *references* that static (it exists purely for its initialization
  side-effect), so a static-library linker's normal dead-code stripping removes it.
- `RenderGraph/CMakeLists.txt` therefore links `RenderGraphNodes` **whole-archive**
  (`/WHOLEARCHIVE`, `--whole-archive`) specifically to defeat that stripping — which means
  every consumer of the `RenderGraph` facade unconditionally pulls in all ~53 node `.cpp` files
  (several 30–50KB: `BodyOctreeSceneNode`, `DescriptorSetNode`,
  `DescriptorResourceGathererNode`, `SwapChainNode`, ...) whether or not the app's actual graph
  ever instantiates them.

This was the right call at the time — §9's whole point was decoupling *compile* granularity
(editing one node shouldn't recompile the world), and it succeeded at that. But it left *link*
granularity maximally coarse, and that coarseness scales with the number of node types VIXEN
ships, not with what any given app uses. As more capabilities land as nodes (which is the
established pattern — see the kernel-codegen, tiered-ESVO, sampled-lighting epics, all of which
add nodes), every app's binary grows regardless of whether that app's graph touches the new
node at all. This is the build-size-creep problem the user flagged.

**Not done today.** Confirmed by direct inspection (2026-07-11): `libraries/CMakeLists.txt`
adds all 12 libraries unconditionally, all are `STATIC`, and `RenderGraphNodes` is force-linked
whole-archive into the `RenderGraph` INTERFACE facade every consumer links. No CMake option,
node-registry mechanism, or graph-declared conditional linking exists anywhere in the tree.

## 2. The exploitable fact: the declaration site already exists

`RenderGraph::AddNode<TNodeType>(...)` (`RenderGraph.h:136-144`) is a **template function**.
Every place an app builds its graph — `BuildRenderGraph.cpp` (41 node headers), `BuildUIGraph.cpp`
(10), the three demo-graph builders (13–19 each) — already names every node type it uses, as a
template argument, at a fixed, greppable call site (`graph->AddNode<XNodeType>("instance-name")`).
This is not a new authoring burden to invent — it is the **existing, single source of truth**
for "what does this app's graph use." The legacy string-based `AddNode(typeName, ...)` overload
(`RenderGraph.h:152`) exists but is **unused by any real app graph builder** (confirmed via
grep, 2026-07-11) — it does not need to be a supported input to a derivation mechanism unless a
future caller reintroduces it.

The user's framing for this epic: **don't hand-author a second manifest that can drift from the
code** (that would just reintroduce the "central list" problem §9.2 deliberately eliminated).
Instead, **derive the linkage/verification manifest FROM the type declarations already present
in the `AddNode<T>` call sites** — one source of intent, multiple tools (linker, build-time
verifier) consuming a manifest generated from it, not hand-synced to it.

## 3. Constraints any solution must satisfy

1. **No manual manifest.** Adding a node to an app's graph must not require editing a second
   file to "register" it for linkage — that already happened once for compile-decoupling
   (§9.2's whole point) and should not be reintroduced for link-decoupling.
2. **No central node-family knowledge in the app.** An app author writes `AddNode<CameraNodeType>`
   and gets correct linkage; they should not need to know or declare which *library*/*family*
   `CameraNodeType` lives in.
3. **Must survive `RegisterAllNodes()`'s decoupling from any one graph.** `RegisterAllNodes`
   populates a *per-`EngineContext`* registry from the *global* self-registration manifest —
   it is intentionally graph-agnostic (a registry answers "what node types could this process
   use," not "what does this specific graph use"). A derivation mechanism must not break that:
   the registry stays global; only *linkage* (what object code ends up in the binary) becomes
   graph-scoped.
4. **Preserve the compile-granularity win.** Whatever mechanism scopes linkage must not
   reintroduce "editing one node recompiles many TUs" — the M2+M3 result (§9.5) is a hard-won
   invariant, not incidental.
5. **Must be statically verifiable as a build failure, not just a size optimization.** Per the
   user's stated secondary goal: if a graph builder TU references a node type that isn't in its
   declared/derived manifest (e.g. a stray include creates an accidental dependency), that
   should be a build error, not a silent link.
6. **Must not regress `test_node_self_registration`'s guarantee** — `RegisterAllNodes()` must
   still be able to report every node type the *process* could instantiate (useful for tooling,
   editor node palettes, etc.), even if a *specific app binary* only linked a subset.

## 4. Candidate approaches (unevaluated, for increment planning)

Recorded here as raw material for whoever plans Inc-1 — not a decision.

### 4a. Build-time scan → generated per-app node-set + selective static libs
A codegen step (parallel to the existing kernel-codegen tooling this repo already trusts —
`[[kernel-codegen-framework-direction]]`) scans each app's `Build*Graph.cpp` set for
`AddNode<...>(` template-argument occurrences (via `clang -Xclang -ast-dump`, a lightweight
regex/tokenizer, or reusing an existing C++ parse the codegen tool already has), emits a
generated `.g.cpp` translation unit per app that explicitly references exactly those node
types' registrar symbols (defeating stripping *without* whole-archive), and links each node's
object code as smaller per-node OBJECT libraries so only the referenced ones enter the binary.
Gives per-node granularity. Heaviest to build; needs the app's actual source tree at generate
time (a build-graph ordering question — codegen must run before compiling the app, after the
node headers exist).

### 4b. Compiler-enforced explicit instantiation manifest, generated from a first build pass
Two-pass build: pass 1 compiles the app's graph-builder TUs only, pass 2 diffs
`nm`/`dumpbin`-visible template instantiations of `AddNode<T>` against the global node registry
to produce a per-app allow-list, pass 3 re-links with only that allow-list's node objects
whole-archived (small per-family or per-node object libs), and asserts (build failure) if any
whole-archived node's registrar fires that isn't in the allow-list. Avoids writing a C++ parser
(reuses the object file itself as the source of truth) but adds a real second build pass —
tension with existing build-time concerns ([[vixen-build-policy]]).

### 4c. Header-driven manifest via a lightweight `AddNode<T>`-visitor macro trick
Redefine `AddNode` call sites to *also* emit a compile-time trait (e.g. via a
`template<> struct NodeUsedBy<AppTag, XNodeType> {};` specialization triggered from the same
call site, using a macro wrapper) collected via a linker-set / init-array pattern per app
binary — similar mechanism to `VIXEN_REGISTER_NODE` itself, but scoped per-app-tag instead of
globally, so the *existing* self-registration machinery gains an app-scoped variant instead of
being replaced. Smallest surface-area change (extends `NodeRegistration.h`'s existing pattern
rather than introducing new tooling), but only defeats stripping's *symptom*; still needs a
way to turn "used trait exists" into "corresponding node object file gets pulled into the
link" — likely still needs per-node OBJECT libraries underneath (shared prerequisite with 4a).

### 4d. Do nothing at the node-type granularity; split by node *family* instead
Coarser fallback if per-node granularity turns out not worth the tooling cost: group the ~53
nodes into a handful of static libs by capability family (e.g. `RenderGraphNodes-Core`,
`-UI`, `-RT/AccelStruct`, `-Voxel`), each still self-registering + whole-archived internally,
and apps link only the families their graph touches (a small, rarely-changing, hand-written
list — 4-8 lines, not 53). Much smaller build/tooling investment; catches the common case
(a headless/tools app that never touches RT or UI nodes) but not fine-grained creep within one
family (e.g. adding a new Voxel node still grows every app that links `-Voxel`).

All four share a prerequisite worth resolving first regardless of which is chosen: **node
object code needs to be linkable at finer granularity than the current single
`RenderGraphNodes` static lib** — either genuine per-node object libraries (4a/4b/4c) or
per-family static libs (4d). That prerequisite is probably Inc-1 regardless of the chosen
end-state, since it's low-risk (pure CMake reorganization, no new registration semantics) and
independently measurable (link-time/size before vs. after, even with nothing yet made
selective).

## 5. Open questions for whoever plans the increments

- Does the codegen tool (`[[kernel-codegen-framework-direction]]`'s Yeroket core) already have
  a C++-source-scanning capability that 4a/4b could reuse, or would this be the first consumer
  needing one?
- Is per-node object-file granularity (4a-style) worth the tooling cost vs. per-family (4d)?
  Needs real numbers: how much does a typical app's graph actually NOT use — if `BuildRenderGraph.cpp`
  alone already touches 41 of 53 node headers, the achievable win might be small for the main
  app and much larger for headless/tools binaries.
- Where does `RegisterAllNodes()`'s global "what could this process use" guarantee (constraint
  #6) get consumed today — editor tooling, node palettes? Needs enumerating before any approach
  risks breaking it.
- Should this be scoped to `VixenApp`/`vixen_editor` first, or would a headless/tools binary
  (smallest graph, cheapest to verify) make a better Inc-1 proving ground before touching the
  main app?

## 6. Non-goals (for now)

- Runtime/dynamic node loading (DLL-per-node-family, mod-loaded nodes) — a different problem
  (plugin architecture), not build-size scoping. Not ruled out for later, but out of scope here.
- Changing the *compile*-granularity model from §9 — that is already solved and this epic must
  not regress it (constraint #4).
