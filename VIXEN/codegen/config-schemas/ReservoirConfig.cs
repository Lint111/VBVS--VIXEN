using Yeroket.Util.KernelFramework;

// Canonical ReservoirConfig — one source for C++ (Vixen::Gpu) + GLSL.
// Sampled Lighting Inc3 M3: ReSTIR reservoir-sampling compute budget as
// drift-guarded data, not magic numbers. Mirrors ShadowConfig.cs/
// AccumulationConfig.cs's precedent exactly (see those files for the
// [GpuStruct]/std430 codegen contract).
//
// M3 SCOPE NOTE: this struct is scaffolding for M4/M5 (RIS + temporal/spatial
// reservoir reuse) — no reservoir sampling logic ships in M3 itself (that's
// this program's Task 4/5). M3 only needs reservoirEnabled (the byte-identity
// escape hatch: reservoirEnabled=0 must reproduce the pre-ReSTIR M1/M2
// baseline) and lightTreeCutThreshold (the mip-cut light-tree's own knob,
// consumed by the CPU-side BuildLightTreeCut today via LightTreeCutParams;
// wiring the SAME threshold value from THIS GPU-uploaded struct is a future
// milestone's job once the light-tree cut itself runs on the GPU).
//
// reservoirEnabled: 0/1 master switch. When 0, ReSTIR/reservoir shading is
// skipped entirely (M3: nothing reads it yet; M4+ gates the reservoir pass on
// it) — the escape hatch every milestone in this program has relied on since
// Inc0.
// candidateCount: M in RIS (weighted reservoir sampling) — number of initial
// candidate samples drawn from the light-tree cut per pixel. Consumed
// starting M4.
// spatialRadius: neighbor-reuse search radius in pixels for M5's spatial
// reservoir pass. Consumed starting M5.
// spatialCount: number of spatial neighbors sampled per pixel in M5's
// spatial reservoir pass. Consumed starting M5.
// temporalCap: cap on a reservoir's accumulated sample count M across frames
// (bounds how "stale" a long-lived temporal reservoir can get) — mirrors
// AccumulationConfig.maxFrames's role for the EWMA history. Consumed
// starting M4's temporal reuse.
// biasedModeEnabled: 0/1 toggle for the 35-65x biased ReSTIR mode (explicitly
// DEFERRED per the plan's Self-Review — unbiased weights only in this
// program's initial landing). The field exists now so the bias ledger has a
// place to record "deferred, not forgotten" without a future struct-layout
// change; unused (must stay 0) until that mode ships.
// lightTreeCutThreshold: the mip-cut light-tree's power-threshold knob (see
// LightTree.h's LightTreeCutParams::powerThreshold) — GPU-uploaded so a
// future GPU-side light-tree cut (or CPU-side authoring UI) can drive the
// same value this milestone's CPU BuildLightTreeCut takes as a parameter.
// frameParity: Sampled Lighting Inc3 M4 — a monotonic per-frame counter,
// incremented by ReservoirConfigNode::ExecuteImpl every Execute (NOT reset by
// camera motion, unlike pc.accumFrameCount — this is what makes it safe as a
// reservoir ping-pong selector: frameParity&1 strictly alternates every frame,
// telling DirectLighting.comp which of reservoirBufferA/B is CURRENT (write)
// vs PREVIOUS (read) this frame, with no CPU-side graph rewiring needed).
[GpuStruct]
public struct ReservoirConfig
{
    public uint reservoirEnabled;
    public uint candidateCount;
    public float spatialRadius;
    public uint spatialCount;
    public uint temporalCap;
    public uint biasedModeEnabled;
    public float lightTreeCutThreshold;
    public uint frameParity;
}
