// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "Nodes/ShaderLibraryNode.h"
#include "ShaderCacheManager.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace Vixen::RenderGraph::Detail {

inline std::filesystem::path FindPhotonShader(const char* shaderName) {
    const std::filesystem::path candidates[] = {
#ifdef VIXEN_SHADER_SOURCE_DIR
        std::filesystem::path(VIXEN_SHADER_SOURCE_DIR) / shaderName,
#endif
        std::filesystem::path("shaders") / shaderName,
        std::filesystem::path("../shaders") / shaderName,
        std::filesystem::path(shaderName),
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) return candidate;
    }
    throw std::runtime_error(std::string(shaderName) +
                             " not found - check photon shader search paths");
}

inline std::string ReadPhotonShader(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Unable to read photon shader " + path.string());
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

inline void RegisterPhotonShader(ShaderLibraryNode& shaderLibrary,
                                 ShaderManagement::ShaderCacheManager* cache,
                                 const char* shaderName,
                                 const char* programName) {
    shaderLibrary.RegisterShaderBuilder(
        [cache, shaderName, programName](int vulkanVersion, int spirvVersion) {
            const auto shaderPath = FindPhotonShader(shaderName);
            ShaderManagement::ShaderBundleBuilder builder;
            builder.SetProgramName(programName)
                   .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
                   .SetTargetVulkanVersion(vulkanVersion)
                   .SetTargetSpirvVersion(spirvVersion)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
                   .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
                   .EnableCaching(cache)
                   .AddStage(ShaderManagement::ShaderStage::Compute,
                             ReadPhotonShader(shaderPath), "main");
            return builder;
        });
}

inline void ConfigurePhotonProducer(ComputeStageNode& node, uint32_t dispatchX) {
    node.SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
    node.SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, dispatchX);
    node.SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
    node.SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
}

} // namespace Vixen::RenderGraph::Detail
