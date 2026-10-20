# wslroute lane report (vixen-wsl-native-route)

Branch `lane-wslroute`, on top of unlanded `lane-vnatconf` (which contains `lane-vixpaths`); `origin/wave/authoring-convergence` and `origin/main` are already contained.

## What changed

All five tasks were still needed on this base: no tracked WSL-side launcher existed (`git grep` for `vixen-native` / `/mnt/c/Windows/System32` found only lane reports), and a fresh native checkout had no `.win-native-toolchain` pins.

One script, `tools/vixen-native.sh`, with commands `cmd`, `checkout`, `sync`, `provision`, `prepare`, `build`, `bat`. Test: `Native.WslRouteLauncher` (`VIXEN/cmake/tests/test_vixen_native_launcher.sh`, registered in `VIXEN/tests/CMakeLists.txt`, UNIX only, label cpu), 13 checks against a fake `cmd.exe` and throwaway git repos.

- T-2952 and T-2953 (81efae9b): `find_cmd` tries PATH, `VIXEN_NATIVE_SYSTEM32`, then every mounted `Windows/System32`. `run_bat` runs the batch file by relative name from its own directory (works with spaces, never starts cmd.exe on a UNC directory), passes arguments through, forwards `UNDERTOW_ROOT`, `VIXEN_BINARY_DIR` and the documented `VIXEN_*` knobs via `WSLENV`, tees to a log and returns the batch exit code. `build.bat` header points to it.
- T-2648 and T-2703 (48479d9c, 560f10dd): `checkout` clones the lane branch to `$VIXEN_NATIVE_ROOT/<lane>` (default `/mnt/c/vixen-native`) when missing; `sync` fetches `refs/heads/<branch>` (a raw SHA is not advertised), fast-forwards, refuses uncommitted tracked changes or a diverged branch, and checks HEAD and tree hash against the lane tip. The lane name must be a plain directory name.
- T-2865 (8dc74455): `provision` (and `prepare`/`build`/`bat`) runs `build.bat provision` when `ninja-bin.txt` or `cmake-exe.txt` is missing and stops with the provisioner's exit code (20 elevation, 30 Vulkan decision).

## Evidence

MEASURED:
- Test through the queue: 13/13 pass (fake cmd.exe). CMake registration checked by a stub project: `ctest -R WslRouteLauncher` 1/1.
- With `PATH=/usr/bin:/bin`, `vixen-native.sh cmd` printed `/mnt/c/Windows/System32/cmd.exe`.
- Real `sync` created `/mnt/c/vixen-native/wslroute` (tree verified), and a second `sync` after a new commit fast-forwarded it (`git status` clean).
- Real `prepare` ran `build.bat provision` through cmd.exe: Ninja 1.13.1 installed into the checkout's tool cache, all six tools reported present, exit 0.
- Real `bat` probe: `UNDERTOW_ROOT` arrived as `\\wsl.localhost\Ubuntu\home\liory\projects\undertow`, `VIXEN_BINARY_DIR` as `C:\vixen-native\wslroute\build\fresh`, an argument with a space stayed one argument, and the batch exit code 3 was returned.

INFERRED / not run:
- No full native configure or build through the launcher: configure still needs `-DYEROKET_ROOT` (no env hook; see issues), so only provision and the interop path were run for real.
- The elevation stop (exit 20) is covered by the fake cmd.exe, not a real unelevated provisioning run.

## SPT DISPOSITION

- T-2865: CLOSE
- T-2952: CLOSE
- T-2953: CLOSE
- T-2648: CLOSE
- T-2703: CLOSE

## CONSOLIDATION ISSUES

- `build.bat configure` cannot pass `-DYEROKET_ROOT`; the launcher forwards `UNDERTOW_ROOT` only. Native configure still needs a preset override until `VIXEN/codegen/CMakeLists.txt` reads a `YEROKET_ROOT` environment variable (T-4148/T-4174).
- `/mnt/c/vixen-native/wslroute` (a clone plus a downloaded Ninja) was left in place for the next lane run; it is this lane's own directory.
- `provision` downloads tools into each lane checkout's `.win-native-toolchain`, so every new lane repeats the Ninja download; a shared download cache is not part of this change.
- This branch contains unlanded lane-vnatconf and lane-vixpaths commits; land those first.
