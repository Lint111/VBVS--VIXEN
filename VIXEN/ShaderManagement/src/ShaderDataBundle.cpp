#include "ShaderManagement/ShaderDataBundle.h"
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace ShaderManagement {

namespace {

/**
 * @brief Helper to append data to hash stream
 */
template<typename T>
void HashAppend(std::ostringstream& oss, const T& value) {
    oss << value << "|";
}

/**
 * @brief Hash descriptor binding (generalized - no shader-specific data)
 */
void HashDescriptorBinding(std::ostringstream& oss, const SpirvDescriptorBinding& binding) {
    // Include ONLY descriptor layout data
    HashAppend(oss, binding.set);
    HashAppend(oss, binding.binding);
    HashAppend(oss, static_cast<uint32_t>(binding.descriptorType));
    HashAppend(oss, binding.descriptorCount);
    HashAppend(oss, binding.stageFlags);

    // Include variable name (as user requested)
    HashAppend(oss, binding.name);

    // Include type information
    HashAppend(oss, static_cast<uint32_t>(binding.typeInfo.baseType));
    HashAppend(oss, binding.typeInfo.width);
    HashAppend(oss, binding.typeInfo.vecSize);
    HashAppend(oss, binding.typeInfo.columns);
    HashAppend(oss, binding.typeInfo.rows);
    HashAppend(oss, binding.typeInfo.arraySize);

    // Include struct layout if present
    if (binding.structDef) {
        HashAppend(oss, binding.structDef->name);
        HashAppend(oss, binding.structDef->sizeInBytes);
        HashAppend(oss, binding.structDef->alignment);

        for (const auto& member : binding.structDef->members) {
            HashAppend(oss, member.name);
            HashAppend(oss, static_cast<uint32_t>(member.type.baseType));
            HashAppend(oss, member.offset);
            HashAppend(oss, member.arrayStride);
            HashAppend(oss, member.matrixStride);
        }
    }
}

/**
 * @brief Hash push constant range
 */
void HashPushConstant(std::ostringstream& oss, const SpirvPushConstantRange& pc) {
    HashAppend(oss, pc.name);
    HashAppend(oss, pc.offset);
    HashAppend(oss, pc.size);
    HashAppend(oss, pc.stageFlags);

    // Include struct layout
    HashAppend(oss, pc.structDef.name);
    HashAppend(oss, pc.structDef.sizeInBytes);
    for (const auto& member : pc.structDef.members) {
        HashAppend(oss, member.name);
        HashAppend(oss, member.offset);
    }
}

/**
 * @brief Hash vertex input
 */
void HashVertexInput(std::ostringstream& oss, const SpirvVertexInput& input) {
    HashAppend(oss, input.location);
    HashAppend(oss, input.name);
    HashAppend(oss, static_cast<uint32_t>(input.format));
    HashAppend(oss, static_cast<uint32_t>(input.type.baseType));
}

} // anonymous namespace

std::string ComputeDescriptorInterfaceHash(const SpirvReflectionData& reflectionData) {
    std::ostringstream hashInput;

    // Hash descriptor sets (sorted by set index for consistency)
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
            HashDescriptorBinding(hashInput, binding);
        }
    }

    // Hash push constants
    for (const auto& pc : reflectionData.pushConstants) {
        HashPushConstant(hashInput, pc);
    }

    // Hash vertex inputs (sorted by location)
    std::vector<SpirvVertexInput> sortedInputs = reflectionData.vertexInputs;
    std::sort(sortedInputs.begin(), sortedInputs.end(),
        [](const auto& a, const auto& b) { return a.location < b.location; });

    for (const auto& input : sortedInputs) {
        HashVertexInput(hashInput, input);
    }

    // Hash struct definitions (sorted by name for consistency)
    std::vector<SpirvStructDefinition> sortedStructs = reflectionData.structDefinitions;
    std::sort(sortedStructs.begin(), sortedStructs.end(),
        [](const auto& a, const auto& b) { return a.name < b.name; });

    for (const auto& structDef : sortedStructs) {
        HashAppend(hashInput, structDef.name);
        HashAppend(hashInput, structDef.sizeInBytes);
        HashAppend(hashInput, structDef.alignment);

        for (const auto& member : structDef.members) {
            HashAppend(hashInput, member.name);
            HashAppend(hashInput, static_cast<uint32_t>(member.type.baseType));
            HashAppend(hashInput, member.offset);
            HashAppend(hashInput, member.arrayStride);
            HashAppend(hashInput, member.matrixStride);
        }
    }

    // Compute SHA-256
    std::string input = hashInput.str();
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);

    // Convert to hex string
    std::ostringstream hexStream;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        hexStream << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(hash[i]);
    }

    return hexStream.str();
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

} // namespace ShaderManagement
