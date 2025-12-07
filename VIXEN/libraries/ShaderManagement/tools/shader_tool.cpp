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
#include "ShaderBundleSerializer.h"
#include "FileManifest.h"
#include "SdiRegistryManager.h"
#include "ShaderCompiler.h"
#include "SPIRVReflection.h"
#include "SpirvInterfaceGenerator.h"
#include "ShaderPipelineUtils.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <optional>
#include <nlohmann/json.hpp>  // For batch config parsing

using namespace ShaderManagement;
namespace fs = std::filesystem;

// Tool version - update on releases
constexpr const char* SDI_TOOL_VERSION = "1.0.0";

// ===== Default Paths (single source of truth) =====
constexpr const char* DEFAULT_OUTPUT_DIR = "./generated";
constexpr const char* DEFAULT_SDI_SUBDIR = "sdi";
constexpr const char* DEFAULT_SDI_NAMESPACE = "SDI";

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
    bool quiet = false;       // CI mode: only output errors
    bool dryRun = false;      // Preview operations without executing
    bool embedSpirv = false;  // Embed SPIRV in JSON (base64) instead of separate files

    SdiGeneratorConfig sdiConfig;

    // Helper: should we print informational messages?
    bool shouldPrint() const { return !quiet; }
    // Helper: should we print verbose messages?
    bool shouldPrintVerbose() const { return verbose && !quiet; }
};

// FileManifest is now in the library (FileManifest.h)

void PrintUsage() {
    std::cout << R"(
SDI Tool - Shader compiler and descriptor interface generator

Usage:
  sdi_tool <shader_files...> [options]    (auto-detect pipeline type)
  sdi_tool compile <input_files...> [options]
  sdi_tool batch <config.json> [options]
  sdi_tool build-registry <bundle1.json> ... [options]
  sdi_tool cleanup <output-dir> [options]
  sdi_tool cleanup-sdi <sdi-dir> [options]
  sdi_tool --help                         (show this help)

Auto-Detection:
  Pipeline type is automatically detected from file extensions:
  - .comp                          -> Compute pipeline
  - .rgen, .rmiss, .rchit, etc.    -> Ray Tracing pipeline
  - .mesh, .task                   -> Mesh Shading pipeline
  - .vert, .frag, .geom, etc.      -> Graphics pipeline

  Priority: RayTracing > Mesh > Compute > Graphics
  (If mixed types are provided, highest priority wins)

Sibling Auto-Discovery:
  When given a single shader file, the tool will automatically discover
  sibling shaders with the same base name. For example:
  - Input: VoxelRT.rgen -> Finds: VoxelRT.rmiss, VoxelRT.rchit, VoxelRT.rint
  - Input: MyShader.frag -> Finds: MyShader.vert
  This allows you to specify just ONE file and get the full pipeline!

Commands:
  compile           Compile shader stages into bundle (auto-detect pipeline)
  batch             Process multiple shaders from config file
  build-registry    Build central SDI registry from bundles
  cleanup           Remove orphaned SPIRV files from output directory
  cleanup-sdi       Remove orphaned SDI headers not referenced by any Names.h

Options:
  -o, --output <path>      Output file path
  -d, --output-dir <dir>   Output directory (default: ./generated)
  -n, --name <name>        Program name (default: first input file stem)
  --sdi-namespace <ns>     SDI namespace prefix (default: SDI)
  --sdi-dir <dir>          SDI output directory (default: ./generated/sdi)
  --no-sdi                 Disable SDI generation
  --embed-spirv            Embed SPIRV in JSON (prevents orphaned .spv files)
  -v, --verbose            Print detailed output
  -q, --quiet              Suppress all output except errors (for CI/CD)
  --dry-run                Preview operations without modifying files
  -h, --help               Show this help
  --version                Show version information

Examples:
  # Auto-detect pipeline type (easiest)
  sdi_tool Shaders/ComputeTest.comp                    # -> Compute
  sdi_tool shader.vert shader.frag -n MyShader         # -> Graphics
  sdi_tool raygen.rgen miss.rmiss hit.rchit            # -> RayTracing

  # With options
  sdi_tool shader.vert shader.frag -n MyShader -d ./out -v
  sdi_tool compute.comp --dry-run                      # Preview only

  # Build registry from existing bundles
  sdi_tool build-registry shader1.json shader2.json -o SDI_Registry.h

  # Batch process from config
  sdi_tool batch shaders.json -d ./generated

  # CI/CD mode (quiet, errors only)
  sdi_tool batch shaders.json -q

  # Clean up orphaned files
  sdi_tool cleanup ./generated -v
  sdi_tool cleanup-sdi ./generated/sdi -v

Batch Config Format (JSON):
  {
    "shaders": [
      {
        "name": "MyShader",
        "stages": ["shader.vert", "shader.frag"],
        "pipeline": "graphics"  // optional: graphics|compute|mesh|raytracing
      }
    ],
    "buildRegistry": true  // optional: generate SDI_Registry.h
  }
)" << std::endl;
}

/**
 * @brief Parse a single command-line option
 * @return 0 = recognized option, 1 = unrecognized option starting with -, 2 = input file
 */
int ParseOption(const std::string& arg, const std::string& nextArg, bool hasNext, ToolOptions& options, int& skip) {
    skip = 0;

    // Help flags
    if (arg == "--help" || arg == "-h") {
        return -1;  // Signal to show help
    }
    // Version flag
    if (arg == "--version") {
        std::cout << "sdi_tool version " << SDI_TOOL_VERSION << "\n";
        std::exit(0);
    }
    // Output path
    if (arg == "--output" || arg == "-o") {
        if (hasNext) { options.outputPath = nextArg; skip = 1; }
        return 0;
    }
    // Output directory
    if (arg == "--output-dir" || arg == "-d") {
        if (hasNext) { options.outputDir = nextArg; skip = 1; }
        return 0;
    }
    // Program name
    if (arg == "--name" || arg == "-n") {
        if (hasNext) { options.programName = nextArg; skip = 1; }
        return 0;
    }
    // SDI namespace
    if (arg == "--sdi-namespace") {
        if (hasNext) { options.sdiConfig.namespacePrefix = nextArg; skip = 1; }
        return 0;
    }
    // SDI directory
    if (arg == "--sdi-dir") {
        if (hasNext) { options.sdiConfig.outputDirectory = nextArg; skip = 1; }
        return 0;
    }
    // Boolean flags
    if (arg == "--no-sdi") {
        options.generateSdi = false;
        return 0;
    }
    if (arg == "--embed-spirv") {
        options.embedSpirv = true;
        return 0;
    }
    if (arg == "--verbose" || arg == "-v") {
        options.verbose = true;
        return 0;
    }
    if (arg == "--quiet" || arg == "-q") {
        options.quiet = true;
        return 0;
    }
    if (arg == "--dry-run") {
        options.dryRun = true;
        return 0;
    }

    // Unknown option (starts with -)
    if (!arg.empty() && arg[0] == '-') {
        return 1;  // Unknown option
    }

    // Input file
    return 2;
}

bool ParseCommandLine(int argc, char** argv, ToolOptions& options) {
    if (argc < 2) {
        return false;
    }

    std::string firstArg = argv[1];

    // Help and version handling
    if (firstArg == "--help" || firstArg == "-h" || firstArg == "help") {
        return false;
    }
    if (firstArg == "--version") {
        std::cout << "sdi_tool version " << SDI_TOOL_VERSION << "\n";
        std::exit(0);
    }

    // Smart default: if first arg is a file path, auto-detect command
    if (firstArg[0] != '-' && (firstArg.find('/') != std::string::npos ||
                                firstArg.find('\\') != std::string::npos ||
                                firstArg.find('.') != std::string::npos)) {
        // Looks like a file path - collect all shader files first for proper detection
        fs::path filePath(firstArg);
        std::string ext = filePath.extension().string();

        // Collect all input files first (to detect pipeline type from all of them)
        options.inputFiles.push_back(firstArg);

        // Parse remaining args early to collect all input files
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            std::string nextArg = (i + 1 < argc) ? argv[i + 1] : "";
            int skip = 0;

            int result = ParseOption(arg, nextArg, i + 1 < argc, options, skip);
            if (result == -1) {
                return false;  // Show help
            } else if (result == 1) {
                std::cerr << "Error: Unknown option '" << arg << "'\n";
                std::cerr << "Run 'sdi_tool --help' for usage information.\n";
                std::exit(1);
            } else if (result == 2) {
                options.inputFiles.push_back(arg);
            }
            i += skip;
        }

        // JSON file -> batch mode
        if (ext == ".json") {
            options.command = "batch";
        } else {
            // Use smart pipeline detection from ALL input files
            // Pipeline type will be detected in CommandCompile using ShaderPipelineUtils
            options.command = "compile";
        }

        // Auto-generate name from filename if not specified
        if (options.programName.empty()) {
            options.programName = filePath.stem().string();
        }

        // Default output directory
        if (options.outputDir.empty()) {
            options.outputDir = DEFAULT_OUTPUT_DIR;
        }
        // Args already parsed above
    } else {
        // Traditional explicit command mode
        options.command = argv[1];

        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            std::string nextArg = (i + 1 < argc) ? argv[i + 1] : "";
            int skip = 0;

            int result = ParseOption(arg, nextArg, i + 1 < argc, options, skip);
            if (result == -1) {
                return false;  // Show help
            } else if (result == 1) {
                std::cerr << "Error: Unknown option '" << arg << "'\n";
                std::cerr << "Run 'sdi_tool --help' for usage information.\n";
                std::exit(1);
            } else if (result == 2) {
                options.inputFiles.push_back(arg);
            }
            i += skip;
        }
    }

    // Set defaults using constants
    if (options.sdiConfig.namespacePrefix.empty()) {
        options.sdiConfig.namespacePrefix = DEFAULT_SDI_NAMESPACE;
    }
    if (options.sdiConfig.outputDirectory.empty() && !options.outputDir.empty()) {
        options.sdiConfig.outputDirectory = fs::path(options.outputDir) / DEFAULT_SDI_SUBDIR;
    } else if (options.sdiConfig.outputDirectory.empty()) {
        options.sdiConfig.outputDirectory = fs::path(DEFAULT_OUTPUT_DIR) / DEFAULT_SDI_SUBDIR;
    }

    // Validate: --quiet and --verbose are mutually exclusive
    if (options.quiet && options.verbose) {
        std::cerr << "Warning: --quiet and --verbose are mutually exclusive. Using --quiet.\n";
        options.verbose = false;
    }

    return true;
}

// ===== Command Implementations =====
// Pipeline detection and sibling discovery use ShaderPipelineUtils from the library
// Serialization uses ShaderBundleSerializer from the library
// (single source of truth - no duplicate implementations)

/**
 * @brief Clean up old SDI and SPIRV files when UUID changes
 *
 * Uses SdiFileManager for SDI cleanup and manual cleanup for SPIRV files.
 */
void CleanupOldSdiFiles(const std::string& oldUuid, const fs::path& sdiDir, bool verbose) {
    if (oldUuid.empty()) {
        return;
    }

    // Use SdiFileManager to delete old SDI header
    SdiFileManager sdiManager(sdiDir);
    if (sdiManager.UnregisterSdi(oldUuid, true)) {
        if (verbose) {
            std::cout << "Cleaning up old SDI: " << oldUuid << "-SDI.h\n";
        }
    }

    // Delete old SPIRV files (pattern: {uuid}_stage*.spv)
    // These are in the output directory, not the SDI directory
    try {
        for (const auto& entry : fs::directory_iterator(sdiDir.parent_path())) {
            std::string filename = entry.path().filename().string();
            if (filename.find(oldUuid + "_stage") == 0 && filename.ends_with(".spv")) {
                if (verbose) {
                    std::cout << "Cleaning up old SPIRV: " << entry.path() << "\n";
                }
                fs::remove(entry.path());
            }
        }
    } catch (const fs::filesystem_error&) {
        // Ignore errors when iterating directory
    }
}

/**
 * @brief Command: Compile shader stages
 */
int CommandCompile(const ToolOptions& options) {
    if (options.inputFiles.empty()) {
        std::cerr << "Error: No input files specified\n";
        std::cerr << "Hint: Provide shader files as arguments, e.g.: sdi_tool shader.vert shader.frag\n";
        return 1;
    }

    if (options.programName.empty()) {
        std::cerr << "Error: Program name not specified\n";
        std::cerr << "Hint: Use -n or --name to specify the shader program name\n";
        return 1;
    }

    // Make a mutable copy of input files (may be expanded by sibling discovery)
    std::vector<std::string> inputFiles = options.inputFiles;

    // === Phase 1: Validate all inputs upfront before any work ===
    for (const auto& inputFile : inputFiles) {
        fs::path filePath(inputFile);
        fs::path validatedPath = ValidateAndSanitizePath(filePath, false);
        if (validatedPath.empty()) {
            std::cerr << "Error: Invalid or unsafe input path: " << inputFile << "\n";
            std::cerr << "Hint: Use absolute paths or paths relative to current directory\n";
            return 1;
        }
        if (!fs::exists(validatedPath)) {
            std::cerr << "Error: Input file not found: " << inputFile << "\n";
            std::cerr << "Hint: Check the file path and ensure the file exists\n";
            return 1;
        }
    }

    if (options.shouldPrintVerbose()) {
        std::cout << "Compiling shader program: " << options.programName << "\n";
        std::cout << "Input files: ";
        for (const auto& file : options.inputFiles) {
            std::cout << file << " ";
        }
        std::cout << "\n";
    }

    // Determine output path early to check for existing bundle
    fs::path outputPath;
    fs::path outputDir;
    if (!options.outputPath.empty()) {
        outputPath = options.outputPath;
        outputDir = outputPath.parent_path();
    } else if (!options.outputDir.empty()) {
        if (!options.dryRun) {
            fs::create_directories(options.outputDir);
        }
        outputDir = options.outputDir;
        outputPath = fs::path(options.outputDir) / (options.programName + ".json");
    } else {
        outputDir = ".";
        outputPath = options.programName + ".json";
    }

    // Load old UUID before building (for cleanup if hash changes)
    std::string oldUuid = ShaderBundleSerializer::LoadUuid(outputPath);
    if (options.shouldPrintVerbose() && !oldUuid.empty()) {
        std::cout << "Existing bundle UUID: " << oldUuid << "\n";
    }

    // Smart pipeline type detection from input files (using library utility)
    PipelineTypeConstraint pipelineType = options.pipelineType;
    auto detection = ShaderPipelineUtils::DetectPipelineFromFiles(inputFiles);
    if (detection.confident) {
        pipelineType = detection.type;
        if (options.shouldPrintVerbose()) {
            std::cout << "Pipeline type auto-detected: " << detection.reason << "\n";
        }
    } else if (options.shouldPrintVerbose()) {
        std::cout << "Pipeline type: " << detection.reason << "\n";
    }

    // Auto-discover sibling shader files with same base name (using library utility)
    uint32_t discovered = ShaderPipelineUtils::DiscoverSiblingShaders(inputFiles, pipelineType);
    if (discovered > 0 && options.shouldPrintVerbose()) {
        std::cout << "Discovered " << discovered << " additional shader file(s)\n";
    }

    // Validate required stages are present (using library utility)
    std::string validationError = ShaderPipelineUtils::ValidatePipelineStages(inputFiles, pipelineType);
    if (!validationError.empty() && options.shouldPrint()) {
        std::cerr << "Warning: " << validationError << "\n";
        // Continue anyway - user may have intentional partial pipeline
    }

    // === Dry run: show what would happen and exit ===
    if (options.dryRun) {
        std::cout << "[DRY RUN] Would compile shader program: " << options.programName << "\n";
        std::cout << "[DRY RUN] Input files:\n";
        for (const auto& file : inputFiles) {
            auto stageOpt = ShaderPipelineUtils::DetectStageFromPath(file);
            std::string stageName = stageOpt ? ShaderStageName(*stageOpt) : "Unknown";
            std::cout << "  - " << file << " (" << stageName << ")\n";
        }
        std::cout << "[DRY RUN] Output: " << outputPath << "\n";
        std::cout << "[DRY RUN] SDI dir: " << options.sdiConfig.outputDirectory << "\n";
        std::cout << "[DRY RUN] Pipeline type: " << PipelineTypeName(pipelineType) << "\n";
        return 0;
    }

    // Create builder
    ShaderBundleBuilder builder;
    builder.SetProgramName(options.programName)
           .SetPipelineType(pipelineType)
           .SetSdiConfig(options.sdiConfig)
           .EnableSdiGeneration(options.generateSdi);

    // Add stages with path validation
    for (const auto& inputFile : inputFiles) {
        fs::path filePath(inputFile);
        fs::path validatedPath = ValidateAndSanitizePath(filePath, false);

        // Use library utility for stage detection (single source of truth)
        auto stageOpt = ShaderPipelineUtils::DetectStageFromPath(validatedPath);
        if (!stageOpt && options.shouldPrint()) {
            std::cerr << "Warning: Unknown shader stage for extension '"
                      << validatedPath.extension() << "', defaulting to Vertex\n";
        }
        ShaderStage stage = stageOpt.value_or(ShaderStage::Vertex);

        if (options.shouldPrintVerbose()) {
            std::cout << "Adding stage: " << ShaderStageName(stage) << " from " << validatedPath << "\n";
        }

        builder.AddStageFromFile(stage, validatedPath);
    }

    // Build
    if (options.shouldPrintVerbose()) {
        std::cout << "Building shader bundle...\n";
    }

    auto result = builder.Build();

    if (!result.success) {
        std::cerr << "Error: Compilation failed: " << result.errorMessage << "\n";
        std::cerr << "Hint: Check shader syntax with 'glslangValidator <shader_file>'\n";
        return 1;
    }

    // Print warnings (unless quiet)
    if (!result.warnings.empty() && options.shouldPrint()) {
        std::cout << "Warnings:\n";
        for (const auto& warning : result.warnings) {
            std::cout << "  - " << warning << "\n";
        }
    }

    // Print statistics
    if (options.shouldPrintVerbose()) {
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

    // Clean up old SDI files if UUID changed (outputPath/outputDir already determined above)
    const std::string& newUuid = result.bundle->uuid;
    if (!oldUuid.empty() && oldUuid != newUuid) {
        if (options.shouldPrintVerbose()) {
            std::cout << "UUID changed: " << oldUuid << " -> " << newUuid << "\n";
        }
        CleanupOldSdiFiles(oldUuid, options.sdiConfig.outputDirectory, options.shouldPrintVerbose());
    } else if (!oldUuid.empty() && options.shouldPrintVerbose()) {
        std::cout << "UUID unchanged, reusing existing SDI\n";
    }

    // Security: Validate output path
    fs::path validatedOutputPath = ValidateAndSanitizePath(outputPath, true);
    if (validatedOutputPath.empty()) {
        std::cerr << "Error: Invalid or unsafe output path: " << outputPath << "\n";
        std::cerr << "Hint: Ensure the output directory exists and is writable\n";
        return 1;
    }

    // Create file manifest for tracking
    FileManifest manifest(outputDir);

    // Configure serializer with manifest tracking
    BundleSerializerConfig serializerConfig;
    serializerConfig.embedSpirv = options.embedSpirv;
    serializerConfig.onFileWritten = [&manifest](const fs::path& file) {
        manifest.TrackFile(file);
    };

    // Save bundle using library serializer
    if (!ShaderBundleSerializer::SaveToJson(*result.bundle, validatedOutputPath, serializerConfig)) {
        std::cerr << "Error: Failed to save bundle\n";
        std::cerr << "Hint: Check disk space and write permissions for " << outputDir << "\n";
        return 1;
    }

    // Save manifest
    manifest.Save();

    if (options.shouldPrintVerbose()) {
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
        std::cerr << "Hint: Provide bundle JSON files, e.g.: sdi_tool build-registry shader1.json shader2.json\n";
        return 1;
    }

    if (options.shouldPrintVerbose()) {
        std::cout << "Building SDI registry from " << options.inputFiles.size() << " bundles\n";
    }

    // Create registry manager
    fs::path registryPath;
    if (!options.outputPath.empty()) {
        registryPath = fs::path(options.outputPath).parent_path();
    } else if (!options.outputDir.empty()) {
        registryPath = options.outputDir;
    } else {
        registryPath = DEFAULT_OUTPUT_DIR;
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
        if (!ShaderBundleSerializer::LoadFromJson(bundleFile, bundle)) {
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
        } else if (options.shouldPrintVerbose()) {
            std::cout << "Registered: " << bundle.program.name << " (UUID: " << bundle.uuid << ")\n";
        }
    }

    // Generate registry header (happens automatically via RegenerateRegistry)
    if (!registry.RegenerateRegistry()) {
        std::cerr << "Error: Failed to generate registry header\n";
        std::cerr << "Hint: Check write permissions for " << registryPath << "\n";
        return 1;
    }

    fs::path outputFile = registryConfig.registryHeaderPath;

    if (options.shouldPrint()) {
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
        std::cerr << "Hint: Provide a JSON config file, e.g.: sdi_tool batch shaders.json\n";
        return 1;
    }

    fs::path configPath = options.inputFiles[0];
    if (!fs::exists(configPath)) {
        std::cerr << "Error: Config file not found: " << configPath << "\n";
        std::cerr << "Hint: Create a batch config JSON file (see --help for format)\n";
        return 1;
    }

    // Load config
    std::ifstream configFile(configPath);
    nlohmann::json config;
    try {
        configFile >> config;
    } catch (const std::exception& e) {
        std::cerr << "Error: Failed to parse config: " << e.what() << "\n";
        std::cerr << "Hint: Ensure the config file is valid JSON (see --help for format)\n";
        return 1;
    }

    // Validate config has required fields
    if (!config.contains("shaders") || !config["shaders"].is_array()) {
        std::cerr << "Error: Config file missing 'shaders' array\n";
        std::cerr << "Hint: Config must have format: { \"shaders\": [...] }\n";
        return 1;
    }

    std::string outputDir = options.outputDir.empty() ? DEFAULT_OUTPUT_DIR : options.outputDir;

    if (!options.dryRun) {
        fs::create_directories(outputDir);
    }

    // Create file manifest for tracking all generated files
    FileManifest manifest(outputDir);

    std::vector<std::string> generatedBundles;
    size_t totalShaders = config["shaders"].size();
    size_t currentShader = 0;

    // Process each shader
    for (const auto& shaderConfig : config["shaders"]) {
        currentShader++;

        if (!shaderConfig.contains("name") || !shaderConfig.contains("stages")) {
            std::cerr << "Error: Shader entry missing 'name' or 'stages' field\n";
            return 1;
        }

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

        // Progress indication
        if (options.shouldPrint()) {
            std::cout << "[" << currentShader << "/" << totalShaders << "] "
                      << shaderOptions.programName;
            if (options.shouldPrintVerbose()) {
                std::cout << "\n";
            } else {
                std::cout << "... ";
                std::cout.flush();
            }
        }

        int result = CommandCompile(shaderOptions);
        if (result != 0) {
            std::cerr << "\nError: Failed to compile shader: " << shaderOptions.programName << "\n";
            std::cerr << "Batch processing aborted due to compilation failure.\n";
            return result;  // Fail fast - exit immediately with error code
        }

        if (options.shouldPrint() && !options.shouldPrintVerbose()) {
            std::cout << "OK\n";
        }

        generatedBundles.push_back((fs::path(outputDir) / (shaderOptions.programName + ".json")).string());
    }

    // Build registry if requested
    if (config.contains("buildRegistry") && config["buildRegistry"].get<bool>()) {
        if (options.shouldPrint()) {
            std::cout << "[Registry] Building SDI_Registry.h... ";
            std::cout.flush();
        }

        ToolOptions registryOptions = options;
        registryOptions.command = "build-registry";
        registryOptions.inputFiles = generatedBundles;
        registryOptions.outputDir = outputDir;

        int result = CommandBuildRegistry(registryOptions);
        if (result != 0) {
            std::cerr << "\nError: Failed to build registry\n";
            return result;  // Return error code for CI/CD
        }

        if (options.shouldPrint() && !options.shouldPrintVerbose()) {
            std::cout << "OK\n";
        }
    }

    // Cleanup orphaned files (skip in dry run)
    if (!options.dryRun) {
        uint32_t removed = manifest.CleanupOrphaned();
        if (removed > 0 && options.shouldPrintVerbose()) {
            std::cout << "Cleaned up " << removed << " orphaned files\n";
        }

        // Save final manifest
        manifest.Save();
    }

    if (options.shouldPrint()) {
        std::cout << "\nBatch processing complete!\n";
        std::cout << "Processed " << generatedBundles.size() << " shaders\n";
        std::cout << "Output directory: " << outputDir << "\n";
    }

    return 0;
}

/**
 * @brief Command: Clean up orphaned SDI header files
 *
 * Uses the library's SdiFileManager to scan naming files and delete orphaned SDIs.
 * This ensures single source of truth - the same logic is used by both tool and library.
 */
int CommandCleanupSdi(const ToolOptions& options) {
    // Check inputFiles first (positional arg), then outputDir, then default
    std::string sdiDir;
    if (!options.inputFiles.empty()) {
        sdiDir = options.inputFiles[0];
    } else if (!options.outputDir.empty()) {
        sdiDir = options.outputDir;
    } else {
        sdiDir = fs::path(DEFAULT_OUTPUT_DIR) / DEFAULT_SDI_SUBDIR;
    }

    if (!fs::exists(sdiDir)) {
        std::cerr << "Error: SDI directory does not exist: " << sdiDir << "\n";
        std::cerr << "Hint: Specify SDI directory, e.g.: sdi_tool cleanup-sdi ./generated/sdi\n";
        return 1;
    }

    if (options.shouldPrintVerbose()) {
        std::cout << "Scanning SDI directory: " << sdiDir << "\n";
    }

    // Use library's SdiFileManager for cleanup (single source of truth)
    SdiFileManager sdiManager(sdiDir);

    // Get referenced UUIDs for verbose output
    if (options.shouldPrintVerbose()) {
        auto referencedUuids = sdiManager.GetReferencedUuids();

        // Print naming file -> SDI mappings
        for (const auto& entry : fs::directory_iterator(sdiDir)) {
            if (!entry.is_regular_file()) continue;
            std::string filename = entry.path().filename().string();

            if (filename.size() > 7 &&
                filename.substr(filename.size() - 7) == "Names.h" &&
                filename.find("-SDI.h") == std::string::npos) {

                std::ifstream file(entry.path());
                if (file.is_open()) {
                    std::string line;
                    while (std::getline(file, line)) {
                        if (line.find("#include") != std::string::npos &&
                            line.find("-SDI.h") != std::string::npos) {
                            std::string uuid = SdiFileManager::ExtractSdiUuidFromInclude(line);
                            if (!uuid.empty()) {
                                std::cout << "  " << filename << " -> " << uuid << "-SDI.h\n";
                            }
                        }
                    }
                    file.close();
                }
            }
        }

        std::cout << "Found " << referencedUuids.size() << " unique SDI(s) referenced by naming files\n";
    }

    // Count total SDIs before cleanup for statistics
    uint32_t totalSdis = 0;
    for (const auto& entry : fs::directory_iterator(sdiDir)) {
        if (!entry.is_regular_file()) continue;
        std::string filename = entry.path().filename().string();
        if (filename.size() > 7 && filename.substr(filename.size() - 7) == "-SDI.h") {
            ++totalSdis;
        }
    }

    // Perform cleanup using library function
    std::vector<std::string> referencedUuidsOut;
    std::vector<fs::path> orphanedFiles;
    uint32_t removed = sdiManager.CleanupOrphanedSdis(
        options.verbose,
        &referencedUuidsOut,
        &orphanedFiles
    );

    // Print deleted files in verbose mode
    if (options.verbose) {
        for (const auto& orphan : orphanedFiles) {
            std::cout << "  Deleted: " << orphan.filename() << "\n";
        }
    }

    // Summary
    std::cout << "SDI cleanup complete:\n";
    std::cout << "  Total SDI files: " << totalSdis << "\n";
    std::cout << "  Referenced by Names.h: " << referencedUuidsOut.size() << "\n";
    std::cout << "  Orphaned (deleted): " << removed << "\n";

    return 0;
}

/**
 * @brief Command: Clean up orphaned SPIRV files
 */
int CommandCleanup(const ToolOptions& options) {
    // Check inputFiles first (positional arg), then outputDir, then default
    std::string outputDir;
    if (!options.inputFiles.empty()) {
        outputDir = options.inputFiles[0];
    } else if (!options.outputDir.empty()) {
        outputDir = options.outputDir;
    } else {
        outputDir = "./generated";
    }

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
            // Deprecation warning for compile-compute
            if (options.command == "compile-compute") {
                if (!options.quiet) {
                    std::cerr << "Warning: 'compile-compute' is deprecated. "
                              << "Use 'compile' with a .comp file instead (auto-detected).\n";
                }
                options.pipelineType = PipelineTypeConstraint::Compute;
            }
            return CommandCompile(options);
        } else if (options.command == "build-registry") {
            return CommandBuildRegistry(options);
        } else if (options.command == "batch") {
            return CommandBatch(options);
        } else if (options.command == "cleanup") {
            return CommandCleanup(options);
        } else if (options.command == "cleanup-sdi") {
            return CommandCleanupSdi(options);
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
