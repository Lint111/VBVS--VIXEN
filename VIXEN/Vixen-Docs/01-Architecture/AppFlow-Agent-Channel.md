# AppFlow Agent Channel

**Status:** R468 design and prototype, before retiring the editor script harness.

## Purpose

Give tests and local agents the same typed AppFlow path used by the editor UI, with a two-way
session channel for discovery, dispatch, observation, frame synchronization, and visual readback.
The editor remains the authority for AppFlow state and rendering. The channel is a transport
adapter around those existing owners.

R468's original capture result was amended for this lane: captures are native-resolution pixels
returned inline with a content hash and frame number; selection may be a screen rectangle, an
AppFlow selector, a typed scene/entity reference, or the full frame. Capture work is asynchronous
and tied to the awaited frame.

## Schema source

`VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h` is emitted from
`VIXEN/codegen/appflow-schemas/AppFlowReference.cs` by the existing `--appflow` generator and
already contains the action IDs and typed parameter declarations, selector patterns, key defaults,
state IDs, and data targets. `ViewNounId.g.h` is emitted from the same view schema and catalog.
Discovery serializes these generated tables at runtime; it does not maintain a second action list.
Wire action and noun IDs use the generated enum's numeric value, so adding or reordering actions
continues to be governed by the existing append-only AppFlow schema contract.

The JSON envelope is the fixed transport contract, while the app-specific catalog is generated
data. The channel routes generic methods and serializes the generated catalog; it does not maintain
a second hand-written action list.

## Transport and framing

The target transport is the launcher's local per-session native pipe. The assigned launcher branch
does not yet contain that host, so this prototype opens a TCP socket bound only to `127.0.0.1`.
Both transports use the same JSON-lines protocol and client API. The editor chooses an ephemeral
port per process and writes a descriptor containing that port and a random per-session token. On
POSIX the descriptor is mode `0600`; on Windows it inherits the current user's ACL. Each request
carries the token, and the server rejects an unauthenticated connection before processing methods.

Each request is one UTF-8 JSON object terminated by `\n`. `id` correlates a response, `token`
authenticates this editor session, `method` names a channel operation, and `params` carries its
typed arguments:

```json
{"id":12,"token":"<session-token>","method":"appflow.dispatch","params":{"selector":"layer-2-toggle"}}
```

A response carries the request id and either a result or an error:

```json
{"id":12,"ok":true,"result":{"dispatchResult":{"code":0,"name":"Ok"},"actionId":12,"frame":31}}
```

Requests are handled one per connection. A response can be deferred while an awaited frame or
readback completes; the connection remains open until the correlated `{id,ok,result}` envelope is
ready. Errors use `{id,ok:false,error}`. The current prototype retains events in a bounded
per-session buffer and returns them to `subscribe(after)`; it does not stream unsolicited messages.

## Operations

| Operation | Request data | Result |
|---|---|---|
| `appflow.list` | none | generated actions and parameter types, element selector patterns, chords, view nouns, flow states, data targets and current state |
| `appflow.dispatch` | exactly one generated action ID plus typed `params`, selector, or key chord; optional selected `readback` | typed `DispatchResult`, request correlation ID, target frame and undo/redo depths; with readback, the first presented frame's pixels |
| `appflow.read` | optional generated noun ID/name | current readable view values, flow state, undo/redo depth and frame |
| `appflow.subscribe` | last event sequence | buffered action and view-reconcile events after that sequence, plus latest sequence |
| `appflow.await_frame` | dispatch request ID or frame number | first completed/presented frame at or after the target |
| `appflow.pause` / `appflow.run` / `appflow.step` | bounded tick count for `step` | whether Update ticks are paused and the current/target frame |
| `appflow.readback` | selection and `png` or `raw` format; optional action request ID/frame fence | pixels inline as base64, 64-bit FNV-1a hash of native pixels, selection bounds, source frame, transfer size, and timing data |

`dispatch` calls `Dispatch`, `DispatchBySelector`, or `DispatchByKey` on the existing runtime.
It does not call handlers directly. `read` uses existing runtime state and generated readable data
targets. The event buffer records action and layer-mask reconciliation events after a frame; the
client polls it by sequence number. It does not introduce a second mutation or reconcile path.

Visual selections use native physical pixels of the source render target. `fullFrame` captures the
entire target; `screenRect` takes integer `x`, `y`, `width`, and `height`; `selector` resolves a live
RmlUi element ID (with optional `#`) to its bounds; `sceneInstance` accepts the typed
`{"type":"bodyInstance","index":N}` reference and projects the instance's conservative bounds
through the current camera. An unknown reference or off-target rectangle is an explicit error; no
resizing or downscaling occurs. The initial formats are PNG and raw RGBA8/BGRA8 pixels, with a
64-bit FNV-1a hash over the pixel bytes.

## Frame synchronization and readback

The editor polls its nonblocking loopback listener on the app frame thread in `PostTick`, after the
render/present step. AppFlow mutations therefore stay on the owning thread and use the existing
runtime, but socket polling, JSON parsing, and response work are part of frame-thread CPU cost.
Dispatch records the next presented-frame number. On the following Update/Render/PostTick cycle,
the editor applies the action through AppFlow, reconciles views, renders and presents; then
`await_frame(actionId)` completes for that frame.

Readback uses a three-slot Vulkan staging-buffer ring. The editor records image-to-buffer copies for
the awaited frame and selection, submits them on the graphics queue under the queue's submit mutex,
and never waits for GPU completion on the frame thread. Later frame ticks poll slot fences without
blocking. Mapping, crop, hashing and PNG encoding currently run on the app frame thread after a slot
completes, so the gate reports both readback round-trip latency and frame-thread CPU work. The ring
rejects a new capture when all slots are in flight rather than waiting for one. On devices that
require full-image transfers, it copies the native target and crops the selected region on CPU; the
result reports that fallback and the actual transfer byte count. The legacy blocking
`CaptureRenderTargetToPng` helper remains for unattended fixture captures and is not used by the
channel.

An MCP `appflow.readback` call may include an action/frame fence, allowing dispatch, first-frame
wait, selection copy, and pixels to be returned as one client operation. The separate
`appflow.await_frame` tool remains available when a caller wants to synchronize before another
read or mutation.

## MCP and client shape

The small client library owns descriptor loading, authentication, JSON-lines request IDs, and
response validation. The stdio MCP server supports modern MCP `2026-07-28` discovery and per-request
protocol metadata, plus the older `initialize` handshake. Modern discovery follows the
[MCP server discovery contract](https://github.com/modelcontextprotocol/modelcontextprotocol/blob/main/docs/specification/2026-07-28/server/discover.mdx).
It maps the six required tools directly onto the client:

- `appflow.list` → `appflow.list`
- `appflow.dispatch` → `dispatch`
- `appflow.read` → `read`
- `appflow.subscribe` → buffered event read
- `appflow.await_frame` → frame synchronization
- `appflow.readback` → selected visual readback

The MCP layer contains no action catalog or editor behavior. The same client can be used by a gate
without log parsing.

## Prior art

| System | Useful shape | Difference for AppFlow |
|---|---|---|
| CDP and Playwright | CDP uses a command/event protocol over WebSocket; Playwright adds page locators, actionability waits, and device-pixel screenshots. | The AppFlow channel follows the command/event and synchronization pattern, but exposes generated AppFlow IDs/selectors and the render target's native pixels instead of a browser DOM and viewport. [CDP protocol](https://chromedevtools.github.io/devtools-protocol/), [Playwright Page API](https://playwright.dev/docs/api/class-page) |
| Unreal Remote Control | Local HTTP/WebSocket JSON endpoints expose engine objects and property/function calls; presets scope exposed objects. | The AppFlow channel is session-scoped and local, and dispatches through AppFlow's guarded/undoable human path rather than exposing arbitrary object mutation. [Remote Control](https://dev.epicgames.com/documentation/unreal-engine/remote-control-for-unreal-engine), [WebSocket reference](https://dev.epicgames.com/documentation/unreal-engine/remote-control-api-websocket-reference-for-unreal-engine) |
| AltTester | An in-app Unity test agent exposes hierarchy/object queries and interaction commands through a client/server connection. | AppFlow uses its existing generated semantic actions and view nouns rather than mirroring a general Unity object tree. [AltTester Unity SDK](https://alttester.com/docs/sdk/latest/home.html), [commands](https://alttester.com/docs/sdk/2.3.1/pages/commands.html) |

## R6 proof and retirement boundary

The new channel gate asserts the running-editor state trail through MCP requests, with no log
parsing: mask `7 → 3 → 7 → 3` and typed dispatch results. It first makes the same four generated
parameter-up edits as the old gate, then checks undo/redo depths
`(4,0) → (5,0) → (4,1) → (5,0)` while driving toggle, undo, and redo through the generated action
catalog. The old log-based gate remains enabled alongside it until the channel gate passes.

The R463 latency probe dispatches the layer toggle, awaits its action frame, and reads the affected
instance region at native resolution. It reports the frame difference between the before/after
readbacks, end-to-end MCP tool time, editor readback round-trip time, submit CPU time, map/crop CPU
time, encode/hash CPU time, their cumulative frame-thread cost, transfer size, and image dimensions.
In the low-contention offscreen DZN run, the native target and projected instance bounds are
500×500. The action readback transferred 1,000,000 bytes and reported 3,062 µs submit, 9,180 µs
map/crop, 14,179 µs encode/hash, and 26,421 µs cumulative frame-thread CPU time; its MCP round
trip was 1.56 s. A separate 64×64 rectangle measured 483 µs frame-thread CPU time and a 474 ms MCP
round trip. This backend requires full-image transfers for that rectangle, so it also transferred
1,000,000 bytes. Full-frame readback measured 11,263 µs frame-thread CPU time and a 659 ms MCP
round trip for the same 1,000,000 bytes.

The low-contention toggle's awaited action readback at frame 20 had pixel hash
`6660846881099648129`; the next sample at frame 24 had hash `7384945432851532593`. The visual
change was therefore first observed five presented-frame numbers after the action frame, but
sequential asynchronous samples skipped frames 21–23, so the exact transition is bounded to after
frame 20 and by frame 24. This probe did not meet the first-presented-frame target used by R463;
report it as a renderer latency finding for R463/R465. `await_frame` itself returned the requested
presented frame. A full-frame readback must not introduce a queue idle or wait for GPU completion
on the frame thread.

The saved end-to-end transcript is from the full CTest run while another lane was rendering on the
same DZN device. The action readback at frame 21 had hash `6660846881099648129`; the next sample at
frame 25 had hash `7384945432851532593`. It reports editor readback round trips of 2.18 s for the
instance region, 5.01 s for the 64×64 selection, and 7.23 s for the full frame; the corresponding
cumulative frame-thread CPU costs were 23,053 µs, 512 µs, and 8,924 µs. Outer MCP round trips were
2.33 s, 5.18 s, and 11.11 s. This is a contention trace, not the isolated latency baseline; see
`reports/agentport-appflow-transcript.json`.

Do not delete the file-script harness in this lane. R468.4's later deletion consists of:

1. `VIXEN/application/editor/include/EditorApplication.h`: remove `ScriptedAction`,
   `scriptedActions_`, and `scriptParsed_`.
2. `VIXEN/application/editor/source/EditorApplication.cpp`: remove `ParseEditorScript`, the
   `VIXEN_EDITOR_SCRIPT` environment parse, and the scripted-action schedule/switch in `PreTick`.
3. `VIXEN/application/editor/CMakeLists.txt`: remove the `VIXEN_EDITOR_SCRIPT` assignment from
   `vixen_editor_capture_producer`; retain the independent capture-frame and capture-directory
   fixture controls.
4. Remove script environment assignments from `VIXEN/tools/measure-editor-latency.py`,
   `VIXEN/tools/run-vixen-windowed-captures.sh`, and `VIXEN/temp/run_editor_script.bat` (or retire
   that script-only runner if it has no remaining capture use).
5. Remove the log-parsing assertions/comments in
   `VIXEN/libraries/RenderGraph/tests/Nodes/test_editor_toggle_undo_capture.cpp` only after the new
   channel gate carries the same state properties; keep unrelated capture assertions if still
   needed.

`VIXEN_EDITOR_CAPTURE_FRAMES` and `VIXEN_EDITOR_CAPTURE_DIR` are separate capture-fixture controls;
they are not part of the `VIXEN_EDITOR_SCRIPT` retirement unless a replacement capture fixture
makes them redundant.
