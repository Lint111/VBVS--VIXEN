#include "ShaderBundleSerializer.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

namespace ShaderManagement {

bool ShaderBundleSerializer::SaveToJson(
    const ShaderDataBundle& bundle,
    const std::filesystem::path& outputPath,
    const BundleSerializerConfig& config
) {
    namespace fs = std::filesystem;
    nlohmann::json j;

    j["uuid"] = bundle.uuid;
    j["programName"] = bundle.program.name;
    j["pipelineType"] = static_cast<int>(bundle.program.pipelineType);
    j["descriptorInterfaceHash"] = bundle.descriptorInterfaceHash;
    j["sdiHeaderPath"] = bundle.sdiHeaderPath.string();
    j["sdiNamespace"] = bundle.sdiNamespace;

    // Save stages
    j["stages"] = nlohmann::json::array();
    for (const auto& stage : bundle.program.stages) {
        nlohmann::json stageJson;
        stageJson["stage"] = static_cast<int>(stage.stage);
        stageJson["entryPoint"] = stage.entryPoint;
        stageJson["spirvSize"] = stage.spirvCode.size();

        if (config.embedSpirv) {
            // Embed SPIRV directly in JSON (prevents orphaned .spv files)
            std::vector<uint32_t> spirvCopy = stage.spirvCode;  // nlohmann::json needs lvalue
            stageJson["spirvData"] = spirvCopy;
        } else {
            // Save SPIRV to separate file
            fs::path spirvPath = outputPath.parent_path() /
                (bundle.uuid + "_stage" + std::to_string(static_cast<int>(stage.stage)) + ".spv");

            std::ofstream spirvFile(spirvPath, std::ios::binary);
            if (!spirvFile.is_open()) {
                std::cerr << "Error: Failed to create SPIRV file: " << spirvPath << "\n";
                return false;
            }

            spirvFile.write(
                reinterpret_cast<const char*>(stage.spirvCode.data()),
                static_cast<std::streamsize>(stage.spirvCode.size() * sizeof(uint32_t))
            );
            spirvFile.close();

            stageJson["spirvFile"] = spirvPath.string();

            // Notify caller of written file (for manifest tracking)
            if (config.onFileWritten) {
                config.onFileWritten(spirvPath);
            }
        }

        j["stages"].push_back(stageJson);
    }

    // Write JSON
    std::ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        std::cerr << "Error: Failed to open output file: " << outputPath << "\n";
        return false;
    }

    outFile << j.dump(2);
    outFile.close();

    // Notify caller of bundle JSON file
    if (config.onFileWritten) {
        config.onFileWritten(outputPath);
    }

    return true;
}

bool ShaderBundleSerializer::LoadFromJson(
    const std::filesystem::path& jsonPath,
    ShaderDataBundle& bundle
) {
    namespace fs = std::filesystem;

    std::ifstream inFile(jsonPath);
    if (!inFile.is_open()) {
        std::cerr << "Error: Failed to open bundle file: " << jsonPath << "\n";
        return false;
    }

    nlohmann::json j;
    try {
        inFile >> j;
    } catch (const std::exception& e) {
        std::cerr << "Error: Failed to parse JSON: " << e.what() << "\n";
        return false;
    }

    bundle.uuid = j["uuid"];
    bundle.program.name = j["programName"];
    bundle.program.pipelineType = static_cast<PipelineTypeConstraint>(j["pipelineType"]);
    bundle.descriptorInterfaceHash = j["descriptorInterfaceHash"];
    bundle.sdiHeaderPath = j["sdiHeaderPath"].get<std::string>();
    bundle.sdiNamespace = j["sdiNamespace"];

    // Load stages
    for (const auto& stageJson : j["stages"]) {
        CompiledShaderStage stage;
        stage.stage = static_cast<ShaderStage>(stageJson["stage"]);
        stage.entryPoint = stageJson["entryPoint"];

        // Load SPIRV - check for embedded vs external file
        if (stageJson.contains("spirvData")) {
            // Embedded SPIRV (stored directly in JSON)
            stage.spirvCode = stageJson["spirvData"].get<std::vector<uint32_t>>();
        } else if (stageJson.contains("spirvFile")) {
            // External SPIRV file
            fs::path spirvPath = stageJson["spirvFile"].get<std::string>();
            std::ifstream spirvFile(spirvPath, std::ios::binary | std::ios::ate);
            if (!spirvFile.is_open()) {
                std::cerr << "Error: Failed to open SPIRV file: " << spirvPath << "\n";
                return false;
            }

            auto fileSize = static_cast<size_t>(spirvFile.tellg());
            spirvFile.seekg(0);

            stage.spirvCode.resize(fileSize / sizeof(uint32_t));
            spirvFile.read(reinterpret_cast<char*>(stage.spirvCode.data()),
                          static_cast<std::streamsize>(fileSize));
            spirvFile.close();
        } else {
            std::cerr << "Error: Stage missing both spirvData and spirvFile\n";
            return false;
        }

        bundle.program.stages.push_back(std::move(stage));
    }

    return true;
}

std::string ShaderBundleSerializer::LoadUuid(const std::filesystem::path& jsonPath) {
    if (!std::filesystem::exists(jsonPath)) {
        return "";
    }

    std::ifstream inFile(jsonPath);
    if (!inFile.is_open()) {
        return "";
    }

    try {
        nlohmann::json j;
        inFile >> j;
        if (j.contains("uuid")) {
            return j["uuid"].get<std::string>();
        }
    } catch (const std::exception&) {
        // Ignore parse errors - return empty
    }

    return "";
}

bool ShaderBundleSerializer::IsValidBundle(const std::filesystem::path& jsonPath) {
    if (!std::filesystem::exists(jsonPath)) {
        return false;
    }

    std::ifstream inFile(jsonPath);
    if (!inFile.is_open()) {
        return false;
    }

    try {
        nlohmann::json j;
        inFile >> j;
        // Check for required fields
        return j.contains("uuid") &&
               j.contains("programName") &&
               j.contains("stages") &&
               j["stages"].is_array();
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace ShaderManagement
