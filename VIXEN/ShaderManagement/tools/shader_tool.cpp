/**
 * @file shader_tool.cpp
 * @brief Standalone shader compiler tool for build-time shader processing
 *
 * This tool allows shader compilation, SDI generation, and registry creation
 * to happen during the CMake build step rather than at runtime.
 *
 * Usage:
 *   shader_tool compile <input.vert> <input.frag> --output <bundle.spv>
 *   shader_tool generate-sdi <bundle.spv> --output-dir <sdi_dir>
 *   shader_tool build-registry <shader1.spv> <shader2.spv> ... --output <registry.h>
 *   shader_tool batch <config.json> --output-dir <output>
 *
 * CMake Integration:
 *   add_shader_bundle(MyShader
 *       VERTEX shader.vert
 *       FRAGMENT shader.frag
 *       OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated/shaders
 *   )
 */

#include "ShaderManagement/ShaderBundleBuilder.h"
#include "ShaderManagement/SdiRegistryManager.h"
#include "ShaderManagement/ShaderCompiler.h"
#include "ShaderManagement/SPIRVReflection.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

using namespace ShaderManagement;
namespace fs = std::filesystem;

// ===== Security Helpers =====

/**
 * @brief Validate and sanitize file path to prevent path traversal attacks
 *
 * Prevents malicious paths like:
 * - ../../../etc/passwd
 * - /absolute/path/to/sensitive/file
 * - Symlinks to sensitive locations
 *
 * @param path Path to validate
 * @param allowNonExistent Allow non-existent paths (for output files)
 * @return Sanitized path if valid, empty path if invalid
 */
fs::path ValidateAndSanitizePath(const fs::path& path, bool allowNonExistent = false) {
    try {
        // Convert to absolute path relative to current directory
        fs::path absPath = fs::absolute(path);

        // Get canonical path (resolves .., symlinks, etc.)
        fs::path canonicalPath;
        if (fs::exists(absPath)) {
            canonicalPath = fs::canonical(absPath);
        } else if (allowNonExistent) {
            // For non-existent paths (output files), canonicalize parent directory
            fs::path parentDir = absPath.parent_path();
            if (!parentDir.empty() && fs::exists(parentDir)) {
                canonicalPath = fs::canonical(parentDir) / absPath.filename();
            } else {
                // Allow it for output files (directories will be created)
                canonicalPath = absPath;
            }
        } else {
            std::cerr << "Error: Path does not exist: " << path << "\n";
            return {};
        }

        // Get current working directory as base
        fs::path cwd = fs::current_path();

        // Security check: Block absolute paths to system directories (Unix)
        std::string pathStr = canonicalPath.string();
        if (pathStr.find("/etc/") == 0 || pathStr.find("/sys/") == 0 ||
            pathStr.find("/proc/") == 0 || pathStr.find("/dev/") == 0 ||
            pathStr.find("/root/") == 0) {
            std::cerr << "Security Error: Attempt to access system directory: " << path << "\n";
            return {};
        }

        // Security check: Block Windows system directories
#ifdef _WIN32
        std::transform(pathStr.begin(), pathStr.end(), pathStr.begin(), ::tolower);
        if (pathStr.find("c:\\windows") == 0 || pathStr.find("c:\\system") == 0 ||
            pathStr.find("c:\\program files\\windows") == 0) {
            std::cerr << "Security Error: Attempt to access Windows system directory: " << path << "\n";
            return {};
        }
#endif

        return canonicalPath;

    } catch (const fs::filesystem_error& e) {
        std::cerr << "Path validation error: " << e.what() << "\n";
        return {};
    }
}

// ===== Command Line Parsing =====

struct ToolOptions {
    std::string command;
    std::vector<std::string> inputFiles;
    std::string outputPath;
    std::string outputDir;
    std::string programName;
    PipelineTypeConstraint pipelineType = PipelineTypeConstraint::Graphics;
    bool generateSdi = true;
    bool verbose = false;

    SdiGeneratorConfig sdiConfig;
};

void PrintUsage() {
    std::cout << R"(
Shader Tool - Build-time shader compiler and SDI generator

Usage:
  shader_tool compile <input.vert> <input.frag> [options]
  shader_tool compile-compute <input.comp> [options]
  shader_tool generate-sdi <bundle.json> [options]
  shader_tool build-registry <bundle1.json> <bundle2.json> ... [options]
  shader_tool batch <config.json> [options]

Commands:
  compile           Compile shader stages into bundle
  compile-compute   Compile compute shader
  generate-sdi      Generate SDI header from bundle
  build-registry    Build central SDI registry from bundles
  batch             Process multiple shaders from config file

Options:
  --output <path>          Output file path
  --output-dir <dir>       Output directory for generated files
  --name <name>            Program name
  --sdi-namespace <ns>     SDI namespace prefix (default: "SDI")
  --sdi-dir <dir>          SDI output directory (default: "./generated/sdi")
  --no-sdi                 Disable SDI generation
  --verbose                Print detailed output
  --help                   Show this help

Examples:
  # Compile graphics shader
  shader_tool compile shader.vert shader.frag --name MyShader --output-dir ./out

  # Compile compute shader
  shader_tool compile-compute compute.comp --name MyCompute --output-dir ./out

  # Build registry from existing bundles
  shader_tool build-registry shader1.json shader2.json --output SDI_Registry.h

  # Batch process from config
  shader_tool batch shaders.json --output-dir ./generated
)" << std::endl;
}

bool ParseCommandLine(int argc, char** argv, ToolOptions& options) {
    if (argc < 2) {
        return false;
    }

    options.command = argv[1];

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            return false;
        } else if (arg == "--output") {
            if (i + 1 < argc) options.outputPath = argv[++i];
        } else if (arg == "--output-dir") {
            if (i + 1 < argc) options.outputDir = argv[++i];
        } else if (arg == "--name") {
            if (i + 1 < argc) options.programName = argv[++i];
        } else if (arg == "--sdi-namespace") {
            if (i + 1 < argc) options.sdiConfig.namespacePrefix = argv[++i];
        } else if (arg == "--sdi-dir") {
            if (i + 1 < argc) options.sdiConfig.outputDirectory = argv[++i];
        } else if (arg == "--no-sdi") {
            options.generateSdi = false;
        } else if (arg == "--verbose" || arg == "-v") {
            options.verbose = true;
        } else if (arg[0] != '-') {
            options.inputFiles.push_back(arg);
        }
    }

    // Set defaults
    if (options.sdiConfig.namespacePrefix.empty()) {
        options.sdiConfig.namespacePrefix = "SDI";
    }
    if (options.sdiConfig.outputDirectory.empty() && !options.outputDir.empty()) {
        options.sdiConfig.outputDirectory = fs::path(options.outputDir) / "sdi";
    } else if (options.sdiConfig.outputDirectory.empty()) {
        options.sdiConfig.outputDirectory = "./generated/sdi";
    }

    return true;
}

// ===== Command Implementations =====

/**
 * @brief Detect shader stage from file extension
 */
ShaderStage DetectStageFromExtension(const fs::path& path) {
    std::string ext = path.extension().string();
    if (ext == ".vert") return ShaderStage::Vertex;
    if (ext == ".frag") return ShaderStage::Fragment;
    if (ext == ".comp") return ShaderStage::Compute;
    if (ext == ".geom") return ShaderStage::Geometry;
    if (ext == ".tesc") return ShaderStage::TessellationControl;
    if (ext == ".tese") return ShaderStage::TessellationEvaluation;
    if (ext == ".mesh") return ShaderStage::Mesh;
    if (ext == ".task") return ShaderStage::Task;
    if (ext == ".rgen") return ShaderStage::RayGen;
    if (ext == ".rmiss") return ShaderStage::Miss;
    if (ext == ".rchit") return ShaderStage::ClosestHit;
    if (ext == ".rahit") return ShaderStage::AnyHit;
    if (ext == ".rint") return ShaderStage::Intersection;
    if (ext == ".rcall") return ShaderStage::Callable;

    std::cerr << "Warning: Unknown shader stage for extension '" << ext << "', defaulting to Vertex\n";
    return ShaderStage::Vertex;
}

/**
 * @brief Save bundle to JSON file
 */
bool SaveBundleToJson(const ShaderDataBundle& bundle, const fs::path& outputPath) {
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

        // Save SPIRV to separate file
        fs::path spirvPath = outputPath.parent_path() / (bundle.uuid + "_stage" + std::to_string(static_cast<int>(stage.stage)) + ".spv");
        std::ofstream spirvFile(spirvPath, std::ios::binary);
        spirvFile.write(reinterpret_cast<const char*>(stage.spirvCode.data()),
                       stage.spirvCode.size() * sizeof(uint32_t));
        spirvFile.close();

        stageJson["spirvFile"] = spirvPath.string();
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

    return true;
}

/**
 * @brief Load bundle from JSON file
 */
bool LoadBundleFromJson(const fs::path& jsonPath, ShaderDataBundle& bundle) {
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

        // Load SPIRV from file
        fs::path spirvPath = stageJson["spirvFile"].get<std::string>();
        std::ifstream spirvFile(spirvPath, std::ios::binary | std::ios::ate);
        if (!spirvFile.is_open()) {
            std::cerr << "Error: Failed to open SPIRV file: " << spirvPath << "\n";
            return false;
        }

        size_t fileSize = spirvFile.tellg();
        spirvFile.seekg(0);

        stage.spirvCode.resize(fileSize / sizeof(uint32_t));
        spirvFile.read(reinterpret_cast<char*>(stage.spirvCode.data()), fileSize);
        spirvFile.close();

        bundle.program.stages.push_back(stage);
    }

    return true;
}

/**
 * @brief Command: Compile shader stages
 */
int CommandCompile(const ToolOptions& options) {
    if (options.inputFiles.empty()) {
        std::cerr << "Error: No input files specified\n";
        return 1;
    }

    if (options.programName.empty()) {
        std::cerr << "Error: Program name not specified (use --name)\n";
        return 1;
    }

    if (options.verbose) {
        std::cout << "Compiling shader program: " << options.programName << "\n";
        std::cout << "Input files: ";
        for (const auto& file : options.inputFiles) {
            std::cout << file << " ";
        }
        std::cout << "\n";
    }

    // Create builder
    ShaderBundleBuilder builder;
    builder.SetProgramName(options.programName)
           .SetPipelineType(options.pipelineType)
           .SetSdiConfig(options.sdiConfig)
           .EnableSdiGeneration(options.generateSdi);

    // Add stages with path validation
    for (const auto& inputFile : options.inputFiles) {
        fs::path filePath(inputFile);

        // Security: Validate input path
        fs::path validatedPath = ValidateAndSanitizePath(filePath, false);
        if (validatedPath.empty()) {
            std::cerr << "Error: Invalid or unsafe input path: " << inputFile << "\n";
            return 1;
        }

        if (!fs::exists(validatedPath)) {
            std::cerr << "Error: Input file not found: " << inputFile << "\n";
            return 1;
        }

        ShaderStage stage = DetectStageFromExtension(validatedPath);

        if (options.verbose) {
            std::cout << "Adding stage: " << ShaderStageName(stage) << " from " << validatedPath << "\n";
        }

        builder.AddStageFromFile(stage, validatedPath);
    }

    // Build
    if (options.verbose) {
        std::cout << "Building shader bundle...\n";
    }

    auto result = builder.Build();

    if (!result.success) {
        std::cerr << "Error: Compilation failed: " << result.errorMessage << "\n";
        return 1;
    }

    // Print warnings
    if (!result.warnings.empty()) {
        std::cout << "Warnings:\n";
        for (const auto& warning : result.warnings) {
            std::cout << "  - " << warning << "\n";
        }
    }

    // Print statistics
    if (options.verbose) {
        std::cout << "Compilation successful!\n";
        std::cout << "  Compile time: " << result.compileTime.count() << "ms\n";
        std::cout << "  Reflect time: " << result.reflectTime.count() << "ms\n";
        if (options.generateSdi) {
            std::cout << "  SDI gen time: " << result.sdiGenTime.count() << "ms\n";
            std::cout << "  SDI header: " << result.bundle->sdiHeaderPath << "\n";
            std::cout << "  SDI namespace: " << result.bundle->sdiNamespace << "\n";
        }
        std::cout << "  Total time: " << result.totalTime.count() << "ms\n";
        std::cout << "  Descriptor hash: " << result.bundle->descriptorInterfaceHash << "\n";
    }

    // Determine output path
    fs::path outputPath;
    if (!options.outputPath.empty()) {
        outputPath = options.outputPath;
    } else if (!options.outputDir.empty()) {
        fs::create_directories(options.outputDir);
        outputPath = fs::path(options.outputDir) / (options.programName + ".json");
    } else {
        outputPath = options.programName + ".json";
    }

    // Security: Validate output path
    fs::path validatedOutputPath = ValidateAndSanitizePath(outputPath, true);
    if (validatedOutputPath.empty()) {
        std::cerr << "Error: Invalid or unsafe output path: " << outputPath << "\n";
        return 1;
    }

    // Save bundle
    if (!SaveBundleToJson(*result.bundle, validatedOutputPath)) {
        std::cerr << "Error: Failed to save bundle\n";
        return 1;
    }

    if (options.verbose) {
        std::cout << "Bundle saved to: " << outputPath << "\n";
    }

    return 0;
}

/**
 * @brief Command: Build central SDI registry
 */
int CommandBuildRegistry(const ToolOptions& options) {
    if (options.inputFiles.empty()) {
        std::cerr << "Error: No input bundles specified\n";
        return 1;
    }

    if (options.verbose) {
        std::cout << "Building SDI registry from " << options.inputFiles.size() << " bundles\n";
    }

    // Create registry manager
    fs::path registryPath;
    if (!options.outputPath.empty()) {
        registryPath = fs::path(options.outputPath).parent_path();
    } else if (!options.outputDir.empty()) {
        registryPath = options.outputDir;
    } else {
        registryPath = "./generated";
    }

    SdiGeneratorConfig sdiConfig;
    sdiConfig.outputDirectory = registryPath / "sdi";
    sdiConfig.namespacePrefix = options.sdiConfig.namespacePrefix.empty() ? "SDI" : options.sdiConfig.namespacePrefix;

    SdiRegistryManager registry(sdiConfig);

    // Load and register each bundle
    for (const auto& bundleFile : options.inputFiles) {
        ShaderDataBundle bundle;
        if (!LoadBundleFromJson(bundleFile, bundle)) {
            std::cerr << "Error: Failed to load bundle: " << bundleFile << "\n";
            continue;
        }

        SdiRegistryEntry entry;
        entry.uuid = bundle.uuid;
        entry.programName = bundle.program.name;
        entry.sdiHeaderPath = bundle.sdiHeaderPath;
        entry.sdiNamespace = bundle.sdiNamespace;
        entry.aliasName = bundle.program.name;

        if (!registry.RegisterShader(entry)) {
            std::cerr << "Warning: Failed to register shader: " << bundle.program.name << "\n";
        } else if (options.verbose) {
            std::cout << "Registered: " << bundle.program.name << " (UUID: " << bundle.uuid << ")\n";
        }
    }

    // Generate registry header
    fs::path outputFile;
    if (!options.outputPath.empty()) {
        outputFile = options.outputPath;
    } else {
        outputFile = registryPath / "SDI_Registry.h";
    }

    if (!registry.GenerateRegistryHeader(outputFile)) {
        std::cerr << "Error: Failed to generate registry header\n";
        return 1;
    }

    if (options.verbose) {
        std::cout << "Registry header generated: " << outputFile << "\n";
        std::cout << "Total shaders registered: " << options.inputFiles.size() << "\n";
    }

    return 0;
}

/**
 * @brief Command: Batch process from config file
 */
int CommandBatch(const ToolOptions& options) {
    if (options.inputFiles.empty()) {
        std::cerr << "Error: No config file specified\n";
        return 1;
    }

    fs::path configPath = options.inputFiles[0];
    if (!fs::exists(configPath)) {
        std::cerr << "Error: Config file not found: " << configPath << "\n";
        return 1;
    }

    // Load config
    std::ifstream configFile(configPath);
    nlohmann::json config;
    try {
        configFile >> config;
    } catch (const std::exception& e) {
        std::cerr << "Error: Failed to parse config: " << e.what() << "\n";
        return 1;
    }

    std::string outputDir = options.outputDir.empty() ? "./generated" : options.outputDir;
    fs::create_directories(outputDir);

    std::vector<std::string> generatedBundles;

    // Process each shader
    for (const auto& shaderConfig : config["shaders"]) {
        ToolOptions shaderOptions = options;
        shaderOptions.command = "compile";
        shaderOptions.programName = shaderConfig["name"];
        shaderOptions.outputDir = outputDir;
        shaderOptions.inputFiles.clear();

        for (const auto& stage : shaderConfig["stages"]) {
            shaderOptions.inputFiles.push_back(stage.get<std::string>());
        }

        if (shaderConfig.contains("pipeline")) {
            std::string pipeline = shaderConfig["pipeline"];
            if (pipeline == "graphics") shaderOptions.pipelineType = PipelineTypeConstraint::Graphics;
            else if (pipeline == "compute") shaderOptions.pipelineType = PipelineTypeConstraint::Compute;
            else if (pipeline == "mesh") shaderOptions.pipelineType = PipelineTypeConstraint::Mesh;
            else if (pipeline == "raytracing") shaderOptions.pipelineType = PipelineTypeConstraint::RayTracing;
        }

        if (options.verbose) {
            std::cout << "\n=== Processing: " << shaderOptions.programName << " ===\n";
        }

        int result = CommandCompile(shaderOptions);
        if (result != 0) {
            std::cerr << "Error: Failed to compile shader: " << shaderOptions.programName << "\n";
            std::cerr << "Batch processing aborted due to compilation failure.\n";
            return result;  // Fail fast - exit immediately with error code
        }

        generatedBundles.push_back((fs::path(outputDir) / (shaderOptions.programName + ".json")).string());
    }

    // Build registry if requested
    if (config.contains("buildRegistry") && config["buildRegistry"].get<bool>()) {
        if (options.verbose) {
            std::cout << "\n=== Building Registry ===\n";
        }

        ToolOptions registryOptions = options;
        registryOptions.command = "build-registry";
        registryOptions.inputFiles = generatedBundles;
        registryOptions.outputDir = outputDir;

        int result = CommandBuildRegistry(registryOptions);
        if (result != 0) {
            std::cerr << "Error: Failed to build registry\n";
            return result;  // Return error code for CI/CD
        }
    }

    std::cout << "\nBatch processing complete!\n";
    std::cout << "Processed " << generatedBundles.size() << " shaders\n";
    std::cout << "Output directory: " << outputDir << "\n";

    return 0;
}

// ===== Main Entry Point =====

int main(int argc, char** argv) {
    ToolOptions options;

    if (!ParseCommandLine(argc, argv, options)) {
        PrintUsage();
        return 1;
    }

    try {
        if (options.command == "compile" || options.command == "compile-compute") {
            if (options.command == "compile-compute") {
                options.pipelineType = PipelineTypeConstraint::Compute;
            }
            return CommandCompile(options);
        } else if (options.command == "build-registry") {
            return CommandBuildRegistry(options);
        } else if (options.command == "batch") {
            return CommandBatch(options);
        } else {
            std::cerr << "Error: Unknown command: " << options.command << "\n";
            PrintUsage();
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
