// ============================================================================
// Shader-Specific Names Header
// ============================================================================
//
// Program: VoxelRayMarch
// UUID: 99ced64ee0e9899b
// Generated: 2025-11-10 13:23:07
//
// This file provides shader-specific constexpr constants and type aliases
// that map to the generic .si.h interface.
//
// Usage: #include "VoxelRayMarchNames.h"
//
// DO NOT MODIFY THIS FILE MANUALLY - it will be regenerated.
//
// ============================================================================

#pragma once

#include "99ced64ee0e9899b-SDI.h"

namespace VoxelRayMarch {

// Reference to generic SDI namespace
namespace SDI = ShaderInterface::_99ced64ee0e9899b;

// ============================================================================
// Descriptor Binding Constants
// ============================================================================

// outputImage (Set 0, Binding 0)
struct outputImage_Ref {
    using SDI_Type = SDI::Set0::outputImage;
    static constexpr uint32_t set = SDI_Type::SET;
    static constexpr uint32_t binding = SDI_Type::BINDING;
    static constexpr VkDescriptorType type = SDI_Type::TYPE;
    static constexpr const char* name = "outputImage";
};
inline constexpr outputImage_Ref outputImage{};

// camera (Set 0, Binding 1)
struct camera_Ref {
    using SDI_Type = SDI::Set0::camera;
    static constexpr uint32_t set = SDI_Type::SET;
    static constexpr uint32_t binding = SDI_Type::BINDING;
    static constexpr VkDescriptorType type = SDI_Type::TYPE;
    static constexpr const char* name = "camera";
};
inline constexpr camera_Ref camera{};

// octreeNodes (Set 0, Binding 2)
struct octreeNodes_Ref {
    using SDI_Type = SDI::Set0::octreeNodes;
    static constexpr uint32_t set = SDI_Type::SET;
    static constexpr uint32_t binding = SDI_Type::BINDING;
    static constexpr VkDescriptorType type = SDI_Type::TYPE;
    static constexpr const char* name = "octreeNodes";
};
inline constexpr octreeNodes_Ref octreeNodes{};

// voxelBricks (Set 0, Binding 3)
struct voxelBricks_Ref {
    using SDI_Type = SDI::Set0::voxelBricks;
    static constexpr uint32_t set = SDI_Type::SET;
    static constexpr uint32_t binding = SDI_Type::BINDING;
    static constexpr VkDescriptorType type = SDI_Type::TYPE;
    static constexpr const char* name = "voxelBricks";
};
inline constexpr voxelBricks_Ref voxelBricks{};

// materialPalette (Set 0, Binding 4)
struct materialPalette_Ref {
    using SDI_Type = SDI::Set0::materialPalette;
    static constexpr uint32_t set = SDI_Type::SET;
    static constexpr uint32_t binding = SDI_Type::BINDING;
    static constexpr VkDescriptorType type = SDI_Type::TYPE;
    static constexpr const char* name = "materialPalette";
};
inline constexpr materialPalette_Ref materialPalette{};

// ============================================================================
// UBO/SSBO Struct Type Aliases
// ============================================================================

using CameraData = SDI::CameraData;
using OctreeNodesBuffer = SDI::OctreeNodesBuffer;
using VoxelBricksBuffer = SDI::VoxelBricksBuffer;
using MaterialPaletteBuffer = SDI::MaterialPaletteBuffer;

} // namespace VoxelRayMarch
