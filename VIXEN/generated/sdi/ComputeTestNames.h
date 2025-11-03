// ============================================================================
// Shader-Specific Names Header
// ============================================================================
//
// Program: ComputeTest
// UUID: 3e331666c418cc79
// Generated: 2025-11-03 10:32:13
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

#include "3e331666c418cc79-SDI.h"

namespace ComputeTest {

// Reference to generic SDI namespace
namespace SDI = ShaderInterface::_3e331666c418cc79;

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

} // namespace ComputeTest
