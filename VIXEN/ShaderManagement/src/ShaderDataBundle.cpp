#include "ShaderManagement/ShaderDataBundle.h"
#include "ShaderManagement/Hash.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

namespace ShaderManagement {

namespace {

/**
 * @brief Efficient binary hash builder
 *
 * Appends data directly as binary instead of converting to strings.
 * 10-20x faster than string stream approach.
 */
class BinaryHashBuilder {
public:
    BinaryHashBuilder() {
        buffer_.reserve(4096);  // Pre-allocate to avoid reallocations
    }

    // Append POD types directly as binary
    template<typename T>
    typename std::enable_if<std::is_trivially_copyable<T>::value, void>::type
    Append(const T& value) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
        buffer_.insert(buffer_.end(), bytes, bytes + sizeof(T));
    }

    // Append strings (length-prefixed for unambiguous parsing)
    void Append(const std::string& str) {
        uint32_t len = static_cast<uint32_t>(str.size());
        Append(len);  // Length prefix
        buffer_.insert(buffer_.end(), str.begin(), str.end());
    }

    const std::vector<uint8_t>& GetBuffer() const { return buffer_; }

private:
    std::vector<uint8_t> buffer_;
};

/**
 * @brief VixenHash descriptor binding (binary - optimized)
 */
void HashDescriptorBinding(BinaryHashBuilder& builder, const SpirvDescriptorBinding& binding) {
    // Include ONLY descriptor layout data
    builder.Append(binding.set);
    builder.Append(binding.binding);
    builder.Append(static_cast<uint32_t>(binding.descriptorType));
    builder.Append(binding.descriptorCount);
    builder.Append(binding.stageFlags);

    // Include variable name
    builder.Append(binding.name);

    // Include type information
    builder.Append(static_cast<uint32_t>(binding.typeInfo.baseType));
    builder.Append(binding.typeInfo.width);
    builder.Append(binding.typeInfo.vecSize);
    builder.Append(binding.typeInfo.columns);
    builder.Append(binding.typeInfo.rows);
    builder.Append(binding.typeInfo.arraySize);

    // TODO: Include struct layout in hash (currently using index-based struct linking)
    // Struct data is stored in SpirvReflectionData::structDefinitions
    builder.Append(binding.structDefIndex);
}

/**
 * @brief VixenHash push constant range (binary - optimized)
 */
void HashPushConstant(BinaryHashBuilder& builder, const SpirvPushConstantRange& pc) {
    builder.Append(pc.name);
    builder.Append(pc.offset);
    builder.Append(pc.size);
    builder.Append(pc.stageFlags);

    // Include struct layout
    builder.Append(pc.structDef.name);
    builder.Append(pc.structDef.sizeInBytes);
    builder.Append(static_cast<uint32_t>(pc.structDef.members.size()));
    for (const auto& member : pc.structDef.members) {
        builder.Append(member.name);
        builder.Append(member.offset);
    }
}

/**
 * @brief VixenHash vertex input (binary - optimized)
 */
void HashVertexInput(BinaryHashBuilder& builder, const SpirvVertexInput& input) {
    builder.Append(input.location);
    builder.Append(input.name);
    builder.Append(static_cast<uint32_t>(input.format));
    builder.Append(static_cast<uint32_t>(input.type.baseType));
}

} // anonymous namespace

std::string ComputeDescriptorInterfaceHash(const SpirvReflectionData& reflectionData) {
    BinaryHashBuilder builder;

    // VixenHash descriptor sets (sorted by set index for consistency)
    std::vector<uint32_t> setIndices;
    for (const auto& [setIdx, _] : reflectionData.descriptorSets) {
        setIndices.push_back(setIdx);
    }
    std::sort(setIndices.begin(), setIndices.end());

    for (uint32_t setIdx : setIndices) {
        const auto& bindings = reflectionData.descriptorSets.at(setIdx);

        // Sort bindings by binding index
        std::vector<SpirvDescriptorBinding> sortedBindings = bindings;
        std::sort(sortedBindings.begin(), sortedBindings.end(),
            [](const auto& a, const auto& b) { return a.binding < b.binding; });

        for (const auto& binding : sortedBindings) {
            HashDescriptorBinding(builder, binding);
        }
    }

    // VixenHash push constants
    for (const auto& pc : reflectionData.pushConstants) {
        HashPushConstant(builder, pc);
    }

    // VixenHash vertex inputs (sorted by location)
    std::vector<SpirvVertexInput> sortedInputs = reflectionData.vertexInputs;
    std::sort(sortedInputs.begin(), sortedInputs.end(),
        [](const auto& a, const auto& b) { return a.location < b.location; });

    for (const auto& input : sortedInputs) {
        HashVertexInput(builder, input);
    }

    // VixenHash struct definitions (sorted by name for consistency)
    std::vector<SpirvStructDefinition> sortedStructs = reflectionData.structDefinitions;
    std::sort(sortedStructs.begin(), sortedStructs.end(),
        [](const auto& a, const auto& b) { return a.name < b.name; });

    for (const auto& structDef : sortedStructs) {
        builder.Append(structDef.name);
        builder.Append(structDef.sizeInBytes);
        builder.Append(structDef.alignment);

        builder.Append(static_cast<uint32_t>(structDef.members.size()));
        for (const auto& member : structDef.members) {
            builder.Append(member.name);
            builder.Append(static_cast<uint32_t>(member.type.baseType));
            builder.Append(member.offset);
            builder.Append(member.arrayStride);
            builder.Append(member.matrixStride);
        }
    }

    const auto& buffer = builder.GetBuffer();
    return ShaderManagement::ComputeSHA256Hex(buffer);
}

ShaderDirtyFlags CompareBundles(
    const ShaderDataBundle& oldBundle,
    ShaderDataBundle& newBundle
) {
    ShaderDirtyFlags flags = ShaderDirtyFlags::None;

    if (!oldBundle.reflectionData || !newBundle.reflectionData) {
        // Can't compare without reflection data
        return ShaderDirtyFlags::All;
    }

    const auto& oldData = *oldBundle.reflectionData;
    const auto& newData = *newBundle.reflectionData;

    // Compare descriptor-only interface hash (fast check)
    if (oldBundle.descriptorInterfaceHash != newBundle.descriptorInterfaceHash) {
        // Interfaces differ - need detailed comparison

        // Check descriptor sets
        if (oldData.descriptorSets.size() != newData.descriptorSets.size()) {
            flags |= ShaderDirtyFlags::DescriptorSets;
        } else {
            // Compare each set
            for (const auto& [setIdx, newBindings] : newData.descriptorSets) {
                auto oldIt = oldData.descriptorSets.find(setIdx);
                if (oldIt == oldData.descriptorSets.end()) {
                    flags |= ShaderDirtyFlags::DescriptorSets;
                    continue;
                }

                const auto& oldBindings = oldIt->second;
                if (oldBindings.size() != newBindings.size()) {
                    flags |= ShaderDirtyFlags::DescriptorBindings;
                }

                // Compare bindings
                for (const auto& newBinding : newBindings) {
                    auto oldBindingIt = std::find_if(oldBindings.begin(), oldBindings.end(),
                        [&](const auto& b) { return b.binding == newBinding.binding; });

                    if (oldBindingIt == oldBindings.end()) {
                        flags |= ShaderDirtyFlags::DescriptorBindings;
                    } else {
                        if (oldBindingIt->descriptorType != newBinding.descriptorType) {
                            flags |= ShaderDirtyFlags::DescriptorTypes;
                        }
                    }
                }
            }
        }

        // Check push constants
        if (oldData.pushConstants.size() != newData.pushConstants.size()) {
            flags |= ShaderDirtyFlags::PushConstants;
        } else {
            for (size_t i = 0; i < oldData.pushConstants.size(); ++i) {
                if (oldData.pushConstants[i].size != newData.pushConstants[i].size ||
                    oldData.pushConstants[i].offset != newData.pushConstants[i].offset) {
                    flags |= ShaderDirtyFlags::PushConstants;
                }
            }
        }

        // Check vertex inputs
        if (oldData.vertexInputs.size() != newData.vertexInputs.size()) {
            flags |= ShaderDirtyFlags::VertexInputs;
        } else {
            for (size_t i = 0; i < oldData.vertexInputs.size(); ++i) {
                if (oldData.vertexInputs[i].location != newData.vertexInputs[i].location ||
                    oldData.vertexInputs[i].format != newData.vertexInputs[i].format) {
                    flags |= ShaderDirtyFlags::VertexInputs;
                }
            }
        }

        // Check struct layouts
        if (oldData.structDefinitions.size() != newData.structDefinitions.size()) {
            flags |= ShaderDirtyFlags::StructLayouts;
        } else {
            for (size_t i = 0; i < oldData.structDefinitions.size(); ++i) {
                const auto& oldStruct = oldData.structDefinitions[i];
                const auto& newStruct = newData.structDefinitions[i];

                if (oldStruct.sizeInBytes != newStruct.sizeInBytes ||
                    oldStruct.alignment != newStruct.alignment ||
                    oldStruct.members.size() != newStruct.members.size()) {
                    flags |= ShaderDirtyFlags::StructLayouts;
                    break;
                }

                for (size_t j = 0; j < oldStruct.members.size(); ++j) {
                    if (oldStruct.members[j].offset != newStruct.members[j].offset) {
                        flags |= ShaderDirtyFlags::StructLayouts;
                        break;
                    }
                }
            }
        }
    }

    // Compare SPIRV bytecode
    bool spirvChanged = false;
    if (oldBundle.program.stages.size() == newBundle.program.stages.size()) {
        for (size_t i = 0; i < oldBundle.program.stages.size(); ++i) {
            const auto& oldStage = oldBundle.program.stages[i];
            const auto& newStage = newBundle.program.stages[i];

            if (oldStage.spirvCode != newStage.spirvCode) {
                spirvChanged = true;
                break;
            }
        }
    } else {
        spirvChanged = true;
    }

    if (spirvChanged) {
        flags |= ShaderDirtyFlags::Spirv;
    }

    // If nothing changed but we compared, mark as metadata only
    if (flags == ShaderDirtyFlags::None && spirvChanged) {
        flags = ShaderDirtyFlags::MetadataOnly;
    }

    // Set dirty flags on new bundle
    newBundle.dirtyFlags = flags;

    return flags;
}

void ShaderDataBundle::ValidateDescriptorPairing() const {
    if (!reflectionData) {
        return;  // No reflection data to validate
    }

    for (const auto& [setIndex, bindings] : reflectionData->descriptorSets) {
        // Collect samplers and sampled images
        std::vector<const SpirvDescriptorBinding*> samplers;
        std::vector<const SpirvDescriptorBinding*> sampledImages;
        std::vector<const SpirvDescriptorBinding*> combinedImageSamplers;
        std::vector<const SpirvDescriptorBinding*> storageImages;  // Output images

        for (const auto& binding : bindings) {
            switch (binding.descriptorType) {
                case VK_DESCRIPTOR_TYPE_SAMPLER:
                    samplers.push_back(&binding);
                    break;
                case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                    sampledImages.push_back(&binding);
                    break;
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                    combinedImageSamplers.push_back(&binding);
                    break;
                case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    storageImages.push_back(&binding);  // Typically outputs, don't need samplers
                    break;
                default:
                    break;
            }
        }

        // Combined image samplers are self-contained, no validation needed
        // Only validate separate samplers + sampled images when BOTH are present

        if (samplers.empty() || sampledImages.empty()) {
            // If only samplers or only images are present, that's fine:
            // - Samplers alone: used with samplerless images elsewhere
            // - Images alone: storage images, compute shader outputs, etc.
            continue;
        }

        // BOTH samplers and sampled images present → must pair correctly
        // Check for dangling samplers (sampler without corresponding texture)
        for (const auto* sampler : samplers) {
            bool foundPair = false;
            std::string expectedTextureName;

            // Convention: "colorTextureSampler" → "colorTexture"
            if (sampler->name.size() > 7 && sampler->name.substr(sampler->name.size() - 7) == "Sampler") {
                expectedTextureName = sampler->name.substr(0, sampler->name.size() - 7);

                for (const auto* texture : sampledImages) {
                    if (texture->name == expectedTextureName) {
                        foundPair = true;
                        break;
                    }
                }
            }

            if (!foundPair) {
                std::ostringstream oss;
                oss << "Shader '" << program.name << "' validation error:\n";
                oss << "  Dangling sampler at set " << setIndex << ", binding " << sampler->binding
                    << " ('" << sampler->name << "')\n";
                oss << "  Expected paired texture: '" << expectedTextureName << "'\n";
                oss << "  Available textures in set " << setIndex << ":\n";
                for (const auto* tex : sampledImages) {
                    oss << "    - " << tex->name << " (binding " << tex->binding << ")\n";
                }
                oss << "\nConvention: sampler should be named '<textureName>Sampler'\n";
                oss << "Example: 'colorTexture' + 'colorTextureSampler'";
                throw std::runtime_error(oss.str());
            }
        }

        // Check for dangling textures (texture without corresponding sampler)
        for (const auto* texture : sampledImages) {
            bool foundPair = false;
            std::string expectedSamplerName = texture->name + "Sampler";

            for (const auto* sampler : samplers) {
                if (sampler->name == expectedSamplerName) {
                    foundPair = true;
                    break;
                }
            }

            if (!foundPair) {
                std::ostringstream oss;
                oss << "Shader '" << program.name << "' validation error:\n";
                oss << "  Dangling texture at set " << setIndex << ", binding " << texture->binding
                    << " ('" << texture->name << "')\n";
                oss << "  Expected paired sampler: '" << expectedSamplerName << "'\n";
                oss << "  Available samplers in set " << setIndex << ":\n";
                for (const auto* samp : samplers) {
                    oss << "    - " << samp->name << " (binding " << samp->binding << ")\n";
                }
                oss << "\nConvention: sampler should be named '<textureName>Sampler'\n";
                oss << "Example: 'colorTexture' + 'colorTextureSampler'";
                throw std::runtime_error(oss.str());
            }
        }
    }
}

const SpirvDescriptorBinding* ShaderDataBundle::FindPairedSampler(
    uint32_t setIndex,
    const SpirvDescriptorBinding& textureBinding
) const {
    if (!reflectionData) {
        return nullptr;
    }

    auto it = reflectionData->descriptorSets.find(setIndex);
    if (it == reflectionData->descriptorSets.end()) {
        return nullptr;
    }

    const auto& bindings = it->second;

    // First pass: look for naming convention match
    std::string expectedSamplerName = textureBinding.name + "Sampler";
    for (const auto& binding : bindings) {
        if (binding.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER &&
            binding.name == expectedSamplerName) {
            return &binding;
        }
    }

    // Second pass: fallback to any sampler in the same set
    for (const auto& binding : bindings) {
        if (binding.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) {
            return &binding;
        }
    }

    return nullptr;
}

} // namespace ShaderManagement
