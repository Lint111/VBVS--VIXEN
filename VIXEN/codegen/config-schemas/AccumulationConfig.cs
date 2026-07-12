using Yeroket.Util.KernelFramework;

// Canonical AccumulationConfig — one source for C++ (Vixen::Gpu) + GLSL.
// Sampled Lighting Inc2 M1: temporal-accumulation compute budget as drift-guarded
// data, not magic numbers. Mirrors ShadowConfig.cs's precedent exactly (see that
// file for the [GpuStruct]/std430 codegen contract).
//
// enabled: 0/1 master switch. When 0, the accumulate seam in
// BodyInstanceRayMarch.comp is a pure no-op passthrough — imageStore(outputImage,...)
// is byte-identical to the pre-Inc2 (Inc1) path. This is the M1 gate lever.
// alpha: EWMA history blend weight. SENTINEL: alpha==0 selects the DEFAULT
// converging 1/N mode (frame-counter-driven, fastest variance->0 on static
// views, self-limiting toward maxFrames); alpha>0 selects fixed-alpha mode
// (a constant blend weight every frame, coded but not the default). Consumed
// starting M2, not read this milestone.
// maxFrames: cap for the converging 1/N mode — once the running frame count
// reaches maxFrames, alpha stops shrinking further (bounds history staleness).
// resetOnMotion: whole-frame history-reset toggle. When 1 (M2's default), any
// camera-state change forces frame counter -> 1 / alpha -> 1.0 (pure current
// frame, zero ghosting by construction). This is M4's fallback path — the
// default remains resetOnMotion=1 so all M1-M3 gates keep reproducing exactly.
// reprojectionEnabled: Sampled Lighting Inc2 M4. Opt-in per-pixel camera-motion
// reprojection — the frame counter is NOT reset on motion (accumulation
// continues through camera movement); instead the accumulate seam reprojects
// each pixel's HitRecord.worldPos through PrevCameraConfig.prevViewProj and
// validates the reprojected history sample (on-screen bounds + motion-
// magnitude) before trusting it, falling back to pure-current-frame (alpha=1)
// per-pixel on rejection. Mutually exclusive in intent with resetOnMotion=1
// (that field is ignored while this one is set) but both fields are kept so a
// caller can flip resetOnMotion back on without losing the reprojection
// plumbing. Default 0 (M1-M3 behavior unchanged).
[GpuStruct]
public struct AccumulationConfig
{
    public uint enabled;
    public float alpha;
    public uint maxFrames;
    public uint resetOnMotion;
    public uint reprojectionEnabled;
}
