#include "ShaderManagement/AsyncShaderBundleBuilder.h"
#include <algorithm>

namespace ShaderManagement {

// ===== AsyncShaderBundleBuilder Implementation =====

AsyncShaderBundleBuilder::AsyncShaderBundleBuilder(
    EventBus::MessageBus* messageBus,
    uint32_t workerThreadCount
)
    : messageBus_(messageBus)
    , workerThreadCount_(workerThreadCount == 0 ? std::thread::hardware_concurrency() : workerThreadCount)
    , running_(true)
{
    // Create worker threads
    for (uint32_t i = 0; i < workerThreadCount_; ++i) {
        workerThreads_.emplace_back(&AsyncShaderBundleBuilder::WorkerThreadLoop, this);
    }
}

AsyncShaderBundleBuilder::~AsyncShaderBundleBuilder() {
    // Signal shutdown
    running_ = false;
    workCV_.notify_all();

    // Wait for all workers
    for (auto& thread : workerThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    // Clean up active builds
    activeBuilds_.clear();
}

AsyncShaderBundleBuilder::AsyncConfigurator AsyncShaderBundleBuilder::BuildAsync(
    EventBus::SenderID sender
) {
    return AsyncConfigurator(this, sender);
}

bool AsyncShaderBundleBuilder::CancelBuild(const std::string& uuid) {
    std::lock_guard<std::mutex> lock(buildsMutex_);

    auto it = activeBuilds_.find(uuid);
    if (it != activeBuilds_.end()) {
        it->second->cancelled = true;
        return true;
    }

    return false;
}

bool AsyncShaderBundleBuilder::IsBuildComplete(const std::string& uuid) const {
    std::lock_guard<std::mutex> lock(buildsMutex_);

    auto it = activeBuilds_.find(uuid);
    if (it != activeBuilds_.end()) {
        return it->second->completed;
    }

    return true; // Not found = already completed/cleaned up
}

bool AsyncShaderBundleBuilder::WaitForBuild(
    const std::string& uuid,
    std::chrono::milliseconds timeout
) {
    auto start = std::chrono::steady_clock::now();

    while (true) {
        {
            std::lock_guard<std::mutex> lock(buildsMutex_);
            auto it = activeBuilds_.find(uuid);
            if (it == activeBuilds_.end() || it->second->completed) {
                return true;
            }
        }

        // Check timeout
        if (timeout.count() > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            if (elapsed >= timeout) {
                return false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool AsyncShaderBundleBuilder::WaitForAll(std::chrono::milliseconds timeout) {
    auto start = std::chrono::steady_clock::now();

    while (true) {
        {
            std::lock_guard<std::mutex> lock(buildsMutex_);
            bool allComplete = std::all_of(activeBuilds_.begin(), activeBuilds_.end(),
                [](const auto& pair) { return pair.second->completed.load(); });

            if (allComplete) {
                return true;
            }
        }

        // Check timeout
        if (timeout.count() > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            if (elapsed >= timeout) {
                return false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

size_t AsyncShaderBundleBuilder::GetActiveBuildCount() const {
    std::lock_guard<std::mutex> lock(buildsMutex_);
    return std::count_if(activeBuilds_.begin(), activeBuilds_.end(),
        [](const auto& pair) { return !pair.second->completed.load(); });
}

std::vector<std::string> AsyncShaderBundleBuilder::GetActiveBuilds() const {
    std::lock_guard<std::mutex> lock(buildsMutex_);

    std::vector<std::string> uuids;
    for (const auto& [uuid, handle] : activeBuilds_) {
        if (!handle->completed) {
            uuids.push_back(uuid);
        }
    }

    return uuids;
}

uint32_t AsyncShaderBundleBuilder::CleanupCompleted() {
    std::lock_guard<std::mutex> lock(buildsMutex_);

    uint32_t count = 0;
    auto it = activeBuilds_.begin();
    while (it != activeBuilds_.end()) {
        if (it->second->completed) {
            it = activeBuilds_.erase(it);
            ++count;
        } else {
            ++it;
        }
    }

    return count;
}

void AsyncShaderBundleBuilder::SubmitBuildInternal(
    ShaderBundleBuilder builder,
    EventBus::SenderID sender
) {
    // Create build handle
    auto handle = std::make_shared<AsyncBuildHandle>(builder.GetUuid());

    {
        std::lock_guard<std::mutex> lock(buildsMutex_);
        activeBuilds_[handle->uuid] = handle;
    }

    // Submit work to thread pool
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        workQueue_.push([this, builder = std::move(builder), sender, handle]() mutable {
            ExecuteBuild(std::move(builder), sender);
            handle->completed = true;
        });
    }

    workCV_.notify_one();
}

void AsyncShaderBundleBuilder::WorkerThreadLoop() {
    while (running_) {
        std::function<void()> work;

        // Wait for work
        {
            std::unique_lock<std::mutex> lock(workMutex_);
            workCV_.wait(lock, [this] { return !workQueue_.empty() || !running_; });

            if (!running_ && workQueue_.empty()) {
                break;
            }

            if (workQueue_.empty()) {
                continue;
            }

            work = std::move(workQueue_.front());
            workQueue_.pop();
        }

        // Execute work
        if (work) {
            work();
        }
    }
}

void AsyncShaderBundleBuilder::ExecuteBuild(
    ShaderBundleBuilder builder,
    EventBus::SenderID sender
) {
    auto startTime = std::chrono::steady_clock::now();

    std::string uuid = builder.GetUuid();
    std::string programName = builder.GetProgramName();
    uint32_t stageCount = builder.GetStageCount();

    // Check if cancelled
    {
        std::lock_guard<std::mutex> lock(buildsMutex_);
        auto it = activeBuilds_.find(uuid);
        if (it != activeBuilds_.end() && it->second->cancelled) {
            return; // Silently abort
        }
    }

    // Publish: Compilation started
    auto startedMsg = std::make_unique<ShaderCompilationStartedMessage>(
        sender, programName, uuid, stageCount);
    messageBus_->Publish(std::move(startedMsg));

    // Progress: 0% (started)
    auto progressMsg = std::make_unique<ShaderCompilationProgressMessage>(
        sender, uuid, "Starting", 0, stageCount * 4); // 4 phases per stage
    messageBus_->Publish(std::move(progressMsg));

    // Perform build
    auto result = builder.Build();

    auto endTime = std::chrono::steady_clock::now();
    auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    if (result.success) {
        // Publish: Compilation completed
        auto completedMsg = std::make_unique<ShaderCompilationCompletedMessage>(
            sender, *result.bundle);
        completedMsg->usedCache = result.usedCache;
        completedMsg->preprocessTime = result.preprocessTime;
        completedMsg->compileTime = result.compileTime;
        completedMsg->reflectTime = result.reflectTime;
        completedMsg->sdiGenTime = result.sdiGenTime;
        completedMsg->totalTime = totalTime;
        completedMsg->warnings = result.warnings;
        messageBus_->Publish(std::move(completedMsg));

        // Publish: SDI generated (if applicable)
        if (result.bundle->HasValidSdi()) {
            auto sdiMsg = std::make_unique<SdiGeneratedMessage>(
                sender,
                result.bundle->uuid,
                result.bundle->sdiHeaderPath.string(),
                result.bundle->sdiNamespace
            );
            messageBus_->Publish(std::move(sdiMsg));
        }
    } else {
        // Publish: Compilation failed
        auto failedMsg = std::make_unique<ShaderCompilationFailedMessage>(
            sender,
            programName,
            uuid,
            result.errorMessage
        );
        failedMsg->warnings = result.warnings;
        messageBus_->Publish(std::move(failedMsg));
    }
}

// ===== AsyncConfigurator Implementation =====

std::string AsyncShaderBundleBuilder::AsyncConfigurator::Submit() {
    std::string uuid = builder_.GetUuid();
    if (uuid.empty()) {
        uuid = builder_.GenerateUuid();
        builder_.SetUuid(uuid);
    }

    parent_->SubmitBuildInternal(std::move(builder_), senderID_);
    return uuid;
}

} // namespace ShaderManagement
