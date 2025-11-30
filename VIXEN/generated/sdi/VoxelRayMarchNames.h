// ============================================================================
// Shader-Specific Names Header
// ============================================================================
//
// Program: VoxelRayMarch
// UUID: 8e9aa6689b32a7b7
// Generated: 2025-11-30 18:06:43
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

#include "8e9aa6689b32a7b7-SDI.h"

namespace VoxelRayMarch {

// Reference to generic SDI namespace
namespace SDI = ShaderInterface::_8e9aa6689b32a7b7;

// Forward declare push constant type to prevent error messages
using pc = SDI::pc;

// ============================================================================
// Descriptor Binding Aliases
// ============================================================================

// outputImage (Set 0, Binding 0)
using outputImage = SDI::Set0::Binding0;
constexpr const char* outputImage_name = "outputImage";
// esvoNodes (Set 0, Binding 1)
using esvoNodes = SDI::Set0::Binding1;
constexpr const char* esvoNodes_name = "esvoNodes";
// brickData (Set 0, Binding 2)
using brickData = SDI::Set0::Binding2;
constexpr const char* brickData_name = "brickData";
// materials (Set 0, Binding 3)
using materials = SDI::Set0::Binding3;
constexpr const char* materials_name = "materials";
// traceWriteIndex (Set 0, Binding 4)
using traceWriteIndex = SDI::Set0::Binding4;
constexpr const char* traceWriteIndex_name = "traceWriteIndex";
// octreeConfig (Set 0, Binding 5)
using octreeConfig = SDI::Set0::Binding5;
constexpr const char* octreeConfig_name = "octreeConfig";

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

} // namespace VoxelRayMarch
