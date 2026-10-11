# gpuyield run 2 reproduction helpers

These are the exact lane-local helpers used or prepared for this run. Paths resolve from the assigned gpuyield worktree. Copy the helpers into `.tmp/gpuyield-run2/` before invoking them; the Windows batches intentionally map this worktree rather than writing to a separate Windows checkout. The generated build trees and executable binaries are not committed.

Every compilation, generation check and test must use the live global queue. The final driver witness requests all 15 physical CPU-pair tokens plus exclusive GPU admission because ordinary GPU CTest jobs currently bypass the device mutex:

```bash
bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource build \
  --label run2-integer-vector-reflection-build -- \
  cmake --build build/gpuyield --parallel 4

bash /home/liory/.local/bin/with-test-lock.sh --agent gpuyield --resource gpu \
  --credits 15 --exclusive --label run2-controlled-driver-witness-retry -- \
  bash .tmp/gpuyield-run2/editor-after.sh
```

The second command depends on the first succeeding. Its phases stop at the first failure: discover and execute seven CPU contract tests; validate the timing summarizer; run a 500-frame Dozen editor with a completion peer; compare seven windowed PNGs with the fresh merged references; measure the native AMD peer alone and during another 500-frame Dozen editor; refresh all 22 generation stamps; run ShaderManagement and RenderGraph/SVO suites once; and require a no-op aggregate build. Nested queue calls use the real wrapper's documented inherited-reservation behavior.

`full-suite.sh` selects ShaderManagement by its CTest directory because those tests have no ShaderManagement label. It accepts only the explicitly allowed RenderGraph/SVO opcode-94 failure signature. Fixture references for that phase come from run 1's original base; windowed references are fresh run 2 captures. Neither comparison certifies the dormant hardware-ray path.

Native configure recovery and the minimal `file(LOCK)` probe remain blocked on the assigned WSL share. The standalone MSVC probe is independent of that full-producer build. Failed and incomplete measurements remain evidence, with their status and invalid-row counts retained; they must not be reported as successful peers or completed producer witnesses.

The run reached its checkpoint during the final generation refresh. A lane-local `.tmp/gpuyield-run2/checkpoint-stop-before-suite` marker was prepared to defer the full-suite phase; remove that marker for a resumed witness. It is not included in these artifacts. The generation refresh itself was interrupted and the suites were not invoked.
