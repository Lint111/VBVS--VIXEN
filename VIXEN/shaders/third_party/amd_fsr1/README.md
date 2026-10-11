# AMD FidelityFX Super Resolution 1

The two FidelityFX shader headers in this directory are from AMD's FidelityFX-FSR
release v1.0.2 (source revision `a21ffb8f6c13233ba336352bdff293894c706575`). The
upscaler uses EASU followed by RCAS. They are distributed under the MIT license;
the full license text is in `license.txt` and is also retained in each upstream header.

The graph's EASU and RCAS compute shaders adapt the upstream callbacks to VIXEN's
storage-image descriptors and keep the images in `GENERAL` for the declared image
sync slots. The separate `FsrInputAA.comp` pass is VIXEN's lightweight edge-aware
input AA, not part of AMD's source.
