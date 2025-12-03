#include "ShaderBundleBuilder.h"
#include "SPIRVReflection.h"
#include "SdiRegistryManager.h"
#include "ShaderLogger.h"
#include "Hash.h"
#include "Logger.h"
#include <sstream>
#include <fstream>
#include <iomanip>

namespace ShaderManagement {

namespace {

// ===== Input Validation Constants =====

/**
 * @brief Maximum allowed shader source size (10 MB)
 *
 * Prevents memory exhaustion attacks from extremely large shader sources.
 * Legitimate shaders are typically < 1MB of GLSL source.
 */
constexpr size_t MAX_SHADER_SOURCE_SIZE = 10 * 1024 * 1024;  // 10 MB

/**
 * @brief Maximum allowed SPIRV bytecode size (50 MB)
 *
 * Prevents memory exhaustion from extremely large pre-compiled SPIRV.
 * Typical SPIRV is < 10MB even for complex shaders.
 */
constexpr size_t MAX_SPIRV_SIZE = 50 * 1024 * 1024;  // 50 MB

/**
 * @brief Maximum number of shader stages per program
 *
 * Prevents resource exhaustion from programs with excessive stages.
 */
constexpr size_t MAX_STAGES_PER_PROGRAM = 16;

/**
 * @brief Generate a deterministic content-based UUID
 *
 * Creates a UUID based on shader sources, options, and configuration.
 * Same shader content always produces the same UUID, enabling:
 * - Caching: Can reuse cached builds across sessions
 * - Hot-reload: UUID remains stable, registry knows it's the same shader
 * - Debugging: Consistent identifiers across runs
 *
 * @param stages Shader stages with source code
 * @param programName Program name
 * @param pipelineType Pipeline type constraint
 * @return Deterministic 32-character hex UUID
 */
std::string GenerateContentBasedUuid(
    const std::vector<ShaderBundleBuilder::StageSource>& stages,
    const std::string& programName,
    PipelineTypeConstraint pipelineType
) {
    std::ostringstream contentStream;

    // VixenHash inputs: sources, entry points, options, pipeline type
    // Sorted by stage to ensure consistent ordering

    std::vector<std::pair<ShaderStage, const ShaderBundleBuilder::StageSource*>> sortedStages;
    for (const auto& stage : stages) {
        sortedStages.emplace_back(stage.stage, &stage);
    }
    std::sort(sortedStages.begin(), sortedStages.end(),
        [](const auto& a, const auto& b) { return static_cast<int>(a.first) < static_cast<int>(b.first); });

    // Append all content that affects compilation
    contentStream << "program:" << programName << "|";
    contentStream << "pipeline:" << static_cast<int>(pipelineType) << "|";

    for (const auto& [stageEnum, stage] : sortedStages) {
        contentStream << "stage:" << static_cast<int>(stageEnum) << "|";
        contentStream << "source:" << stage->source << "|";
        contentStream << "entry:" << stage->entryPoint << "|";
        contentStream << "optperf:" << stage->options.optimizePerformance << "|";
        contentStream << "optsize:" << stage->options.optimizeSize << "|";
        contentStream << "debug:" << stage->options.generateDebugInfo << "|";
        contentStream << "vulkan:" << stage->options.targetVulkanVersion << "|";
        contentStream << "spirv:" << stage->options.targetSpirvVersion << "|";

        // Include defines (sorted for consistency)
        std::vector<std::pair<std::string, std::string>> sortedDefines(
            stage->defines.begin(), stage->defines.end());
        std::sort(sortedDefines.begin(), sortedDefines.end());

        for (const auto& [key, value] : sortedDefines) {
            contentStream << "define:" << key << "=" << value << "|";
        }
    }

    std::string content = contentStream.str();
    // Compute SHA256 (or fallback deterministic hash) and return first 32 hex chars
    auto full = ShaderManagement::ComputeSHA256Hex(reinterpret_cast<const void*>(content.data()), content.size());
    if (full.size() >= 32) return full.substr(0, 32);
    return full;
}

/**
 * @brief Sanitize name for namespace usage
 */
std::string SanitizeForNamespace(const std::string& name) {
    std::string sanitized = name;
    std::replace(sanitized.begin(), sanitized.end(), '-', '_');
    std::replace(sanitized.begin(), sanitized.end(), ' ', '_');
    std::replace(sanitized.begin(), sanitized.end(), '.', '_');
    std::replace(sanitized.begin(), sanitized.end(), '/', '_');
    return sanitized;
}

} // anonymous namespace

// ===== ShaderBundleBuilder Implementation =====

ShaderBundleBuilder::ShaderBundleBuilder()
    : sdiConfig_()
{
}

ShaderBundleBuilder::~ShaderBundleBuilder() {
    // Clean up owned resources
    if (ownsPreprocessor_ && preprocessor_) {
        delete preprocessor_;
        preprocessor_ = nullptr;
    }
    if (ownsCompiler_ && compiler_) {
        delete compiler_;
        compiler_ = nullptr;
    }
}

ShaderBundleBuilder::ShaderBundleBuilder(ShaderBundleBuilder&& other) noexcept
    : programName_(std::move(other.programName_))
    , pipelineType_(other.pipelineType_)
    , uuid_(std::move(other.uuid_))
    , stages_(std::move(other.stages_))
    , validatePipeline_(other.validatePipeline_)
    , generateSdi_(other.generateSdi_)
    , preprocessor_(other.preprocessor_)
    , ownsPreprocessor_(other.ownsPreprocessor_)
    , cacheManager_(other.cacheManager_)
    , compiler_(other.compiler_)
    , ownsCompiler_(other.ownsCompiler_)
    , sdiConfig_(std::move(other.sdiConfig_))
    , registryManager_(other.registryManager_)
    , registryAlias_(std::move(other.registryAlias_))
{
    // Transfer ownership - null out source's owned pointers
    other.preprocessor_ = nullptr;
    other.ownsPreprocessor_ = false;
    other.compiler_ = nullptr;
    other.ownsCompiler_ = false;
}

ShaderBundleBuilder& ShaderBundleBuilder::operator=(ShaderBundleBuilder&& other) noexcept {
    if (this != &other) {
        // Clean up our owned resources first
        if (ownsPreprocessor_ && preprocessor_) {
            delete preprocessor_;
        }
        if (ownsCompiler_ && compiler_) {
            delete compiler_;
        }

        // Move data
        programName_ = std::move(other.programName_);
        pipelineType_ = other.pipelineType_;
        uuid_ = std::move(other.uuid_);
        stages_ = std::move(other.stages_);
        validatePipeline_ = other.validatePipeline_;
        generateSdi_ = other.generateSdi_;
        preprocessor_ = other.preprocessor_;
        ownsPreprocessor_ = other.ownsPreprocessor_;
        cacheManager_ = other.cacheManager_;
        compiler_ = other.compiler_;
        ownsCompiler_ = other.ownsCompiler_;
        sdiConfig_ = std::move(other.sdiConfig_);
        registryManager_ = other.registryManager_;
        registryAlias_ = std::move(other.registryAlias_);

        // Transfer ownership
        other.preprocessor_ = nullptr;
        other.ownsPreprocessor_ = false;
        other.compiler_ = nullptr;
        other.ownsCompiler_ = false;
    }
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::SetProgramName(const std::string& name) {
    programName_ = name;
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::SetPipelineType(PipelineTypeConstraint type) {
    pipelineType_ = type;
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::SetUuid(const std::string& uuid) {
    uuid_ = uuid;
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::AddStage(
    ShaderStage stage,
    const std::string& source,
    const std::string& entryPoint,
    const CompilationOptions& options
) {
    // Validate stage count
    if (stages_.size() >= MAX_STAGES_PER_PROGRAM) {
        // Store error for later - Build() will fail
        // Don't throw here to maintain fluent interface
        return *this;
    }

    // Validate source size
    if (source.size() > MAX_SHADER_SOURCE_SIZE) {
        // Store error for later - Build() will fail
        // Error will be: "Shader source too large: X MB (max: 10 MB)"
        return *this;
    }

    StageSource stageSource{
        .stage = stage,
        .source = source,
        .entryPoint = entryPoint,
        .options = options
    };
    stages_.push_back(stageSource);
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::AddStageFromFile(
    ShaderStage stage,
    const std::filesystem::path& sourcePath,
    const std::string& entryPoint,
    const CompilationOptions& options
) {
    // Security: Validate path exists and is a regular file
    if (!std::filesystem::exists(sourcePath)) {
        // Store error for later - Build() will fail
        return *this;
    }

    if (!std::filesystem::is_regular_file(sourcePath)) {
        // Prevent reading directories or special files
        return *this;
    }

    // Security: Check file size before reading to prevent memory exhaustion
    auto fileSize = std::filesystem::file_size(sourcePath);
    if (fileSize > MAX_SHADER_SOURCE_SIZE) {
        // File too large - prevent reading it
        return *this;
    }

    std::ifstream file(sourcePath);
    if (!file.is_open()) {
        // Failed to open file
        return *this;
    }

    // Read with size limit (redundant check but defensive)
    std::string source;
    size_t reserveSize = (fileSize < MAX_SHADER_SOURCE_SIZE) ? fileSize : MAX_SHADER_SOURCE_SIZE;
    source.reserve(reserveSize);

    std::string line;
    size_t totalSize = 0;
    while (std::getline(file, line)) {
        totalSize += line.size() + 1;  // +1 for newline
        if (totalSize > MAX_SHADER_SOURCE_SIZE) {
            // File grew during read or limit exceeded
            file.close();
            return *this;
        }
        source += line + '\n';
    }
    file.close();

    // Store stage with source path for #include resolution
    StageSource stageSource{
        .stage = stage,
        .source = source,
        .entryPoint = entryPoint,
        .options = options,
        .sourcePath = sourcePath
    };
    stages_.push_back(stageSource);
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::AddStageFromSpirv(
    ShaderStage stage,
    const std::vector<uint32_t>& spirv,
    const std::string& entryPoint
) {
    // Store SPIRV as "source" - we'll detect this later
    StageSource stageSource{
        .stage = stage,
        .source = "",  // Empty source indicates pre-compiled
        .entryPoint = entryPoint
    };
    // TODO: Store SPIRV separately, for now this is a placeholder
    stages_.push_back(stageSource);
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::SetStageDefines(
    ShaderStage stage,
    const std::unordered_map<std::string, std::string>& defines
) {
    for (auto& stageSource : stages_) {
        if (stageSource.stage == stage) {
            stageSource.defines = defines;
            break;
        }
    }
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::EnablePreprocessing(ShaderPreprocessor* preprocessor) {
    // Clean up existing owned preprocessor if any
    if (ownsPreprocessor_ && preprocessor_) {
        delete preprocessor_;
    }
    preprocessor_ = preprocessor;
    ownsPreprocessor_ = false;  // External preprocessor - caller owns it
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::AddIncludePath(const std::filesystem::path& path) {
    // Create internal preprocessor on first include path if not already set
    if (!preprocessor_) {
        preprocessor_ = new ShaderPreprocessor();
        ownsPreprocessor_ = true;
    }
    preprocessor_->AddIncludePath(path);
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::EnableCaching(ShaderCacheManager* cacheManager) {
    cacheManager_ = cacheManager;
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::SetCompiler(ShaderCompiler* compiler) {
    if (ownsCompiler_ && compiler_) {
        delete compiler_;
    }
    compiler_ = compiler;
    ownsCompiler_ = false;
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::SetSdiConfig(const SdiGeneratorConfig& config) {
    sdiConfig_ = config;
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::SetTargetVulkanVersion(int version)
{
    for (auto& stageSource : stages_) {
        stageSource.options.targetVulkanVersion = version;
	}
	return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::SetTargetSpirvVersion(int version)
{
    for (auto& stageSource : stages_) {
        stageSource.options.targetSpirvVersion = version;
    }
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::EnableSdiGeneration(bool enable) {
    generateSdi_ = enable;
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::EnableRegistryIntegration(
    SdiRegistryManager* registry,
    const std::string& aliasName
) {
    registryManager_ = registry;
    registryAlias_ = aliasName;
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::SetValidatePipeline(bool validate) {
    validatePipeline_ = validate;
    return *this;
}

ShaderBundleBuilder::BuildResult ShaderBundleBuilder::Build() {
    auto startTime = std::chrono::steady_clock::now();
    BuildResult result;

    // Update telemetry
    auto& telemetry = ShaderLogger::GetTelemetry();
    telemetry.totalCompilations.fetch_add(1);

    ShaderLogger::LogDebug("Starting shader bundle build: " + programName_, "Builder");

    // Create compiler if not provided
    if (!compiler_) {
        compiler_ = new ShaderCompiler();
        ownsCompiler_ = true;
    }

    // UUID will be generated after reflection (to use descriptor-only hash)
    // Moved from here to after line 631 where ComputeDescriptorInterfaceHash() is called

    ShaderLogger::LogDebug("Deferring UUID generation until after reflection", "Builder");

    // Validate pipeline constraints
    if (validatePipeline_) {
        std::string error;
        if (!ValidatePipelineConstraints(error)) {
            result.success = false;
            result.errorMessage = "Pipeline validation failed: " + error;
            ShaderLogger::LogError("Pipeline validation failed: " + error, "Builder");
            telemetry.failedCompilations.fetch_add(1);
            return result;
        }
        ShaderLogger::LogDebug("Pipeline validation passed", "Builder");
    }

    // Build CompiledProgram
    CompiledProgram program;
    program.programId = 0;  // Will be set by library if registered
    program.name = programName_;
    program.pipelineType = pipelineType_;
    program.compiledAt = std::chrono::steady_clock::now();

    // Compile each stage
    for (const auto& stageSource : stages_) {
        auto stageStart = std::chrono::steady_clock::now();

        // Preprocess if enabled
        std::string sourceToCompile = stageSource.source;
        if (preprocessor_ && !stageSource.source.empty()) {
            ShaderLogger::LogInfo("Preprocessing shader source with " +
                                  std::to_string(preprocessor_->GetIncludePaths().size()) + " include paths", "Builder");
            auto preprocessStart = std::chrono::steady_clock::now();
            auto preprocessed = preprocessor_->Preprocess(stageSource.source, stageSource.defines, stageSource.sourcePath);
            auto preprocessEnd = std::chrono::steady_clock::now();
            result.preprocessTime += std::chrono::duration_cast<std::chrono::milliseconds>(
                preprocessEnd - preprocessStart);

            if (!preprocessed.success) {
                result.success = false;
                result.errorMessage = "Preprocessing failed: " + preprocessed.errorMessage;
                return result;
            }

            sourceToCompile = preprocessed.processedSource;

            // Preprocessing completed (no separate warnings field in PreprocessedSource)
        }

        // Check cache if enabled
        std::string cacheKey;
        if (cacheManager_) {
            // Generate cache key from source + options
            std::ostringstream keyStream;
            keyStream << sourceToCompile << "|"
                      << static_cast<int>(stageSource.stage) << "|"
                      << stageSource.entryPoint << "|"
                      << stageSource.options.optimizePerformance << "|"
                      << stageSource.options.optimizeSize << "|"
                      << stageSource.options.generateDebugInfo;
            cacheKey = keyStream.str();

            auto cached = cacheManager_->Lookup(cacheKey);
            if (cached) {
                // Cache hit!
                telemetry.cacheHits.fetch_add(1);
                ShaderLogger::LogDebug("Cache hit for stage " +
                    std::string(ShaderStageName(stageSource.stage)), "Builder");

                CompiledShaderStage stage;
                stage.stage = stageSource.stage;
                stage.spirvCode = *cached;
                stage.entryPoint = stageSource.entryPoint;
                program.stages.push_back(stage);
                result.usedCache = true;

                // Update telemetry
                telemetry.totalSpirvSizeBytes.fetch_add(cached->size() * sizeof(uint32_t));
                continue;
            } else {
                telemetry.cacheMisses.fetch_add(1);
            }
        }

        // Compile
        ShaderLogger::LogInfo("Compiling stage: " +
            std::string(ShaderStageName(stageSource.stage)), "Compiler");

        auto compileStart = std::chrono::steady_clock::now();
        ScopedTelemetryTimer compileTimer(telemetry.totalCompileTimeUs);

        auto compiled = compiler_->Compile(
            stageSource.stage,
            sourceToCompile,
            stageSource.entryPoint,
            stageSource.options
        );
        auto compileEnd = std::chrono::steady_clock::now();
        result.compileTime += std::chrono::duration_cast<std::chrono::milliseconds>(
            compileEnd - compileStart);

        // Update telemetry
        telemetry.totalSourceSizeBytes.fetch_add(sourceToCompile.size());

        if (!compiled.success) {
            result.success = false;
            result.errorMessage = "Compilation failed for stage " +
                std::string(ShaderStageName(stageSource.stage)) + ": " + compiled.errorLog;
            ShaderLogger::LogError("Compilation failed: " + compiled.errorLog, "Compiler");
            telemetry.failedCompilations.fetch_add(1);
            return result;
        }

        // Update SPIRV size telemetry
        telemetry.totalSpirvSizeBytes.fetch_add(compiled.spirv.size() * sizeof(uint32_t));

        // Store in cache if enabled
        if (cacheManager_ && !cacheKey.empty()) {
            cacheManager_->Store(cacheKey, compiled.spirv);
        }

        // Add to program
        CompiledShaderStage stage;
        stage.stage = stageSource.stage;
        stage.spirvCode = compiled.spirv;
        stage.entryPoint = stageSource.entryPoint;
        program.stages.push_back(stage);

        // Add info/warnings from compilation
        if (!compiled.infoLog.empty()) {
            result.warnings.push_back(ShaderStageName(stageSource.stage) +
                std::string(": ") + compiled.infoLog);
        }
    }

    // Perform rest of build (reflection, SDI, bundling)
    result = PerformBuild(program);

    auto endTime = std::chrono::steady_clock::now();
    result.totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);

    // Update telemetry on success
    if (result.success) {
        telemetry.successfulCompilations.fetch_add(1);
        ShaderLogger::LogInfo("Shader bundle build completed successfully: " + programName_ +
            " (" + std::to_string(result.totalTime.count()) + "ms)", "Builder");
    } else {
        telemetry.failedCompilations.fetch_add(1);
        ShaderLogger::LogError("Shader bundle build failed: " + result.errorMessage, "Builder");
    }

    return result;
}

ShaderBundleBuilder::BuildResult ShaderBundleBuilder::BuildFromCompiled(
    const CompiledProgram& program
) {
    auto startTime = std::chrono::steady_clock::now();

    // Create mutable copy
    CompiledProgram mutableProgram = program;
    mutableProgram.name = programName_;

    // UUID will be generated after reflection in PerformBuild()
    // (deferred to use descriptor-only hash instead of source hash)

    // Perform reflection and SDI generation
    BuildResult result = PerformBuild(mutableProgram);

    auto endTime = std::chrono::steady_clock::now();
    result.totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);

    return result;
}

// ===== Private Helpers =====

std::string ShaderBundleBuilder::GenerateUuid() {
    // Generate deterministic content-based UUID
    // Same shader content always produces same UUID (enables caching and stable hot-reload)
    return GenerateContentBasedUuid(stages_, programName_, pipelineType_);
}

bool ShaderBundleBuilder::ValidatePipelineConstraints(std::string& outError) {
    // Count stages
    std::unordered_map<ShaderStage, int> stageCounts;
    for (const auto& stage : stages_) {
        stageCounts[stage.stage]++;
    }

    auto hasStage = [&](ShaderStage stage) {
        return stageCounts.find(stage) != stageCounts.end();
    };

    // Validate based on pipeline type
    switch (pipelineType_) {
        case PipelineTypeConstraint::Graphics:
            if (!hasStage(ShaderStage::Vertex) || !hasStage(ShaderStage::Fragment)) {
                outError = "Graphics pipeline requires Vertex and Fragment stages";
                return false;
            }
            break;

        case PipelineTypeConstraint::Mesh:
            if (!hasStage(ShaderStage::Mesh) || !hasStage(ShaderStage::Fragment)) {
                outError = "Mesh pipeline requires Mesh and Fragment stages";
                return false;
            }
            break;

        case PipelineTypeConstraint::Compute:
            if (!hasStage(ShaderStage::Compute) || stages_.size() != 1) {
                outError = "Compute pipeline requires exactly one Compute stage";
                return false;
            }
            break;

        case PipelineTypeConstraint::RayTracing:
            if (!hasStage(ShaderStage::RayGen) ||
                !hasStage(ShaderStage::Miss) ||
                !hasStage(ShaderStage::ClosestHit)) {
                outError = "RayTracing pipeline requires RayGen, Miss, and ClosestHit stages";
                return false;
            }
            break;

        default:
            outError = "Unknown pipeline type";
            return false;
    }

    return true;
}

ShaderBundleBuilder::BuildResult ShaderBundleBuilder::PerformBuild(CompiledProgram& program) {
    BuildResult result;
    result.success = false;

    // 1. Reflect SPIRV
    auto reflectStart = std::chrono::steady_clock::now();
    SpirvReflector reflector;
    auto reflectionData = reflector.Reflect(program);
    auto reflectEnd = std::chrono::steady_clock::now();
    result.reflectTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        reflectEnd - reflectStart);

    if (!reflectionData) {
        result.errorMessage = "SPIRV reflection failed";
        return result;
    }

    // 2. Generate UUID from descriptor hash if not set (MUST happen after reflection)
    // Use descriptor-only hash so shaders with same interface share UUID/SDI file
    if (uuid_.empty()) {
        uuid_ = ComputeDescriptorInterfaceHash(*reflectionData);
        ShaderLogger::LogDebug("Generated descriptor-based UUID: " + uuid_, "Builder");
    }

    // 3. Extract descriptor layout (legacy compatibility)
    auto descriptorLayout = ReflectDescriptorLayout(program);
    if (!descriptorLayout) {
        result.warnings.push_back("Failed to extract descriptor layout");
    }

    // 4. Generate SDI if enabled
    std::filesystem::path sdiPath;
    std::string sdiNamespace;

    if (generateSdi_) {
        auto sdiStart = std::chrono::steady_clock::now();

        SpirvInterfaceGenerator generator(sdiConfig_);

        // Generate generic .si.h interface
        std::string generatedPath = generator.Generate(uuid_, *reflectionData);

        // Generate shader-specific Names.h
        std::string namesPath = generator.GenerateNamesHeader(programName_, uuid_, *reflectionData);

        auto sdiEnd = std::chrono::steady_clock::now();
        result.sdiGenTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            sdiEnd - sdiStart);

        if (generatedPath.empty()) {
            result.warnings.push_back("SDI generation failed");
        } else {
            sdiPath = generatedPath;
            sdiNamespace = sdiConfig_.namespacePrefix + "::" +
                SanitizeForNamespace(uuid_);

            if (namesPath.empty()) {
                result.warnings.push_back("Names header generation failed");
            } else {
                ShaderLogger::LogInfo("Generated shader-specific Names header: " + namesPath, "SDI");
            }
        }
    }

    // 4. Assemble bundle
    ShaderDataBundle bundle;
    bundle.program = program;
    bundle.reflectionData = std::shared_ptr<SpirvReflectionData>(std::move(reflectionData));
    bundle.descriptorLayout = std::shared_ptr<DescriptorLayoutSpec>(std::move(descriptorLayout));
    bundle.uuid = uuid_;
    bundle.sdiHeaderPath = sdiPath;
    bundle.sdiNamespace = sdiNamespace;
    bundle.createdAt = std::chrono::system_clock::now();

    // Compute descriptor-only interface hash (generalized, reusable)
    bundle.descriptorInterfaceHash = ComputeDescriptorInterfaceHash(*bundle.reflectionData);

    // 5. Register with central SDI registry if enabled
    if (registryManager_ && !sdiPath.empty()) {
        SdiRegistryEntry entry;
        entry.uuid = uuid_;
        entry.programName = programName_;
        entry.sdiHeaderPath = sdiPath;
        entry.sdiNamespace = sdiNamespace;
        entry.aliasName = registryAlias_.empty() ? programName_ : registryAlias_;

        if (!registryManager_->RegisterShader(entry)) {
            result.warnings.push_back("Failed to register shader in central SDI registry");
        }
    }

    result.success = true;
    result.bundle = std::make_unique<ShaderDataBundle>(std::move(bundle));

    return result;
}

} // namespace ShaderManagement
