# rtlightfix1 — SDI reflector investigation

Date: 2026-09-02  
Lane: `rtlightfix`  
Branch: `lane-rtlightfix`

## Status

Implementation is present but verification is blocked by the shared build queue.
No commit was made; the lane is controller-gated per the brief.

## Root cause location and fix

The accepted controller ruling is that the reflector is consistent. In
`VIXEN/libraries/ShaderManagement/src/SpirvReflector.cpp`,
`SpirvReflector::ReflectDescriptors` uses the descriptor variable name when
SPIRV-Reflect provides one and otherwise falls back to the block type name.
The production declaration was an anonymous interface block, so its PascalCase
block label became the SDI key while the graph provider registered the member
identifier.

Applied source fix:

```glsl
layout(std430, set = 0, binding = 43) readonly buffer rtQueryProxyAabbs {
    ShellProxyAabb rtQueryProxyAabbs[];
};
```

This is a metadata-only rename. The block remains anonymous, so GLSL member
access is still unqualified and unchanged. The reflector was intentionally not
modified, and no first-member derivation rule was added.

## Regression seam

The existing enabled seam is
`VIXEN/libraries/ShaderManagement/tests/test_spirv_access.cpp`. The pre-existing
uncommitted test `SpirvAccess.UsesBufferBlockInstanceNameWhenDeclared` is
preserved and pins a genuinely two-name declaration (`NamedBlock` plus
`namedInstance`) to the instance-name key. It was not run because the target
build never started.

## Generated SDI-key diff accounting

Regeneration was not reached. The committed generated headers therefore remain
unchanged, including:

- `ShadowVisibilityWave-SDI.g.h`, binding 40: `rtQueryTlas`
- `ShadowVisibilityWave-SDI.g.h`, binding 43: the old block label
- `BodyInstanceRayMarch-SDI.g.h`, binding 43: the old block label

Expected regeneration from the one shared shader declaration is exactly one
logical binding-key change per affected generated interface: binding 43 changes
from the old block label to `rtQueryProxyAabbs`; binding 40 remains
`rtQueryTlas`. A full diff of all `VIXEN/generated/sdi/merged/*-SDI.g.h` files
was not possible until `sdi_tool merge-variants` can run. No generated header
was hand-edited.

## Literal consumer audit

After the source rename, the old block label has no non-generated code consumer.
The remaining matches are the stale generated headers and this historical report
until regeneration replaces the headers. `BuildRenderGraph.cpp` already uses
the provider name `rtQueryProxyAabbs`.

## Compile evidence

- The lane-local `build-rtlightfix` directory was already configured.
- The required queue wrapper was `/home/liory/.local/bin/with-test-lock.sh`;
  `tools/with-test-lock.sh` is absent in this Vixen worktree.
- `sdi_tool` was queued through the wrapper as:
  `cmake --build build-rtlightfix --target sdi_tool --parallel 8`.
- Queue status showed both build slots held continuously by
  `wslgfix:accept-build` and `h20fix:wizard-check-after`, with CPU-active
  processes. The request waited about 11 minutes without reaching CMake and
  was stopped with exit 130. It is a non-run, not a compile failure.
- Consequently `sdi_tool` regeneration, the targeted ShaderManagement/
  RenderGraph compile, and the regression test are not claimed.
- No SVO target or SVO generated header was touched or built.

## Not delivered vs brief

- Generated SDI regeneration and complete zero-unexplained-key diff: blocked by
  queue admission.
- Targeted SDI-consuming compile and regression test execution: blocked by the
  same queue admission.
- Commit: intentionally not made because the generated artifacts and required
  verification remain outstanding.

## Documentation consulted

- `VIXEN/Vixen-Docs/00-Index/Quick-Lookup.md`
- `VIXEN/Vixen-Docs/02-Implementation/Shaders.md`
- `VIXEN/memory-bank/systemPatterns.md` (split SDI architecture and generated
  header locations)

No existing documentation contradicted the accepted naming ruling; no broader
documentation amendment was needed.
