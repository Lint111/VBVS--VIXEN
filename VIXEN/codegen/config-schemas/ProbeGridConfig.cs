using Yeroket.Util.KernelFramework;

// Canonical ProbeGridConfig — one source for C++ (Vixen::Gpu) + GLSL.
// Sampled Lighting Inc4 M2: DDGI probe-grid placement + compute budget as
// drift-guarded data, not magic numbers. Mirrors ReservoirConfig.cs's precedent
// exactly (see that file for the [GpuStruct]/std430 codegen contract).
//
// M2 SCOPE NOTE: this struct is scaffolding for M3-M6 (probe-update compute
// pass, Chebyshev visibility, shade-pass gather) -- no probe-update shader
// reads it yet (that's M3's job). M2 only needs probeGridEnabled (the
// byte-identity escape hatch: probeGridEnabled=0 must reproduce the
// pre-DDGI baseline) plus the placement/budget fields a future shader will
// consume with no struct-layout change.
//
// Vec3 fields are flattened to X/Y/Z scalars (mirrors LightTreeBuffer.cs's
// own LightTreeGpuNode.worldPos precedent -- no native vec3 codegen type in
// this pipeline) to keep std430 offsets simple and unsurprising.
//
// probeGridEnabled: 0/1 master switch. When 0, DDGI gather/update is skipped
// entirely -- the escape hatch every milestone in this program has relied on
// since Inc0.
// originX/Y/Z: world-space position of probe (0,0,0) -- the grid's corner.
// spacingX/Y/Z: world-space distance between adjacent probes along each axis
// (uniform grid, per the design's own open-decision #2 -- octree-anchored/
// cascaded placement is explicitly deferred).
// countX/Y/Z: number of probes along each axis (grid dimensions). Total probe
// count = countX*countY*countZ; also the atlas layout driver (M2's
// ProbeAtlasNode sizes its images from these).
// raysPerProbe: ray-cast budget per probe per update (M3's fixed/deterministic
// sample-direction-set size, e.g. spherical Fibonacci count).
// hysteresisRate: EWMA blend rate for the irradiance/visibility temporal
// accumulate (0..1), mirrors AccumulationConfig.alpha's role for camera-frame
// accumulation, applied per-probe instead of per-pixel.
[GpuStruct]
public struct ProbeGridConfig
{
    public uint probeGridEnabled;
    public float originX;
    public float originY;
    public float originZ;
    public float spacingX;
    public float spacingY;
    public float spacingZ;
    public uint countX;
    public uint countY;
    public uint countZ;
    public uint raysPerProbe;
    public float hysteresisRate;
}
