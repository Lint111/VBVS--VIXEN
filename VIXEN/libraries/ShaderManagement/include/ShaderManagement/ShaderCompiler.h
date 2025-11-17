#pragma once

#include "ShaderStage.h"
#include <vector>
#include <string>
#include <cstdint>
#include <chrono>
#include <optional>
#include <filesystem>

namespace ShaderManagement {

/**
 * @brief Shader compilation options
 */
struct CompilationOptions {
    bool optimizePerformance = true;    // Enable SPIR-V optimization
    bool optimizeSize = false;          // Optimize for size instead of performance
    bool generateDebugInfo = false;     // Include debug symbols
    bool treatWarningsAsErrors = false;
    int targetVulkanVersion = 130;      // 110, 111, 120, 130 (Vulkan 1.x.0)
    int targetSpirvVersion = 160;       // 100, 110, 120, 130, 140, 150, 160 (SPIR-V 1.x) - SPIR-V 1.6 for Vulkan 1.3

    // Validation
    bool validateSpirv = false;         // Run SPIR-V validator after compilation (can enable for debugging, but has known issues with glslang-generated SPIR-V)
};

/**
 * @brief Compilation result
 */
struct CompilationOutput {
    bool success = false;
    std::vector<uint32_t> spirv;        // Compiled SPIR-V bytecode
    std::string infoLog;                // Info/warning messages
    std::string errorLog;               // Error messages
    std::chrono::milliseconds compilationTime{0};

    operator bool() const { return success; }

    std::string GetFullLog() const {
        std::string fullLog;
        if (!infoLog.empty()) {
            fullLog += "Info:\n" + infoLog + "\n";
        }
        if (!errorLog.empty()) {
            fullLog += "Errors:\n" + errorLog + "\n";
        }
        return fullLog;
    }
};

/**
 * @brief GLSL to SPIR-V compiler (device-agnostic)
 *
 * Wraps glslang compiler with sensible defaults for Vulkan.
 * Thread-safe: Can compile shaders from multiple threads.
 *
 * Design:
 * - Uses glslang for GLSL -> SPIR-V compilation
 * - Stateless operation (thread-safe)
 * - Returns SPIR-V bytecode only (no VkShaderModule creation)
 * - Proper error reporting with line numbers
 */
class ShaderCompiler {
public:
    ShaderCompiler();
    ~ShaderCompiler();

    // Disable copy (manages glslang state)
    ShaderCompiler(const ShaderCompiler&) = delete;
    ShaderCompiler& operator=(const ShaderCompiler&) = delete;

    // ===== Compilation =====

    /**
     * @brief Compile GLSL source to SPIR-V
     * @param stage Shader stage
     * @param source Preprocessed GLSL source code
     * @param entryPoint Entry point function name
     * @param options Compilation options
     * @return Compilation result with SPIR-V bytecode
     */
    CompilationOutput Compile(
        ShaderStage stage,
        const std::string& source,
        const std::string& entryPoint = "main",
        const CompilationOptions& options = {}
    );

    /**
     * @brief Compile from file
     * @param stage Shader stage
     * @param filePath Path to GLSL file
     * @param entryPoint Entry point function name
     * @param options Compilation options
     * @return Compilation result
     */
    CompilationOutput CompileFile(
        ShaderStage stage,
        const std::filesystem::path& filePath,
        const std::string& entryPoint = "main",
        const CompilationOptions& options = {}
    );

    /**
     * @brief Load pre-compiled SPIR-V from file
     * @param filePath Path to .spv file
     * @param validate If true, validates SPIR-V bytecode
     * @return Compilation result with loaded SPIR-V
     */
    CompilationOutput LoadSpirv(
        const std::filesystem::path& filePath,
        bool validate = true
    );

    // ===== Validation =====

    /**
     * @brief Validate SPIR-V bytecode
     * @param spirv SPIR-V bytecode to validate
     * @return true if valid, false otherwise
     */
    bool ValidateSpirv(const std::vector<uint32_t>& spirv, std::string& outError);

    /**
     * @brief Disassemble SPIR-V to human-readable text
     * @param spirv SPIR-V bytecode
     * @return Disassembled text (SPIR-V assembly)
     */
    std::string DisassembleSpirv(const std::vector<uint32_t>& spirv);

    // ===== Utility =====

    /**
     * @brief Check if glslang is available
     * @return true if compiler can be used
     */
    static bool IsAvailable();

    /**
     * @brief Get glslang version string
     */
    static std::string GetVersion();

private:
    // Internal helpers
    void InitializeGlslang();
    void FinalizeGlslang();
    CompilationOutput CompileInternal(
        ShaderStage stage,
        const std::string& source,
        const std::string& sourceName,
        const std::string& entryPoint,
        const CompilationOptions& options
    );

    void SetupShaderResources(void* resources);  // TBuiltInResource*
    void* GetShaderLanguage(ShaderStage stage);  // EShLanguage
    void* CreateShader(void* language);          // glslang::TShader*

    // State
    bool initialized = false;
};

/**
 * @brief Get file extension for shader stage
 * @param stage Shader stage
 * @return Extension string (e.g., "vert", "frag", "comp")
 */
inline const char* GetShaderStageExtension(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:       return "vert";
        case ShaderStage::Fragment:     return "frag";
        case ShaderStage::Compute:      return "comp";
        case ShaderStage::Geometry:     return "geom";
        case ShaderStage::TessControl:  return "tesc";
        case ShaderStage::TessEval:     return "tese";
        case ShaderStage::Mesh:         return "mesh";
        case ShaderStage::Task:         return "task";
        case ShaderStage::RayGen:       return "rgen";
        case ShaderStage::Miss:         return "rmiss";
        case ShaderStage::ClosestHit:   return "rchit";
        case ShaderStage::AnyHit:       return "rahit";
        case ShaderStage::Intersection: return "rint";
        case ShaderStage::Callable:     return "rcall";
        default:                        return "unknown";
    }
}

/**
 * @brief Infer shader stage from file extension
 * @param path File path
 * @return Shader stage or nullopt if unknown
 */
std::optional<ShaderStage> InferStageFromPath(const std::filesystem::path& path);

} // namespace ShaderManagement
