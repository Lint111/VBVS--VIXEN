#pragma once
#include "Headers.h"
#include "VulkanApplication.h"
#include "Logger.h"
#include <map>
#include <optional>
#include <filesystem>
#include <fstream>

const int MAX_SHADER_STAGES = 6; // Vertex, Fragment, Geometry, TessControl, TessEval, Compute

/**
 * @brief Shader compilation options
 */
struct ShaderCompileOptions {
    std::map<std::string, std::string> defines;  // Preprocessor defines
    std::string entryPoint = "main";             // Entry point function name
    bool enableOptimization = true;              // Enable SPIR-V optimization
    bool enableDebugInfo = false;                // Include debug information
    std::vector<std::string> includePaths;       // Paths for #include resolution
};

/**
 * @brief Shader stage information
 */
struct ShaderStageInfo {
    VkShaderStageFlagBits stage;
    std::string source;                          // GLSL source code
    std::vector<uint32_t> spirv;                 // Compiled SPIR-V
    std::string entryPoint = "main";
    VkShaderModule module = VK_NULL_HANDLE;
    VkSpecializationInfo* specializationInfo = nullptr;
};

/**
 * @brief Shader reflection data
 */
struct ShaderReflection {
    struct DescriptorBinding {
        uint32_t set;
        uint32_t binding;
        VkDescriptorType type;
        uint32_t count;
        std::string name;
    };

    struct PushConstantRange {
        VkShaderStageFlags stages;
        uint32_t offset;
        uint32_t size;
    };

    std::vector<DescriptorBinding> descriptorBindings;
    std::vector<PushConstantRange> pushConstants;
    std::vector<VkVertexInputAttributeDescription> inputAttributes;
};

/**
 * @brief Enhanced Vulkan Shader Manager
 *
 * Features:
 * - Support for all shader stages (vertex, fragment, geometry, tessellation, compute)
 * - Shader reflection and introspection
 * - Shader caching system
 * - Hot reloading support
 * - Preprocessor defines for shader variants
 * - Custom entry points
 * - Include file support
 * - Optimization control
 * - Specialization constants
 * - Thread-safe compilation
 * - Comprehensive error handling
 */
class VulkanShader {
public:
    VulkanShader();
    ~VulkanShader();

    // ===== Builder Pattern API =====

    /**
     * @brief Add a shader stage from source code
     * @param stage Shader stage type
     * @param source GLSL source code
     * @param entryPoint Entry point function name (default: "main")
     * @return Reference to this for chaining
     */
    VulkanShader& AddStage(VkShaderStageFlagBits stage, const std::string& source,
                          const std::string& entryPoint = "main");

    /**
     * @brief Add a shader stage from SPIR-V binary
     * @param stage Shader stage type
     * @param spirv SPIR-V binary data
     * @param entryPoint Entry point function name (default: "main")
     * @return Reference to this for chaining
     */
    VulkanShader& AddStageSPV(VkShaderStageFlagBits stage, const std::vector<uint32_t>& spirv,
                             const std::string& entryPoint = "main");

    /**
     * @brief Add a shader stage from file
     * @param stage Shader stage type
     * @param filepath Path to shader file (.glsl, .vert, .frag, .comp, .geom, .tesc, .tese, or .spv)
     * @param entryPoint Entry point function name (default: "main")
     * @return Reference to this for chaining
     */
    VulkanShader& AddStageFromFile(VkShaderStageFlagBits stage, const std::string& filepath,
                                   const std::string& entryPoint = "main");

    /**
     * @brief Set compilation options
     * @param options Compilation options (defines, optimization, etc.)
     * @return Reference to this for chaining
     */
    VulkanShader& SetCompileOptions(const ShaderCompileOptions& options);

    /**
     * @brief Add a preprocessor define
     * @param name Define name
     * @param value Define value
     * @return Reference to this for chaining
     */
    VulkanShader& AddDefine(const std::string& name, const std::string& value = "1");

    /**
     * @brief Enable shader caching
     * @param cachePath Path to cache directory
     * @return Reference to this for chaining
     */
    VulkanShader& EnableCache(const std::string& cachePath = "./shader_cache");

    /**
     * @brief Build all shader modules
     * @return true if successful, false otherwise
     */
    bool Build();

    // ===== Legacy API (for backwards compatibility) =====

    /**
     * @brief Build shader modules from pre-compiled SPIR-V (vertex + fragment only)
     * @deprecated Use builder API instead
     */
    void BuildShaderModuleWithSPV(uint32_t* vertShaderText, size_t vertexSPVSize,
                                  uint32_t* fragShaderText, size_t fragSPVSize);

#ifdef AUTO_COMPILE_GLSL_TO_SPV
    /**
     * @brief Build shader from GLSL source (vertex + fragment only)
     * @deprecated Use builder API instead
     */
    void BuildShader(const char* vertShaderText, const char* fragShaderText);
#endif

    // ===== Shader Management =====

    /**
     * @brief Destroy all shader modules
     */
    void DestroyShader();

    /**
     * @brief Hot reload shaders from source files
     * @return true if reload successful, false otherwise
     */
    bool HotReload();

    /**
     * @brief Check if any source files have been modified
     * @return true if files have changed, false otherwise
     */
    bool HasSourceChanged() const;

    // ===== Shader Reflection =====

    /**
     * @brief Get shader reflection data
     * @return Reflection data structure
     */
    const ShaderReflection& GetReflection() const { return reflection; }

    /**
     * @brief Reflect shader to extract descriptor bindings, push constants, etc.
     * @return true if reflection successful, false otherwise
     */
    bool ReflectShader();

    // ===== Accessors =====

    /**
     * @brief Get shader stage create infos for pipeline creation
     * @return Array of shader stage create infos
     */
    const VkPipelineShaderStageCreateInfo* GetStages() const { return shaderStages; }

    /**
     * @brief Get number of shader stages
     * @return Stage count
     */
    uint32_t GetStageCount() const { return stagesCount; }

    /**
     * @brief Check if shader is initialized
     * @return true if initialized, false otherwise
     */
    bool IsInitialized() const { return initialized; }

    /**
     * @brief Set logger for this shader
     * @param logger Shared pointer to logger instance
     */
    void SetLogger(std::shared_ptr<Logger> logger) { this->logger = logger; }

private:
    // ===== Internal Methods =====

#ifdef AUTO_COMPILE_GLSL_TO_SPV
    /**
     * @brief Convert GLSL to SPIR-V
     * @param stage Shader stage type
     * @param source GLSL source code
     * @param spirv Output SPIR-V binary
     * @param options Compilation options
     * @return true if successful, false otherwise
     */
    bool GLSLtoSPV(VkShaderStageFlagBits stage, const std::string& source,
                   std::vector<uint32_t>& spirv, const ShaderCompileOptions& options);

    /**
     * @brief Preprocess GLSL source (handle #include, #define)
     * @param source Original source
     * @param options Compilation options
     * @return Preprocessed source
     */
    std::string PreprocessSource(const std::string& source, const ShaderCompileOptions& options);

    /**
     * @brief Resolve #include directives
     * @param source Source code
     * @param currentPath Current file path for relative includes
     * @param includePaths Search paths for includes
     * @param includeGuard Set of already included files (prevent circular includes)
     * @return Source with includes resolved
     */
    std::string ResolveIncludes(const std::string& source, const std::string& currentPath,
                               const std::vector<std::string>& includePaths,
                               std::set<std::string>& includeGuard);

    EShLanguage GetLanguage(VkShaderStageFlagBits shaderType);
    void InitializeResources(TBuiltInResource& Resources);
#endif

    /**
     * @brief Create shader module from SPIR-V
     * @param spirv SPIR-V binary
     * @param module Output shader module
     * @return true if successful, false otherwise
     */
    bool CreateShaderModule(const std::vector<uint32_t>& spirv, VkShaderModule& module);

    /**
     * @brief Load SPIR-V from cache
     * @param cacheKey Cache key (hash of source + options)
     * @param spirv Output SPIR-V binary
     * @return true if found in cache, false otherwise
     */
    bool LoadFromCache(const std::string& cacheKey, std::vector<uint32_t>& spirv);

    /**
     * @brief Save SPIR-V to cache
     * @param cacheKey Cache key
     * @param spirv SPIR-V binary to save
     */
    void SaveToCache(const std::string& cacheKey, const std::vector<uint32_t>& spirv);

    /**
     * @brief Generate cache key from source and options
     * @param source Source code
     * @param stage Shader stage
     * @param options Compile options
     * @return Cache key string
     */
    std::string GenerateCacheKey(const std::string& source, VkShaderStageFlagBits stage,
                                const ShaderCompileOptions& options);

    /**
     * @brief Get file extension for shader stage
     * @param stage Shader stage
     * @return File extension string
     */
    std::string GetStageExtension(VkShaderStageFlagBits stage) const;

    // ===== Member Variables =====

    VkPipelineShaderStageCreateInfo shaderStages[MAX_SHADER_STAGES];
    std::vector<ShaderStageInfo> stages;
    ShaderReflection reflection;
    ShaderCompileOptions compileOptions;

    bool initialized = false;
    uint32_t stagesCount = 0;

    // Caching
    bool cachingEnabled = false;
    std::string cachePath;

    // Hot reloading
    std::map<VkShaderStageFlagBits, std::string> stageFilePaths;
    std::map<std::string, std::filesystem::file_time_type> fileModTimes;

    // Thread safety
    mutable std::mutex shaderMutex;

    // Logging
    std::shared_ptr<Logger> logger;
};