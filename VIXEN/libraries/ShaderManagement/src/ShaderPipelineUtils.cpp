#include "ShaderPipelineUtils.h"
#include <iostream>

namespace ShaderManagement {

std::optional<ShaderStage> ShaderPipelineUtils::DetectStageFromPath(const std::filesystem::path& path) {
    std::string ext = path.extension().string();

    // Handle both ".vert" and "vert" formats
    if (!ext.empty() && ext[0] == '.') {
        ext = ext.substr(1);
    }

    if (ext == "vert") return ShaderStage::Vertex;
    if (ext == "frag") return ShaderStage::Fragment;
    if (ext == "comp") return ShaderStage::Compute;
    if (ext == "geom") return ShaderStage::Geometry;
    if (ext == "tesc") return ShaderStage::TessControl;
    if (ext == "tese") return ShaderStage::TessEval;
    if (ext == "mesh") return ShaderStage::Mesh;
    if (ext == "task") return ShaderStage::Task;
    if (ext == "rgen") return ShaderStage::RayGen;
    if (ext == "rmiss") return ShaderStage::Miss;
    if (ext == "rchit") return ShaderStage::ClosestHit;
    if (ext == "rahit") return ShaderStage::AnyHit;
    if (ext == "rint") return ShaderStage::Intersection;
    if (ext == "rcall") return ShaderStage::Callable;

    return std::nullopt;
}

std::optional<PipelineTypeConstraint> ShaderPipelineUtils::DetectPipelineFromExtension(const std::string& extension) {
    std::string ext = extension;

    // Handle both ".vert" and "vert" formats
    if (!ext.empty() && ext[0] == '.') {
        ext = ext.substr(1);
    }

    // Compute pipeline
    if (ext == "comp") {
        return PipelineTypeConstraint::Compute;
    }

    // Ray tracing pipeline
    if (ext == "rgen" || ext == "rmiss" || ext == "rchit" ||
        ext == "rahit" || ext == "rint" || ext == "rcall") {
        return PipelineTypeConstraint::RayTracing;
    }

    // Mesh shading pipeline
    if (ext == "mesh" || ext == "task") {
        return PipelineTypeConstraint::Mesh;
    }

    // Graphics pipeline (traditional rasterization)
    if (ext == "vert" || ext == "frag" || ext == "geom" ||
        ext == "tesc" || ext == "tese") {
        return PipelineTypeConstraint::Graphics;
    }

    return std::nullopt;
}

PipelineDetectionResult ShaderPipelineUtils::DetectPipelineFromFiles(const std::vector<std::string>& files) {
    PipelineDetectionResult result;

    bool hasRayTracing = false;
    bool hasMesh = false;
    bool hasCompute = false;
    bool hasGraphics = false;

    std::string rtStage, meshStage, computeStage, graphicsStage;

    for (const auto& file : files) {
        std::filesystem::path filePath(file);
        std::string ext = filePath.extension().string();

        auto detected = DetectPipelineFromExtension(ext);
        if (!detected) continue;

        switch (*detected) {
            case PipelineTypeConstraint::RayTracing:
                hasRayTracing = true;
                if (rtStage.empty()) rtStage = ext;
                break;
            case PipelineTypeConstraint::Mesh:
                hasMesh = true;
                if (meshStage.empty()) meshStage = ext;
                break;
            case PipelineTypeConstraint::Compute:
                hasCompute = true;
                if (computeStage.empty()) computeStage = ext;
                break;
            case PipelineTypeConstraint::Graphics:
                hasGraphics = true;
                if (graphicsStage.empty()) graphicsStage = ext;
                break;
        }
    }

    // Priority: RayTracing > Mesh > Compute > Graphics
    if (hasRayTracing) {
        result.type = PipelineTypeConstraint::RayTracing;
        result.reason = "detected ray tracing stage (" + rtStage + ")";
        result.confident = true;
    } else if (hasMesh) {
        result.type = PipelineTypeConstraint::Mesh;
        result.reason = "detected mesh shading stage (" + meshStage + ")";
        result.confident = true;
    } else if (hasCompute && !hasGraphics) {
        result.type = PipelineTypeConstraint::Compute;
        result.reason = "detected compute stage (" + computeStage + ")";
        result.confident = true;
    } else if (hasGraphics) {
        result.type = PipelineTypeConstraint::Graphics;
        result.reason = "detected graphics stage (" + graphicsStage + ")";
        result.confident = true;
    } else {
        result.type = PipelineTypeConstraint::Graphics;
        result.reason = "no recognized shader extensions, defaulting to graphics";
        result.confident = false;
    }

    // Warn about mixed pipeline types (unusual but not necessarily wrong)
    int pipelineCount = (hasRayTracing ? 1 : 0) + (hasMesh ? 1 : 0) +
                        (hasCompute ? 1 : 0) + (hasGraphics ? 1 : 0);
    if (pipelineCount > 1) {
        std::cerr << "Warning: Mixed pipeline types detected in input files. "
                  << "Using " << result.reason << "\n";
    }

    return result;
}

PipelineExtensions ShaderPipelineUtils::GetPipelineExtensions(PipelineTypeConstraint pipelineType) {
    PipelineExtensions ext;

    switch (pipelineType) {
        case PipelineTypeConstraint::Graphics:
            ext.required = {".vert", ".frag"};  // Minimal graphics pipeline
            ext.optional = {".geom", ".tesc", ".tese"};
            break;

        case PipelineTypeConstraint::Compute:
            ext.required = {".comp"};
            ext.optional = {};  // Compute is standalone
            break;

        case PipelineTypeConstraint::RayTracing:
            ext.required = {".rgen"};  // Ray gen is required
            ext.optional = {".rmiss", ".rchit", ".rahit", ".rint", ".rcall"};
            break;

        case PipelineTypeConstraint::Mesh:
            ext.required = {".mesh"};  // Mesh shader is required
            ext.optional = {".task", ".frag"};  // Task is optional, frag for output
            break;
    }

    return ext;
}

uint32_t ShaderPipelineUtils::DiscoverSiblingShaders(
    std::vector<std::string>& inputFiles,
    PipelineTypeConstraint pipelineType
) {
    if (inputFiles.empty()) return 0;

    namespace fs = std::filesystem;

    // Get the base path from the first input file
    fs::path firstFile(inputFiles[0]);
    fs::path directory = firstFile.parent_path();
    std::string baseName = firstFile.stem().string();

    // Collect already-specified extensions
    std::unordered_set<std::string> existingExtensions;
    for (const auto& file : inputFiles) {
        fs::path p(file);
        existingExtensions.insert(p.extension().string());
    }

    // Get expected extensions for this pipeline type
    auto pipelineExt = GetPipelineExtensions(pipelineType);

    // Combine required and optional into search list
    std::vector<std::string> searchExtensions;
    searchExtensions.insert(searchExtensions.end(),
                           pipelineExt.required.begin(),
                           pipelineExt.required.end());
    searchExtensions.insert(searchExtensions.end(),
                           pipelineExt.optional.begin(),
                           pipelineExt.optional.end());

    uint32_t discovered = 0;

    for (const auto& ext : searchExtensions) {
        // Skip if already in input files
        if (existingExtensions.count(ext) > 0) continue;

        // Try to find sibling file
        fs::path siblingPath = directory / (baseName + ext);

        if (fs::exists(siblingPath)) {
            inputFiles.push_back(siblingPath.string());
            existingExtensions.insert(ext);
            ++discovered;
        }
    }

    return discovered;
}

std::string ShaderPipelineUtils::ValidatePipelineStages(
    const std::vector<std::string>& inputFiles,
    PipelineTypeConstraint pipelineType
) {
    auto pipelineExt = GetPipelineExtensions(pipelineType);

    // Collect extensions from input files
    std::unordered_set<std::string> presentExtensions;
    for (const auto& file : inputFiles) {
        std::filesystem::path p(file);
        presentExtensions.insert(p.extension().string());
    }

    // Check if at least one required extension is present
    bool hasRequired = false;
    for (const auto& req : pipelineExt.required) {
        if (presentExtensions.count(req) > 0) {
            hasRequired = true;
            break;
        }
    }

    if (!hasRequired && !pipelineExt.required.empty()) {
        std::string reqList;
        for (size_t i = 0; i < pipelineExt.required.size(); ++i) {
            if (i > 0) reqList += ", ";
            reqList += pipelineExt.required[i];
        }
        return "Missing required shader stage. Expected one of: " + reqList;
    }

    return "";
}

const char* ShaderPipelineUtils::GetPipelineTypeName(PipelineTypeConstraint type) {
    switch (type) {
        case PipelineTypeConstraint::Graphics:   return "Graphics";
        case PipelineTypeConstraint::Compute:    return "Compute";
        case PipelineTypeConstraint::RayTracing: return "RayTracing";
        case PipelineTypeConstraint::Mesh:       return "Mesh";
        default:                                 return "Unknown";
    }
}

} // namespace ShaderManagement
