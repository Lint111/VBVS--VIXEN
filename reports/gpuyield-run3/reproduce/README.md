# Native and Linux witnesses

Run from the assigned Linux worktree. All build/test/probe payloads use the live box queue, agent `gpuyield`.

The Windows-local checkout was provisioned with:

```bash
bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource light --label clone-native -- \
  git clone --no-hardlinks --branch lane-gpuyield "$PWD" /mnt/c/vixen-native/gpuyield
```

`CMakeUserPresets.json` is copied into the owned native checkout's `VIXEN/` directory. The tracked pin `c3fb3847979e3f9eef04783510c42b70d7058ae7` is materialized into its private `.tmp/native-kernel-cache` by the documented configure entry point. The source kernel checkout is read only.

Native Git cannot resolve the primary WSL worktree's Linux `.git` pointer. `native-bounded.sh` therefore fetches through Linux Git into the NTFS clone, then `native-bounded.bat` fast-forwards with native Git and invokes the documented configure/build wrappers after `vcvars64.bat`.

The unbounded control began at `5da61a1897c40ae2f5b7affc1a44d594c4976d24`; portability commit `9cbba374` was cherry-picked into its detached worktree. Before the final controls it merged wave `4ddea47390880152d35371193e10c7ac722101d9`, producing `2e7dde20053f8d8329b745f99dd3693061f852ca`. Exact merge:

```powershell
& 'C:\Program Files\Git\cmd\git.exe' -C 'C:\vixen-native\gpuyield\.tmp\unbounded-base' -c user.name=lior -c user.email=liory7@gmail.com merge --no-edit 4ddea47390880152d35371193e10c7ac722101d9
```

Queued build/probe commands:

```bash
bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource build --label native-unbounded-merged-wave --credits 4 -- bash .tmp/gpuyield-run3/native-wave-base.sh
bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource build --label native-affine-cost-build --credits 4 -- bash .tmp/gpuyield-run3/native-bounded.sh
bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource gpu --label affine-native-and-dozen-driver-matrix --credits 15 --exclusive -- bash .tmp/gpuyield-run3/final-driver-matrix.sh
bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource gpu --label recovered-full-rendergraph-svo-shader-suites --credits 15 --exclusive -- bash .tmp/gpuyield-run3/full-suite.sh
```

Helpers in this directory retain the exact PowerShell process ownership, MSVC/CMake/SDK paths, native targets, scale, resolution, frame counts, hardware-query and bucketed flags. No tracked generated outputs were edited to clear checks.

Final recovery commands (runtime-only edits; generation inputs unchanged):

```bash
bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource build --label timer-and-query-reference-recovery-build --credits 4 -- cmake --build build/gpuyield --parallel 4
bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource build --label native-timer-and-tile-recovery --credits 4 -- bash .tmp/gpuyield-run3/native-bounded.sh
bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource gpu --label recovered-final-driver-and-full-suite --credits 15 --exclusive -- bash .tmp/gpuyield-run3/final-recovery-witness.sh
bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource build --label final-recovered-no-op --credits 1 -- cmake --build build/gpuyield --parallel 1
```

The unstarted four-token recovery wait and its dependent witness were canceled with SIGTERM (143). The unchanged four-worker native build was resubmitted with two pair-tokens:

```bash
bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource build --label native-timer-and-tile-recovery-two-pairs --credits 2 -- bash .tmp/gpuyield-run3/native-bounded.sh
bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource gpu --label recovered-final-driver-and-full-suite-two-pairs --credits 15 --exclusive -- bash .tmp/gpuyield-run3/final-recovery-witness.sh
```
