#include "ShaderManagement/ShaderBundleBuilder.h"
#include "ShaderManagement/SPIRVReflection.h"
#include <sstream>
#include <fstream>
#include <iomanip>
#include <random>

namespace ShaderManagement {

namespace {

/**
 * @brief Generate a UUID-like string
 */
std::string GenerateRandomUuid() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(16) << dist(gen);
    oss << std::setw(16) << dist(gen);
    return oss.str();
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
    std::ifstream file(sourcePath);
    if (!file.is_open()) {
        // Store error for later - build() will fail
        return *this;
    }

    std::string source((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    file.close();

    return AddStage(stage, source, entryPoint, options);
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
    preprocessor_ = preprocessor;
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

ShaderBundleBuilder& ShaderBundleBuilder::EnableSdiGeneration(bool enable) {
    generateSdi_ = enable;
    return *this;
}

ShaderBundleBuilder& ShaderBundleBuilder::SetValidatePipeline(bool validate) {
    validatePipeline_ = validate;
    return *this;
}

ShaderBundleBuilder::BuildResult ShaderBundleBuilder::Build() {
    auto startTime = std::chrono::steady_clock::now();
    BuildResult result;

    // Create compiler if not provided
    if (!compiler_) {
        compiler_ = new ShaderCompiler();
        ownsCompiler_ = true;
    }

    // Generate UUID if not set
    if (uuid_.empty()) {
        uuid_ = GenerateUuid();
    }

    // Validate pipeline constraints
    if (validatePipeline_) {
        std::string error;
        if (!ValidatePipelineConstraints(error)) {
            result.success = false;
            result.errorMessage = "Pipeline validation failed: " + error;
            return result;
        }
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
            auto preprocessStart = std::chrono::steady_clock::now();
            auto preprocessed = preprocessor_->Preprocess(stageSource.source, stageSource.defines);
            auto preprocessEnd = std::chrono::steady_clock::now();
            result.preprocessTime += std::chrono::duration_cast<std::chrono::milliseconds>(
                preprocessEnd - preprocessStart);

            if (!preprocessed.success) {
                result.success = false;
                result.errorMessage = "Preprocessing failed: " + preprocessed.errorLog;
                return result;
            }

            sourceToCompile = preprocessed.processedSource;

            // Add warnings
            for (const auto& warning : preprocessed.warnings) {
                result.warnings.push_back("Preprocessor: " + warning);
            }
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
                CompiledShaderStage stage;
                stage.stage = stageSource.stage;
                stage.spirvCode = *cached;
                stage.entryPoint = stageSource.entryPoint;
                program.stages.push_back(stage);
                result.usedCache = true;
                continue;
            }
        }

        // Compile
        auto compileStart = std::chrono::steady_clock::now();
        auto compiled = compiler_->Compile(
            stageSource.stage,
            sourceToCompile,
            stageSource.entryPoint,
            stageSource.options
        );
        auto compileEnd = std::chrono::steady_clock::now();
        result.compileTime += std::chrono::duration_cast<std::chrono::milliseconds>(
            compileEnd - compileStart);

        if (!compiled.success) {
            result.success = false;
            result.errorMessage = "Compilation failed for stage " +
                std::string(ShaderStageName(stageSource.stage)) + ": " + compiled.errorLog;
            return result;
        }

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

        // Add warnings
        for (const auto& warning : compiled.warnings) {
            result.warnings.push_back(ShaderStageName(stageSource.stage) +
                std::string(": ") + warning);
        }
    }

    // Perform rest of build (reflection, SDI, bundling)
    result = PerformBuild(program);

    auto endTime = std::chrono::steady_clock::now();
    result.totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);

    return result;
}

ShaderBundleBuilder::BuildResult ShaderBundleBuilder::BuildFromCompiled(
    const CompiledProgram& program
) {
    auto startTime = std::chrono::steady_clock::now();

    // Create mutable copy
    CompiledProgram mutableProgram = program;
    mutableProgram.name = programName_;

    // Generate UUID if not set
    if (uuid_.empty()) {
        uuid_ = GenerateUuid();
    }

    // Perform reflection and SDI generation
    BuildResult result = PerformBuild(mutableProgram);

    auto endTime = std::chrono::steady_clock::now();
    result.totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);

    return result;
}

// ===== Private Helpers =====

std::string ShaderBundleBuilder::GenerateUuid() {
    return GenerateRandomUuid();
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
    auto reflectionData = SpirvReflector::Reflect(program);
    auto reflectEnd = std::chrono::steady_clock::now();
    result.reflectTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        reflectEnd - reflectStart);

    if (!reflectionData) {
        result.errorMessage = "SPIRV reflection failed";
        return result;
    }

    // 2. Extract descriptor layout (legacy compatibility)
    auto descriptorLayout = ReflectDescriptorLayout(program);
    if (!descriptorLayout) {
        result.warnings.push_back("Failed to extract descriptor layout");
    }

    // 3. Generate SDI if enabled
    std::filesystem::path sdiPath;
    std::string sdiNamespace;

    if (generateSdi_) {
        auto sdiStart = std::chrono::steady_clock::now();

        SpirvInterfaceGenerator generator(sdiConfig_);
        std::string generatedPath = generator.Generate(uuid_, *reflectionData);

        auto sdiEnd = std::chrono::steady_clock::now();
        result.sdiGenTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            sdiEnd - sdiStart);

        if (generatedPath.empty()) {
            result.warnings.push_back("SDI generation failed");
        } else {
            sdiPath = generatedPath;
            sdiNamespace = sdiConfig_.namespacePrefix + "::" +
                SanitizeForNamespace(uuid_);
        }
    }

    // 4. Assemble bundle
    ShaderDataBundle bundle;
    bundle.program = program;
    bundle.reflectionData = reflectionData;
    bundle.descriptorLayout = descriptorLayout;
    bundle.uuid = uuid_;
    bundle.sdiHeaderPath = sdiPath;
    bundle.sdiNamespace = sdiNamespace;
    bundle.createdAt = std::chrono::system_clock::now();

    result.success = true;
    result.bundle = bundle;

    return result;
}

} // namespace ShaderManagement
