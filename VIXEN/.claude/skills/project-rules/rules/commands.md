# Build & Test Commands

<rule id="commands" reiterate="task-relevant:building,testing">

## Build Environment — PREFER WINDOWS-SIDE (strong default, esp. GPU)

**Windows-native build/test is much faster than WSL** — the `/mnt/c` cross-mount is slow (a first WSL configure with `-DBUILD_TESTS=ON` is ~500s of FetchContent + provisioning; a large `git worktree remove` on `/mnt/c` can exceed 2 minutes). Windows-side uses the `vixen-ninja` preset with **sccache** (fast incremental).

**Strong default: build and test on the WINDOWS side.** For GPU / render / Vulkan work this is the strong recommendation, not just a preference. Fall back to WSL only when Windows-side is unavailable or a flow is genuinely WSL-only (e.g. Linux/GCC-specific validation via the `vixen-wsl` / `vixen-wsl-debug` presets = Mesa-Dozen real-GPU).

Windows-side invocation (presets: `vixen-ninja` build/config; templates in `VIXEN/temp/win_*.bat`). Because **WSL env vars don't reach a Windows `.exe`** and `cmake.exe` needs the MSVC env, drive it through a `.bat` via `cmd.exe /c`:
```bash
# Configure (first time / after CMake changes) — runs vcvars64 then cmake.exe --preset
cmd.exe /c "C:\\cpp\\VBVS--VIXEN\\VIXEN\\temp\\win_configure.bat"
# Build (vcvars64 + cmake.exe --build --preset vixen-ninja --target <targets>)
cmd.exe /c "C:\\cpp\\VBVS--VIXEN\\VIXEN\\temp\\win_build.bat"
```
Adapt/copy a `win_*.bat` when you need different targets. Set any `VIXEN_*` runtime env INSIDE the `.bat` (as `temp/run_debug_1440.bat` does), never as a bash `VAR=1 ./x.exe` prefix.

**Note:** a WSL `cmake --build` in this harness AUTO-BACKGROUNDS even without a background flag — overlapping builds of the SAME target race on the link output and truncate the binary. Build one target at a time and block on it. (See the watcher-polling note under Test Commands and the friction log.)

## Build Commands

### Incremental Build (Daily Workflow)
```bash
cmake --build build --config Debug --parallel 16
```

### Full Build with PDB Filter
```bash
cmake --build build --config Debug --parallel 16 2>&1 | grep -v "warning LNK4099"
```

### Single Library Build
```bash
cmake --build build --config Debug --target SVO --parallel 16
```

### Multiple Targets
```bash
cmake --build build --config Debug --target Core GaiaVoxelWorld SVO --parallel 16
```

### Configure (First Time / After CMake Changes)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
```

## Test Commands

### Run Specific Test
```bash
./build/libraries/SVO/tests/Debug/test_rebuild_hierarchy.exe --gtest_brief=1
```

### Run All Tests in Directory
```bash
cd build/libraries/SVO/tests/Debug && for test in test_*.exe; do ./$test --gtest_brief=1; done
```

## Watching long-running builds/tests — POLL ON AN INTERVAL, don't blind-wait

A background build/configure/render can run minutes. A blind `wait` that emits no output makes the agent go IDLE (a subagent's turn ends when it stops emitting tool calls) — and an idle waiter can silently stall and MISS the real completion nudge. Instead, **watch with an interval loop that writes readable progress to the output every N seconds**, so (a) the user sees live state and (b) the periodic output keeps the agent awake to catch the finish.

Rule of thumb — every ~15–30s while a long op runs, emit one status line (PID alive?, log tail, elapsed):
```bash
# poll a background build PID, print progress, exit when it finishes
BUILD_PID=<pid>; LOG=<build.log>; t=0
until ! kill -0 "$BUILD_PID" 2>/dev/null; do
  echo "[watch +${t}s] building… $(tail -1 "$LOG" 2>/dev/null)"
  sleep 15; t=$((t+15))
done
echo "[watch] DONE ($(tail -3 "$LOG"))"
```
Run this as a FOREGROUND command (it owns the turn until the op ends), or via the harness Monitor/until-loop pattern. Prefer this over `sleep 300` blind waits and over no-op polling. (Codified globally too — see `~/.claude/RTK.md` and the post-brainstorm-context-manager skill.)

## Build Times

| Type | Time |
|------|------|
| Full project | ~3-5 minutes |
| Single library | ~30-60 seconds |
| Incremental | ~10-30 seconds |
| Single test | ~2-3 seconds |

## Build Tips

✅ Use `--parallel 16` (all CPU cores)
✅ Build specific targets during development
✅ Filter PDB warnings with `grep -v`
❌ Don't clean unnecessarily
❌ Don't build all tests for one library

</rule>
