#include "ShaderManagement/ShaderLibrary.h"
#include <fstream>
#include <stdexcept>
#include <algorithm>

namespace ShaderManagement {

// Placeholder for BackgroundCompiler (will implement separately)
class BackgroundCompiler {
public:
    BackgroundCompiler() = default;
    ~BackgroundCompiler() = default;

    uint32_t SubmitJob(uint32_t programId, const ShaderProgramDefinition& def, ShaderSwapPolicy policy) {
        // TODO: Implement background compilation
        return 0;
    }

    bool IsJobComplete(uint32_t jobId) const { return false; }
    std::optional<CompilationResult> GetJobResult(uint32_t jobId) { return std::nullopt; }
    void CancelJob(uint32_t jobId) {}
    void WaitForAll() {}
    void Shutdown() {}
};

// ===== ShaderLibrary Implementation =====

ShaderLibrary::ShaderLibrary()
    : backgroundCompiler(std::make_unique<BackgroundCompiler>())
{
}

ShaderLibrary::~ShaderLibrary() {
    Shutdown();
}

uint32_t ShaderLibrary::RegisterProgram(const ShaderProgramDefinition& definition) {
    std::lock_guard<std::mutex> lock(libraryMutex);

    // Validate definition
    ValidateProgramDefinition(definition);

    // Allocate ID
    uint32_t programId = AllocateProgramId();

    // Store definition
    ShaderProgramDefinition def = definition;
    def.programId = programId;
    definitions[programId] = def;

    // Map name to ID
    if (!def.name.empty()) {
        nameToId[def.name] = programId;
    }

    // Initialize status
    compilationStatus[programId] = CompilationStatus::NotCompiled;

    // Update file timestamps for watching
    UpdateFileTimestamps(programId);

    return programId;
}

void ShaderLibrary::UpdateProgram(uint32_t programId, const ShaderProgramDefinition& definition) {
    std::lock_guard<std::mutex> lock(libraryMutex);

    if (!HasProgram(programId)) {
        throw std::runtime_error("Program ID " + std::to_string(programId) + " not found");
    }

    ValidateProgramDefinition(definition);

    // Update definition
    ShaderProgramDefinition def = definition;
    def.programId = programId;
    definitions[programId] = def;

    // Update name mapping
    if (!def.name.empty()) {
        nameToId[def.name] = programId;
    }

    // Mark for recompilation
    compilationStatus[programId] = CompilationStatus::NotCompiled;

    // Update file timestamps
    UpdateFileTimestamps(programId);
}

void ShaderLibrary::RemoveProgram(uint32_t programId) {
    std::lock_guard<std::mutex> lock(libraryMutex);

    // Remove from name mapping
    auto defIt = definitions.find(programId);
    if (defIt != definitions.end() && !defIt->second.name.empty()) {
        nameToId.erase(defIt->second.name);
    }

    // Cancel any pending compilation
    CancelCompilation(programId);

    // Remove all data
    definitions.erase(programId);
    compiledPrograms.erase(programId);
    compilationStatus.erase(programId);
    programToJobId.erase(programId);

    // Remove from pending swaps
    pendingSwaps.erase(
        std::remove_if(pendingSwaps.begin(), pendingSwaps.end(),
            [programId](const auto& req) { return req.programId == programId; }),
        pendingSwaps.end()
    );
}

bool ShaderLibrary::HasProgram(uint32_t programId) const {
    std::lock_guard<std::mutex> lock(libraryMutex);
    return definitions.find(programId) != definitions.end();
}

ShaderProgramDefinition* ShaderLibrary::GetProgramDefinition(uint32_t programId) {
    std::lock_guard<std::mutex> lock(libraryMutex);
    auto it = definitions.find(programId);
    return (it != definitions.end()) ? &it->second : nullptr;
}

const ShaderProgramDefinition* ShaderLibrary::GetProgramDefinition(uint32_t programId) const {
    std::lock_guard<std::mutex> lock(libraryMutex);
    auto it = definitions.find(programId);
    return (it != definitions.end()) ? &it->second : nullptr;
}

CompilationResult ShaderLibrary::CompileProgram(uint32_t programId) {
    std::lock_guard<std::mutex> lock(libraryMutex);

    auto defIt = definitions.find(programId);
    if (defIt == definitions.end()) {
        CompilationResult result;
        result.programId = programId;
        result.status = CompilationStatus::Failed;
        result.errorMessage = "Program ID not found";
        return result;
    }

    compilationStatus[programId] = CompilationStatus::Compiling;

    CompilationResult result = CompileProgramInternal(defIt->second);

    if (result.status == CompilationStatus::Completed) {
        compiledPrograms[programId] = result.program;
        compilationStatus[programId] = CompilationStatus::Completed;
    } else {
        compilationStatus[programId] = CompilationStatus::Failed;
    }

    return result;
}

uint32_t ShaderLibrary::CompileProgramAsync(uint32_t programId, ShaderSwapPolicy policy) {
    std::lock_guard<std::mutex> lock(libraryMutex);

    auto defIt = definitions.find(programId);
    if (defIt == definitions.end()) {
        throw std::runtime_error("Program ID " + std::to_string(programId) + " not found");
    }

    compilationStatus[programId] = CompilationStatus::Pending;

    // Submit to background compiler
    uint32_t jobId = backgroundCompiler->SubmitJob(programId, defIt->second, policy);
    programToJobId[programId] = jobId;

    // Add to pending swaps
    ShaderSwapRequest request;
    request.programId = programId;
    request.policy = policy;
    request.isReady = false;
    pendingSwaps.push_back(request);

    return jobId;
}

uint32_t ShaderLibrary::CompileAllPrograms() {
    std::lock_guard<std::mutex> lock(libraryMutex);

    uint32_t successCount = 0;
    for (const auto& [programId, def] : definitions) {
        compilationStatus[programId] = CompilationStatus::Compiling;

        CompilationResult result = CompileProgramInternal(def);

        if (result.status == CompilationStatus::Completed) {
            compiledPrograms[programId] = result.program;
            compilationStatus[programId] = CompilationStatus::Completed;
            successCount++;
        } else {
            compilationStatus[programId] = CompilationStatus::Failed;
        }
    }

    return successCount;
}

CompilationStatus ShaderLibrary::GetCompilationStatus(uint32_t programId) const {
    std::lock_guard<std::mutex> lock(libraryMutex);
    auto it = compilationStatus.find(programId);
    return (it != compilationStatus.end()) ? it->second : CompilationStatus::NotCompiled;
}

bool ShaderLibrary::IsCompiling(uint32_t programId) const {
    CompilationStatus status = GetCompilationStatus(programId);
    return status == CompilationStatus::Pending || status == CompilationStatus::Compiling;
}

const CompiledProgram* ShaderLibrary::GetCompiledProgram(uint32_t programId) const {
    std::lock_guard<std::mutex> lock(libraryMutex);
    auto it = compiledPrograms.find(programId);
    return (it != compiledPrograms.end()) ? &it->second : nullptr;
}

std::vector<const CompiledProgram*> ShaderLibrary::GetAllCompiledPrograms() const {
    std::lock_guard<std::mutex> lock(libraryMutex);

    std::vector<const CompiledProgram*> programs;
    programs.reserve(compiledPrograms.size());

    for (const auto& [id, program] : compiledPrograms) {
        programs.push_back(&program);
    }

    return programs;
}

const CompiledProgram* ShaderLibrary::GetCompiledProgramByName(const std::string& name) const {
    std::lock_guard<std::mutex> lock(libraryMutex);

    auto idIt = nameToId.find(name);
    if (idIt == nameToId.end()) {
        return nullptr;
    }

    return GetCompiledProgram(idIt->second);
}

void ShaderLibrary::ReloadProgram(uint32_t programId, ShaderSwapPolicy policy) {
    CompileProgramAsync(programId, policy);
}

std::vector<uint32_t> ShaderLibrary::GetPendingSwaps(ShaderSwapPolicy policy) const {
    std::lock_guard<std::mutex> lock(libraryMutex);

    std::vector<uint32_t> programIds;
    for (const auto& request : pendingSwaps) {
        if (request.policy == policy && request.isReady) {
            programIds.push_back(request.programId);
        }
    }

    return programIds;
}

void ShaderLibrary::ConfirmSwaps(const std::vector<uint32_t>& programIds) {
    std::lock_guard<std::mutex> lock(libraryMutex);

    for (uint32_t programId : programIds) {
        pendingSwaps.erase(
            std::remove_if(pendingSwaps.begin(), pendingSwaps.end(),
                [programId](const auto& req) { return req.programId == programId; }),
            pendingSwaps.end()
        );
    }
}

bool ShaderLibrary::SwapProgram(uint32_t programId) {
    std::lock_guard<std::mutex> lock(libraryMutex);

    // Find pending swap for this program
    auto it = std::find_if(pendingSwaps.begin(), pendingSwaps.end(),
        [programId](const auto& req) { return req.programId == programId; });

    if (it == pendingSwaps.end() || !it->isReady) {
        return false;
    }

    // Remove from pending
    pendingSwaps.erase(it);
    return true;
}

void ShaderLibrary::NotifyStateChange(ApplicationState newState) {
    std::lock_guard<std::mutex> lock(libraryMutex);

    currentState = newState;

    // Mark OnStateChange swaps as ready
    for (auto& request : pendingSwaps) {
        if (request.policy == ShaderSwapPolicy::OnStateChange) {
            request.canSwapNow = true;
        }
    }
}

void ShaderLibrary::EnableFileWatching(bool enable) {
    std::lock_guard<std::mutex> lock(libraryMutex);
    fileWatchingEnabled = enable;
}

bool ShaderLibrary::IsFileWatchingEnabled() const {
    std::lock_guard<std::mutex> lock(libraryMutex);
    return fileWatchingEnabled;
}

std::vector<uint32_t> ShaderLibrary::CheckForFileChanges() {
    std::lock_guard<std::mutex> lock(libraryMutex);

    if (!fileWatchingEnabled) {
        return {};
    }

    std::vector<uint32_t> changedPrograms;

    for (auto& [programId, def] : definitions) {
        bool hasChanges = false;

        for (auto& stage : def.stages) {
            if (HasFileChanged(stage)) {
                hasChanges = true;
                stage.needsRecompile = true;
                stage.lastModified = std::filesystem::last_write_time(stage.spirvPath);
            }
        }

        if (hasChanges) {
            changedPrograms.push_back(programId);
            // Trigger async recompilation
            CompileProgramAsync(programId, ShaderSwapPolicy::NextFrame);
        }
    }

    return changedPrograms;
}

uint32_t ShaderLibrary::ProcessCompletedJobs() {
    std::lock_guard<std::mutex> lock(libraryMutex);

    uint32_t processedCount = 0;

    // Check all active compilation jobs
    std::vector<uint32_t> completedPrograms;
    for (const auto& [programId, jobId] : programToJobId) {
        if (backgroundCompiler->IsJobComplete(jobId)) {
            auto result = backgroundCompiler->GetJobResult(jobId);
            if (result) {
                if (result->status == CompilationStatus::Completed) {
                    compiledPrograms[programId] = result->program;
                    compilationStatus[programId] = CompilationStatus::Completed;

                    // Mark swap as ready
                    for (auto& request : pendingSwaps) {
                        if (request.programId == programId) {
                            request.isReady = true;
                            break;
                        }
                    }
                } else {
                    compilationStatus[programId] = CompilationStatus::Failed;
                }

                completedPrograms.push_back(programId);
                processedCount++;
            }
        }
    }

    // Clean up completed jobs
    for (uint32_t programId : completedPrograms) {
        programToJobId.erase(programId);
    }

    return processedCount;
}

void ShaderLibrary::CancelCompilation(uint32_t programId) {
    std::lock_guard<std::mutex> lock(libraryMutex);

    auto it = programToJobId.find(programId);
    if (it != programToJobId.end()) {
        backgroundCompiler->CancelJob(it->second);
        programToJobId.erase(it);
    }

    compilationStatus[programId] = CompilationStatus::NotCompiled;
}

void ShaderLibrary::WaitForAllCompilations() {
    backgroundCompiler->WaitForAll();
}

void ShaderLibrary::Shutdown() {
    if (backgroundCompiler) {
        backgroundCompiler->Shutdown();
    }
}

size_t ShaderLibrary::GetProgramCount() const {
    std::lock_guard<std::mutex> lock(libraryMutex);
    return definitions.size();
}

size_t ShaderLibrary::GetCompiledProgramCount() const {
    std::lock_guard<std::mutex> lock(libraryMutex);
    return compiledPrograms.size();
}

size_t ShaderLibrary::GetCompilingProgramCount() const {
    std::lock_guard<std::mutex> lock(libraryMutex);
    return programToJobId.size();
}

// ===== Private Helpers =====

uint32_t ShaderLibrary::AllocateProgramId() {
    return nextProgramId++;
}

void ShaderLibrary::ValidateProgramDefinition(const ShaderProgramDefinition& def) const {
    if (def.stages.empty()) {
        throw std::runtime_error("Shader program must have at least one stage");
    }

    if (!def.IsValid()) {
        throw std::runtime_error("Invalid shader program: stages don't match pipeline type constraints");
    }

    // Check all files exist
    for (const auto& stage : def.stages) {
        if (!std::filesystem::exists(stage.spirvPath)) {
            throw std::runtime_error("Shader file not found: " + stage.spirvPath.string());
        }
    }
}

void ShaderLibrary::UpdateFileTimestamps(uint32_t programId) {
    auto defIt = definitions.find(programId);
    if (defIt == definitions.end()) return;

    for (auto& stage : defIt->second.stages) {
        if (std::filesystem::exists(stage.spirvPath)) {
            stage.lastModified = std::filesystem::last_write_time(stage.spirvPath);
        }
    }
}

bool ShaderLibrary::HasFileChanged(const ShaderStageDefinition& stage) const {
    if (!std::filesystem::exists(stage.spirvPath)) {
        return false;
    }

    auto currentModified = std::filesystem::last_write_time(stage.spirvPath);
    return currentModified > stage.lastModified;
}

CompilationResult ShaderLibrary::CompileProgramInternal(const ShaderProgramDefinition& def) {
    CompilationResult result;
    result.programId = def.programId;
    result.status = CompilationStatus::Compiling;

    auto startTime = std::chrono::steady_clock::now();

    try {
        CompiledProgram program;
        program.programId = def.programId;
        program.name = def.name;
        program.pipelineType = def.pipelineType;
        program.generation = 0;

        // Load SPIRV for each stage
        for (const auto& stageDef : def.stages) {
            CompiledShaderStage compiledStage;
            compiledStage.stage = stageDef.stage;
            compiledStage.spirvCode = LoadSpirvFile(stageDef.spirvPath);
            compiledStage.entryPoint = stageDef.entryPoint;
            compiledStage.generation = 0;

            // Copy specialization constants
            for (const auto& [id, value] : stageDef.specializationConstants) {
                compiledStage.specializationConstantIds.push_back(id);
                compiledStage.specializationConstantValues.push_back(value);
            }

            program.stages.push_back(compiledStage);
        }

        program.compiledAt = std::chrono::steady_clock::now();

        auto endTime = std::chrono::steady_clock::now();

        result.status = CompilationStatus::Completed;
        result.program = program;
        result.compilationTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    } catch (const std::exception& e) {
        result.status = CompilationStatus::Failed;
        result.errorMessage = e.what();

        auto endTime = std::chrono::steady_clock::now();
        result.compilationTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    }

    return result;
}

std::vector<uint32_t> ShaderLibrary::LoadSpirvFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open SPIRV file: " + path.string());
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    if (fileSize % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Invalid SPIRV file size (not multiple of 4): " + path.string());
    }

    std::vector<uint32_t> spirv(fileSize / sizeof(uint32_t));

    file.seekg(0);
    file.read(reinterpret_cast<char*>(spirv.data()), fileSize);

    if (!file) {
        throw std::runtime_error("Failed to read SPIRV file: " + path.string());
    }

    // Basic SPIRV validation (magic number)
    if (spirv.empty() || spirv[0] != 0x07230203) {
        throw std::runtime_error("Invalid SPIRV file (bad magic number): " + path.string());
    }

    return spirv;
}

} // namespace ShaderManagement