# R486 contract audit (before conversion)

This is source evidence, not a completed bounded-path or byte-parity witness.

## Hardware rays

`VIXEN/shaders/VoxelRT.rgen:40` defines the existing push-constant block.
The ray-generation pixel coordinate comes from `gl_LaunchIDEXT`, and the UV
denominator comes from `gl_LaunchSizeEXT` (lines 56–57). A tiled launch therefore
needs both a tile origin and the full image extent in that existing contract;
changing launch size alone changes rays. `TraceRaysNode.cpp:286` consumes the
existing push bytes, and line 326 records one full-image launch. Bindings, SBT,
camera fields, image transitions and final semaphore/fence ownership must survive
conversion. Reflection must supply the new member offsets; a parallel hardcoded
camera struct is unnecessary.

## Shell dilation and observable frontier

The public `ShellRevalidateNodeConfig.h:119` output contract explicitly exposes
SURFACE (bit 0), SHELL (bit 1) and FRONTIER (bit 2) in the flags SSBO. The shipped
GPU readback test (`tests/Nodes/test_shell_revalidate_node.cpp:364`) compares only
SURFACE and SHELL against `SVO/include/ShellDerive.h:407`'s canonical algorithm.
The production committed shell cache instead uses `DeriveShellPool`
(`BodyOctreeSceneNode.cpp:1487`) and `RevalidateShellBricks` (line 599).
No production consumer of this GPU flags output was found in the application or
RenderGraph source search; this does not revoke the exposed bit-2 contract.

`ShellDerive.comp:99` reads FRONTIER from the same flags that other invocations
update with atomic OR and clear with atomic AND (lines 124–127). Atomic individual
operations do not make the per-layer frontier immutable. The comment claiming a
double-buffered frontier does not match the implementation. Changing dispatch
partition/order can consequently change same-layer propagation and final bits.

The canonical CPU algorithm (`ShellDerive.h:489–527`) reads an immutable frontier,
writes a separate next frontier and merges it after each layer. It also normalizes
zero requested dilation to one layer, unlike the GPU host's direct layer loop.
A canonical conversion must distinguish full raw-flag parity from the current
two-bit classification test. It must retain a defined last-layer frontier if bit
2 remains exposed, and compare the committed shell data/lookup bytes separately.
Clearing FRONTIER and calling the masked test byte parity would be invalid.

## Indirect and grouped procedural work

`RecipeInstanceBucketing.comp:242–271` derives one three-uint indirect command per
bucket from that bucket's finished coverage rectangle. `DispatchPass.h:60`
already carries its buffer and byte offset. `MultiDispatchNode.cpp:599` uses the
indirect command in one command buffer; grouped passes share that same buffer.
Vulkan has no dispatch-base variant for an indirect command.

The existing specialized shader maps `gl_GlobalInvocationID.xy` to the bucket's
rectangle origin (`SpecializedRecipeShaderGlsl.h:264–267`). Its bounds check clips
to the full screen, while the indirect command rounds coverage up to whole 8×8
workgroups. A conversion must preserve that padded original invocation domain,
derive chunk counts/origins from the bucket/tile partition, retain empty-bucket
zero dispatches, and preserve explicit/automatic barriers and pass ordering.
Using a full-screen fixed grid without a matching bucket-domain guard would
introduce extra writes. Merely splitting submissions around an unchanged indirect
dispatch does not bound the dispatch.

The MultiDispatch timing wrapper currently writes its start query before
`RecordDispatches` begins the primary command buffer, and writes its end query
after that function ends it (`MultiDispatchNode.cpp:368–389`, `:522`, `:737`).
Those calls cannot provide a trustworthy per-pass timing inventory. Queries must
be recorded inside the command-buffer lifetime as part of the conversion.

## Conversion status in run 2

The ray-generation conversion extends this push block with reflected `uvec3`
`tileOrigin` and `fullLaunchExtent`. Every chunk retains the original global pixel
coordinates and full-image UV denominator. TraceRaysNode records separate primary
buffers, four timestamps per chunk, and uses the existing SubmitRecord channel
(first acquire, final completion/fence). This is a source-level contract argument;
it does not substitute for an AMD hardware-ray byte/timing witness.

MultiDispatch timestamp reset/start/end were moved inside its command-buffer
recording lifetime. Unknown estimates now reserve the configured frame budget,
including null/uncalibrated profiles, while mandatory passes remain accepted.
This repairs accounting and measurement but does not partition the indirect or
grouped dispatches. Those conversions remain outstanding.

The shell decision is narrower than run 1's missing-design STOP: the owner selected
the canonical derivation contract. The unresolved question is whether the exposed
raw FRONTIER bit must reproduce the current in-place implementation or receive the
canonical last-layer meaning. Source inspection cannot prove all-32-bit byte parity
for an order-dependent original. No shell bytes or masked golden expectations have
been changed.
