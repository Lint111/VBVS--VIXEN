// ============================================================================
// Shader-Specific Names Header
// ============================================================================
//
// Program: VoxelRayMarch
// UUID: bc61d7a06c98333f
// Generated: 2025-11-08 21:46:38
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

#include "bc61d7a06c98333f-SDI.h"

namespace VoxelRayMarch {

// Reference to generic SDI namespace
namespace SDI = ShaderInterface::bc61d7a06c98333f;

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

// voxelGrid (Set 0, Binding 2)
struct voxelGrid_Ref {
    using SDI_Type = SDI::Set0::voxelGrid;
    static constexpr uint32_t set = SDI_Type::SET;
    static constexpr uint32_t binding = SDI_Type::BINDING;
    static constexpr VkDescriptorType type = SDI_Type::TYPE;
    static constexpr const char* name = "voxelGrid";
};
inline constexpr voxelGrid_Ref voxelGrid{};

// ============================================================================
// UBO/SSBO Struct Type Aliases
// ============================================================================

using CameraData = SDI::CameraData;

} // namespace VoxelRayMarch
