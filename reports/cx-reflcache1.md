# cx-reflcache1

STATUS: COMPLETE

Base SHA: `c012413c3a9bbfae26d715e79ccda6e410d29487`  
Final VIXEN SHA: `5829605f903027e93e0d79a035f0c1aa0eb78b38`  
Fix commit: `5829605f9` (`Fix reflection cache map ordering across platforms`)

## ROOT CAUSE

`SpirvReflectionData::descriptorSets` was an `std::unordered_map`. `ShaderReflectionCache::Deserialize` tried to reproduce the serialized iteration order with reverse insertion and rejected the payload when MSVC's order differed. That made `CorruptOrTruncatedPayloadIsRejected` reject a valid payload and made warm builds reflect again instead of restoring the cache. The relevant declaration is `VIXEN/libraries/ShaderManagement/include/SpirvReflectionData.h:189`; the old non-portable reconstruction was `VIXEN/libraries/ShaderManagement/src/ShaderReflectionCache.cpp:354-365` on the base.

Row 102's `CacheEnvelope` publish defect is a separate Windows reader/share-delete path. These tests use `ShaderCacheManager`/`AtomicWriteBytes`; the native failures were in reflection payload deserialization, so the row 102 fix was not applicable.

## CHANGE

- Changed `descriptorSets` to `std::map`, making serialized reflection and regenerated SDI order deterministic on libstdc++ and MSVC.
- Restored cache records directly into the ordered map and removed the unordered-map insertion-order heuristic/rejection.
- No generated artifacts or fixtures changed.

## WITNESS

- WSL base build: queued `cmake -S VIXEN --preset vixen-wsl && cmake --build build/wsl --target ShaderManagementTests`, PASS.
- WSL base focused CTest: both registered tests, PASS 2/2.
- Native first target-only build failed before compilation because `build/ninja` did not exist; log: `/home/liory/codeman-cases/undertow/reports/cx-reflcache1-evidence/native-baseline-build.log`.
- Recovery A, queued documented configure+build (`tools/vixen-native.sh ... build all vixen-ninja ShaderManagementTests`), PASS; log: `/home/liory/codeman-cases/undertow/reports/cx-reflcache1-evidence/native-baseline-configure-build.log`.
- Native base focused CTest reproduced Row 109: both tests failed; queue log: `/home/liory/.local/state/undertow/undertow-box-logs/1794805874-test-cx-reflcache1:native-baseline-reflection-tests.log`.
- WSL after focused CTest: PASS 2/2.
- Native after rebuild: `ShaderManagementTests`, PASS.
- Native after CTest `--repeat until-fail:20`: both tests PASS for 20 repeats each.
- WSL after CTest `--repeat until-fail:20`: both tests PASS for 20 repeats each.
- Native full affected executable CTest: 51/51 runnable PASS; 1 disabled test unchanged.
- WSL full affected executable CTest: 51/51 runnable PASS; 1 disabled test unchanged.
- Canonical-shape added-line scan and `git diff --check`: PASS.

## REDS

- Row 109 was reproduced on this base and fixed by this lane: native Windows `ShaderReflectionCacheTest.WarmBuildRestoresReflectionAndRegeneratesIdenticalSdi` and `ShaderReflectionCacheTest.CorruptOrTruncatedPayloadIsRejected`. Register evidence remains `/home/liory/codeman-cases/undertow/reports/base-reds.md`; published base evidence is `~/.local/state/undertow/undertow-box-logs/1794793412-test-cx-cacheatomic1:round2-base-seven.log`.

## STOPs

None.

## SPT DISPOSITION

No dispatched SPT task IDs were supplied. Proposed `de362f88df5a` is OPEN: fresh native target builds need an explicit configure prerequisite; evidence is the pre-compile failure and Recovery A above.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Native target builds need an explicit configure prerequisite on fresh lanes (`de362f88df5a`).
- existing, not re-proposed: first-run native Ninja/ccache provisioning (`T-6644`).
