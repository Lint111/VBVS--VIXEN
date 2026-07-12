---
name: gaia-semantics
description: Use before writing or reviewing any code that touches Gaia ECS (world.h, query, components, mt/jobs, systems) in VIXEN. Answers "how does Gaia do X" by pointing at exact file:line in the vendored source instead of re-deriving semantics from scratch. Triggers on "gaia", "ecs::World", "query().all<", ".changed<", "func_set", "gaia job", "gaia thread pool", "gaia system", "parallel dispatch ecs".
---

# Gaia ECS Semantics — Doc-Access Map

## Overview

VIXEN vendors Gaia ECS v0.9.2 via CMake `FetchContent`, at:
`/mnt/c/vixen-fetchcontent-cache/gaia-src` (also mirrored read-only at
`/home/liory/.cache/vixen-fetchcontent-cache/gaia-src` — same content, either path works from
WSL). This is the **only** source of truth for Gaia's actual behavior — Gaia's own doc comments
are sparse in places, so when a question isn't answered below, go read the cited file:line
yourself rather than guessing from the API name. Every fact in this skill was verified by reading
the real function body, not inferred from a signature or comment — do the same before adding to
it.

**Why this skill exists**: every VIXEN increment that touches Gaia (Inc-B's `.changed<T>()`
reconcile, Inc-C's `IViewSelectionProvider`, Inc-D's `ActionStack`-over-ECS undo) has had to
re-derive some piece of Gaia's real semantics from scratch by reading source, because the wrong
assumption (e.g. "a query's iteration order is stable," "structural changes are safe under a
parallel `.each()`") causes a real, hard-to-spot bug. This skill is the accumulated map so that
lookup, not re-derivation, is the default.

## How to use this skill

1. Find the section below matching your question (World/Entity basics, Components & data access,
   Queries & filters, Change detection, Multithreading/Jobs, Systems).
2. Follow the file:line pointer and **read that code directly** — the line numbers here are a
   fast path to the right place, not a substitute for reading the actual body, since Gaia is a
   third-party dependency that can shift lines on an update (see "Keeping this skill fresh" below).
3. Check the "Do / Don't" table for your topic before writing code — most entries there are a
   real bug or a real design decision from a prior VIXEN increment, not a hypothetical.

## 1. World & entity basics

| Topic | File:line | Notes |
|---|---|---|
| `World` class | `include/gaia/ecs/world.h:46` (fwd decl), full definition later in the same file | The root ECS container. VIXEN wraps one instance per `GaiaVoxelWorld`. |
| `world.add()` / `world.add<T>(entity)` | `ecs/world.h` — grep `inline Entity World::add` / `add<T>` | Creates an entity, or attaches a component/tag to an existing one. |
| `world.del(entity)` | `ecs/world.h` — grep `inline void World::del` | Destroys an entity. **Any captured `Entity` handle must be re-validated after this can have run** — see §2 Do/Don't. |
| `world.valid(entity)` | `ecs/world.h` — grep `bool World::valid` | The liveness check. **This is the API to call, not a guess** — confirmed via real usage in `GaiaArchetypes/ArchetypeBuilder.cpp`, `RelationshipObserver.cpp`, and VIXEN's own `GaiaVoxelWorld::getComponentValue`/`setComponent`. |
| Structural-change lock counter | `ecs/world.h:146` (`m_structuralChangesLocked`), lock/unlock at `ecs/world.h:2278-2293` | **Plain `uint32_t`, not atomic.** Safe only under Gaia's single-orchestrating-thread model (see §5) — see Do/Don't. |

## 2. Components & data access (`GaiaVoxelWorld`'s own wrapping)

VIXEN doesn't call raw Gaia component APIs directly in most places — `GaiaVoxelWorld::getComponentValue<T>`/`setComponent<T>` wrap them (see `application/editor`/`libraries/GaiaVoxelWorld`). Confirmed (Inc-D Milestone 1, verified independently by both implementer and Opus validator): both wrapper methods internally guard on `world.valid(id)` before touching component storage, returning `nullopt`/no-op on a dead entity — **so a caller of the wrapper gets crash-safety for free**, but see the Do/Don't table for the one case that still needs a caller-side re-check.

| Do | Don't |
|---|---|
| Capture `(entity, value)` **by value** in any closure that outlives the current call (e.g. an undo/redo lambda) | Capture a raw pointer/reference into Gaia component storage across a frame boundary or across a possible structural change — a chunk relocation (tag add/remove) can invalidate it without warning |
| Re-check `world.valid(entity)` in any code that runs **later than dispatch time** (undo, redo, a deferred callback) | Assume an entity handle captured earlier is still live just because it was live when captured — this is the exact hazard Inc-D's Milestone 2 (undo-over-a-set) exists to close |
| Use `GaiaVoxelWorld::getComponentValue`/`setComponent` (or the equivalent already-wrapped wrapper for your component) | Call `chunk`-level or `component_cache_item`-level APIs directly from VIXEN code — those are Gaia-internal, wrapped for a reason |
| Add a **zero-field tag struct** (e.g. `Selected`, `Solid`) for a boolean/flag concept that's low-frequency to toggle | Add/remove a tag component on every-frame-changing state (e.g. mouse hover) — add/remove is a **structural change = archetype move = chunk relocation**, expensive and disruptive to other queries' `.changed<T>()` state; use a plain value field instead (see Inc-C's design doc §6 committed-vs-transient split) |

## 3. Queries & filters (`ecs/query.h`)

| Topic | File:line | Notes |
|---|---|---|
| `query().all<T>()` / `.any<T>()` / `.no<T>()` | `ecs/query.h` — grep the `QueryImpl` builder methods | Standard filter builder, chainable. |
| `.each(Func func)` | `ecs/query.h:1747` | Default = `QueryExecType::Serial` (main thread). See §5 for the parallel variant. |
| `QueryExecType` enum | `ecs/query.h:35` | `Serial` (default) / `Parallel` / `ParallelPerf` / `ParallelEff`. |
| `.arr()` | grep `QueryImpl::arr` in `ecs/query.h` | Materializes matches into a container. **Ordering guarantee**: relies on append-order iteration for an append-only archetype — VIXEN's own Inc-C verified this against Gaia's own vendored test suite (`src/test/src/main.cpp` ~line 4470) before depending on it. Don't assume stable order across a `.changed<T>()`-triggered re-run without re-checking this if Gaia's version changes. |
| `.changed<T>()` | grep `changed<` across `include/gaia/ecs/*.h` (not in `query.h` itself — check `query_common.h`/the filter-builder header) | **Chunk-granular, not per-entity.** A single changed entity marks the WHOLE chunk as changed. There is no per-entity diff and no old-value in the query result — confirmed by VIXEN's own View-Model-Binding design doc §4 (critic item 2). Requires a **persistent** `Query` object per view (a fresh query = version 0 = matches everything). |
| `.changed<T>()` const-assert | (Inc-B gotcha, carried from the View-Data-Provider-Seam design) | The reconcile's READ path must declare **immutable** access (`.all<T>()`, not `.all<T&>()`) or it trips Gaia's hard query-constness `GAIA_ASSERT`. |

| Do | Don't |
|---|---|
| Keep one **persistent** `Query` object per bound view/reconcile (create once, reuse every frame) | Rebuild a fresh `Query` every frame for change detection — a fresh query has last-seen-version 0, so it matches everything, defeating `.changed<T>()`'s whole purpose |
| Treat `.changed<T>()` as "this chunk had a write since I last checked," and re-push the current value | Treat `.changed<T>()` as a diff or an old/new value pair — Gaia doesn't give you one; if you need per-entity dirty tracking, you need your own last-seen cache (opt-in cost, not free) |

## 4. `func_set` hook (push-based change notification)

| Topic | File:line |
|---|---|
| The hook slot | `include/gaia/ecs/component_cache_item.h:70` (`FuncOnSet* func_set{}`) |
| Where it fires | `include/gaia/ecs/chunk.h:230-231, 262-263, 532-533, 553-554` — fires synchronously inside the chunk's own `set`/mutable-view-write path |

| Do | Don't |
|---|---|
| Treat `func_set` as an **opt-in immediacy signal** — set a dirty flag the per-frame reconcile consumes the same frame | Do RmlUi (or any non-reentrant) work **inside** the hook itself — it fires **synchronously mid-write**, re-entering the ECS or UI layer from inside it is a real reentrancy hazard (View-Model-Binding design doc §4) |
| Register through **one owning dispatcher** if multiple subsystems need the same component's hook | Let two subsystems both call the raw hook-registration API on the same component type — it's **one global slot per component type per World**; last-writer-wins on the raw slot silently breaks the other subscriber |

## 5. Multithreading / jobs (`include/gaia/mt/`)

**Confirmed 2026-07-12: VIXEN currently uses ZERO of this** (`gaia/mt/*` grep across the live VIXEN tree returns no hits) — everything below is a capability, not something already wired up. If you're the first to use it, update this line once it's no longer true.

| Topic | File:line | Notes |
|---|---|---|
| `ThreadPool` class | `include/gaia/mt/threadpool.h:73` | **Global singleton** — `ThreadPool::get()` at line 130. You cannot own a separate instance per subsystem. |
| Worker count / priority split | `threadpool.h:115-121` (ctor), `set_max_workers` at `154` | Spawns `hw_thread_cnt()-1` workers on first `get()`, split High/Low priority by perf-vs-efficiency core detection. Tunable at runtime; `set_max_workers(0,0)` disables workers (submissions run inline). |
| `hw_thread_cnt()` | `threadpool.h:563` | Hardware concurrency, min 1. |
| `add(TJob&&)` / `submit(JobHandle)` / `sched(Job&)` | `threadpool.h:292, 337/368, 393/405` | Fire-and-forget submission primitives. Most mutating `ThreadPool` calls assert `main_thread()` — this is built around one orchestrating thread, not multiple independent callers. |
| `dep(JobHandle, JobHandle)` | `threadpool.h:247` (single), `257` (span) | Wires an explicit dependency edge — **must be called before either job is submitted**; once submitted, a job's dependencies are frozen. This is a real DAG (job completion decrements a dependent's counter, auto-submits at zero), not flat fire-and-forget, but the graph is 100% hand-wired by the caller. |
| `sched_par(JobParallel&, itemsToProcess, groupSize)` | `threadpool.h:419` | The data-parallel "parallel-for" entry point — splits `itemsToProcess` into groups across workers; `groupSize=0` auto-computes. Returns a sync-job handle all group jobs depend on. |
| `wait(JobHandle)` | `threadpool.h:513` | **Cooperative fence** — the waiting thread executes other ready jobs (local queue → global queue → steal) while polling, only truly blocking (futex) as a last resort. Not a passive block. |
| `update()` | `threadpool.h:556` | Lets the calling thread help drain the global queues without waiting on a specific handle. |
| `JobHandle` bit layout | `include/gaia/mt/jobhandle.h` | Packed `IdBits=20 / GenBits=11 / PrioBits=1` — **hard ceiling ~2^20 (≈1,048,576) live job slots.** Relevant if ever bursting many small per-chunk jobs across several concurrent queries. |
| `MaxWorkers` ceiling | `JobState::DEP_BITS` = 27 (jobmanager.h) | Hard compile-time cap on total worker count, independent of the job-slot ceiling above. |

**Query integration — the part that matters most for VIXEN:**

`Query::each(func, QueryExecType::Parallel)` (`ecs/query.h:1747` + the `execType` overload) is a genuine, verified dispatch into this thread pool: `each` → `run_query_on_chunks<ExecType>` → builds one `mt::JobParallel` whose lambda processes a batch of matched chunks → `tp.sched_par(...)` → `tp.wait(...)`. The unit of parallelism is a **matched, filtered chunk batch** — filtering (`.all/.any/.no/.changed`) happens before batching, so it composes with everything in §3 for free.

| Do | Don't |
|---|---|
| Pass `QueryExecType::Parallel` (or `ParallelPerf`/`ParallelEff`) as `.each()`'s second argument when a query's per-chunk work is genuinely CPU-heavy and chunk count is large | Assume `.each(func, Parallel)` is async — `wait()` happens **inside** the call; it parallelizes the internal iteration, control does not return to the caller until every chunk is done |
| Route any structural change (`add`/`del`) issued from inside a parallel `.each()` lambda through Gaia's **deferred command-buffer** mechanism (`command_buffer.h`'s `AccessContextMT`, a real `mt::SpinLock`) | Call `world.add()`/`world.del()` directly from inside a `QueryExecType::Parallel` lambda — the world's structural-change lock counter (`m_structuralChangesLocked`, §1) is a **plain, non-atomic `uint32_t`**; concurrent direct structural changes from worker threads are not safe against it |
| Treat this as the tool for "N chunks of read-mostly/independent-write work" (e.g. a large voxel-field reconcile pass) | Reach for it on VIXEN's current small selections (Inc-C/D's 2-3 test entities) — the overhead of job dispatch isn't worth it below real chunk-count scale; this is a scaling tool, not a default |

## 6. Systems (`world.system()` / `SystemBuilder`, `ecs/system.inl`)

Gated by `GAIA_SYSTEMS_ENABLED` (on by default, `config.h`).

| Topic | File:line | Notes |
|---|---|---|
| `World::systems_run()` | `ecs/world.h:1971` (decl), `4657` (impl) | Iterates all registered `System` entities **serially, one after another on the calling thread** — each system's *internal* query can be `Parallel`, but systems don't run concurrently with each other automatically. |
| `World::update()` | `ecs/world.h` (grep `inline void World::update`) | `systems_run()` + `del_finalize()` + `gc()` + profiler marker — the natural integration point IF VIXEN adopts the systems concept (it currently doesn't; VIXEN calls `.each()`/`.arr()` directly from `ViewReconcileNode`, not via `world.system()`). |
| Manual system ordering | `DependsOn`/`ChildOf` relation pairs, wired by hand via `w.add(sys1.entity(), Pair{DependsOn, sys2})` | **No automatic read/write-set dependency analysis between systems** — ordering is either implicit entity-id order or explicit relation pairs the human sets up. |
| Cross-system job DAG escape hatch | `system.inl:81`, `317` (`job_handle()`) | Lets a human bypass `systems_run()`'s serial loop and build a real concurrent DAG via `tp.dep(job1.job_handle(), job2.job_handle())` + manual `submit`/`wait` — a hand-wired escape hatch, not something `World` does automatically. |

| Do | Don't |
|---|---|
| Treat `world.system()`/`systems_run()` as an alternative **entry point** to the same query/each machinery, not a different runtime | Expect "declare two systems" to auto-parallelize them against each other — they run serially by default; only their *internal* chunk iteration can be `Parallel` |

## 7. Combined-usage precedent

The **only** place in Gaia's own repo where `ecs::Query`/`System` and `mt::` are exercised together is the perf harness, `src/perf/mt/src/main.cpp:130-198` (`BM_Schedule_ECS_Simple`/`Complex`) — `world.system().all<T>().mode(QueryExecType::Parallel).on_each(...)`, then `w.update()` in a loop. Gaia's own unit-test suite (`src/test/src/main.cpp`, ~9900+ lines) tests the `mt::` module and the `ecs::` module in **complete isolation from each other** — zero test cases combine them. Treat the perf-harness snippet as the closest thing to a reference usage pattern; there is no broader precedent to lean on.

## Keeping this skill fresh

The vendored Gaia version is pinned via CMake `FetchContent` — check `_deps/gaia-src` HEAD (or the `FetchContent_Declare` tag in `VIXEN/CMakeLists.txt`) against what this doc was written against (**v0.9.2**, verified 2026-07-12) before trusting a line number verbatim after any Gaia version bump. If a cited file:line looks wrong, re-grep the symbol name in the current vendored source and correct the entry — don't just delete it, since the surrounding "why" (Do/Don't reasoning) usually still holds even if the exact line moved.
