# T-1170 Mining Feedback Beam

The lane began at base SHA 7f4db6877bac7062502b1cb603b68464cf42da21. It adds a feature-gated mining-beam buffer node that accepts up to 16 source/target instance pairs, supports runtime SetBeams and SetEnabled calls, and starts disabled with an empty list. The SRS shader resolves stored-body offsets through octree transforms and procedural-body offsets relative to each instance center, then adds a modest emissive core and halo to displayed radiance. Beam emission does not enter body lighting in this lane.

For R419.7, a later shared-light-set integration should add each beam as a light-source entry alongside emissive bodies, carrying its endpoints, luminosity, and purpose scale so the normal light-gathering path can account for its modest spill. This lane only draws the visible beam.

The headless Cornell capture is byte-identical to the baseline when default, disabled with a populated list, or enabled with an empty list. The active capture differs and is repeatable. The active image is available at build/wsl/headless-cornell-frame-beam-active-a.png; its SHA-256 is 2ad885a3bb08a2af05e92dcd3ca257229a83daec1a955e7daabba5155d18c931. All seven HUD and editor captures also match the baseline byte-for-byte.

After correcting procedural endpoint translation, one focused run transiently produced a blank enabled-empty capture and failed the byte-identity assertion. The immediate repeat passed and reproduced the expected hashes: default, disabled, and empty all 2cc9f0c7a02d4757e682a88e0dbcf5f98429d6111cda18aa09f96e81a02279a1; active 2ad885a3bb08a2af05e92dcd3ca257229a83daec1a955e7daabba5155d18c931. The failure was not reproducible.

The fresh queued CMake build passed, the focused shadow and Cornell tests passed, the SDI drift check passed, and the full RenderGraph suite passed all 1,337 tests with five skips. CodegenTool restore, build, and --check passed. The SVO suite retains the same baseline failure, RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes, due to unsupported capability 94; all other cases passed or were skipped or disabled.

The standard capture helper requires an absolute output directory because it launches the app from its runtime directory. The recovered run passed, and the helper path issue is recorded in .spt-proposals/minebeam.jsonl as “Resolve capture output paths before launching runtime processes.” The available wave ref origin/wave/authoring-convergence equals the lane base, so its merge was already up to date; this checkout has no origin/main-wave ref.
