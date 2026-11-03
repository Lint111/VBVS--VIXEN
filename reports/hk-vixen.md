# hk-vixen report

Worktree: `/home/liory/projects/VBVS--VIXEN/.claude-worktrees/hkvixen`, branch `lane-hkvixen`, base `0d7cf275`. Tip `aa433a28`. Nothing pushed.

## Carried Haiku work (merge of `lane-hk-vixen`, commit `605bd3a2`)

- **T-5197 (cache codec stream close): FIXED.** Commit `323fd3c1` applied cleanly. Proof: `build/wsl-debug/libraries/CashSystem/tests/test_cache_codec` built through the queue (exit 0, 185/185) and ran through the test queue: `[  PASSED  ] 12 tests` (job `hk-vixen:cache-codec-test2`, exit 0).
- **T-5089 + T-5488 (FidelityFX tool PATH): FIXED (already equivalent on base).** Commit `1ecd3b5b` conflicted with base in `VIXEN/application/main/cmake/FidelityFXFsr3.cmake`. Base already sets `FFX_TOOL_PATH "${FFX_SHADER_TOOL_DIR}"` with no `$ENV{PATH}`, and that is the same effect as `1ecd3b5b`. The conflict was a branch with duplicated identical `if(WIN32)`/`else()` arms. I kept the base form. Proof: `grep ENV{PATH}` on the file returns nothing, and the configure of the preset (`vixen-wsl-debug`) passed with that file.

## Tasks

- **T-5215 (channel_vocabulary_check drift target): NOT-HAIKU.** Commit `24c6b293` is on `lane-channels`, not on this base. This base has no `config-schemas/ChannelVocabulary.cs`, no `VoxelChannelVocabulary.g.h` or `.glsl`, and no channel targets in `codegen/CMakeLists.txt`. Re-adding the two targets alone would point at missing inputs. Restoring the feature touches about 20 files (SVO headers, EditorDocumentModel, schema, shaders, codegen) and is generation machinery. Suggested tier: sonnet/opus lane that owns `lane-channels`, or land `24c6b293` from that lane.
  - Duplicate-hint list (about 40 ids on `VIXEN/codegen/CMakeLists.txt`): I checked the titles. None mentions channel vocabulary, so I cannot confirm any as the same issue. I did not mark any as DUPLICATE.
- **T-2630 (trailing whitespace in channel vocabulary GLSL): NOT-HAIKU (cross-repo, feature absent here).** The emitter is `Yeroket-Fantasy:Packages/com.yeroket.utility.kernel-framework/CodegenTool~/ChannelVocabularyCodegen.cs`, outside this worktree. At the pinned kernel `6e347432` the file has no lines with trailing spaces (`grep -n ' $'` empty). The VIXEN-side generated GLSL does not exist on this base, so there is nothing to measure here. Suggested: kernel lane, after T-5215 lands the feature.
- **T-2817 (short kernel SHA): FIXED, commit `aa433a28`.** `vixen_kernel_snapshot` now resolves a 4–39 digit hex SHA with `git rev-parse --disambiguate`. It fails with `names no object` when nothing matches and with `is ambiguous ... give more hex digits` when several objects match. Full SHAs take the old path unchanged.
  - Proof (throwaway repo of 700 empty commits in `.tmp/`, removed afterwards): 8-digit unique prefix → `cached pinned kernel snapshot: .../yeroket-<full sha>`, `RESULT` = full path. Prefix `326d` (measured collision) → refused as ambiguous. `deadbeef` → refused, names no object. Full SHA → `reusing pinned kernel snapshot`.
  - Real configure: `vixen-wsl-debug` configure (with `-DVIXEN_AUTO_PROVISION_WSL_VULKAN=OFF`) passed with the full pin `6e347432`, through the queue.
  - Not added: a committed regression test in `cmake/tests/`. The manual proof above is the evidence.
- **T-2908 (contact sheet row height): FIXED, commit `c37e2bfe`.** Cherry-picked `a33aa633` (originally on `lane-vimpl`, not landed), which applied cleanly. Measured on base: `sheet()` always wrote 4 rows of height, and it accepted incomplete rows. Proof: `python3 -I tools/tests/test_check_lightunify_sheet.py` on the fix → `Ran 3 tests, OK`. Same test against the base script → 2 errors and 1 failure.
- **T-5070 (stb floating tag): STALE.** `VIXEN/dependencies/CMakeLists.txt` pins `GIT_TAG 2b2e64bc4778df6a06fbea3a5dbe61f7a76ecf19` on this base. Not edited.

## SPT DISPOSITION

- CLOSE T-2908: FIXED by `c37e2bfe` (proof above).
- CLOSE T-2817: FIXED by `aa433a28` (proof above).
- CLOSE T-5197: FIXED by merge of `323fd3c1` (proof above).
- CLOSE T-5089, T-5488: FIXED by the merge (base already equivalent; grep proof above).
- CLOSE T-5070: STALE (pinned `2b2e64bc` on this base).
- KEEP OPEN T-5215, T-2630: NOT-HAIKU (reasons above).

## LANDABLE NOW

- `lane-hkvixen` tip `aa433a28` (VIXEN repo). Commits on top of base `0d7cf275`: merge `605bd3a2`, `c37e2bfe`, `aa433a28`.

## STOPs

- T-5215 and T-2630 need the channel-vocabulary feature (`lane-channels`, commit `24c6b293`) and a kernel lane respectively.

## CONSOLIDATION ISSUES

- `lane-hk-vixen` and `lane-vimpl` both hold commits that the base does not have. `lane-vimpl` holds `a33aa633` (T-2908 fix) that has never landed; the controller may want to land it through the vimpl lane instead of this cherry-pick.
- Configure/build from a worktree creates `build/wsl-debug` and `.tmp/fetch` in the worktree (both ignored). The first cold configure took about 4 minutes of queue wait plus the compile.
