#pragma once

#include "ShaderDataBundle.h"
#include "ShaderCompiler.h"
#include "ShaderPreprocessor.h"
#include "ShaderCacheManager.h"
#include "SpirvReflector.h"
#include "SpirvInterfaceGenerator.h"
#include <optional>
#include <random>

namespace ShaderManagement {

/**
 * @brief Builder for creating complete ShaderDataBundle instances
 *
 * Orchestrates the entire shader compilation pipeline:
 * 1. Preprocessing (optional)
 * 2. Compilation (GLSL → SPIRV)
 * 3. Caching (optional)
 * 4. Reflection (SPIRV → metadata)
 * 5. SDI generation (metadata → C++ header)
 * 6. Bundle assembly
 *
 * Fluent interface for easy configuration:
 *
 * @code
 * auto bundle = ShaderBundleBuilder()
 *     .SetProgramName("MyShader")
 *     .AddStage(ShaderStage::Vertex, vertexSource)
 *     .AddStage(ShaderStage::Fragment, fragmentSource)
 *     .EnableCaching(cacheManager)
 *     .EnablePreprocessing(preprocessor)
 *     .SetSdiConfig(sdiConfig)
 *     .Build();
 *
 * if (bundle) {
 *     // Use bundle->GetSpirv(), bundle->GetSdiIncludePath(), etc.
 * }
 * @endcode
 */
class ShaderBundleBuilder {
public:
    /**
     * @brief Stage source specification
     */
    struct StageSource {
        ShaderStage stage;
        std::string source;                 // GLSL source code
        std::string entryPoint = "main";
        CompilationOptions options;

        // Preprocessing options (applied if preprocessor is enabled)
        std::unordered_map<std::string, std::string> defines;
    };

    /**
     * @brief Build result
     */
    struct BuildResult {
        bool success = false;
        std::unique_ptr<ShaderDataBundle> bundle;
        std::string errorMessage;
        std::vector<std::string> warnings;

        // Build statistics
        std::chrono::milliseconds preprocessTime{0};
        std::chrono::milliseconds compileTime{0};
        std::chrono::milliseconds reflectTime{0};
        std::chrono::milliseconds sdiGenTime{0};
        std::chrono::milliseconds totalTime{0};
        bool usedCache = false;

        operator bool() const { return success; }

        const ShaderDataBundle& operator*() const {
            if (!bundle) {
                throw std::runtime_error("BuildResult: Attempted to dereference null bundle");
            }
            return *bundle;
        }

        const ShaderDataBundle* operator->() const {
            if (!bundle) {
                throw std::runtime_error("BuildResult: Attempted to access null bundle");
            }
            return bundle.get();
        }

        ShaderDataBundle* get() { return bundle.get(); }
        const ShaderDataBundle* get() const { return bundle.get(); }
    };

    ShaderBundleBuilder();

    // ===== Configuration Methods =====

    /**
     * @brief Set program name for debugging/logging
     */
    ShaderBundleBuilder& SetProgramName(const std::string& name);

    /**
     * @brief Set pipeline type constraint
     */
    ShaderBundleBuilder& SetPipelineType(PipelineTypeConstraint type);

    /**
     * @brief Set explicit UUID (otherwise auto-generated)
     */
    ShaderBundleBuilder& SetUuid(const std::string& uuid);

    /**
     * @brief Add a shader stage from source code
     */
    ShaderBundleBuilder& AddStage(
        ShaderStage stage,
        const std::string& source,
        const std::string& entryPoint = "main",
        const CompilationOptions& options = {}
    );

    /**
     * @brief Add a shader stage from file
     */
    ShaderBundleBuilder& AddStageFromFile(
        ShaderStage stage,
        const std::filesystem::path& sourcePath,
        const std::string& entryPoint = "main",
        const CompilationOptions& options = {}
    );

    /**
     * @brief Add a shader stage from pre-compiled SPIRV
     */
    ShaderBundleBuilder& AddStageFromSpirv(
        ShaderStage stage,
        const std::vector<uint32_t>& spirv,
        const std::string& entryPoint = "main"
    );

    /**
     * @brief Set preprocessor defines for a specific stage
     */
    ShaderBundleBuilder& SetStageDefines(
        ShaderStage stage,
        const std::unordered_map<std::string, std::string>& defines
    );

    /**
     * @brief Enable preprocessing with custom preprocessor
     */
    ShaderBundleBuilder& EnablePreprocessing(ShaderPreprocessor* preprocessor);

    /**
     * @brief Enable caching with custom cache manager
     */
    ShaderBundleBuilder& EnableCaching(ShaderCacheManager* cacheManager);

    /**
     * @brief Set custom compiler (otherwise uses default)
     */
    ShaderBundleBuilder& SetCompiler(ShaderCompiler* compiler);

    /**
     * @brief Set SDI generator configuration
     */
    ShaderBundleBuilder& SetSdiConfig(const SdiGeneratorConfig& config);

    /**
     * @brief Enable/disable SDI generation
     */
    ShaderBundleBuilder& EnableSdiGeneration(bool enable);

    /**
     * @brief Enable central SDI registry integration
     *
     * When enabled, built shader bundles are automatically registered
     * in the central SDI_Registry.h for convenient single-include access.
     *
     * @param registry Pointer to registry manager
     * @param aliasName Optional friendly alias (defaults to program name)
     * @return Reference to this builder
     */
    ShaderBundleBuilder& EnableRegistryIntegration(
        class SdiRegistryManager* registry,
        const std::string& aliasName = ""
    );

    /**
     * @brief Set whether to validate pipeline type constraints
     */
    ShaderBundleBuilder& SetValidatePipeline(bool validate);

    // ===== Build Method =====

    /**
     * @brief Build the shader bundle
     *
     * Executes the full pipeline and returns a complete bundle.
     *
     * @return Build result with bundle or error information
     */
    BuildResult Build();

    /**
     * @brief Build from pre-compiled program
     *
     * Skips compilation and builds bundle from existing CompiledProgram.
     * Still performs reflection and SDI generation.
     *
     * @param program Pre-compiled shader program
     * @return Build result with bundle or error information
     */
    BuildResult BuildFromCompiled(const CompiledProgram& program);

    // ===== Query Methods (for async builder) =====

    /**
     * @brief Get current UUID (may be empty if not set)
     */
    std::string GetUuid() const { return uuid_; }

    /**
     * @brief Get program name
     */
    std::string GetProgramName() const { return programName_; }

    /**
     * @brief Get number of stages
     */
    size_t GetStageCount() const { return stages_.size(); }

private:
    // Configuration
    std::string programName_ = "Unnamed";
    PipelineTypeConstraint pipelineType_ = PipelineTypeConstraint::Graphics;
    std::string uuid_;
    std::vector<StageSource> stages_;
    bool validatePipeline_ = true;
    bool generateSdi_ = true;

    // Optional components
    ShaderPreprocessor* preprocessor_ = nullptr;
    ShaderCacheManager* cacheManager_ = nullptr;
    ShaderCompiler* compiler_ = nullptr;
    bool ownsCompiler_ = false;
    SdiGeneratorConfig sdiConfig_;
    class SdiRegistryManager* registryManager_ = nullptr;
    std::string registryAlias_;

    // Helper methods (public for async builder)
public:
    std::string GenerateUuid();

private:
    bool ValidatePipelineConstraints(std::string& outError);
    BuildResult PerformBuild(CompiledProgram& program);
};

} // namespace ShaderManagement
