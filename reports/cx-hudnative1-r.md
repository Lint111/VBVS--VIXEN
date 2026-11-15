# cx-hudnative1-r

## STATUS

PASS. The row-79 native dependency defect is fixed in commit `a7b287e30d69`. Native Windows Release, WSL Release, RenderGraph CPU, and the native HUD capture all completed successfully. No HUD reference changed.

## IMPLEMENTATION

`VIXEN/cmake/VixenChecks.cmake` now owns the shared `vixen_codegen_native_dependency_path()` conversion. It preserves WSL paths for managed-tool command arguments while converting every native Ninja prerequisite through the existing WSL bridge mapping (with `wslpath -w` fallback).

`VIXEN/codegen/CMakeLists.txt` uses that helper for every recipe-services source/container input in `DEPENDS`; it no longer special-cases or records a WSL-spelled `RecipeElementKinds.cs` prerequisite in the Windows Ninja graph.

## RECOVERY / BASELINE

Base was landed d9 VIXEN `002a5f6b6adf53644e53ef947c16538440497411`; register row 79 identified the pre-fix failure. The first native Release build configured successfully but Ninja recorded the WSL dependency and stopped before compilation. After the fix, that missing-prerequisite failure did not recur.

The first fresh native build then exposed a separate pinned-tool provisioning issue at `codegen/tool/.complete`: the snapshot did not contain the documented `tools/ensure-unity-analyzer.sh`, and the analyzer stamp invocation had a malformed native/WSL path. The documented wrapper attempt exited 127. The supported queued equivalent, a Release `dotnet build` of `SDFNodeGenerator.csproj` with `-p:DeployUnityAnalyzer=true`, succeeded; the native build retry then passed.

The first WSL build was admitted and reached 980/1400 actions before the live queue's elastic host-CPU parser failed with `box-queue-core unexpected argument 'true'` (exit 70). The retry with `UT_QUEUE_HOST_CPU_WSL=0` passed. This is recorded as an incidental queue issue, not a product red.

## BUILD AND TEST WITNESS

- Native Windows Release: PASS, all targets built successfully. The fresh native graph contained 1393 Ninja actions; after analyzer provisioning, the retry completed the remaining 717 actions. The vx8 reference log reports 248 incremental actions/targets, so the counts are not directly comparable; this run's authoritative result is the native build summary `All targets built successfully.` Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794707884-build-cx-hudnative1-r:native-release-build-retry.log`.
- WSL Release: PASS, 421/421 remaining actions, including recipe-services and all codegen golden checks. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794706946-build-cx-hudnative1-r:wsl-release-build-retry.log`.
- RenderGraph CPU: PASS. CTest executed 1738 tests with 0 failures (22 disabled/skipped); the RenderGraph label was 320/320. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794707362-test-cx-hudnative1-r:rendergraph-cpu.log`.

The native configure again reported that its no-new-mutex validation was skipped because native `bash`/`python3` were unavailable. This did not block the requested build, and is recorded as a consolidation issue.

## HUD HASHES

Capture root: `/mnt/c/vixen-native/hudnative1/VIXEN/temp`. All images are 500x500, 8-bit RGB PNGs.

| Frame | Captured SHA-256 | vx8 Release reference | Result |
|---:|---|---|---|
| 5 | `754bcd8e7e3e433dfc42b5635283611d55c4b8821094699b19a70fb04c19a157` | `754bcd8e7e3e433dfc42b5635283611d55c4b8821094699b19a70fb04c19a157` | byte-identical |
| 45 | `7c4d3688aad930e0af47e93ce76bed92595255e4413c6be082fc5cde1b793801` | `7c4d3688aad930e0af47e93ce76bed92595255e4413c6be082fc5cde1b793801` | byte-identical |
| 75 | `cdb9b3932d7c516695690956cc6b9702667ade3a54c31427da31dfba761052a3` | `cdb9b3932d7c516695690956cc6b9702667ade3a54c31427da31dfba761052a3` | byte-identical |

## IMAGE DESCRIPTIONS

- Frame 5: dark indigo background; three central circular bodies, with the textured blue body in the middle. The lower-left `(no building)` panel lists empty owner/power/demand/status fields and circular supply indicators. The upper-right HUD reads `tick 0 · bodies 0 · ×1`, `Lens: None (0)`, with empty Factions and Events sections.
- Frame 45: the same three-body scene and lower-left panel. The upper-right HUD reads `tick 30 · bodies 3 · ×1`, `Lens: Logistics (4)`, shows faction `acme` with value `3.5`, and event `T1 raid`.
- Frame 75: the same scene and lower-left panel. The upper-right HUD reads `tick 60 · bodies 3 · ×1`, `Lens: Intel (1)`, shows faction `umbra` with value `0.2`, and an empty Events section.

## DIFF / ATTRIBUTION

No pixel diff was required: all three SHA-256 values are byte-identical to the vx8 native Release references, so differing bbox and max-channel delta are both not applicable (`bbox = none`, `max delta = 0`). There is no d9 lighting-profile, opcode-owner, or HUD-kit visual delta to attribute.

The successful native capture log is `/home/liory/.local/state/undertow/undertow-box-logs/1794709313-gpu-cx-hudnative1-r:native-hud-capture-retry.log`; its retained capture root is `/home/liory/.local/state/undertow/captures/cx-hudnative1/20261115T042153.2235894`.

## COPY-PASTE HUD ROUTE

```bash
bash /home/liory/.local/bin/with-test-lock.sh --agent cx-hudnative1 --resource gpu --gpu-benchmark --label cx-hudnative1-r:native-hud-capture --no-replay -- bash -lc 'cd /home/liory/projects/VBVS--VIXEN/.claude-worktrees/hudnative1; tools/vixen-native.sh --lane hudnative1 --branch lane-hudnative1 --log /home/liory/codeman-cases/undertow/reports/cx-hudnative1-r-evidence/native-hud-capture.log bat VIXEN/application/main/main_hud_capture.bat'
```

The batch route sets `VIXEN_HUD_SCRIPT=A@30,B@60`, captures frames `5,45,75`, uses offscreen capture, and exits after frame 85.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Windows native recipe-services codegen records a WSL dependency path
- proposed: Live queue elastic host-CPU parser rejects admitted VIXEN builds
- proposed: Pinned kernel snapshots omit the documented Unity analyzer provisioning wrapper
- proposed: Native VIXEN configure skips the no-new-mutex gate when bash or python3 is absent

## STOPs

None. The initial queue exit 79 was the documented benchmark quiet-window refusal; the same route passed after the competing GPU job ended. The analyzer provisioning and WSL queue reds were recovered and are recorded above.
