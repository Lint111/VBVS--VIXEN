// ============================================================================
// Shader-Specific Names Header
// ============================================================================
//
// Program: VoxelRayMarch
// UUID: 43bded93fcbc37f9
// Generated: 2025-11-14 12:07:06
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

#include "43bded93fcbc37f9-SDI.h"

namespace VoxelRayMarch {

// Reference to generic SDI namespace
namespace SDI = ShaderInterface::_43bded93fcbc37f9;

// ============================================================================
// Descriptor Binding Aliases
// ============================================================================

// outputImage (Set 0, Binding 0)
inline constexpr auto& outputImage = SDI::Set0::outputImage;

// ESVOBuffer (Set 0, Binding 1)
inline constexpr auto& ESVOBuffer = SDI::Set0::Binding1;

// BrickBuffer (Set 0, Binding 2)
inline constexpr auto& BrickBuffer = SDI::Set0::Binding2;

// MaterialBuffer (Set 0, Binding 3)
inline constexpr auto& MaterialBuffer = SDI::Set0::Binding3;

// ============================================================================
// Push Constant Member Aliases (With Metadata)
// ============================================================================

// pc (Size: 64 bytes)
using cameraPos = SDI::pc::DataType::pc_0;
constexpr const char* cameraPos_name = "cameraPos";
using time = SDI::pc::DataType::pc_1;
constexpr const char* time_name = "time";
using cameraDir = SDI::pc::DataType::pc_2;
constexpr const char* cameraDir_name = "cameraDir";
using fov = SDI::pc::DataType::pc_3;
constexpr const char* fov_name = "fov";
using cameraUp = SDI::pc::DataType::pc_4;
constexpr const char* cameraUp_name = "cameraUp";
using aspect = SDI::pc::DataType::pc_5;
constexpr const char* aspect_name = "aspect";
using cameraRight = SDI::pc::DataType::pc_6;
constexpr const char* cameraRight_name = "cameraRight";
using pad = SDI::pc::DataType::pc_7;
constexpr const char* pad_name = "pad";

// ============================================================================
// Push Constant Struct Type Aliases
// ============================================================================

using pc = SDI::pc;

// ============================================================================
// UBO/SSBO Member Aliases (With Metadata)
// ============================================================================

// ESVOBuffer (Size: 0 bytes)
using esvoNodes = SDI::ESVOBuffer::esvoNodes_t;

// BrickBuffer (Size: 0 bytes)
using brickData = SDI::BrickBuffer::brickData_t;

// MaterialBuffer (Size: 0 bytes)
using materials = SDI::MaterialBuffer::materials_t;

// ============================================================================
// UBO/SSBO Struct Type Aliases
// ============================================================================

using ESVOBuffer = SDI::ESVOBuffer;
using BrickBuffer = SDI::BrickBuffer;
using MaterialBuffer = SDI::MaterialBuffer;

} // namespace VoxelRayMarch
