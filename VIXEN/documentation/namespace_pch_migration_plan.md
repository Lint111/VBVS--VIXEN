# Namespace & PCH Migration Plan

Purpose
-------
This document describes the automated migration I will perform to standardize namespaces and precompiled-header (PCH) usage across the VIXEN repo, remove repeating/fragile includes, and run tests.

Scope
-----
- Targeted namespace standardization:
  - `SVO` -> `Vixen::SVO`
  - `GaiaVoxel` -> `Vixen::GaiaVoxel`

- PCH standardization:
  - Ensure libraries that opt-in to `USE_PRECOMPILED_HEADERS` have a `include/pch.h` file and that primary translation units include it as the first header.
  - Optionally add a small CMake guard to error if `USE_PRECOMPILED_HEADERS` is set but `include/pch.h` is missing.

- Include hygiene:
  - Replace fragile relative includes in tests and sources found during earlier audits.
  - Remove duplicated includes where present (i.e., repeated consecutive includes), and normalize to project `include/` layout.

Actions (high level)
--------------------
1. Repo scan for all occurrences of `namespace SVO`, `SVO::`, `namespace GaiaVoxel`, and `GaiaVoxel::` to produce a list of affected files (headers, sources, tests).
2. Create a safe, reversible migration plan:
   - Each file edit will be committed in two focused commits: one for `SVO` → `Vixen::SVO`, one for `GaiaVoxel` → `Vixen::GaiaVoxel`.
   - Keep changes minimal per file: update namespace declarations, corresponding closing comments, and references using `SVO::` or `GaiaVoxel::` to `Vixen::SVO::` / `Vixen::GaiaVoxel::`.
   - Update `using namespace` lines to the new qualifiers.
3. Ensure `include/pch.h` exists for each library where `target_precompile_headers` is invoked. If missing, create a minimal `pch.h` that includes commonly used headers (e.g., `<vector>`, `<string>`, `<memory>`, `<glm/glm.hpp>`) — only for libraries that already used PCH in CMake.
4. Normalize repeated includes in modified files (if the same header appears more than once in the include list, remove duplicates).
5. Build and run unit tests for affected libraries and integration tests:
   - Run `cmake --build build --config Debug --target <library>` for each changed library.
   - Run `ctest -R '<suite-name>'` or execute test binaries directly for fast failure detection.
6. If compilation or tests fail due to ambiguous symbol references, iterate with source-local fixes (small, mechanical): e.g., add `using` alias in local scope or qualify ambiguous types.

Risk and Rollback
-----------------
- Risk: Large-scale namespace refactor can break external code or scripts that expect the old namespace. Risk reduced because repo is self-contained and we will update all references inside.
- Rollback: Changes applied in focused commits; if issues appear, checkout the prior commit to revert a specific mapping. I will not push changes to remote without your instruction.

Deliverables
------------
- `documentation/namespace_pch_migration_plan.md` (this file)
- Two commits per namespace migration (one for `SVO`, one for `GaiaVoxel`) containing all code changes.
- Optional small `pch.h` files created only where missing but enabled in CMake.
- A follow-up report with build/test results and any manual fixes applied.

Request
-------
Proceed with the automated namespace and PCH migration described above?

If you want to change the mapping (e.g., prefer `Vixen::Voxel` instead of `Vixen::GaiaVoxel`) or scope (apply to additional namespaces), modify the plan now. Otherwise reply `Proceed` and I will start the code changes in the repo. 
