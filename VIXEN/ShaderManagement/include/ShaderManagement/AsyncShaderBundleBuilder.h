#pragma once

#include "ShaderBundleBuilder.h"
#include "ShaderEvents.h"
#include <EventBus/MessageBus.h>
#include <EventBus/WorkerThreadBridge.h>
#include <thread>
#include <future>
#include <unordered_map>
#include <atomic>

namespace ShaderManagement {

/**
 * @brief Asynchronous shader bundle builder
 *
 * Performs heavy work (compilation, I/O, SDI generation) on worker threads
 * and publishes progress/completion events through EventBus for safe main
 * thread integration.
 *
 * Benefits:
 * - Non-blocking shader compilation
 * - Responsive main thread during heavy I/O
 * - Progress tracking via events
 * - Hot-reload without stuttering
 * - Multiple shaders compile in parallel
 * - Safe EventBus integration for main thread callbacks
 *
 * Architecture:
 * ```
 * Main Thread                Worker Thread
 * ───────────                ─────────────
 * BuildAsync()
 *    ↓
 * Publish: CompilationStarted
 *    ↓
 * [Work Queue] ────────────→ Preprocess
 *                            Compile SPIRV
 *                            Reflect metadata
 *                            Generate SDI
 *                                 ↓
 * EventBus ←──────────────── Publish: CompilationCompleted
 *    ↓
 * ProcessMessages()
 *    ↓
 * Subscriber receives bundle
 * ```
 *
 * Usage:
 * @code
 * Vixen::EventBus::MessageBus bus;
 * AsyncShaderBundleBuilder asyncBuilder(&bus);
 *
 * // Subscribe to completion
 * bus.Subscribe(ShaderCompilationCompletedMessage::TYPE, [](const Message& msg) {
 *     auto& completed = static_cast<const ShaderCompilationCompletedMessage&>(msg);
 *     UpdateShaderLibrary(completed.bundle);
 *     return true;
 * });
 *
 * // Start async build (non-blocking!)
 * std::string uuid = asyncBuilder.BuildAsync()
 *     .SetProgramName("MyShader")
 *     .AddStageFromFile(ShaderStage::Vertex, "shader.vert")
 *     .AddStageFromFile(ShaderStage::Fragment, "shader.frag")
 *     .Submit();
 *
 * // Main thread continues running...
 * // When compilation finishes, event arrives on next ProcessMessages()
 * @endcode
 */
class AsyncShaderBundleBuilder {
public:
    /**
     * @brief Async build handle
     *
     * Tracks an in-progress async build operation.
     */
    struct AsyncBuildHandle {
        std::string uuid;
        std::future<void> future;
        std::atomic<bool> completed;
        std::atomic<bool> cancelled;

        AsyncBuildHandle(std::string id)
            : uuid(std::move(id))
            , completed(false)
            , cancelled(false) {}
    };

    /**
     * @brief Async builder configurator (fluent interface)
     *
     * Configures async build and submits to worker thread.
     */
    class AsyncConfigurator {
    public:
        AsyncConfigurator(AsyncShaderBundleBuilder* parent, Vixen::EventBus::SenderID sender)
            : parent_(parent)
            , senderID_(sender) {}

        // Inherit all ShaderBundleBuilder configuration methods
        AsyncConfigurator& SetProgramName(const std::string& name) {
            builder_.SetProgramName(name);
            return *this;
        }

        AsyncConfigurator& SetPipelineType(PipelineTypeConstraint type) {
            builder_.SetPipelineType(type);
            return *this;
        }

        AsyncConfigurator& SetUuid(const std::string& uuid) {
            builder_.SetUuid(uuid);
            return *this;
        }

        AsyncConfigurator& AddStage(
            ShaderStage stage,
            const std::string& source,
            const std::string& entryPoint = "main",
            const CompilationOptions& options = {}
        ) {
            builder_.AddStage(stage, source, entryPoint, options);
            return *this;
        }

        AsyncConfigurator& AddStageFromFile(
            ShaderStage stage,
            const std::filesystem::path& sourcePath,
            const std::string& entryPoint = "main",
            const CompilationOptions& options = {}
        ) {
            builder_.AddStageFromFile(stage, sourcePath, entryPoint, options);
            return *this;
        }

        AsyncConfigurator& SetStageDefines(
            ShaderStage stage,
            const std::unordered_map<std::string, std::string>& defines
        ) {
            builder_.SetStageDefines(stage, defines);
            return *this;
        }

        AsyncConfigurator& EnablePreprocessing(ShaderPreprocessor* preprocessor) {
            builder_.EnablePreprocessing(preprocessor);
            return *this;
        }

        AsyncConfigurator& EnableCaching(ShaderCacheManager* cacheManager) {
            builder_.EnableCaching(cacheManager);
            return *this;
        }

        AsyncConfigurator& SetCompiler(ShaderCompiler* compiler) {
            builder_.SetCompiler(compiler);
            return *this;
        }

        AsyncConfigurator& SetSdiConfig(const SdiGeneratorConfig& config) {
            builder_.SetSdiConfig(config);
            return *this;
        }

        AsyncConfigurator& EnableSdiGeneration(bool enable) {
            builder_.EnableSdiGeneration(enable);
            return *this;
        }

        AsyncConfigurator& EnableRegistryIntegration(
            SdiRegistryManager* registry,
            const std::string& aliasName = ""
        ) {
            builder_.EnableRegistryIntegration(registry, aliasName);
            return *this;
        }

        AsyncConfigurator& SetValidatePipeline(bool validate) {
            builder_.SetValidatePipeline(validate);
            return *this;
        }

        /**
         * @brief Submit build to worker thread (non-blocking)
         *
         * @return UUID for tracking this build
         */
        std::string Submit();

    private:
        ShaderBundleBuilder builder_;  // Internal builder
        AsyncShaderBundleBuilder* parent_;  // Parent async builder
        Vixen::EventBus::SenderID senderID_;
    };

    /**
     * @brief Constructor
     *
     * @param messageBus EventBus for publishing events
     * @param workerThreadCount Number of worker threads (0 = hardware concurrency)
     */
    explicit AsyncShaderBundleBuilder(
        Vixen::EventBus::MessageBus* messageBus,
        uint32_t workerThreadCount = 0
    );

    ~AsyncShaderBundleBuilder();

    /**
     * @brief Start configuring an async build
     *
     * @param sender Sender ID for events
     * @return Configurator for fluent interface
     */
    AsyncConfigurator BuildAsync(Vixen::EventBus::SenderID sender = 0);

    /**
     * @brief Cancel an in-progress build
     *
     * Best effort - if compilation already started, may complete anyway.
     *
     * @param uuid Build UUID to cancel
     * @return True if found and marked for cancellation
     */
    bool CancelBuild(const std::string& uuid);

    /**
     * @brief Check if a build is complete
     *
     * @param uuid Build UUID
     * @return True if completed (successfully or failed)
     */
    bool IsBuildComplete(const std::string& uuid) const;

    /**
     * @brief Wait for a specific build to complete
     *
     * Blocks until build finishes. Use sparingly - prefer event-driven approach.
     *
     * @param uuid Build UUID
     * @param timeout Max wait time (0 = infinite)
     * @return True if completed within timeout
     */
    bool WaitForBuild(
        const std::string& uuid,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(0)
    );

    /**
     * @brief Wait for all builds to complete
     *
     * @param timeout Max wait time (0 = infinite)
     * @return True if all completed within timeout
     */
    bool WaitForAll(std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

    /**
     * @brief Get number of active builds
     *
     * @return Count of in-progress builds
     */
    size_t GetActiveBuildCount() const;

    /**
     * @brief Get all active build UUIDs
     *
     * @return Vector of UUIDs currently building
     */
    std::vector<std::string> GetActiveBuilds() const;

    /**
     * @brief Clean up completed build handles
     *
     * Removes completed builds from tracking.
     *
     * @return Number of handles cleaned up
     */
    uint32_t CleanupCompleted();

private:
    void SubmitBuildInternal(ShaderBundleBuilder builder, Vixen::EventBus::SenderID sender);

    Vixen::EventBus::MessageBus* messageBus_;
    uint32_t workerThreadCount_;

    // Thread pool for compilation with per-thread work queues (reduces contention)
    std::vector<std::thread> workerThreads_;

    // Per-thread work queues for better cache locality and reduced contention
    struct ThreadLocalQueue {
        std::queue<std::function<void()>> tasks;
        std::mutex mutex;
    };
    std::vector<std::unique_ptr<ThreadLocalQueue>> perThreadQueues_;

    // Round-robin counter for work distribution
    std::atomic<uint32_t> nextQueueIndex_{0};

    // Shared condition variable for waking idle workers
    std::condition_variable workCV_;
    std::mutex cvMutex_;
    std::atomic<bool> running_;

    // Active build tracking
    std::unordered_map<std::string, std::shared_ptr<AsyncBuildHandle>> activeBuilds_;
    mutable std::mutex buildsMutex_;

    void WorkerThreadLoop(uint32_t threadIndex);
    void ExecuteBuild(ShaderBundleBuilder builder, Vixen::EventBus::SenderID sender);
    bool TryStealWork(uint32_t myIndex, std::function<void()>& outWork);
};

} // namespace ShaderManagement
