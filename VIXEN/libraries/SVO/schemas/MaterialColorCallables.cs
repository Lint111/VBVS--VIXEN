// The material-id -> RGB table, authored once (R619.1). `--callable-cpp` generates the C++ face
// (SVORebuild's DXT colour derivation) and `--callable-glsl` the GLSL face (Materials.glsl's
// getMaterialColor); neither side keeps a hand copy. Exemplar: kernel
// Runtime/Channels/Regime/CellFootprintRegimeCallables.cs (one callable, both backends).
//
// A callable returns one scalar, so the colour is read per channel (0 = R, 1 = G, 2 = B); the
// consumers pack the three reads into their vec3.
//
// Material ID ranges by scene type:
//   Cornell Box: 1-20
//   Noise/Tunnel: 30-40
//   Cityscape: 50-61
using Yeroket.Util.KernelFramework;

namespace Vixen.Materials
{
    public static class MaterialColorCallables
    {
        [KernelCallable]
        public static float PickChannel(uint channel, float r, float g, float b) =>
            channel == 0u ? r : (channel == 1u ? g : b);

        [KernelCallable]
        public static float MaterialColorChannel(uint matId, uint channel)
        {
            // Cornell Box materials (1-20)
            if (matId == 1u) return PickChannel(channel, 1.0f, 0.0f, 0.0f);      // Red (left wall)
            if (matId == 2u) return PickChannel(channel, 0.0f, 1.0f, 0.0f);      // Green (right wall)
            if (matId == 3u) return PickChannel(channel, 0.9f, 0.9f, 0.9f);      // Light gray (white wall)
            if (matId == 4u) return PickChannel(channel, 0.85f, 0.85f, 0.85f);   // Floor (white/light gray)
            if (matId == 5u) return PickChannel(channel, 0.95f, 0.95f, 0.95f);   // White (ceiling)
            if (matId == 6u) return PickChannel(channel, 0.8f, 0.8f, 0.8f);      // Medium gray (floor light)
            if (matId == 7u) return PickChannel(channel, 0.4f, 0.4f, 0.4f);      // Darker gray (floor dark)
            if (matId == 10u) return PickChannel(channel, 0.8f, 0.6f, 0.2f);     // Tan/wooden (left cube)
            if (matId == 11u) return PickChannel(channel, 0.6f, 0.8f, 0.9f);     // Light blue (right cube)
            if (matId == 20u) return PickChannel(channel, 1.0f, 0.98f, 0.9f);    // Warm white (ceiling light)

            // Noise/Tunnel materials (30-40) - earth/stone tones
            if (matId == 30u) return PickChannel(channel, 0.5f, 0.45f, 0.4f);    // Stone
            if (matId == 31u) return PickChannel(channel, 0.6f, 0.55f, 0.5f);    // Stalactite
            if (matId == 32u) return PickChannel(channel, 0.55f, 0.5f, 0.45f);   // Stalagmite
            if (matId >= 33u && matId <= 39u)
            {
                // Stone variants - interpolate between two tones
                float t = (float)(matId - 33u) / 6.0f;
                return PickChannel(channel,
                    0.52f * (1.0f - t) + 0.68f * t,
                    0.48f * (1.0f - t) + 0.6f * t,
                    0.42f * (1.0f - t) + 0.52f * t);
            }
            if (matId == 40u) return PickChannel(channel, 0.8f, 0.6f, 0.2f);     // Ore (golden)

            // Cityscape materials (50-61)
            if (matId == 50u) return PickChannel(channel, 0.2f, 0.2f, 0.22f);    // Asphalt
            if (matId == 60u) return PickChannel(channel, 0.6f, 0.58f, 0.55f);   // Concrete
            if (matId == 61u) return PickChannel(channel, 0.7f, 0.85f, 0.95f);   // Glass

            // Default: HSV color wheel for unknown materials, more visible than a dark grey fallback
            float hue = (float)(matId % 256u) / 256.0f;
            float h = hue * 6.0f;
            int i = (int)h;
            float f = h - (float)i;
            float p = 0.8f * 0.3f;
            float q = 0.8f * (1.0f - 0.7f * f);
            float u = 0.8f * (1.0f - 0.7f * (1.0f - f));
            if (i == 0) return PickChannel(channel, 0.8f, u, p);
            if (i == 1) return PickChannel(channel, q, 0.8f, p);
            if (i == 2) return PickChannel(channel, p, 0.8f, u);
            if (i == 3) return PickChannel(channel, p, q, 0.8f);
            if (i == 4) return PickChannel(channel, u, p, 0.8f);
            return PickChannel(channel, 0.8f, p, q);
        }
    }
}
