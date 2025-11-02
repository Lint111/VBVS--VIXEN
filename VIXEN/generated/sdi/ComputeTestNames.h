// ============================================================================
// Shader-Specific Names Header
// ============================================================================
//
// Program: ComputeTest
// UUID: 7a57264d155fdf74
// Generated: 2025-11-02 18:21:06
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

#include "7a57264d155fdf74-SDI.h"

namespace ComputeTest {

// Reference to generic SDI namespace
namespace SDI = ShaderInterface::_7a57264d155fdf74;

// ============================================================================
// Descriptor Binding Constants
// ============================================================================

// outputImage (Set 0, Binding 0)
using outputImage_t = SDI::Set0::outputImage;
constexpr uint32_t outputImage_SET = outputImage_t::SET;
constexpr uint32_t outputImage_BINDING = outputImage_t::BINDING;
constexpr VkDescriptorType outputImage_TYPE = outputImage_t::TYPE;

} // namespace ComputeTest
