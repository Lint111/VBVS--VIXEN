#pragma once

#include "ShaderProgram.h"
#include "ShaderSwapPolicy.h"
#include "ILoggable.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <optional>
#include <mutex>

namespace ShaderManagement {

// Forward declarations
class BackgroundCompiler;

/**
 * @brief Main shader library manager (device-agnostic)
 *
 * Manages shader program definitions, compilation, and hot reload.
 * Does NOT create VkShaderModule - only handles SPIRV bytecode.
 * Graph side (ShaderLibraryNode) converts SPIRV to Vulkan objects.
 *
 * Thread-safe: All public methods can be called from any thread.
 *
 * Logging: Inherits from ILoggable for external logging control.
 * Use SetLoggerEnabled() and SetLoggerTerminalOutput() to configure logging.
 */
class ShaderLibrary : public ILoggable {
public:
    ShaderLibrary();
    ~ShaderLibrary();

    // Disable copy/move (manages background threads)
    ShaderLibrary(const ShaderLibrary&) = delete;
    ShaderLibrary& operator=(const ShaderLibrary&) = delete;

    // ===== Program Registration =====

    /**
     * @brief Register a new shader program
     * @param definition Program definition (stages, pipeline type)
     * @return Program ID for future reference
     * @throws std::runtime_error if program is invalid
     */
    uint32_t RegisterProgram(const ShaderProgramDefinition& definition);

    /**
     * @brief Update existing program definition
     * @param programId Program to update
     * @param definition New definition (triggers recompilation)
     * @throws std::runtime_error if program not found or invalid
     */
    void UpdateProgram(uint32_t programId, const ShaderProgramDefinition& definition);

    /**
     * @brief Remove program from library
     * @param programId Program to remove
     */
    void RemoveProgram(uint32_t programId);

    /**
     * @brief Check if program exists
     */
    bool HasProgram(uint32_t programId) const;

    /**
     * @brief Get program definition (editable)
     */
    ShaderProgramDefinition* GetProgramDefinition(uint32_t programId);
    const ShaderProgramDefinition* GetProgramDefinition(uint32_t programId) const;

    // ===== Compilation =====

    /**
     * @brief Compile program synchronously (blocking)
     * @param programId Program to compile
     * @return Compilation result
     */
    CompilationResult CompileProgram(uint32_t programId);

    /**
     * @brief Compile program asynchronously (background thread)
     * @param programId Program to compile
     * @param policy When to swap to compiled version
     * @return Job ID for tracking
     */
    uint32_t CompileProgramAsync(uint32_t programId, ShaderSwapPolicy policy);

    /**
     * @brief Compile all registered programs synchronously
     * @return Number of programs compiled successfully
     */
    uint32_t CompileAllPrograms();

    /**
     * @brief Get compilation status
     */
    CompilationStatus GetCompilationStatus(uint32_t programId) const;

    /**
     * @brief Check if program is currently compiling
     */
    bool IsCompiling(uint32_t programId) const;

    // ===== Compiled Programs =====

    /**
     * @brief Get compiled program (SPIRV bytecode, not VkShaderModule)
     * @return nullptr if not compiled or compilation failed
     */
    const CompiledProgram* GetCompiledProgram(uint32_t programId) const;

    /**
     * @brief Get all compiled programs
     */
    std::vector<const CompiledProgram*> GetAllCompiledPrograms() const;

    /**
     * @brief Get program by name (for debugging)
     */
    const CompiledProgram* GetCompiledProgramByName(const std::string& name) const;

    // ===== Hot Reload =====

    /**
     * @brief Reload program (recompile from disk)
     * @param programId Program to reload
     * @param policy When to swap to recompiled version
     */
    void ReloadProgram(uint32_t programId, ShaderSwapPolicy policy = ShaderSwapPolicy::NextFrame);

    /**
     * @brief Get programs pending swap for given policy
     * @param policy Swap policy to check
     * @return Program IDs ready to swap
     */
    std::vector<uint32_t> GetPendingSwaps(ShaderSwapPolicy policy) const;

    /**
     * @brief Mark pending swaps as completed (clear from queue)
     * @param programIds Programs that have been swapped
     */
    void ConfirmSwaps(const std::vector<uint32_t>& programIds);

    /**
     * @brief Manually swap to newly compiled program
     * @param programId Program to swap
     * @return true if swap occurred, false if no new version available
     */
    bool SwapProgram(uint32_t programId);

    /**
     * @brief Notify library of application state change
     * @param newState New application state (for OnStateChange policy)
     */
    void NotifyStateChange(ApplicationState newState);

    // ===== File Watching =====

    /**
     * @brief Enable/disable file watching for automatic hot reload
     * @param enable If true, monitors shader files for changes
     */
    void EnableFileWatching(bool enable = true);

    /**
     * @brief Check if file watching is enabled
     */
    bool IsFileWatchingEnabled() const;

    /**
     * @brief Poll filesystem for changes to shader files
     * @return Program IDs with changed files (marked for recompilation)
     */
    std::vector<uint32_t> CheckForFileChanges();

    // ===== Background Compilation Control =====

    /**
     * @brief Process completed compilation jobs
     * @return Number of jobs processed
     */
    uint32_t ProcessCompletedJobs();

    /**
     * @brief Cancel pending compilation job
     * @param programId Program whose compilation to cancel
     */
    void CancelCompilation(uint32_t programId);

    /**
     * @brief Wait for all pending compilations to complete
     */
    void WaitForAllCompilations();

    /**
     * @brief Shutdown background compiler thread
     */
    void Shutdown();

    // ===== Statistics =====

    /**
     * @brief Get total number of registered programs
     */
    size_t GetProgramCount() const;

    /**
     * @brief Get number of compiled programs
     */
    size_t GetCompiledProgramCount() const;

    /**
     * @brief Get number of programs currently compiling
     */
    size_t GetCompilingProgramCount() const;

private:
    // Internal helpers
    uint32_t AllocateProgramId();
    void ValidateProgramDefinition(const ShaderProgramDefinition& def) const;
    void UpdateFileTimestamps(uint32_t programId);
    bool HasFileChanged(const ShaderStageDefinition& stage) const;
    CompilationResult CompileProgramInternal(const ShaderProgramDefinition& def);
    std::vector<uint32_t> LoadSpirvFile(const std::filesystem::path& path);

    // Storage
    std::unordered_map<uint32_t, ShaderProgramDefinition> definitions;
    std::unordered_map<uint32_t, CompiledProgram> compiledPrograms;
    std::unordered_map<uint32_t, CompilationStatus> compilationStatus;
    std::unordered_map<std::string, uint32_t> nameToId;

    // Pending swap tracking
    std::vector<ShaderSwapRequest> pendingSwaps;

    // Background compilation
    std::unique_ptr<BackgroundCompiler> backgroundCompiler;
    std::unordered_map<uint32_t, uint32_t> programToJobId;  // programId -> jobId

    // File watching
    bool fileWatchingEnabled = false;
    ApplicationState currentState = ApplicationState::Editing;

    // Thread safety
    mutable std::mutex libraryMutex;

    // ID allocation
    uint32_t nextProgramId = 0;
};

} // namespace ShaderManagement