#pragma once
#include "Data/Core/ResourceConfig.h"

namespace Vixen::RenderGraph {

// Compile-time slot counts
namespace RaySizeCoefNodeCounts {
    static constexpr size_t INPUTS  = 1;  // HEIGHT
    static constexpr size_t OUTPUTS = 1;  // RAY_SIZE_COEF
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Derives the LOD ray-cone spread constant (raySizeCoef) from a live render-target height.
 *
 * raySizeCoef = 2*tan((fovYRadians / height) / 2) — matches SVOLOD.h::LODParameters::fromCamera.
 * Recomputed every Compile from HEIGHT (a Dependency input, so this node is a transitive
 * dependent of whatever publishes the height — RenderTargetNode::HEIGHT_OUT — and rides the
 * standard resize->recompile cascade with no per-frame extent checks). Fixes Widescreen-Perf-Fix
 * rank 6: the previous ConstantNode-based wiring computed raySizeCoef ONCE at graph-build time
 * from the initial window height and never updated it, silently under-detailing large windows.
 *
 * Inputs: 1
 *   - HEIGHT (uint32_t, Dependency) — live render-target height in pixels
 * Outputs: 1
 *   - RAY_SIZE_COEF (float) — LOD cone-spread constant for the BodyInstanceRayMarch push constant
 * Parameters:
 *   - PARAM_FOV_DEGREES (float, default 45.0) — vertical FOV in degrees; must match CameraNode's
 *     PARAM_FOV so the LOD cone and the camera projection stay in lock-step.
 */
CONSTEXPR_NODE_CONFIG(RaySizeCoefNodeConfig,
                      RaySizeCoefNodeCounts::INPUTS,
                      RaySizeCoefNodeCounts::OUTPUTS,
                      RaySizeCoefNodeCounts::ARRAY_MODE) {

    static constexpr const char* PARAM_FOV_DEGREES = "fovDegrees";

    // ----- Input slots -----
    INPUT_SLOT(HEIGHT, uint32_t, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ----- Output slots -----
    OUTPUT_SLOT(RAY_SIZE_COEF, float, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    RaySizeCoefNodeConfig() {
        HandleDescriptor heightDesc{"uint32_t"};
        INIT_INPUT_DESC(HEIGHT, "height", ResourceLifetime::Transient, heightDesc);

        HandleDescriptor coefDesc{"float"};
        INIT_OUTPUT_DESC(RAY_SIZE_COEF, "ray_size_coef", ResourceLifetime::Transient, coefDesc);
    }

    static_assert(HEIGHT_Slot::index == 0, "HEIGHT must be at index 0");
    static_assert(!HEIGHT_Slot::nullable, "HEIGHT must not be nullable");
    static_assert(std::is_same_v<HEIGHT_Slot::Type, uint32_t>, "HEIGHT must be uint32_t");

    static_assert(RAY_SIZE_COEF_Slot::index == 0, "RAY_SIZE_COEF must be at index 0");
    static_assert(std::is_same_v<RAY_SIZE_COEF_Slot::Type, float>, "RAY_SIZE_COEF must be float");

    VALIDATE_NODE_CONFIG(RaySizeCoefNodeConfig, RaySizeCoefNodeCounts);
};

} // namespace Vixen::RenderGraph
