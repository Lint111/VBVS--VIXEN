// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "Data/Core/ResourceConfig.h"
#include "VulkanDeviceFwd.h"
#include <array>
#include <vector>
#include <vulkan/vulkan.h>

namespace Vixen::Vulkan::Resources { struct IRenderTarget; }

namespace Vixen::RenderGraph {

using VulkanDevice  = Vixen::Vulkan::Resources::VulkanDevice;
using IRenderTarget = Vixen::Vulkan::Resources::IRenderTarget;

// An optional, source-neutral contribution to the procedural sky. The direction is in world
// space; brightness is a relative scalar. Keeping this at the render-node boundary allows a
// later deep-field source to feed stars without committing this node to that source's storage.
struct SkySphereStar {
    std::array<float, 3> direction{0.0f, 0.0f, 1.0f};
    float brightness = 1.0f;
};

// Pointer-carried list lets an optional producer own/update its per-frame values without
// registering a second generic resource-container schema in the graph type system.
struct SkySphereStarList {
    std::vector<SkySphereStar> stars;
};

namespace SkySphereNodeCounts {
    static constexpr size_t INPUTS  = 3;  // device, command pool, optional star list
    static constexpr size_t OUTPUTS = 2;  // SKY_SPHERE, CURRENT_VIEW
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Parameters and resource slots for the procedural sky-sphere cache.
 *
 * The persistent RGBA16F octahedral image is refreshed only when enabled, seed, brightness, or
 * an optional star-list input changes. `enabled` defaults to false in BuildRenderGraph, so the
 * consumer retains the existing background until explicitly opted in. The list slot is
 * intentionally not connected by the default graph while owner question Q7 is open.
 *
 * Star colors and galaxy tint are restrained linear-light constants in SkySphereNode.cpp;
 * `brightness` scales both. It affects only miss pixels in SpatialReuseShade, so it does not
 * alter the scene's bi-modal body lighting.
 */
CONSTEXPR_NODE_CONFIG(SkySphereNodeConfig,
                      SkySphereNodeCounts::INPUTS,
                      SkySphereNodeCounts::OUTPUTS,
                      SkySphereNodeCounts::ARRAY_MODE) {

    // ----- Input slots -----
    INPUT_SLOT(VULKAN_DEVICE_IN, VulkanDevice*, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(COMMAND_POOL, VkCommandPool, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(STAR_LIST, SkySphereStarList*, 2,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // ----- Output slots -----
    OUTPUT_SLOT(SKY_SPHERE, IRenderTarget*, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(CURRENT_VIEW, VkImageView, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ----- Parameter names -----
    static constexpr const char* PARAM_WIDTH  = "width";
    static constexpr const char* PARAM_HEIGHT = "height";
    static constexpr const char* PARAM_FORMAT = "format";  // VkFormat stored as uint32_t
    static constexpr const char* PARAM_ENABLED = "enabled";  // uint32_t; defaults to 0/off
    static constexpr const char* PARAM_SEED = "seed";  // uint32_t; stable procedural seed
    static constexpr const char* PARAM_BRIGHTNESS = "brightness";  // float linear-light scale
    static constexpr const char* PARAM_REFRESH_CADENCE_FRAMES = "refresh_cadence_frames";

    // ----- Runtime resource descriptors -----
    SkySphereNodeConfig() {
        HandleDescriptor deviceDesc{"VulkanDevice*"};
        INIT_INPUT_DESC(VULKAN_DEVICE_IN, "vulkan_device", ResourceLifetime::Persistent, deviceDesc);

        HandleDescriptor poolDesc{"VkCommandPool"};
        INIT_INPUT_DESC(COMMAND_POOL, "command_pool", ResourceLifetime::Persistent, poolDesc);

        HandleDescriptor starsDesc{"SkySphereStarList*"};
        INIT_INPUT_DESC(STAR_LIST, "star_list", ResourceLifetime::Persistent, starsDesc);

        HandleDescriptor skyDesc{"IRenderTarget*"};
        INIT_OUTPUT_DESC(SKY_SPHERE, "sky_sphere", ResourceLifetime::Persistent, skyDesc);

        HandleDescriptor viewDesc{"VkImageView"};
        INIT_OUTPUT_DESC(CURRENT_VIEW, "current_view", ResourceLifetime::Persistent, viewDesc);
    }

    static_assert(VULKAN_DEVICE_IN_Slot::index == 0, "VULKAN_DEVICE_IN must be at index 0");
    static_assert(COMMAND_POOL_Slot::index == 1, "COMMAND_POOL must be at index 1");
    static_assert(STAR_LIST_Slot::index == 2 && STAR_LIST_Slot::nullable,
                  "STAR_LIST must remain optional at index 2");
    static_assert(SKY_SPHERE_Slot::index == 0 &&
                  std::is_same_v<SKY_SPHERE_Slot::Type, IRenderTarget*>,
                  "SKY_SPHERE must be IRenderTarget*");
    static_assert(CURRENT_VIEW_Slot::index == 1 &&
                  std::is_same_v<CURRENT_VIEW_Slot::Type, VkImageView>,
                  "CURRENT_VIEW must be VkImageView");

    VALIDATE_NODE_CONFIG(SkySphereNodeConfig, SkySphereNodeCounts);
};

} // namespace Vixen::RenderGraph
