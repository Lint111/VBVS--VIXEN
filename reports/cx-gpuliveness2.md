# cx-gpuliveness2

STATUS: PASS

Base/source tip: `f6b49c61ec74` (`lane-gpuliveness2`). The native mirror was synchronized to the same tip. This lane added no product-source change; it completed the GPU resource-liveness witness for the already landed `f4cb6ea91`/ `f6b49c61e` implementation.

Evidence roots:

- Lane: `/mnt/c/vixen-native/gpuliveness2-evidence/`
- Published baseline: `/mnt/c/vixen-native/gpumem1-evidence/`
- Analysis helper: `/home/liory/codeman-cases/undertow/reports/sn-gpumem1-evidence/analyze_ledger.py`

## HASHES

All runs used the plain GPU queue resource, with no timing or benchmark flag.

| config | resolution | baseline | lane | ten frame hashes |
|---|---:|---|---|---|
| default, no overrides | 500x500 | `d-500-c1` | `d-500-default` | byte-identical |
| default, no overrides | 1920x1080 | `d-1080-c1` | `d-1080-default` | byte-identical |
| feature-on (accumulation, reservoirs, DDGI, photon/hit accumulation) | 500x500 | `fm-500-c1` | `fm-500-feature` | byte-identical |

Hash vectors, in frame order 59, 179, 239, 299, 419, 539, 599, 689, 779, 869:

- default 500: `2917d63ffb871eae c101769c704073f2 f9df288be317897e a8a95482258e5c6c 3e2bfba2c2f53bf1 d633f03193f7c46a f2f7be32763ac1e8 433d6da7214a185d 1c01273e3cc3f5ab 7d02cba60e78ed0c`
- default 1080: `e4c13bd6aef2a9f3 3f0e51700a5fd18b 15edcdac48846e53 1ff7c3f04d115054 e6be7a74fd4bbc9c fea063208087fb10 c0f0fcca2bee1913 f0edae8ad6a821ca 5b865dc1dbfe2325 c6c7c895ca92cc4d`
- feature-on 500: `a090f9f5984323a6 aba9ad39d2565fcc 292ba62fb1b9a448 28a8706bd151bc12 87d051aae72fd27b aba9ad39d2565fcc 5ce98b5275f42401 18ccd23822659a5e f96b4f4a115d1a8a 2f3b7fa478ac3e51`

## LEDGER MB

Values are MiB (1024^2), from resident live resources at the end of each run. The feature-on row is included to verify that active stages retain their full resources. The 1080p raw total is 347.04 MiB; the sum of independently rounded category cells is 347.05 MiB.

| category | default 500 before | default 500 after | default 1080 before | default 1080 after | feature-on 500 before | feature-on 500 after |
|---|---:|---:|---:|---:|---:|---:|
| G-buffer / hit / pick-id / depth | 25.26 | 25.26 | 207.25 | 207.25 | 25.26 | 25.26 |
| Accumulation / history / radiance | 16.00 | 4.00 | 131.25 | 31.88 | 16.00 | 16.00 |
| Reservoirs (RIS) | 11.44 | 0.00 | 94.92 | 0.00 | 11.44 | 11.44 |
| Photon cells / hit accumulation | — | — | — | — | 12.50 | 12.50 |
| Final render targets | 8.02 | 8.02 | 67.69 | 67.69 | 8.02 | 8.02 |
| Staging pool | 17.06 | 17.06 | 17.06 | 17.06 | 17.06 | 17.06 |
| Scene / ESVO / recipe | 10.68 | 10.68 | 10.68 | 10.68 | 10.68 | 10.68 |
| Shadow rays | 4.50 | 4.50 | 4.50 | 4.50 | 4.50 | 4.50 |
| Sky image | 4.00 | 4.00 | 4.00 | 4.00 | 4.00 | 4.00 |
| Probe grid (DDGI) | 2.75 | 2.75 | 2.75 | 2.75 | 2.75 | 2.75 |
| Instance rings | 1.21 | 1.21 | 1.21 | 1.21 | 1.21 | 1.21 |
| Frame-packet blocks | 0.03 | 0.03 | 0.03 | 0.03 | 0.03 | 0.03 |
| **sum resources** | **100.95** | **77.51** | **541.34** | **347.04** | **113.45** | **113.45** |
| **delta** |  | **-23.44** |  | **-194.30** |  | **0.00** |

The default deltas are the expected savings from disabling the default-gated accumulation/history and reservoir resources. Feature-on resources and pixels remain unchanged.

## RECOVERY / VERIFICATION

The first queued native target build failed before compilation because the fresh native tree had no `build/ninja-release` directory:

- queue log: `/home/liory/.local/state/undertow/undertow-box-logs/1794577792-build-cx-gpuliveness2:scenario-build.log`
- native diagnostic: `/mnt/c/vixen-native/gpuliveness2/.tmp/native-build/vixen_build_-gpuliveness2-.log`
- diagnostic: `C:/vixen-native/gpuliveness2/build/ninja-release is not a directory`

Recovery A succeeded: queued `scenario-configure` generated `vixen-ninja-release`, and queued `scenario-build3` then linked `vixen_load_scenario` with no compiler warnings. The first-run native preparation also provisioned the pinned Ninja and ccache inputs. The benchmark configure was enabled through the temporary native-only configure batch required by the existing benchmark-route gap; the ledger layer and scenario executable then built successfully. Recovery B was not applicable: no generated product contract was changed in this lane. Recovery C was not needed because the required native GPU scope passed after Recovery A.

The three required GPU jobs exited 0:

- `cx-gpuliveness2:gpu-d-500`
- `cx-gpuliveness2:gpu-d-1080`
- `cx-gpuliveness2:gpu-fm-500`

## FINDINGS

### Windows evidence-root backslash loss

This was reproduced without launching a scenario using a temporary echo batch:

```text
bash /home/liory/.local/bin/with-test-lock.sh --agent cx-gpuliveness2 --resource light --label cx-gpuliveness2:bat-backslash-repro -- \
  bash tools/vixen-native.sh --log .../bat-backslash-repro.log bat .tmp/echo-args.bat \
  --evidence-root C:\vixen-native\gpuliveness2-evidence repro
```

The wrapper logged and the batch received:

```text
--evidence-root C:vixen-nativegpuliveness2-evidence
ARG2=[C:vixen-nativegpuliveness2-evidence]
NATIVE=["--evidence-root" "C:vixen-nativegpuliveness2-evidence" "repro"]
```

Passing the same argument quoted as `'C:\vixen-native\gpuliveness2-evidence'` preserves the separators; that is how the three successful GPU captures were launched. This is not fixed here because the quoted route did not block the witness. A current SPT proposal extends the prior lane finding.

Existing incidental setup gaps were encountered and carried forward rather than fixed in this witness: fresh native build-tree configuration, first-run Ninja/ccache provisioning, and the first-class benchmark-ledger configure route. Their prior proposals are in the lane-1 inbox.

The queued targeted `python3 -m unittest tools/tests/test_vixen_native_args.py` check is a pre-existing fixture red: 4/5 tests stop before their assertions because the synthetic checkout copies `vixen-native.sh` but not its required `tools/node-width.sh` sibling. Queue log: `/home/liory/.local/state/undertow/undertow-box-logs/1794601416-light-cx-gpuliveness2:native-args-test.log`. This is outside the completed GPU ledger scope; the direct queued echo probe exercised the requested evidence-root path boundary successfully.

## CHANGE

No additional product-source change. The witness validates the existing resource-liveness implementation at `f6b49c61e`.

## STOPs

None. Required default hashes, feature-on hashes, and 500x500/1080p ledger measurements are complete.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Reconfirm vixen-native bat backslash loss at the cmd.exe argument boundary
- proposed: Copy vixen-native helper dependencies into the argument-test fixture
