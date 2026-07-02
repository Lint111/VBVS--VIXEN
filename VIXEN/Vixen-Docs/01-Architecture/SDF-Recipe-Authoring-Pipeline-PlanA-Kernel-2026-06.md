# Recipe Authoring → Render — Plan A (Kernel/Yeroket: Container Format + Writer + C++ Reader Emitter) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`). This plan runs in the **Yeroket repo** (`/home/liory/Github/Yeroket-Fantasy`), not VIXEN. Load the `yeroket-kernel-framework` skill before starting.

**Goal:** Define a canonical, versioned **recipe container format** (header + `SDFInstruction[]` stream) with a C# **writer** and a **generated C++ reader** (`RecipeContainer.g.h`), so VIXEN (Plan B) can ingest pre-compiled arbitrary recipes from one source of truth.

**Architecture:** The container header struct + the `SDFInstruction` struct are the canonical C# source. A new source-gen emitter reflects them into a C++ header (structs + a generic `ReadRecipeContainer`), mirroring the existing `EmitSdfOpCodeEnum` (reflects the canonical enum) + `CppEmitter` (wrap → `AddSource`) + golden-test pattern. The C# writer blits the same canonical structs, so writer and reader cannot drift. The committed `.g.h` is vendored into VIXEN verbatim.

**Tech Stack:** C# (netstandard2.0 source-gen, net6.0 tests), Roslyn 4.3, `~/.dotnet/dotnet`, Unity.Mathematics (`float4`), GoogleTest (VIXEN cross-repo parity).

## Global Constraints

- **Spec of record:** VIXEN `Vixen-Docs/01-Architecture/SDF-Recipe-Authoring-Pipeline-Design-2026-06.md`. The **shared interface block** in Plan B defines the exact `RecipeContainerHeader` / `RecipeContainerView` / `ReadRecipeContainer` this plan must **Produce**, byte-for-byte.
- **Single source of truth:** the C# canonical structs drive BOTH the C# writer and the emitted C++ reader. No hand-written C++ reader; no hand-written second layout.
- **Source-gen DLL workflow:** editing `.cs` under `SourceGenerator~/` has NO effect until the DLL is rebuilt with **Linux dotnet** (`~/.dotnet/dotnet`, never the Windows one) and redeployed. Commit the DLL alongside source. Make a real content change to force Unity/consumers to re-emit.
- **Golden-file discipline:** generated artifacts are validated byte-identical by a test; update goldens only via `UPDATE_GOLDENS=1`. Committed artifacts are what VIXEN vendors.
- **Build/test:**
  ```bash
  cd /home/liory/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework/SourceGenerator~
  ~/.dotnet/dotnet build -c Release SDFNodeGenerator.csproj          # rebuild DLL (CopyToUnityAnalyzers deploys it)
  ~/.dotnet/dotnet test  -c Release Tests/SDFNodeGenerator.Tests.csproj   # golden validation
  UPDATE_GOLDENS=1 ~/.dotnet/dotnet test -c Release Tests/SDFNodeGenerator.Tests.csproj  # refresh goldens
  ```
- **Commit trailers** (every commit):
  ```
  Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01FyfX5aZWhF1kakkUE98u4c
  ```

## Key existing anchors (from exploration — verify before editing)

- Canonical `SDFInstruction` (132 B, `OpCode`+`InputMask`+`ParamMask`+`_pad1`+`float4 Data0..7`): `Packages/com.utility.graph-framework/Runtime/VM/SDFInstruction.cs:242-284`.
- Canonical `SDFOpCode` (151 members, append-only): `…/SDFInstruction.cs:14-236`.
- C++ emitter: `…/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/CppEmitter.cs` (`BuildCppHeader`, `WrapAsCSharpSource`, the `Yeroket::Sdf::Generated` namespace preamble).
- Emit entry + the **enum-reflection precedent** to mirror: `…/SourceGenerator~/SDFNodeGenerator.cs` — `EmitCppEmitter` (~line 1913) and `EmitSdfOpCodeEnum` (the existing reflect-canonical-C#-type → C++ `.g.h` pattern + its 3-stage golden test `RegeneratedSdfOpCodeEnum_MatchesCommittedArtifact`).
- Golden test fixture: `…/SourceGenerator~/Tests/CppEmitterTests.cs` (`RunAndFindGenerated`, `AssertBodyByteIdentical`, `UPDATE_GOLDENS`).
- Committed C++ artifacts dir (where `.g.h`/`.g.hpp` land before vendoring): `Packages/com.utility.sdf/Runtime/GPU/Generated/`.
- VIXEN vendor target (Plan B consumes): `…/VBVS--VIXEN/VIXEN/libraries/SVO/include/Recipe/generated/RecipeContainer.g.h`.

---

## Milestone Map (post-brainstorm-context-manager — segmented 2026-06-29)

Cross-repo plan; segmented at the **repo seam** so each milestone is single-repo / single-worktree.
Implementer = Sonnet, Validator = Opus, per milestone.

- [x] **M1 — Yeroket: produce + commit the generated reader** · Tasks **I1.1, I1.2, I1.3, I1.4 (Yeroket-side)**: canonical `RecipeContainer.cs` + C# writer → emit C++ structs → emit generic reader → rebuild DLL + generate/commit `RecipeContainer.g.h` + byte-identical golden + DLL. Worktree: Yeroket `.worktrees/recipe-container-format` (branch `feat/recipe-container-format`, off Yeroket `main`). Gate: `~/.dotnet/dotnet test -c Release Tests/SDFNodeGenerator.Tests.csproj` green. Deliverable: canonical container format + writer + emitted C++ reader, committed in Yeroket; `.g.h` on disk.
- [x] **M2 — VIXEN: vendor + consume + parity** · Tasks **I1.4 (VIXEN-side), I1.5, I1.6**: vendor the Yeroket-committed `.g.h` into VIXEN → retire VIXEN's hand-written `SdfInstruction` mirror (alias the generated struct) → cross-repo writer↔reader parity test. Worktree: VIXEN `.claude/worktrees/recipe-container-vixen` (branch `feat/recipe-container-vixen`, off VIXEN `main`). Gate: `cmake --build build-wsl` green + the `test_recipe_*` suite + new `test_recipe_container_parity` pass.

> **I1.4 is intentionally split at the repo seam** (justified exception to "never split a task"): its Yeroket half (produce/commit the artifact + DLL + golden) lands in M1; its VIXEN half (vendor the committed `.g.h`) opens M2. Each half is independently committable and testable; the handoff is the committed `.g.h` on disk in the Yeroket worktree.

### Progress Log
- Milestone M1 (Tasks I1.1–I1.4 Yeroket-side): DONE · commits `d010388c`..`551729ab` (I1.1 `d010388c`, I1.2+I1.3 `e6b0131c`, I1.4 `8660c3a9`; fix-loop B1 `18286cef`, B2 `551729ab`) · Opus validator APPROVED — 2 blockers caught + closed (SdfOpCode enum-golden regression from a test-DLL float4 export; frozen `SdfInstruction` mirror → now reflected via `sdfInstructionSym`) · suite 99 pass / 4 pre-existing fail · `RecipeContainer.g.h` byte-identical & committed · 2026-06-29
- Milestone M2 (Tasks I1.4 VIXEN-side, I1.5, I1.6): DONE · VIXEN commits `48198a0f` (vendor) / `9a2315a0` (retire hand-written mirror → alias generated struct) / `cde56258` (cross-repo parity test) + Yeroket `50b5ba16` (reproducible `sphere26.vrc` fixture dumper) · Opus validator APPROVED — parity test empirically failable (validator corrupted magic/count/radius/opcode → asserts trip), vendored `.g.h` byte-identical end-to-end, recipe suite green (bake 1/1, eval-parity 91/91, codegen 10/10, sdf_recipes 6/6), Yeroket suite no new failures · 2026-06-29
- ★ **Plan A (Increment I1) COMPLETE** — both milestones DONE + Opus-validated. Single-source chain intact: canonical C# → reflected emitter → `RecipeContainer.g.h` → VIXEN alias → C#-writer→C++-reader parity.
- ★ **MERGED + PUSHED to origin (2026-06-29):** VIXEN `main` `82c2707e` (Plan-A merge) + Yeroket `main` `50b5ba16`, both pushed to `origin/main`. Feature branches `feat/recipe-container-format` (Yeroket) / `feat/recipe-container-vixen` (VIXEN) merged + cleaned. Plan B (VIXEN I2–I4) followed — see [[SDF-Recipe-Authoring-Pipeline-PlanB-VIXEN-2026-06]].

---

## Increment I1 — Container format, writer, reader emitter, generated struct

### Task I1.1: Canonical container header + C# writer

**Files:**
- Create: `Packages/com.utility.graph-framework/Runtime/VM/RecipeContainer.cs`
- Test: `Packages/com.utility.graph-framework/Tests/` (or the VM tests asmdef) `RecipeContainerWriterTests.cs` — follow the package's existing NUnit test pattern.

**Interfaces:**
- Produces (C#):
  ```csharp
  [StructLayout(LayoutKind.Sequential)]
  public struct RecipeContainerHeader {
      public uint  Magic;            // 0x31435256 = 'VRC1'
      public uint  FormatVersion;    // 1
      public uint  InstructionCount;
      public uint  BakeResolution;   // 0 = engine default
      public float BandVoxels;       // 0 = engine default
      public uint  BrickDepth;       // 0 = engine default (3)
      public uint  Reserved0;        // 0
      public uint  Reserved1;        // 0
  }                                   // 32 bytes
  public static class RecipeContainer {
      public const uint Magic = 0x31435256u;
      public const uint Version = 1u;
      public static byte[] Serialize(SDFInstruction[] program,
          uint bakeResolution = 0, float bandVoxels = 0f, uint brickDepth = 0);
      public static bool TryGetHeader(byte[] blob, out RecipeContainerHeader header);
  }
  ```

- [ ] **Step 1: Write the failing test** — `Serialize` of a 1-instruction program yields `32 + 132` bytes; `TryGetHeader` round-trips magic/version/count/params; the instruction bytes after offset 32 equal a blit of the input.

```csharp
[Test] public void Serialize_RoundTripsHeaderAndInstructions() {
    var prog = new SDFInstruction[] { new SDFInstruction { OpCode = SDFOpCode.Sphere, Data0 = new float4(0,0,0,26f) } };
    byte[] blob = RecipeContainer.Serialize(prog, bakeResolution: 64, bandVoxels: 2.5f, brickDepth: 3);
    Assert.AreEqual(32 + 132, blob.Length);
    Assert.IsTrue(RecipeContainer.TryGetHeader(blob, out var h));
    Assert.AreEqual(RecipeContainer.Magic, h.Magic);
    Assert.AreEqual(1u, h.InstructionCount);
    Assert.AreEqual(64u, h.BakeResolution);
}
```

- [ ] **Step 2: Run** the package tests → FAIL (`RecipeContainer` missing). Use the package's test runner (Unity EditMode or the dotnet test if the VM asmdef has one — match the package convention).
- [ ] **Step 3: Implement** `RecipeContainer.cs` — `Serialize` writes the header (8×4 bytes, little-endian via `BitConverter`/`MemoryMarshal`) then blits each `SDFInstruction` (`MemoryMarshal.AsBytes(program.AsSpan())`); assert `Marshal.SizeOf<SDFInstruction>() == 132` and `Marshal.SizeOf<RecipeContainerHeader>() == 32` at the top of `Serialize` (guards layout drift).
- [ ] **Step 4: Run** → PASS.
- [ ] **Step 5: Commit** (in Yeroket repo) `feat(recipe): canonical RecipeContainer header + C# writer (single source of truth)`.

### Task I1.2: Emit the C++ structs (`SDFInstruction` + `RecipeContainerHeader`) from canonical C#

**Files:**
- Create: `…/SourceGenerator~/Transpiler/RecipeContainerEmitter.cs`
- Modify: `…/SourceGenerator~/SDFNodeGenerator.cs` (add an emit call alongside `EmitCppEmitter`/`EmitSdfOpCodeEnum`)
- Test: `…/SourceGenerator~/Tests/RecipeContainerEmitterTests.cs`

**Interfaces:**
- Produces: a `RecipeContainerEmitter.BuildHeader(INamedTypeSymbol sdfInstruction, INamedTypeSymbol header) -> string` that emits, into namespace `Yeroket::Sdf::Generated`, the C++ structs — reflecting the C# fields field-by-field (mirror `EmitSdfOpCodeEnum`'s reflection of the canonical enum). Target output (this is the EXACT C++ to produce for the struct section):

```cpp
// (inside namespace Yeroket::Sdf::Generated)
struct SdfInstruction {            // mirrors C# SDFInstruction (132 B)
    uint8_t opCode; uint8_t inputMask; uint8_t paramMask; uint8_t _pad1;
    float data[32];                // Data0..7 → 8 float4 = 32 floats
};
static_assert(sizeof(SdfInstruction) == 132, "SdfInstruction must be 132 B");

struct RecipeContainerHeader {     // mirrors C# RecipeContainerHeader (32 B)
    uint32_t magic; uint32_t formatVersion; uint32_t instructionCount;
    uint32_t bakeResolution; float bandVoxels; uint32_t brickDepth;
    uint32_t reserved0; uint32_t reserved1;
};
static_assert(sizeof(RecipeContainerHeader) == 32, "RecipeContainerHeader must be 32 B");
```

- [ ] **Step 1: Write the failing golden test** — mirror `CppEmitterTests`: feed framework stubs + the two canonical structs, run the generator, assert the generated text `StringAssert.Contains` the two `struct` decls + both `static_assert`s.
- [ ] **Step 2: Run** `~/.dotnet/dotnet test -c Release Tests/SDFNodeGenerator.Tests.csproj --filter RecipeContainerEmitter` → FAIL (emitter missing).
- [ ] **Step 3: Implement** `RecipeContainerEmitter.BuildHeader` — reflect the C# struct fields (map `byte→uint8_t`, `uint→uint32_t`, `float→float`, `float4 Data0..7 → float data[32]`); emit the two structs + static_asserts. Mirror exactly how `EmitSdfOpCodeEnum` locates the canonical type symbol and formats output.
- [ ] **Step 4: Run** → PASS.
- [ ] **Step 5: Commit** `feat(recipe): emit C++ SdfInstruction + RecipeContainerHeader structs from canonical C#`.

### Task I1.3: Emit the reader (`RecipeContainerView` + `ReadRecipeContainer`) → `RecipeContainer.g.h`

**Files:**
- Modify: `…/Transpiler/RecipeContainerEmitter.cs` (append the reader to the header text)
- Modify: `…/SDFNodeGenerator.cs` (emit `RecipeContainer.g.h.cs` via `WrapAsCSharpSource` + `ctx.AddSource`, like `SdfCoreKernels.g.hpp.cs`)
- Test: `…/Tests/RecipeContainerEmitterTests.cs` (add the reader assertions + a byte-identical golden once the artifact is committed in I1.4)

**Interfaces:**
- Produces: the full `RecipeContainer.g.h` body — the structs from I1.2 plus (this generic reader body is fixed; it depends only on the reflected struct names + `sizeof`):

```cpp
struct RecipeContainerView {
    RecipeContainerHeader header;
    const SdfInstruction* instructions;
};
inline bool ReadRecipeContainer(const uint8_t* blob, size_t len, RecipeContainerView& out) {
    if (!blob || len < sizeof(RecipeContainerHeader)) return false;
    RecipeContainerHeader h{};
    for (size_t i = 0; i < sizeof(RecipeContainerHeader); ++i)
        reinterpret_cast<uint8_t*>(&h)[i] = blob[i];
    if (h.magic != 0x31435256u) return false;          // 'VRC1'
    if (h.formatVersion != 1u) return false;
    const size_t need = sizeof(RecipeContainerHeader) + (size_t)h.instructionCount * sizeof(SdfInstruction);
    if (len != need) return false;
    out.header = h;
    out.instructions = reinterpret_cast<const SdfInstruction*>(blob + sizeof(RecipeContainerHeader));
    return true;
}
```

> The magic/version constants are emitted from the canonical C# `RecipeContainer.Magic`/`Version` (read the symbol's const value, like the enum emitter reads enumerators) — not hand-typed — so they stay single-sourced.

- [ ] **Step 1: Write the failing test** — generated text contains `RecipeContainerView`, `ReadRecipeContainer`, the magic check, and the `len != need` guard.
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement** — append the reader template to `BuildHeader`; wire `ctx.AddSource("RecipeContainer.g.h.cs", WrapAsCSharpSource("RecipeContainerHeaderText", text))` in the generator.
- [ ] **Step 4: Run** → PASS.
- [ ] **Step 5: Commit** `feat(recipe): emit generic C++ RecipeContainer reader (ReadRecipeContainer) from canonical schema`.

### Task I1.4: Rebuild DLL, extract + commit the `.g.h`, add the byte-identical golden, vendor to VIXEN

**Files:**
- Modify (build artifact): `…/RoslynAnalyzers/SDFNodeGenerator.dll` (rebuilt; commit it)
- Create (committed artifact): `Packages/com.utility.sdf/Runtime/GPU/Generated/RecipeContainer.g.h`
- Modify: `…/Tests/RecipeContainerEmitterTests.cs` — add `RegeneratedRecipeContainer_MatchesCommittedArtifact` (mirror `RegeneratedSdfOpCodeEnum_MatchesCommittedArtifact` + `AssertBodyByteIdentical`)
- Create (vendored into VIXEN, verbatim): `…/VBVS--VIXEN/VIXEN/libraries/SVO/include/Recipe/generated/RecipeContainer.g.h`

- [ ] **Step 1:** `~/.dotnet/dotnet build -c Release SDFNodeGenerator.csproj` (deploys the DLL).
- [ ] **Step 2:** Generate + extract the artifact: `UPDATE_GOLDENS=1 ~/.dotnet/dotnet test -c Release Tests/SDFNodeGenerator.Tests.csproj --filter RecipeContainer` (writes `RecipeContainer.g.h` to the committed Generated dir, following the enum-golden mechanism). Read it back; confirm it matches the I1.2/I1.3 target.
- [ ] **Step 3:** Re-run WITHOUT `UPDATE_GOLDENS` → the byte-identical golden test PASSES.
- [ ] **Step 4:** Vendor into VIXEN — copy the committed `RecipeContainer.g.h` to `VIXEN/libraries/SVO/include/Recipe/generated/RecipeContainer.g.h` (verbatim; include-guarded; namespace `Yeroket::Sdf::Generated`).
- [ ] **Step 5: Commit** in BOTH repos:
  - Yeroket: `feat(recipe): commit generated RecipeContainer.g.h + byte-identical golden; rebuild DLL`
  - VIXEN: `vendor(recipe): RecipeContainer.g.h (generated reader) into libraries/SVO/include/Recipe/generated`

### Task I1.5: Retire VIXEN's hand-written `SdfInstruction` mirror → use the generated struct

**Files:**
- Modify: `VIXEN/libraries/SVO/include/Recipe/SdfInstruction.h` — replace the hand-written 132-B struct with an include of the generated header + a `using Vixen::SVO::Recipe::SdfInstruction = Yeroket::Sdf::Generated::SdfInstruction;` alias (keep the existing namespace path so all VIXEN call sites compile unchanged).
- Test: rely on the existing `static_assert(sizeof==132)` + the full SVO test suite (no behavior change).

- [ ] **Step 1:** Make the edit (alias the generated struct under the existing `Vixen::SVO::Recipe::SdfInstruction` name).
- [ ] **Step 2:** Build VIXEN: `cmake --build build-wsl` → green (every recipe call site now resolves to the generated struct).
- [ ] **Step 3:** Run `test_recipe_bake`, `test_recipe_eval_parity`, `test_recipe_codegen`, `test_recipe_registry` → all PASS (proves the generated struct is layout-identical).
- [ ] **Step 4: Commit** (VIXEN) `refactor(recipe): VIXEN consumes the generated SdfInstruction struct (retire hand-written mirror)`.

### Task I1.6: Cross-repo writer↔reader parity test

**Files:**
- Create: `VIXEN/libraries/SVO/tests/test_recipe_container_parity.cpp` + CMake entry
- Create: `VIXEN/libraries/SVO/tests/fixtures/recipe-container/sphere26.vrc` — a blob written by the C# `RecipeContainer.Serialize` (generate it once with a tiny Yeroket-side script/test and commit the bytes).

**Interfaces:** consumes the generated `ReadRecipeContainer` (VIXEN-vendored) on a C#-authored blob — the end-to-end proof that the single-sourced writer and reader agree across repos.

- [ ] **Step 1: Write the failing test** — read `sphere26.vrc`, `ReadRecipeContainer` → true; header magic/version/count == expected; `instructions[0].opCode == Sphere`, `data[3] == 26.0f`.
- [ ] **Step 2: Run** → FAIL (no fixture / no test).
- [ ] **Step 3:** Generate the fixture from C# (`RecipeContainer.Serialize(new[]{ sphere(26) })`), commit it; implement the test.
- [ ] **Step 4: Run** → PASS.
- [ ] **Step 5: Commit** (VIXEN) `test(recipe): cross-repo parity — C# writer blob round-trips through the generated C++ reader`.

---

## Self-review (run before handing off)

- **Spec coverage:** I1.1 = container format + writer (spec §4.1 "the container is defined canonically… C# writer"); I1.2/I1.3 = the generated C++ reader (spec §4.1 "source-gen emits the C++ reader as a vendored .g.h"); I1.5 = "promote SdfInstruction from hand-mirror to generated" (spec §4.1); I1.4/I1.6 = vendoring + the byte-identical/parity guarantee (spec testing §5 "reader parity").
- **Produces matches Plan B Consumes:** `RecipeContainerHeader` (8 fields, 32 B), `RecipeContainerView`, `bool ReadRecipeContainer(const uint8_t*, size_t, RecipeContainerView&)`, namespace `Yeroket::Sdf::Generated` — identical to Plan B's shared-interface block. Magic `0x31435256`, version `1`, header 32 B, instruction 132 B consistent across both plans.
- **Single source of truth preserved:** structs reflected from canonical C#; magic/version read from C# consts; writer + reader both blit the same layout; no hand-written second mirror survives (I1.5 retires VIXEN's).
- **No placeholders:** the exact target `.g.h` C++ is specified verbatim; emitter steps reference the concrete precedent (`EmitSdfOpCodeEnum` + `CppEmitter` + `CppEmitterTests`) to mirror; build/regen/vendor commands are exact.
- **DLL workflow honored:** I1.4 rebuilds with `~/.dotnet/dotnet` and commits the DLL + artifact; golden test guards regressions.
