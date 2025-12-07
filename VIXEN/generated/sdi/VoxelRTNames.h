// ============================================================================
// Shader-Specific Names Header
// ============================================================================
//
// Program: VoxelRT
// UUID: 9f672bc594467878
// Generated: 2025-12-07 16:44:40
//
// This file provides shader-specific constexpr constants and type aliases
// that map to the generic .si.h interface.
//
// Usage: #include "VoxelRTNames.h"
//
// DO NOT MODIFY THIS FILE MANUALLY - it will be regenerated.
//
// ============================================================================

#pragma once

#include "9f672bc594467878-SDI.h"

namespace VoxelRT {

// Reference to generic SDI namespace
namespace SDI = ShaderInterface::_9f672bc594467878;

// Forward declare push constant type to prevent error messages
using pc = SDI::pc;

// ============================================================================
// Descriptor Binding Aliases
// ============================================================================

// outputImage (Set 0, Binding 0)
using outputImage = SDI::Set0::Binding0;
constexpr const char* outputImage_name = "outputImage";
// topLevelAS (Set 0, Binding 1)
using topLevelAS = SDI::Set0::Binding1;
constexpr const char* topLevelAS_name = "topLevelAS";
// aabbBuffer (Set 0, Binding 2)
using aabbBuffer = SDI::Set0::Binding2;
constexpr const char* aabbBuffer_name = "aabbBuffer";

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
using debugMode = SDI::pc::DataType::pc_7;
constexpr const char* debugMode_name = "debugMode";

} // namespace VoxelRT
