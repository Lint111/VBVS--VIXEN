#pragma once

#include "DescriptorLayoutSpec.h"
#include "ShaderProgram.h"
#include <memory>

namespace ShaderManagement {

/**
 * @brief Reflect descriptor layout from compiled SPIRV bytecode
 * 
 * Uses SPIRV-Reflect to parse shader bytecode and extract:
 * - Descriptor set bindings (uniforms, samplers, storage buffers, etc.)
 * - Binding indices, types, counts, and shader stage access
 * 
 * Merges bindings from all stages in the program (vertex, fragment, etc.)
 * and validates that bindings with the same index have compatible types.
 * 
 * @param program Compiled shader program with SPIRV bytecode
 * @return DescriptorLayoutSpec with all reflected bindings, or nullptr on error
 */
std::unique_ptr<DescriptorLayoutSpec> ReflectDescriptorLayout(const CompiledProgram& program);

/**
 * @brief Reflect vertex input attributes from vertex shader SPIRV
 * 
 * Extracts vertex input locations, formats, and offsets.
 * Future enhancement - not implemented yet.
 * 
 * @param vertexSpirv Vertex shader SPIRV bytecode
 * @return Vertex input attribute list
 */
// std::vector<VertexInputAttribute> ReflectVertexInputs(const std::vector<uint32_t>& vertexSpirv);

/**
 * @brief Reflect push constant ranges from SPIRV
 * 
 * Extracts push constant blocks and their sizes.
 * Future enhancement - not implemented yet.
 * 
 * @param program Compiled shader program
 * @return Push constant ranges
 */
// std::vector<PushConstantRange> ReflectPushConstants(const CompiledProgram& program);

} // namespace ShaderManagement
