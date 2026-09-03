# viewrestore1 — report: the view-query contract, all HUD panels restored through it

Date: 2026-09-03
Lane: `lane-viewrestore` (engine worktree `vixen/engine/.claude-worktrees/viewrestore`, off
`wave/authoring-convergence`). NEVER pushed.

## Result

The retired monolithic HUD read is replaced by a **section-composition + predicate-filter query
contract** (`view_query.h`/`.cpp`). All four HUD panels — factions, events, inspect, and the building
inspector — are restored THROUGH it, with every lost field flowing from a live sim producer. The
every-frame-dead read (a single merged-blob version reject) is GONE: each section now decodes by its
OWN structural version, so a mismatch skips one section, never the frame. A round-trip test proves
partial fetch, per-section independent versioning, predicate filtering, and that the restored fields
survive with non-default values.

This was ruled (controller/owner) as "B extended into a view-query contract": keep undertow's live
per-section writers, make the VIXEN consumer a query over sections, filter row sections by predicate.
See [[view-query-contract]] and [[hud-wire-contract-severed]].

## The contract (`vixen/render/view_query.h`)

- **`QuerySpec`** — a panel declares the set of `ContainerSection`s it needs, plus an optional row
  `Predicate`. Named per-panel sets: `HudRootQuery`, `FactionsQuery`, `EventsQuery`, `InspectQuery`,
  `BuildingInspectorQuery(selectedId)`.
- **`Predicate`** — minimal grammar, one op (`EqU64`) over one named column against one value. Grows
  without touching call sites. First (only) case: `rowId == selectedBuildingId`.
- **`Query(container, spec)`** — fetches each requested section's UTVA span from the
  `ViewContainerReader`, decodes it with THAT section's own `ViewBlob` (own version), and — for row
  sections — filters rows by the predicate as they decode. Returns a plain-data `HudQueryResult`
  (RmlUi-free) the panels compose from. BuildingFacets/Power/Labor are joined by `rowId` into one
  `BuildingRow` per building.
- Mirrors the schema-catalogue derived queries that replaced the readmodel roots on the producer side
  (`core/src/Undertow.Authoring/Schema/SchemaJson.cs` `derivedQueries`): the view WIRE becomes
  queryable the way the content catalogue is.

The building inspector's "one selected building" is the predicate `rowId == selected` at fetch time
over the all-buildings wire — a query-builder answer, not a producer-side selection (the owner's
directive).

## How each section decodes independently (the fix for the every-frame reject)

Each undertow wire section already ships with its own structural version. I authored the VIXEN-side
`[View]` MIRRORS of those sections (`VIXEN/codegen/view-schemas/HudSections.cs`) so the existing
`--view-blob`/`--typed-accessor-cpp` emitters produce a C++ decode face per section. Each mirror
reproduces the undertow section's committed hash EXACTLY (verified by generation):

| Section | id | version | mirror struct |
|---|---|---|---|
| Hud root | 0 | `0x2EEF0434` | `UndertowHud` (tick/bodyCount/activeLens/activeLensCount) |
| HudFactions | 1 | `0xCC4C8C33` | `UndertowHudFactions` rows: name/grievance/focused/known/inLens/strengthBand/confidence/**recentEventAge**/**rowId** |
| HudEvents | 2 | `0xB2C48BDD` | `UndertowHudEvents` rows: kind/tick/perpName/victimName/rowId |
| HudInspect | 3 | `0x398FCD18` | `UndertowHudInspect`: selected/name/**maxGrievance**/**strength**/**topRelName**/**topRelSig**/cause |
| BuildingFacets | 6 | `0x657E4C79` | `UndertowBuildingFacets` rows: def/flags/owner/isOwnerViewer/rowId |
| BuildingPower | 7 | `0x302BE8A8` | `UndertowBuildingPower` rows: demand/generated/stored/net/connected/impact/rowId |
| BuildingLabor | 8 | `0xC9C17A25` | `UndertowBuildingLabor` rows: supply/need/needMet/rowId |

The version hash is `ViewVersionHash.Compute` over the OUTER struct name + each field's name|kind|
layout (nested struct names are NOT hashed), so naming each mirror after its section and matching the
wire's bool→int / EntityId→U64 widenings reproduces the exact hash. NO kernel-tool change and NO
undertow producer change was needed — the sections are already written every frame.

## Per-field restore table

| Lost field | Restored via | Producer (undertow, already live) |
|---|---|---|
| Faction recentEventAge | `HudFactions` section column + `HudFaction.recentEventAge` bind | `SimFrame.HudFactionView.RecentEventAge` |
| Faction rowId | `HudFactions` column (query result join key) | `HudFactionView.RowId` |
| Inspect max_grievance | `HudInspect` column + `Hud.inspectMaxGrievance` bind | `UndertowSim.BuildInspectRow` |
| Inspect strength | `HudInspect` column + `Hud.inspectStrength` bind | `BuildInspectRow` (`Strength(sel)`) |
| Inspect top_relationship name+sig | `HudInspect` columns + `Hud.inspectTopRelName/Sig` binds | `BuildInspectRow` (`_surfacing.Links`) |
| Building power (demand/generated/stored/net/connected/impact) | `BuildingPower` section → `BuildingInspectorIn` | `UndertowSim.Buildings.cs:BuildBuildingPowerRow` |
| Building labor (supply/need/needMet) | `BuildingLabor` section → `BuildingInspectorIn` | `BuildingLaborView` |

renderfeat's "no surviving source for the building inspector" verdict was **wrong**: the sim computes
this every frame. The building inspector VIEW (`BuildingInspectorView`, `BuildingInspectorIn`) also
already exists and is mounted — it was simply never fed. The query layer now feeds it.

## Which panels are now data-ready

- **Factions panel**: data-ready. `recentEventAge` (raw, capped 255) rides the wire; `HudFaction`
  gains a `recentEventAge` int bind alongside the derived `recentChanged` pulse.
- **Events panel**: data-ready (kind/tick, plus perp/victim carried).
- **Inspect panel**: data-ready with the RESTORED diplomacy detail (maxGrievance/strength/topRel);
  `Hud` gains `inspectMaxGrievance/Strength/TopRelName/TopRelSig` binds.
- **Building inspector**: data-ready — `main.cpp` now composes BuildingFacets+Power+Labor, predicate-
  filters to the selected building, and calls the (already-mounted) `PushBuildingInspector`.

## Round-trip evidence (gate)

`vixen/render/test_view_query.cpp` builds a real UTVC container of the six sections and asserts:
1. **Partial fetch** — a spec omitting the building sections leaves them empty while root/factions/
   events/inspect populate.
2. **Per-section independent version** — section 0 stamped with a WRONG version (`0xDEADBEEF`) is
   skipped alone; factions still decode. The monolithic reject is gone.
3. **Predicate filter** — `BuildingInspectorQuery(0xB002)` yields exactly that building, joined across
   facets/power/labor; a different id yields the other; a no-match / id-0 reads empty.
4. **Restored fields survive** — faction recentEventAge=3/255/40 + rowId; inspect maxGrievance=0.9,
   strength=12.5, topRel; building power net=-4/impact=2, labor supply/need/needMet.

Built + run locally (RmlUi symbols the decode never executes were stubbed for the standalone link):
`test_view_query: ALL PASS`. Wired as ctest `undertow_view_query` in `vixen/app/CMakeLists.txt`
(links RenderGraph for the ViewStore substrate).

## Drift + build status

- **0-drift**: Hud family (`--view/--view-blob/--view-markup/--view-writer/--typed-accessor-cpp`) and
  all 7 section blob+typed headers `--check` clean. `AppFlow.g.h` unchanged. `ViewNounId.g.h`
  regenerated (+4 nouns for the restored `Hud` inspect-detail fields; ids renumbered) and 0-drift.
- **Noun collision handled**: the section mirrors carry the same names as the catalogue's derived-
  query sections, which duplicated enumerators in `ViewNounId`. Fixed by adding the 7 mirrors to the
  `--exclude` list in BOTH `view_noun_enum` and `appflow` drift-guard args (they are decode faces,
  not nouns). Verified: no duplicate enumerators, both artifacts 0-drift.
- **C++ syntax-verified** (standalone `-fsyntax-only`, worktree headers): `view_query.cpp`,
  `view_container_reader.cpp`, `test_view_query.cpp`, `HudView.cpp` all clean. The full VIXEN link
  needs the Vulkan SDK (glslang) the environment lacks (same block hudwire hit) — the query layer +
  test were built and RUN standalone to prove the contract independently of that.

## Two-repo change set (IMPORTANT for the controller)

The lane spans two git repos:

- **Engine submodule** (worktree `lane-viewrestore`, committed there): `Hud.cs`, `HudSections.cs`
  (new), the 7 section blob/typed headers (new) + regen'd Hud family, `HudView.{h,cpp}`,
  `HudViewBridge.{h,cpp}`, `VulkanGraphApplication.{h,cpp}`, `HudFactionEventTypes.h`, `Hud.view.rml`,
  `ViewNounId.g.h`, `VIXEN/codegen/CMakeLists.txt` (the `--exclude` additions).
- **Superproject main checkout** (`/home/liory/Github/undertow`, NOT committed by this lane): the
  render host is here — `vixen/render/view_query.{h,cpp}` (new), `vixen/render/test_view_query.cpp`
  (new), `vixen/app/src/main.cpp` (the producer now queries + pushes all panels),
  `vixen/app/CMakeLists.txt` (adds `view_query.cpp` to the host + the ctest target), and the retirement
  of `vixen/render/hud_view.{cpp,h}` + `test_hud_view.cpp` (superseded by the query layer; nothing
  else referenced them). The controller ruling granted these consumer-side edits, but they live in the
  shared superproject checkout, so this lane left them UNCOMMITTED for a controller step to apply/
  commit. `git status` there shows the exact set.

## One producer-diff follow-up (undertow, NOT a blocker)

The building inspector's `ownerName` (owner faction display label) needs an EntityId→name lookup the
`BuildingFacets` section does not carry (it carries the owner's raw EntityId `owner`). Two clean
options, both small, for whoever owns the undertow producer:
  (a) add an `ownerName` string column to the `building_facets_view` derived query (join the entity
      name table, as `hud_events_view` already does for perp/victim), or
  (b) carry the selected building's owner name on the `HudInspect` row.
Until then the inspector shows `defId` + all power/labor telemetry (the brief's actual lost fields);
`ownerName` is left defaulted. Also: building SELECTION currently reuses the pick path's rowId
(`selectedRowId` in main.cpp); a dedicated building-pick/selection ABI would let a user pick a
building directly — a separate input-layer follow-up, not part of the lost-field restore.
