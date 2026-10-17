# Lane upscaleprofiles — R604 step d

## LANDABLE NOW

- VIXEN code commit: `ff4060aede7fca1fac3c206d0431949cc0f9f456` (`Add upscaler capability records and machine profiles`). It is based on `ad9fa0bdd8b0bf811cc07d9d15e6c2bb0253e074` and includes the forward merge `b546c3022fa0bf2397d45c54da929a3ed7c41779` from `origin/wave/authoring-convergence`.
- CapabilityGraph now stores build, runtime, qualification, and policy records and exposes them through a const query. Build facts report the current artifact's Native, Linear, and FSR 1 adapters; runtime facts include the selected adapter identity, supported and enabled features, and Vulkan format properties.
- Declared profiles cover the 8060S on Windows and Linux, RTX 3060 on Windows and Linux, AMD laptop, WSL2/Dozen, build worker, headless native runner, and Intel Arc. Each compile set retains `Upscaler.Native`; Dozen results carry the `Dozen` label.
- `BuildRenderGraph` reads the records before creating the live device, using a temporary CapabilityGraph populated from the same selected physical adapter. Canonical requests require a passing qualification; explicitly requested relaxed views may use an unqualified provider with a warning. Optional providers also have to be present in the current artifact's actual compiled-adapter list.
- Profile compile sets include future FSR 3.1.4, DLSS SR, and XeSS entries where appropriate. Their adapters are not present in this artifact, so the selector cannot choose them until their provider lanes add and build those modules.

## Verification

- Base SHA `ad9fa0bdd8b0bf811cc07d9d15e6c2bb0253e074`: fresh WSL Release/Ninja full build and focused CapabilityGraph tests passed; native Windows configure/build and focused tests passed. Logs are under [the evidence directory](/home/liory/codeman-cases/undertow/reports/upscaleprofiles-evidence/).
- Merged WSL Release build passed, followed by `test_vulkanresources_basic` rebuild. Final focused run passed 14/14 tests: 13 CapabilityGraph tests and `Application.UpscalerSelection`; the device-identity test was skipped by its existing runtime condition.
- Native Windows targets `VulkanResources`, `test_vulkanresources_basic`, and `vixen_lookdev_capture` built successfully after the merge and after the final test edit. Final focused run passed 13/13 CapabilityGraph tests; the same device-identity test was skipped.
- `Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically` passed on Dozen. The output has 32 PNGs; recursive `diff -qr` and SHA-256 manifest comparison against the wave reference both returned zero differences.
- Build, test, capture, and hash evidence: [upscaleprofiles-evidence](/home/liory/codeman-cases/undertow/reports/upscaleprofiles-evidence/).

## Recovery notes

- The initial native invocation through `cmd.exe` on WSL PATH failed with exit 127. Using `/mnt/c/Windows/System32/cmd.exe` and a quoted lane-local batch path recovered the route. `cmd.exe` still reports that the WSL UNC current directory is unsupported and defaults to the Windows directory; the lane scripts use absolute build paths and completed successfully. Matching tracked issues: T-2953 and T-4124.
- Native configure's default Yeroket root was absent; the supported `YEROKET_ROOT` override to the pinned snapshot allowed configure/build to proceed (T-4207/T-4174 family). The native and WSL builds used isolated FetchContent locations (tracked by T-2089 and its duplicate family).
- CMake regenerated `UiShaderSpirv.g.h` with Windows/WSL line-ending drift. Its normalized blob matched HEAD, and the worktree copy was restored; this is covered by T-2637.
- The native no-new-mutex declaration gate was skipped because `bash`/`python3` are unavailable in CMD. The same gate ran and passed during the WSL build. No target failure remained.

## STOPs

None.

## SPT DISPOSITION

- T-4586 — CLOSE. The four records, declarative profiles, read-only record query, selector integration, focused tests, WSL/native builds, and Dozen parity witness are complete. Evidence is linked above.

## CONSOLIDATION ISSUES

None proposed by this lane; the recovered invocation, cache, and generated-file issues already have matching SPT entries listed above.
