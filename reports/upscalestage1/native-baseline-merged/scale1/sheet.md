# Native merged baseline: midday, 500×500, R/D 1

Fresh Windows MSVC Release capture at merged tip `decc023fb82f65d3899d3838dcbb08f07c41f6b3`, using the native Radeon 8060S. The production graph renders at full 500×500. The fixed camera, midday preset and 45° FOV use four yaw angles; recipe values and exposure remain fixed. The native CMake wrapper ran two fresh processes and confirmed all four PNGs byte-identical.

| Yaw 0° | Yaw 90° |
| --- | --- |
| ![Native merged baseline, yaw 0 degrees](angle-0.png) | ![Native merged baseline, yaw 90 degrees](angle-1.png) |

| Yaw 180° | Yaw 270° |
| --- | --- |
| ![Native merged baseline, yaw 180 degrees](angle-2.png) | ![Native merged baseline, yaw 270 degrees](angle-3.png) |

The captured GPU CSV is `perf.csv`; fixed-LOD override is `0.00165685425`. This initial CSV predates explicit exposure-stage columns; the final timing baseline will be recaptured with the expanded full-frame logger.
