# Traversal default witnesses

The native helper provisions `/mnt/c/vixen-native/travdefault` if it is missing,
fetches the committed `lane-travdefault` tree, configures the Release MSVC preset,
and builds `vixen_lookdev_capture` on the Radeon 8060S.

Run the build from the assigned worktree root through the live queue:

```bash
bash /home/liory/.local/bin/with-test-lock.sh --agent travdefault --resource build --label travdefault:native-build -- bash reports/travdefault/reproduce/native-build.sh
```

The capture runner resets inherited `VIXEN_*` variables per process. `default`
leaves `VIXEN_COMPOSED_TRAVERSAL` unset; `esvo` sets it to `0`. Each run captures
midday, late-afternoon, overcast, and night-service at four camera yaws.

Run the A/B matrix through the GPU queue, once for each scene and resolution:

```bash
bash /home/liory/.local/bin/with-test-lock.sh --agent travdefault --resource gpu --label travdefault:lookdev-500 -- bash reports/travdefault/reproduce/run-capture-matrix.sh lookdev 500 500 default,esvo 1 r505-lookdev-500
bash /home/liory/.local/bin/with-test-lock.sh --agent travdefault --resource gpu --label travdefault:lookdev-1080 -- bash reports/travdefault/reproduce/run-capture-matrix.sh lookdev 1920 1080 default,esvo 1 r505-lookdev-1080
bash /home/liory/.local/bin/with-test-lock.sh --agent travdefault --resource gpu --label travdefault:stored-sdf-500 -- bash reports/travdefault/reproduce/run-capture-matrix.sh stored-sdf 500 500 default,esvo 1 r505-stored-sdf-500
bash /home/liory/.local/bin/with-test-lock.sh --agent travdefault --resource gpu --label travdefault:stored-sdf-1080 -- bash reports/travdefault/reproduce/run-capture-matrix.sh stored-sdf 1920 1080 default,esvo 1 r505-stored-sdf-1080
```

The runner writes PNGs, logs, status JSON, and per-frame CSVs under the native
checkout at `reports/travdefault/native/`. Compare stored-SDF default captures
with the `composed` captures in
`C:\vixen-native\brickcost\reports\brickcost\native\brickmapscene500-guarded`
and `brickmapscene1080-guarded`.

## Dozen fallback correctness witness

The capture fixture defaults to 64 frames per camera angle. For this correctness-only
software-driver check, set `VIXEN_LOOKDEV_CAPTURE_FRAMES=1` to render one frame at
each of the four angles while keeping the default composed traversal request:

```bash
bash /home/liory/.local/bin/with-test-lock.sh --agent travdefault --resource test --label travdefault:dozen-dda-fallback -- env -u VIXEN_COMPOSED_TRAVERSAL -u VIXEN_RTQUERY_TRAVERSAL -u VIXEN_BRICKMAP_TRAVERSAL VK_ICD_FILENAMES=/home/liory/.cache/vixen/wsl-vulkan/dzn_icd.json VK_LAYER_PATH="$PWD/.vulkan-sdk/1.4.350.1/x86_64/share/vulkan/explicit_layer.d" VIXEN_BRICKMAP_SCENE=1 VIXEN_LOOKDEV_CAPTURE_STATE=midday VIXEN_LOOKDEV_CAPTURE_FRAMES=1 VIXEN_LOOKDEV_CAPTURE_DIR="$PWD/../build/wsl/runtime-captures/travdefault-dda-fallback" VIXEN_WINDOW_WIDTH=500 VIXEN_WINDOW_HEIGHT=500 ../build/wsl/application/main/vixen_lookdev_capture --gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically
```

The log must report `[ComposedBackend] DDA` and the capture test must pass.

## R491 feature inventory update

| Feature row | Updated classification |
| --- | --- |
| `VIXEN_COMPOSED_TRAVERSAL` | Planned/default-on composed traversal; CapabilityGraph selects ray query when available and composed DDA fallback otherwise. Keep `0`/`1` as developer A/B overrides. |
| `VIXEN_BRICKMAP_TRAVERSAL` | Retired as a standalone product switch. DDA remains an internal composed fallback tier. |
