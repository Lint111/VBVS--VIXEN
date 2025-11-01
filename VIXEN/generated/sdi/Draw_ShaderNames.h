// ============================================================================
// Shader-Specific Names Header
// ============================================================================
//
// Program: Draw_Shader
// UUID: 2071dff093caf4b3
// Generated: 2025-11-01 14:29:42
//
// This file provides shader-specific constexpr constants and type aliases
// that map to the generic .si.h interface.
//
// Usage: #include "Draw_ShaderNames.h"
//
// DO NOT MODIFY THIS FILE MANUALLY - it will be regenerated.
//
// ============================================================================

#pragma once

#include "2071dff093caf4b3-SDI.h"

namespace Draw_Shader {

// Reference to generic SDI namespace
namespace SDI = ShaderInterface::_2071dff093caf4b3;

// ============================================================================
// Descriptor Binding Constants
// ============================================================================

// myBufferVals (Set 0, Binding 0)
using myBufferVals_t = SDI::Set0::myBufferVals;
constexpr uint32_t myBufferVals_SET = myBufferVals_t::SET;
constexpr uint32_t myBufferVals_BINDING = myBufferVals_t::BINDING;
constexpr VkDescriptorType myBufferVals_TYPE = myBufferVals_t::TYPE;

// tex (Set 0, Binding 1)
using tex_t = SDI::Set0::tex;
constexpr uint32_t tex_SET = tex_t::SET;
constexpr uint32_t tex_BINDING = tex_t::BINDING;
constexpr VkDescriptorType tex_TYPE = tex_t::TYPE;

// ============================================================================
// UBO/SSBO Struct Type Aliases
// ============================================================================

using bufferVals = SDI::bufferVals;

} // namespace Draw_Shader
