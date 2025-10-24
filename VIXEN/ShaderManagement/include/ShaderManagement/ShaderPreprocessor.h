#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace ShaderManagement {

/**
 * @brief Preprocessor configuration
 */
struct PreprocessorConfig {
    std::vector<std::filesystem::path> includePaths;
    std::unordered_map<std::string, std::string> globalDefines;
    bool enableLineDirectives = false;  // Add #line directives for debugging
    uint32_t maxIncludeDepth = 32;      // Prevent infinite recursion
};

/**
 * @brief Preprocessor result
 */
struct PreprocessedSource {
    std::string processedSource;
    std::vector<std::filesystem::path> includedFiles;  // All files that were included
    bool success = false;
    std::string errorMessage;

    operator bool() const { return success; }
};

/**
 * @brief GLSL preprocessor (device-agnostic)
 *
 * Handles:
 * - Preprocessor defines injection
 * - #include directive resolution
 * - Circular include prevention
 * - Include path searching
 *
 * Pure string manipulation - no Vulkan objects or compilation.
 */
class ShaderPreprocessor {
public:
    explicit ShaderPreprocessor(const PreprocessorConfig& config = {});
    ~ShaderPreprocessor() = default;

    // ===== Preprocessing =====

    /**
     * @brief Preprocess GLSL source code
     * @param source Original GLSL source
     * @param defines Shader-specific defines (added to global defines)
     * @param currentFilePath Path of current file (for relative includes)
     * @return Preprocessed source with all includes resolved
     */
    PreprocessedSource Preprocess(
        const std::string& source,
        const std::unordered_map<std::string, std::string>& defines = {},
        const std::filesystem::path& currentFilePath = {}
    );

    /**
     * @brief Preprocess from file
     * @param filePath Path to GLSL file
     * @param defines Shader-specific defines
     * @return Preprocessed source
     */
    PreprocessedSource PreprocessFile(
        const std::filesystem::path& filePath,
        const std::unordered_map<std::string, std::string>& defines = {}
    );

    // ===== Configuration =====

    /**
     * @brief Add include search path
     * @param path Directory to search for #include files
     */
    void AddIncludePath(const std::filesystem::path& path);

    /**
     * @brief Set all include paths (replaces existing)
     * @param paths Include search paths
     */
    void SetIncludePaths(const std::vector<std::filesystem::path>& paths);

    /**
     * @brief Get current include paths
     */
    const std::vector<std::filesystem::path>& GetIncludePaths() const;

    /**
     * @brief Add global preprocessor define
     * @param name Define name
     * @param value Define value (empty for simple defines)
     */
    void AddGlobalDefine(const std::string& name, const std::string& value = "");

    /**
     * @brief Remove global define
     */
    void RemoveGlobalDefine(const std::string& name);

    /**
     * @brief Clear all global defines
     */
    void ClearGlobalDefines();

    /**
     * @brief Get all global defines
     */
    const std::unordered_map<std::string, std::string>& GetGlobalDefines() const;

    /**
     * @brief Enable/disable #line directives in output
     * @param enable If true, adds #line directives for better error messages
     */
    void SetLineDirectives(bool enable);

private:
    // Internal processing
    PreprocessedSource ProcessRecursive(
        const std::string& source,
        const std::filesystem::path& currentFilePath,
        const std::unordered_map<std::string, std::string>& allDefines,
        std::unordered_set<std::string>& includeGuard,
        uint32_t depth
    );

    std::string InjectDefines(
        const std::string& source,
        const std::unordered_map<std::string, std::string>& defines
    );

    std::optional<std::filesystem::path> ResolveIncludePath(
        const std::string& includeName,
        const std::filesystem::path& currentFilePath
    );

    std::string ReadFileToString(const std::filesystem::path& path);

    bool IsIncludeDirective(const std::string& line, std::string& outIncludeName);

    std::string NormalizeIncludePath(const std::filesystem::path& path);

    // Configuration
    PreprocessorConfig config;
};

/**
 * @brief Parse preprocessor defines from command-line style string
 *
 * Example: "USE_PBR=1,MAX_LIGHTS=16,ENABLE_SHADOWS" ->
 *          {"USE_PBR": "1", "MAX_LIGHTS": "16", "ENABLE_SHADOWS": ""}
 *
 * @param definesString Comma-separated defines string
 * @return Map of define name to value
 */
std::unordered_map<std::string, std::string> ParseDefinesString(
    const std::string& definesString
);

/**
 * @brief Convert defines map to command-line style string
 *
 * @param defines Map of define name to value
 * @return Comma-separated string
 */
std::string DefinesToString(
    const std::unordered_map<std::string, std::string>& defines
);

} // namespace ShaderManagement
