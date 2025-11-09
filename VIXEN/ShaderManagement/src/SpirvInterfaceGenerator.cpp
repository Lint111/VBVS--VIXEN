#include "ShaderManagement/SpirvInterfaceGenerator.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <algorithm>

namespace ShaderManagement {

namespace {

/**
 * @brief Get current timestamp as string
 */
std::string GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm timeInfo;
    localtime_s(&timeInfo, &time);
    std::ostringstream oss;
    oss << std::put_time(&timeInfo, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

/**
 * @brief Convert descriptor type to string constant name
 */
std::string DescriptorTypeConstName(VkDescriptorType type) {
    switch (type) {
        case VK_DESCRIPTOR_TYPE_SAMPLER: return "SAMPLER";
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return "COMBINED_IMAGE_SAMPLER";
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return "SAMPLED_IMAGE";
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return "STORAGE_IMAGE";
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return "UNIFORM_BUFFER";
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return "STORAGE_BUFFER";
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: return "UNIFORM_BUFFER_DYNAMIC";
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: return "STORAGE_BUFFER_DYNAMIC";
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: return "INPUT_ATTACHMENT";
        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: return "ACCELERATION_STRUCTURE_KHR";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Convert shader stage flags to string
 */
std::string StageFlagsToString(VkShaderStageFlags flags) {
    std::vector<std::string> stages;

    if (flags & VK_SHADER_STAGE_VERTEX_BIT) stages.push_back("VERTEX");
    if (flags & VK_SHADER_STAGE_FRAGMENT_BIT) stages.push_back("FRAGMENT");
    if (flags & VK_SHADER_STAGE_COMPUTE_BIT) stages.push_back("COMPUTE");
    if (flags & VK_SHADER_STAGE_GEOMETRY_BIT) stages.push_back("GEOMETRY");
    if (flags & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) stages.push_back("TESS_CONTROL");
    if (flags & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) stages.push_back("TESS_EVAL");

    if (stages.empty()) {
        return "ALL";
    }

    std::ostringstream oss;
    for (size_t i = 0; i < stages.size(); ++i) {
        if (i > 0) oss << " | ";
        oss << stages[i];
    }
    return oss.str();
}

/**
 * @brief Sanitize name for C++ identifier
 */
std::string SanitizeName(const std::string& name) {
    std::string sanitized = name;
    std::replace(sanitized.begin(), sanitized.end(), ' ', '_');
    std::replace(sanitized.begin(), sanitized.end(), '-', '_');
    std::replace(sanitized.begin(), sanitized.end(), '.', '_');

    // Ensure name doesn't start with a digit (for UUIDs)
    if (!sanitized.empty() && std::isdigit(static_cast<unsigned char>(sanitized[0]))) {
        sanitized = "_" + sanitized;
    }

    return sanitized;
}

/**
 * @brief Compute FNV-1a hash for struct layout (Phase H: Discovery System)
 *
 * Hashes struct name, total size, and each field's (name, offset, size, type).
 * Matches algorithm in UnknownTypeRegistry::LayoutHasher.
 */
uint64_t ComputeStructLayoutHash(const SpirvStructDefinition& structDef) {
    uint64_t hash = 0xcbf29ce484222325ULL;  // FNV-1a offset basis
    const uint64_t prime = 0x100000001b3ULL;

    // Hash struct name
    for (char c : structDef.name) {
        hash ^= static_cast<uint64_t>(c);
        hash *= prime;
    }

    // Hash total size
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&structDef.sizeInBytes);
        for (size_t i = 0; i < sizeof(structDef.sizeInBytes); ++i) {
            hash ^= static_cast<uint64_t>(bytes[i]);
            hash *= prime;
        }
    }

    // Hash each field
    for (const auto& member : structDef.members) {
        // Hash field name
        for (char c : member.name) {
            hash ^= static_cast<uint64_t>(c);
            hash *= prime;
        }

        // Hash offset
        {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&member.offset);
            for (size_t i = 0; i < sizeof(member.offset); ++i) {
                hash ^= static_cast<uint64_t>(bytes[i]);
                hash *= prime;
            }
        }

        // Hash size
        {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&member.type.sizeInBytes);
            for (size_t i = 0; i < sizeof(member.type.sizeInBytes); ++i) {
                hash ^= static_cast<uint64_t>(bytes[i]);
                hash *= prime;
            }
        }

        // Hash type string (simplified - real implementation would hash base type enum)
        std::string typeStr = member.type.ToCppType();
        for (char c : typeStr) {
            hash ^= static_cast<uint64_t>(c);
            hash *= prime;
        }

        // Hash array size
        {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&member.type.arraySize);
            for (size_t i = 0; i < sizeof(member.type.arraySize); ++i) {
                hash ^= static_cast<uint64_t>(bytes[i]);
                hash *= prime;
            }
        }
    }

    return hash;
}

} // anonymous namespace

// ===== SpirvInterfaceGenerator Implementation =====

SpirvInterfaceGenerator::SpirvInterfaceGenerator(const SdiGeneratorConfig& config)
    : config_(config)
{
    // Ensure output directory exists
    std::filesystem::create_directories(config_.outputDirectory);
}

std::string SpirvInterfaceGenerator::Generate(
    const std::string& uuid,
    const SpirvReflectionData& reflectionData
) {
    try {
        // Generate code to string
        std::string code = GenerateToString(uuid, reflectionData);

        // Write to file
        std::filesystem::path filePath = GetSdiPath(uuid);
        std::ofstream file(filePath);
        if (!file.is_open()) {
            return "";
        }

        file << code;
        file.close();

        return filePath.string();

    } catch (const std::exception& e) {
        LOG_ERROR(std::string("SDI generation failed: ") + e.what());
        return "";
    }
}

std::string SpirvInterfaceGenerator::GenerateToString(
    const std::string& uuid,
    const SpirvReflectionData& reflectionData
) {
    std::ostringstream code;

    // Generate header
    code << GenerateHeader(uuid, reflectionData);

    // Open namespace
    code << GenerateNamespaceBegin(uuid);

    // Generate struct definitions
    code << GenerateStructDefinitions(reflectionData);

    // Generate descriptor info
    code << GenerateDescriptorInfo(reflectionData);

    // Generate push constant info
    code << GeneratePushConstantInfo(reflectionData);

    // Generate vertex input info
    code << GenerateVertexInputInfo(reflectionData);

    // Generate metadata
    code << GenerateMetadata(reflectionData);

    // Generate hash validator
    code << GenerateInterfaceHashValidator(reflectionData);

    // Generate accessor class (if enabled)
    if (config_.generateAccessorHelpers) {
        code << GenerateAccessorClass(reflectionData);
    }

    // Close namespace
    code << GenerateNamespaceEnd();

    return code.str();
}

bool SpirvInterfaceGenerator::DeleteSdi(const std::string& uuid) {
    std::filesystem::path filePath = GetSdiPath(uuid);
    if (std::filesystem::exists(filePath)) {
        return std::filesystem::remove(filePath);
    }
    return false;
}

bool SpirvInterfaceGenerator::SdiExists(const std::string& uuid) const {
    return std::filesystem::exists(GetSdiPath(uuid));
}

std::filesystem::path SpirvInterfaceGenerator::GetSdiPath(const std::string& uuid) const {
    return config_.outputDirectory / (uuid + "-SDI.h");
}

std::string SpirvInterfaceGenerator::GenerateNamesHeader(
    const std::string& programName,
    const std::string& uuid,
    const SpirvReflectionData& reflectionData
) {
    try {
        std::ostringstream code;

        // Header
        code << "// ============================================================================\n";
        code << "// Shader-Specific Names Header\n";
        code << "// ============================================================================\n";
        code << "//\n";
        code << "// Program: " << programName << "\n";
        code << "// UUID: " << uuid << "\n";
        code << "// Generated: " << GetTimestamp() << "\n";
        code << "//\n";
        code << "// This file provides shader-specific constexpr constants and type aliases\n";
        code << "// that map to the generic .si.h interface.\n";
        code << "//\n";
        code << "// Usage: #include \"" << programName << "Names.h\"\n";
        code << "//\n";
        code << "// DO NOT MODIFY THIS FILE MANUALLY - it will be regenerated.\n";
        code << "//\n";
        code << "// ============================================================================\n";
        code << "\n";
        code << "#pragma once\n";
        code << "\n";
        code << "#include \"" << uuid << "-SDI.h\"\n";
        code << "\n";

        // Namespace
        std::string sanitizedName = SanitizeName(programName);
        code << "namespace " << sanitizedName << " {\n";
        code << "\n";

        // Reference to SDI namespace
        std::string sdiNs = config_.namespacePrefix + "::" + SanitizeName(uuid);
        code << "// Reference to generic SDI namespace\n";
        code << "namespace SDI = " << sdiNs << ";\n";
        code << "\n";

        // Generate descriptor binding constants
        if (!reflectionData.descriptorSets.empty()) {
            code << "// ============================================================================\n";
            code << "// Descriptor Binding Constants\n";
            code << "// ============================================================================\n";
            code << "\n";

            for (const auto& [setIndex, bindings] : reflectionData.descriptorSets) {
                for (const auto& binding : bindings) {
                    std::string bindingName = SanitizeName(binding.name);
                    if (bindingName.empty()) {
                        bindingName = "Binding" + std::to_string(binding.binding);
                    }

                    // Generate binding ref struct for use with ConnectVariadic
                    code << "// " << binding.name << " (Set " << setIndex
                         << ", Binding " << binding.binding << ")\n";
                    code << "struct " << bindingName << "_Ref {\n";
                    code << "    using SDI_Type = SDI::Set" << setIndex << "::" << bindingName << ";\n";
                    code << "    static constexpr uint32_t set = SDI_Type::SET;\n";
                    code << "    static constexpr uint32_t binding = SDI_Type::BINDING;\n";
                    code << "    static constexpr VkDescriptorType type = SDI_Type::TYPE;\n";
                    code << "    static constexpr const char* name = \"" << binding.name << "\";\n";
                    code << "};\n";
                    code << "inline constexpr " << bindingName << "_Ref " << bindingName << "{};\n";
                    code << "\n";
                }
            }
        }

        // Generate struct type aliases
        if (!reflectionData.structDefinitions.empty()) {
            code << "// ============================================================================\n";
            code << "// UBO/SSBO Struct Type Aliases\n";
            code << "// ============================================================================\n";
            code << "\n";

            for (const auto& structDef : reflectionData.structDefinitions) {
                code << "using " << structDef.name << " = SDI::" << structDef.name << ";\n";
            }
            code << "\n";
        }

        // Close namespace
        code << "} // namespace " << sanitizedName << "\n";

        // Write to file
        std::filesystem::path filePath = config_.outputDirectory / (programName + "Names.h");
        std::ofstream file(filePath);
        if (!file.is_open()) {
            return "";
        }

        file << code.str();
        file.close();

        return filePath.string();

    } catch (const std::exception& e) {
        LOG_ERROR(std::string("Names header generation failed: ") + e.what());
        return "";
    }
}

// ===== Code Generation Helpers =====

std::string SpirvInterfaceGenerator::GenerateHeader(
    const std::string& uuid,
    const SpirvReflectionData& data
) {
    std::ostringstream code;

    code << "// ============================================================================\n";
    code << "// SPIRV Descriptor Interface (SDI)\n";
    code << "// ============================================================================\n";
    code << "//\n";
    code << "// UUID: " << uuid << "\n";
    code << "// Generated: " << GetTimestamp() << "\n";
    code << "//\n";
    code << "// This file provides compile-time type-safe access to shader resources.\n";
    code << "// It is automatically generated from SPIRV reflection data.\n";
    code << "//\n";
    code << "// DO NOT MODIFY THIS FILE MANUALLY - it will be regenerated.\n";
    code << "//\n";
    code << "// ============================================================================\n";
    code << "\n";
    code << "#pragma once\n";
    code << "\n";
    code << "#include <cstdint>\n";
    code << "#include <vulkan/vulkan.h>\n";
    code << "#include <glm/glm.hpp>\n";
    code << "\n";

    return code.str();
}

std::string SpirvInterfaceGenerator::GenerateNamespaceBegin(const std::string& uuid) {
    std::ostringstream code;
    std::string sanitizedUuid = SanitizeName(uuid);

    code << "namespace " << config_.namespacePrefix << " {\n";
    code << "namespace " << sanitizedUuid << " {\n";
    code << "\n";

    return code.str();
}

std::string SpirvInterfaceGenerator::GenerateNamespaceEnd() {
    std::ostringstream code;
    code << "} // namespace\n";
    code << "} // namespace " << config_.namespacePrefix << "\n";
    return code.str();
}

std::string SpirvInterfaceGenerator::GenerateStructDefinitions(
    const SpirvReflectionData& data
) {
    if (data.structDefinitions.empty()) {
        return "";
    }

    std::ostringstream code;

    code << "// ============================================================================\n";
    code << "// Shader Struct Definitions\n";
    code << "// ============================================================================\n";
    code << "\n";

    for (const auto& structDef : data.structDefinitions) {
        code << GenerateStructDefinition(structDef);
        code << "\n";
    }

    return code.str();
}

std::string SpirvInterfaceGenerator::GenerateStructDefinition(
    const SpirvStructDefinition& structDef
) {
    std::ostringstream code;

    // Compute layout hash for discovery system (Phase H)
    uint64_t layoutHash = ComputeStructLayoutHash(structDef);

    code << "/**\n";
    code << " * @brief " << structDef.name << "\n";
    if (config_.generateLayoutInfo) {
        code << " * Size: " << structDef.sizeInBytes << " bytes\n";
        code << " * Alignment: " << structDef.alignment << " bytes\n";
        code << " * Layout Hash: 0x" << std::hex << layoutHash << std::dec << " (for runtime discovery)\n";
    }
    code << " */\n";
    code << "struct " << structDef.name << " {\n";

    // Emit layout hash as static constexpr for discovery system
    code << Indent(1) << "// Phase H: Discovery system layout hash\n";
    code << Indent(1) << "static constexpr uint64_t LAYOUT_HASH = 0x"
         << std::hex << layoutHash << std::dec << "ULL;\n";
    code << "\n";

    for (const auto& member : structDef.members) {
        if (config_.generateComments) {
            code << Indent(1) << "// Offset: " << member.offset << " bytes";
            if (member.arrayStride > 0) {
                code << ", Array stride: " << member.arrayStride;
            }
            code << "\n";
        }

        code << Indent(1) << member.type.ToCppType() << " " << member.name;

        if (member.type.arraySize > 0) {
            code << "[" << member.type.arraySize << "]";
        }

        code << ";\n";
    }

    code << "};\n";

    return code.str();
}

std::string SpirvInterfaceGenerator::GenerateDescriptorInfo(
    const SpirvReflectionData& data
) {
    if (data.descriptorSets.empty()) {
        return "";
    }

    std::ostringstream code;

    code << "// ============================================================================\n";
    code << "// Descriptor Bindings\n";
    code << "// ============================================================================\n";
    code << "\n";

    for (const auto& [setIndex, bindings] : data.descriptorSets) {
        code << "namespace Set" << setIndex << " {\n";
        code << "\n";

        for (const auto& binding : bindings) {
            std::string bindingName = SanitizeName(binding.name);
            if (bindingName.empty()) {
                bindingName = "Binding" + std::to_string(binding.binding);
            }

            if (config_.generateComments) {
                code << Indent(1) << "/**\n";
                code << Indent(1) << " * @brief " << binding.name << "\n";
                code << Indent(1) << " * Type: " << DescriptorTypeConstName(binding.descriptorType) << "\n";
                code << Indent(1) << " * Stages: " << StageFlagsToString(binding.stageFlags) << "\n";
                code << Indent(1) << " * Count: " << binding.descriptorCount << "\n";
                code << Indent(1) << " */\n";
            }

            code << Indent(1) << "struct " << bindingName << " {\n";
            code << Indent(2) << "static constexpr uint32_t SET = " << setIndex << ";\n";
            code << Indent(2) << "static constexpr uint32_t BINDING = " << binding.binding << ";\n";
            code << Indent(2) << "static constexpr VkDescriptorType TYPE = VK_DESCRIPTOR_TYPE_"
                 << DescriptorTypeConstName(binding.descriptorType) << ";\n";
            code << Indent(2) << "static constexpr uint32_t COUNT = " << binding.descriptorCount << ";\n";
            code << Indent(2) << "static constexpr VkShaderStageFlags STAGES = ";

            // Generate stage flags constant
            if (binding.stageFlags & VK_SHADER_STAGE_VERTEX_BIT) {
                code << "VK_SHADER_STAGE_VERTEX_BIT";
            } else if (binding.stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT) {
                code << "VK_SHADER_STAGE_FRAGMENT_BIT";
            } else if (binding.stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
                code << "VK_SHADER_STAGE_COMPUTE_BIT";
            } else {
                code << "VK_SHADER_STAGE_ALL";
            }
            code << ";\n";

            // If this binding references a struct, include type alias
            if (binding.structDefIndex >= 0 &&
                binding.structDefIndex < static_cast<int>(data.structDefinitions.size())) {
                const auto& structDef = data.structDefinitions[binding.structDefIndex];
                code << Indent(2) << "using DataType = " << structDef.name << ";\n";
            }

            code << Indent(1) << "};\n";
            code << "\n";
        }

        code << "} // namespace Set" << setIndex << "\n";
        code << "\n";
    }

    return code.str();
}

std::string SpirvInterfaceGenerator::GeneratePushConstantInfo(
    const SpirvReflectionData& data
) {
    if (data.pushConstants.empty()) {
        return "";
    }

    std::ostringstream code;

    code << "// ============================================================================\n";
    code << "// Push Constants\n";
    code << "// ============================================================================\n";
    code << "\n";

    // First, generate struct definitions for push constant data types
    for (const auto& pushConst : data.pushConstants) {
        if (!pushConst.structDef.name.empty()) {
            code << GenerateStructDefinition(pushConst.structDef);
            code << "\n";
        }
    }

    // Then generate push constant info structs
    for (const auto& pushConst : data.pushConstants) {
        std::string name = SanitizeName(pushConst.name);

        if (config_.generateComments) {
            code << "/**\n";
            code << " * @brief " << pushConst.name << "\n";
            code << " * Offset: " << pushConst.offset << " bytes\n";
            code << " * Size: " << pushConst.size << " bytes\n";
            code << " * Stages: " << StageFlagsToString(pushConst.stageFlags) << "\n";
            code << " */\n";
        }

        code << "struct " << name << " {\n";
        code << Indent(1) << "static constexpr uint32_t OFFSET = " << pushConst.offset << ";\n";
        code << Indent(1) << "static constexpr uint32_t SIZE = " << pushConst.size << ";\n";
        code << Indent(1) << "static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_ALL;\n";
        if (!pushConst.structDef.name.empty()) {
            code << Indent(1) << "using DataType = " << pushConst.structDef.name << ";\n";
        }
        code << "};\n";
        code << "\n";
    }

    return code.str();
}

std::string SpirvInterfaceGenerator::GenerateVertexInputInfo(
    const SpirvReflectionData& data
) {
    if (data.vertexInputs.empty()) {
        return "";
    }

    std::ostringstream code;

    code << "// ============================================================================\n";
    code << "// Vertex Inputs\n";
    code << "// ============================================================================\n";
    code << "\n";

    code << "namespace VertexInput {\n";
    code << "\n";

    for (const auto& input : data.vertexInputs) {
        std::string name = SanitizeName(input.name);
        if (name.empty()) {
            name = "Attribute" + std::to_string(input.location);
        }

        if (config_.generateComments) {
            code << Indent(1) << "/**\n";
            code << Indent(1) << " * @brief " << input.name << "\n";
            code << Indent(1) << " * Location: " << input.location << "\n";
            code << Indent(1) << " * Type: " << input.type.ToGlslType() << "\n";
            code << Indent(1) << " */\n";
        }

        code << Indent(1) << "struct " << name << " {\n";
        code << Indent(2) << "static constexpr uint32_t LOCATION = " << input.location << ";\n";
        code << Indent(2) << "using DataType = " << input.type.ToCppType() << ";\n";
        code << Indent(1) << "};\n";
        code << "\n";
    }

    code << "} // namespace VertexInput\n";
    code << "\n";

    return code.str();
}

std::string SpirvInterfaceGenerator::GenerateMetadata(
    const SpirvReflectionData& data
) {
    std::ostringstream code;

    code << "// ============================================================================\n";
    code << "// Shader Metadata\n";
    code << "// ============================================================================\n";
    code << "\n";

    code << "struct Metadata {\n";
    code << Indent(1) << "static constexpr const char* PROGRAM_NAME = \"" << data.programName << "\";\n";
    code << Indent(1) << "static constexpr const char* INTERFACE_HASH = \"" << data.interfaceHash << "\";\n";
    code << Indent(1) << "static constexpr uint32_t NUM_DESCRIPTOR_SETS = " << data.descriptorSets.size() << ";\n";
    code << Indent(1) << "static constexpr uint32_t NUM_PUSH_CONSTANTS = " << data.pushConstants.size() << ";\n";
    code << Indent(1) << "static constexpr uint32_t NUM_VERTEX_INPUTS = " << data.vertexInputs.size() << ";\n";
    code << "};\n";
    code << "\n";

    return code.str();
}

std::string SpirvInterfaceGenerator::GenerateInterfaceHashValidator(
    const SpirvReflectionData& data
) {
    std::ostringstream code;

    code << "// ============================================================================\n";
    code << "// Interface Hash Validation\n";
    code << "// ============================================================================\n";
    code << "\n";

    code << "/**\n";
    code << " * @brief Validate that runtime shader matches this interface\n";
    code << " *\n";
    code << " * @param runtimeHash Hash computed from runtime SPIRV bytecode\n";
    code << " * @return True if interface matches\n";
    code << " */\n";
    code << "inline bool ValidateInterfaceHash(const char* runtimeHash) {\n";
    code << Indent(1) << "return std::string(runtimeHash) == Metadata::INTERFACE_HASH;\n";
    code << "}\n";
    code << "\n";

    return code.str();
}

std::string SpirvInterfaceGenerator::GenerateAccessorClass(
    const SpirvReflectionData& data
) {
    // TODO: Generate helper accessor class with methods like:
    // - GetDescriptorSetLayout()
    // - GetPushConstantRanges()
    // - GetVertexInputAttributes()
    // This is a future enhancement
    return "";
}

std::string SpirvInterfaceGenerator::Indent(uint32_t level) const {
    if (!config_.prettyPrint) {
        return "";
    }
    return std::string(level * 4, ' ');
}

// ===== SdiFileManager Implementation =====

SdiFileManager::SdiFileManager(const std::filesystem::path& sdiDirectory)
    : sdiDirectory_(sdiDirectory)
{
    std::filesystem::create_directories(sdiDirectory_);
    LoadRegistry();
}

void SdiFileManager::RegisterSdi(const std::string& uuid, const std::filesystem::path& filePath) {
    registeredSdis_[uuid] = filePath;
    SaveRegistry();
}

bool SdiFileManager::UnregisterSdi(const std::string& uuid, bool deleteFile) {
    auto it = registeredSdis_.find(uuid);
    if (it == registeredSdis_.end()) {
        return false;
    }

    if (deleteFile && std::filesystem::exists(it->second)) {
        std::filesystem::remove(it->second);
    }

    registeredSdis_.erase(it);
    SaveRegistry();
    return true;
}

std::vector<std::string> SdiFileManager::GetRegisteredUuids() const {
    std::vector<std::string> uuids;
    uuids.reserve(registeredSdis_.size());
    for (const auto& [uuid, _] : registeredSdis_) {
        uuids.push_back(uuid);
    }
    return uuids;
}

uint32_t SdiFileManager::CleanupOrphans() {
    uint32_t count = 0;

    // Find all SDI files in directory
    for (const auto& entry : std::filesystem::directory_iterator(sdiDirectory_)) {
        if (entry.is_regular_file() && entry.path().extension() == ".h") {
            std::string filename = entry.path().stem().string();

            // Check if it's an SDI file (ends with -SDI)
            if (filename.size() > 4 && filename.substr(filename.size() - 4) == "-SDI") {
                // Extract UUID
                std::string uuid = filename.substr(0, filename.size() - 4);

                // Check if registered
                if (registeredSdis_.find(uuid) == registeredSdis_.end()) {
                    std::filesystem::remove(entry.path());
                    ++count;
                }
            }
        }
    }

    return count;
}

uint32_t SdiFileManager::DeleteAll() {
    uint32_t count = 0;

    for (const auto& [uuid, filePath] : registeredSdis_) {
        if (std::filesystem::exists(filePath)) {
            std::filesystem::remove(filePath);
            ++count;
        }
    }

    registeredSdis_.clear();
    SaveRegistry();

    return count;
}

std::filesystem::path SdiFileManager::GetSdiPath(const std::string& uuid) const {
    auto it = registeredSdis_.find(uuid);
    if (it != registeredSdis_.end()) {
        return it->second;
    }
    return {};
}

bool SdiFileManager::IsRegistered(const std::string& uuid) const {
    return registeredSdis_.find(uuid) != registeredSdis_.end();
}

void SdiFileManager::LoadRegistry() {
    std::filesystem::path registryPath = sdiDirectory_ / "sdi_registry.txt";

    if (!std::filesystem::exists(registryPath)) {
        return;
    }

    std::ifstream file(registryPath);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        size_t sep = line.find('=');
        if (sep != std::string::npos) {
            std::string uuid = line.substr(0, sep);
            std::string path = line.substr(sep + 1);
            registeredSdis_[uuid] = path;
        }
    }

    file.close();
}

void SdiFileManager::SaveRegistry() {
    std::filesystem::path registryPath = sdiDirectory_ / "sdi_registry.txt";

    std::ofstream file(registryPath);
    if (!file.is_open()) {
        return;
    }

    for (const auto& [uuid, filePath] : registeredSdis_) {
        file << uuid << "=" << filePath.string() << "\n";
    }

    file.close();
}

} // namespace ShaderManagement
