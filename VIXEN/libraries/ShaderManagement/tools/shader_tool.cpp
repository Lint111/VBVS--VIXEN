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

#include "ShaderBundleBuilder.h"
#include "SdiRegistryManager.h"
#include "ShaderCompiler.h"
#include "SPIRVReflection.h"
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
    bool embedSpirv = false;  // Embed SPIRV in JSON (base64) instead of separate files

    SdiGeneratorConfig sdiConfig;
};

// ===== File Tracking for Cleanup =====

/**
 * @brief Manifest tracking generated SPIRV files
 *
 * Prevents orphaned files by recording all generated outputs.
 * Format: .shader_tool_manifest.json in output directory
 */
class FileManifest {
public:
    explicit FileManifest(const fs::path& outputDir)
        : manifestPath_(outputDir / ".shader_tool_manifest.json")
        , outputDir_(outputDir) {
        Load();
    }

    ~FileManifest() {
        Save();
    }

    void TrackFile(const fs::path& file) {
        fs::path relPath = fs::relative(file, outputDir_);
        trackedFiles_.insert(relPath.string());
    }

    void UntrackFile(const fs::path& file) {
        fs::path relPath = fs::relative(file, outputDir_);
        trackedFiles_.erase(relPath.string());
    }

    uint32_t CleanupOrphaned() {
        std::unordered_set<std::string> existingFiles;

        // Find all .spv and .json files in output directory
        if (fs::exists(outputDir_)) {
            for (const auto& entry : fs::recursive_directory_iterator(outputDir_)) {
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension();
                    if (ext == ".spv" || ext == ".json") {
                        fs::path relPath = fs::relative(entry.path(), outputDir_);
                        existingFiles.insert(relPath.string());
                    }
                }
            }
        }

        // Find orphaned files (exist on disk but not in manifest)
        uint32_t removed = 0;
        for (const auto& file : existingFiles) {
            if (file == ".shader_tool_manifest.json") continue;  // Skip manifest itself

            if (trackedFiles_.find(file) == trackedFiles_.end()) {
                // Orphaned file - remove it
                fs::path fullPath = outputDir_ / file;
                std::error_code ec;
                if (fs::remove(fullPath, ec)) {
                    ++removed;
                }
            }
        }

        // Remove dead entries from manifest (tracked but don't exist)
        auto it = trackedFiles_.begin();
        while (it != trackedFiles_.end()) {
            if (existingFiles.find(*it) == existingFiles.end()) {
                it = trackedFiles_.erase(it);
            } else {
                ++it;
            }
        }

        return removed;
    }

    size_t GetTrackedCount() const { return trackedFiles_.size(); }

    void Save() {
        nlohmann::json j;
        j["version"] = 1;
        j["files"] = nlohmann::json::array();
        for (const auto& file : trackedFiles_) {
            j["files"].push_back(file);
        }

        std::ofstream file(manifestPath_);
        if (file.is_open()) {
            file << j.dump(2);
        }
    }

private:
    void Load() {
        if (!fs::exists(manifestPath_)) return;

        std::ifstream file(manifestPath_);
        if (!file.is_open()) return;

        try {
            nlohmann::json j;
            file >> j;
            if (j.contains("files") && j["files"].is_array()) {
                for (const auto& item : j["files"]) {
                    trackedFiles_.insert(item.get<std::string>());
                }
            }
        } catch (...) {
            // Corrupted manifest - start fresh
            trackedFiles_.clear();
        }
    }

    fs::path manifestPath_;
    fs::path outputDir_;
    std::unordered_set<std::string> trackedFiles_;
};

void PrintUsage() {
    std::cout << R"(
SDI Tool - Shader compiler and descriptor interface generator

Usage:
  sdi_tool <shader.comp> [options]                 (auto-detect compute shader)
  sdi_tool <shader.vert> <shader.frag> [options]   (auto-detect graphics shader)
  sdi_tool compile <input.vert> <input.frag> [options]
  sdi_tool compile-compute <input.comp> [options]
  sdi_tool generate-sdi <bundle.json> [options]
  sdi_tool build-registry <bundle1.json> <bundle2.json> ... [options]
  sdi_tool batch <config.json> [options]
  sdi_tool cleanup <output-dir> [options]
  sdi_tool /help                                    (show this help)

Commands:
  compile           Compile shader stages into bundle
  compile-compute   Compile compute shader
  generate-sdi      Generate SDI header from bundle
  build-registry    Build central SDI registry from bundles
  batch             Process multiple shaders from config file
  cleanup           Remove orphaned SPIRV files from output directory

Options:
  --output <path>          Output file path
  --output-dir <dir>       Output directory for generated files
  --name <name>            Program name
  --sdi-namespace <ns>     SDI namespace prefix (default: "SDI")
  --sdi-dir <dir>          SDI output directory (default: "./generated/sdi")
  --no-sdi                 Disable SDI generation
  --embed-spirv            Embed SPIRV in JSON (prevents orphaned .spv files)
  --verbose                Print detailed output
  --help                   Show this help

Examples:
  # Auto-detect and compile (easiest)
  sdi_tool Shaders/ComputeTest.comp
  sdi_tool shader.vert shader.frag --name MyShader

  # Explicit commands
  sdi_tool compile shader.vert shader.frag --name MyShader --output-dir ./out
  sdi_tool compile-compute compute.comp --name MyCompute --output-dir ./out

  # Build registry from existing bundles
  sdi_tool build-registry shader1.json shader2.json --output SDI_Registry.h

  # Batch process from config
  sdi_tool batch shaders.json --output-dir ./generated

  # Clean up orphaned SPIRV files
  sdi_tool cleanup ./generated --verbose
)" << std::endl;
}

bool ParseCommandLine(int argc, char** argv, ToolOptions& options) {
    if (argc < 2) {
        return false;
    }

    std::string firstArg = argv[1];

    // Support /help, --help, -h, help
    if (firstArg == "/help" || firstArg == "--help" || firstArg == "-h" || firstArg == "help") {
        return false;
    }

    // Smart default: if first arg is a file path, auto-detect command
    if (firstArg[0] != '-' && (firstArg.find('/') != std::string::npos ||
                                firstArg.find('\\') != std::string::npos ||
                                firstArg.find('.') != std::string::npos)) {
        // Looks like a file path - auto-detect based on extension
        fs::path filePath(firstArg);
        std::string ext = filePath.extension().string();

        if (ext == ".comp") {
            options.command = "compile-compute";
        } else if (ext == ".vert" || ext == ".frag" || ext == ".tesc" ||
                   ext == ".tese" || ext == ".geom") {
            options.command = "compile";
        } else if (ext == ".json") {
            options.command = "batch";
        } else {
            std::cerr << "Error: Unable to auto-detect command for file: " << firstArg << "\n";
            std::cerr << "Please specify command explicitly (compile, compile-compute, batch)\n";
            return false;
        }

        // Parse remaining args as if command was specified
        options.inputFiles.push_back(firstArg);

        // Auto-generate name from filename if not specified
        if (options.programName.empty()) {
            options.programName = filePath.stem().string();
        }

        // Default output directory
        if (options.outputDir.empty()) {
            options.outputDir = "./generated";
        }

        // Parse remaining args
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h" || arg == "/help") {
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
            } else if (arg == "--embed-spirv") {
                options.embedSpirv = true;
            } else if (arg == "--verbose" || arg == "-v") {
                options.verbose = true;
            } else if (arg[0] != '-') {
                options.inputFiles.push_back(arg);
            }
        }
    } else {
        // Traditional explicit command mode
        options.command = argv[1];

        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "--help" || arg == "-h" || arg == "/help") {
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
            } else if (arg == "--embed-spirv") {
                options.embedSpirv = true;
            } else if (arg == "--verbose" || arg == "-v") {
                options.verbose = true;
            } else if (arg[0] != '-') {
                options.inputFiles.push_back(arg);
            }
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
    if (ext == ".tesc") return ShaderStage::TessControl;
    if (ext == ".tese") return ShaderStage::TessEval;
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
bool SaveBundleToJson(
    const ShaderDataBundle& bundle,
    const fs::path& outputPath,
    bool embedSpirv = false,
    FileManifest* manifest = nullptr
) {
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

        if (embedSpirv) {
            // Embed SPIRV directly in JSON (prevents orphaned .spv files)
            std::vector<uint32_t> spirvCopy = stage.spirvCode;  // nlohmann::json needs lvalue
            stageJson["spirvData"] = spirvCopy;
        } else {
            // Save SPIRV to separate file
            fs::path spirvPath = outputPath.parent_path() / (bundle.uuid + "_stage" + std::to_string(static_cast<int>(stage.stage)) + ".spv");
            std::ofstream spirvFile(spirvPath, std::ios::binary);
            if (!spirvFile.is_open()) {
                std::cerr << "Error: Failed to create SPIRV file: " << spirvPath << "\n";
                return false;
            }

            spirvFile.write(reinterpret_cast<const char*>(stage.spirvCode.data()),
                           stage.spirvCode.size() * sizeof(uint32_t));
            spirvFile.close();

            stageJson["spirvFile"] = spirvPath.string();

            // Track SPIRV file in manifest
            if (manifest) {
                manifest->TrackFile(spirvPath);
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

    // Track bundle JSON in manifest
    if (manifest) {
        manifest->TrackFile(outputPath);
    }

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

            size_t fileSize = spirvFile.tellg();
            spirvFile.seekg(0);

            stage.spirvCode.resize(fileSize / sizeof(uint32_t));
            spirvFile.read(reinterpret_cast<char*>(stage.spirvCode.data()), fileSize);
            spirvFile.close();
        } else {
            std::cerr << "Error: Stage missing both spirvData and spirvFile\n";
            return false;
        }

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
    fs::path outputDir;
    if (!options.outputPath.empty()) {
        outputPath = options.outputPath;
        outputDir = outputPath.parent_path();
    } else if (!options.outputDir.empty()) {
        fs::create_directories(options.outputDir);
        outputDir = options.outputDir;
        outputPath = fs::path(options.outputDir) / (options.programName + ".json");
    } else {
        outputDir = ".";
        outputPath = options.programName + ".json";
    }

    // Security: Validate output path
    fs::path validatedOutputPath = ValidateAndSanitizePath(outputPath, true);
    if (validatedOutputPath.empty()) {
        std::cerr << "Error: Invalid or unsafe output path: " << outputPath << "\n";
        return 1;
    }

    // Create file manifest for tracking
    FileManifest manifest(outputDir);

    // Save bundle
    if (!SaveBundleToJson(*result.bundle, validatedOutputPath, options.embedSpirv, &manifest)) {
        std::cerr << "Error: Failed to save bundle\n";
        return 1;
    }

    // Save manifest
    manifest.Save();

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

    // Create registry config
    SdiRegistryManager::Config registryConfig;
    registryConfig.sdiDirectory = sdiConfig.outputDirectory;
    registryConfig.registryHeaderPath = registryPath / "SDI_Registry.h";
    registryConfig.registryNamespace = sdiConfig.namespacePrefix;

    SdiRegistryManager registry(registryConfig);

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

    // Generate registry header (happens automatically via RegenerateRegistry)
    if (!registry.RegenerateRegistry()) {
        std::cerr << "Error: Failed to generate registry header\n";
        return 1;
    }

    fs::path outputFile = registryConfig.registryHeaderPath;

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

    // Create file manifest for tracking all generated files
    FileManifest manifest(outputDir);

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

    // Cleanup orphaned files
    uint32_t removed = manifest.CleanupOrphaned();
    if (removed > 0 && options.verbose) {
        std::cout << "Cleaned up " << removed << " orphaned files\n";
    }

    // Save final manifest
    manifest.Save();

    std::cout << "\nBatch processing complete!\n";
    std::cout << "Processed " << generatedBundles.size() << " shaders\n";
    std::cout << "Output directory: " << outputDir << "\n";

    return 0;
}

/**
 * @brief Command: Clean up orphaned SPIRV files
 */
int CommandCleanup(const ToolOptions& options) {
    std::string outputDir = options.outputDir.empty() ? "./generated" : options.outputDir;

    if (!fs::exists(outputDir)) {
        std::cerr << "Error: Output directory does not exist: " << outputDir << "\n";
        return 1;
    }

    if (options.verbose) {
        std::cout << "Cleaning up orphaned files in: " << outputDir << "\n";
    }

    // Load manifest and cleanup
    FileManifest manifest(outputDir);
    uint32_t removed = manifest.CleanupOrphaned();

    if (removed > 0) {
        std::cout << "Removed " << removed << " orphaned file(s)\n";
        manifest.Save();
    } else {
        std::cout << "No orphaned files found\n";
    }

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
        } else if (options.command == "cleanup") {
            return CommandCleanup(options);
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
