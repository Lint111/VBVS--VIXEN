using System;
using Undertow.Modding.Schema;
using Undertow.Sim.Systems;
using Yeroket.Util.KernelFramework;

namespace Vixen.Codegen.Gpu;

// The GPU wizard recognizes these names as GLSL intrinsics. They exist only as
// syntax in this source root; the generated body is the executable implementation.
internal static class math
{
    public static float max(float left, float right) => throw new NotSupportedException();
    public static float min(float left, float right) => throw new NotSupportedException();
    public static float frac(float value) => throw new NotSupportedException();
    public static float pow(float value, float exponent) => throw new NotSupportedException();
    public static float clamp(float value, float minimum, float maximum) => throw new NotSupportedException();
    public static float select(float whenFalse, float whenTrue, bool condition) => throw new NotSupportedException();
}

[KernelSystem(Emit = KernelEmitTargets.Glsl)]
public static class ExposureTonemapSystem
{
    [GaiaQuery]
    public static QuerySpec Select() => Gaia.Query()
        .All<StateFrameTargetsRef, Rgba16f>(row => row.ResolvedRadiance)
        .All<StateFrameTargetsRef, Rgba8>(row => row.OutputImage);

    [Update]
    public static void Run(
        in Texture2D<Rgba16f> resolvedRadiance,
        ref Texture2D<Rgba8> outputImage,
        in ExposureMeterState[] meter,
        in LightingConfig[] LightingConfigSSBO,
        int i)
    {
        Rgba16f source = resolvedRadiance.Current[i];
        float exposure = math.pow(2.0f, meter[0].log2Exposure + LightingConfigSSBO[0].exposureCompensationEV);

        float red = math.max(source.x * exposure, 0.0f);
        float green = math.max(source.y * exposure, 0.0f);
        float blue = math.max(source.z * exposure, 0.0f);

        // Look saturation in linear space before the curve; the neutral value skips the mix so default output is bit-exact.
        float saturation = LightingConfigSSBO[0].lookSaturation;
        bool neutral = saturation == 1.0f;
        float luma = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
        red = math.select(math.max(red * saturation + luma * (1.0f - saturation), 0.0f), red, neutral);
        green = math.select(math.max(green * saturation + luma * (1.0f - saturation), 0.0f), green, neutral);
        blue = math.select(math.max(blue * saturation + luma * (1.0f - saturation), 0.0f), blue, neutral);

        red = math.clamp((red * (2.51f * red + 0.03f)) / (red * (2.43f * red + 0.59f) + 0.14f), 0.0f, 1.0f);
        float redLinear = 12.92f * red;
        float redEncoded = 1.055f * math.pow(math.max(red, 0.0f), 1.0f / 2.4f) - 0.055f;
        red = math.select(redEncoded, redLinear, red <= 0.0031308f);

        green = math.clamp((green * (2.51f * green + 0.03f)) / (green * (2.43f * green + 0.59f) + 0.14f), 0.0f, 1.0f);
        float greenLinear = 12.92f * green;
        float greenEncoded = 1.055f * math.pow(math.max(green, 0.0f), 1.0f / 2.4f) - 0.055f;
        green = math.select(greenEncoded, greenLinear, green <= 0.0031308f);

        blue = math.clamp((blue * (2.51f * blue + 0.03f)) / (blue * (2.43f * blue + 0.59f) + 0.14f), 0.0f, 1.0f);
        float blueLinear = 12.92f * blue;
        float blueEncoded = 1.055f * math.pow(math.max(blue, 0.0f), 1.0f / 2.4f) - 0.055f;
        blue = math.select(blueEncoded, blueLinear, blue <= 0.0031308f);

        // Dither is presentation-only and is applied in encoded space immediately before the
        // Rgba8 UNORM store. The avalanche is the MurmurHash3 finalizer shape (link-only prior art):
        // https://github.com/aappleby/smhasher/blob/master/src/MurmurHash3.cpp
        // FrameSync's existing frame seed keeps the sequence deterministic without another upload.
        uint pixelSeed = (uint)i ^ (LightingConfigSSBO[0].frameSeed * 0x9e3779b9u);
        pixelSeed ^= pixelSeed >> 16;
        pixelSeed *= 0x85ebca6bu;
        pixelSeed ^= pixelSeed >> 13;
        pixelSeed *= 0xc2b2ae35u;
        pixelSeed ^= pixelSeed >> 16;
        // Eight half-LSB spans give the low-gradient sky enough code values to avoid
        // visible plateaus while the zero-mean hash keeps the average luma unchanged.
        float ditherStep = math.select(0.0f, 8.0f / 255.0f, LightingConfigSSBO[0].presentationMode == 1u);
        red += ((float)(pixelSeed & 0xffu) / 255.0f - 0.5f) * ditherStep;
        green += ((float)((pixelSeed >> 8) & 0xffu) / 255.0f - 0.5f) * ditherStep;
        blue += ((float)((pixelSeed >> 16) & 0xffu) / 255.0f - 0.5f) * ditherStep;

        outputImage.Current[i] = new Rgba8(red, green, blue, 1.0f);
    }
}
