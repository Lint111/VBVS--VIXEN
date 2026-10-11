# Look-dev capture attribution

Captures use the fixed 64-frame, four-angle look-dev test. Each PNG is 500x500.

## Midday: complete merge-step comparison

All four angle images are byte-identical at every step:

| Comparison | Result |
|---|---|
| wave base `5dd78d86` → after gpuyield `1e26ddec` | 4/4 byte-identical; zero differing pixels |
| after gpuyield → after lights0 `8e31dbb1` | 4/4 byte-identical; zero differing pixels |
| after lights0 → after shellasync0 `90ec57b4` | 4/4 byte-identical; zero differing pixels |
| after shellasync0 → fixed tip `a400a730` and current tip `853b659c` | 4/4 byte-identical; zero differing pixels |

The base, intermediate, and tip PNGs are preserved in their corresponding directories here. The comparisons used `tools/compare-capture-pixels.py --require-byte-identical`; each angle reports `byte_diff=0`, `pixel_diff=0`, `max_channel_delta=0`, and `bbox=none`.

## Overcast and night-service

The current-tip four-angle captures are saved under `tip/overcast` and `tip/night-service`. The overcast CTest repeat completed and all four repeat PNGs are byte-identical to the first capture. Night-service produced and saved all four first-pass angles; the repeat CTest exceeded its 1200-second timeout after writing two repeat angles, and those two are byte-identical to the matching first-pass images.

The wave-base overcast retry lost the DZN Vulkan device at `FrameSyncNode::vkWaitSemaphores` after writing angles 0–2. Those three saved originals match the corresponding tip angles by SHA-256; angle 3 is missing, so the state comparison is partial. See `wave-base/overcast-partial/capture-status.txt` and log `/home/liory/.local/state/undertow/undertow-box-logs/1791726216-gpu-wave-base-lookdev-overcast.log`.

The wave-base night-service capture also lost the DZN device during the earlier retry (`/home/liory/.local/state/undertow/undertow-box-logs/1791726137-gpu-wave-base-lookdev-night-service.log`). No complete base night-service image set is available, so that comparison remains unverified. There are no observed pixel changes to explain; midday is fully attributed across every merge boundary, and the available overcast pixels match exactly.
