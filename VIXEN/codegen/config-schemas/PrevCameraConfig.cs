using Yeroket.Util.KernelFramework;

// Canonical PrevCameraConfig — one source for C++ (Vixen::Gpu) + GLSL.
// Sampled Lighting Inc2 M3: prev-frame camera matrix plumbing. Mirrors
// ShadowConfig.cs / AccumulationConfig.cs's precedent exactly (see those
// files for the [GpuStruct]/std430 codegen contract).
//
// A mat4 (64 B) does not fit the march's existing push-constant block
// alongside the basis-vector fields already there (72 B used of the 128 B
// Vulkan-guaranteed minimum; +64 B would overflow it) — so, per the M3 plan,
// this is a small dedicated SSBO (binding 21) instead of a push-constant
// extension, mirroring AccumulationConfig's own separate-record precedent.
//
// prevViewProj: LAST frame's view*projection matrix (world -> prev-frame
// clip space), retained by CameraNode (compute-current-then-store-previous
// each frame — see CameraNode::UpdateCameraData/CompileImpl). Uploaded every
// frame (Sampled Lighting Inc2 M3) but NOT YET CONSUMED by the shader — M4
// reprojects HitRecord.worldPos through this matrix to sample historyImage
// at last frame's screen position. This milestone's gate is byte-identical
// output: the upload must have zero effect on outColor.
[GpuStruct]
public struct PrevCameraConfig
{
    public Mat4 prevViewProj;
    // Current input-pixel jitter (XY), native picking ray-cone coefficient (Z).
    public Float3 reconstructionSampling;
    // The input extent the jitter and the motion vectors are measured against, and the
    // anti-aliasing module's history-weight ceiling (R676; read by TemporalAntiAliasingSystem).
    // They ride this per-frame record: no transfer of their own (R674.2).
    public uint inputWidth;
    public uint inputHeight;
    public float historyWeight;
    // Round the struct size to std430's 16-byte structure alignment.
    public uint _tailPadding1;
    public uint _tailPadding2;
}
