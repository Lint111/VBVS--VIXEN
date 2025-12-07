// ============================================================================
// Shader-Specific Names Header
// ============================================================================
//
// Program: ComputeTest
// UUID: 5dc25d782ed96b29
// Generated: 2025-12-07 20:08:20
//
// This file provides shader-specific constexpr constants and type aliases
// that map to the generic .si.h interface.
//
// Usage: #include "ComputeTestNames.h"
//
// DO NOT MODIFY THIS FILE MANUALLY - it will be regenerated.
//
// ============================================================================

#pragma once

#include "5dc25d782ed96b29-SDI.h"

namespace ComputeTest {

// Reference to generic SDI namespace
namespace SDI = ShaderInterface::_5dc25d782ed96b29;

// Forward declare push constant type to prevent error messages
using pc = SDI::pc;

// ============================================================================
// Descriptor Binding Aliases
// ============================================================================

// outputImage (Set 0, Binding 0)
using outputImage = SDI::Set0::Binding0;
constexpr const char* outputImage_name = "outputImage";

// ============================================================================
// Push Constant Member Aliases (With Metadata)
// ============================================================================

// pc (Size: 16 bytes)
using time = SDI::pc::DataType::pc_0;
constexpr const char* time_name = "time";
using frame = SDI::pc::DataType::pc_1;
constexpr const char* frame_name = "frame";

} // namespace ComputeTest
