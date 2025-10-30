#include "ShaderManagement/AsyncShaderBundleBuilder.h"
#include <algorithm>

namespace ShaderManagement {

// ===== AsyncShaderBundleBuilder Implementation =====

AsyncShaderBundleBuilder::AsyncShaderBundleBuilder(
    Vixen::EventBus::MessageBus* messageBus,
    uint32_t workerThreadCount
)
    : messageBus_(messageBus)
    , workerThreadCount_(workerThreadCount == 0 ? std::thread::hardware_concurrency() : workerThreadCount)
    , running_(true)
{
    // Create per-thread work queues
    for (uint32_t i = 0; i < workerThreadCount_; ++i) {
        perThreadQueues_.push_back(std::make_unique<ThreadLocalQueue>());
    }

    // Create worker threads with thread indices
    for (uint32_t i = 0; i < workerThreadCount_; ++i) {
        workerThreads_.emplace_back(&AsyncShaderBundleBuilder::WorkerThreadLoop, this, i);
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
    Vixen::EventBus::SenderID sender
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
    Vixen::EventBus::SenderID sender
) {
    // Create build handle
    auto handle = std::make_shared<AsyncBuildHandle>(builder.GetUuid());

    {
        std::lock_guard<std::mutex> lock(buildsMutex_);
        activeBuilds_[handle->uuid] = handle;
    }

    // Use round-robin to distribute work across threads (reduces contention)
    uint32_t targetQueue = nextQueueIndex_.fetch_add(1, std::memory_order_relaxed) % workerThreadCount_;

    // Submit work to selected thread's queue
    {
        std::lock_guard<std::mutex> lock(perThreadQueues_[targetQueue]->mutex);
        perThreadQueues_[targetQueue]->tasks.push([this, builder = std::move(builder), sender, handle]() mutable {
            ExecuteBuild(std::move(builder), sender);
            handle->completed = true;
        });
    }

    // Wake one worker (they'll check their queue first, then steal if needed)
    workCV_.notify_one();
}

void AsyncShaderBundleBuilder::WorkerThreadLoop(uint32_t threadIndex) {
    while (running_) {
        std::function<void()> work;
        bool gotWork = false;

        // 1. Try to get work from own queue first (best cache locality)
        {
            std::unique_lock<std::mutex> lock(perThreadQueues_[threadIndex]->mutex);
            if (!perThreadQueues_[threadIndex]->tasks.empty()) {
                work = std::move(perThreadQueues_[threadIndex]->tasks.front());
                perThreadQueues_[threadIndex]->tasks.pop();
                gotWork = true;
            }
        }

        // 2. If own queue empty, try to steal work from other threads
        if (!gotWork) {
            gotWork = TryStealWork(threadIndex, work);
        }

        // 3. If still no work, wait for notification
        if (!gotWork) {
            std::unique_lock<std::mutex> lock(cvMutex_);
            workCV_.wait_for(lock, std::chrono::milliseconds(100), [this, threadIndex] {
                if (!running_) return true;

                // Check if any queue has work
                for (const auto& queue : perThreadQueues_) {
                    std::lock_guard<std::mutex> qLock(queue->mutex);
                    if (!queue->tasks.empty()) return true;
                }
                return false;
            });

            if (!running_) {
                break;
            }

            continue; // Loop back to try getting work again
        }

        // Execute work
        if (work) {
            work();
        }
    }
}

bool AsyncShaderBundleBuilder::TryStealWork(uint32_t myIndex, std::function<void()>& outWork) {
    // Try to steal from other threads (work stealing for load balancing)
    // Start from next thread (circular) to distribute stealing attempts
    for (uint32_t offset = 1; offset < workerThreadCount_; ++offset) {
        uint32_t targetIndex = (myIndex + offset) % workerThreadCount_;

        std::unique_lock<std::mutex> lock(perThreadQueues_[targetIndex]->mutex, std::try_to_lock);
        if (lock.owns_lock() && !perThreadQueues_[targetIndex]->tasks.empty()) {
            // Successfully stole work!
            outWork = std::move(perThreadQueues_[targetIndex]->tasks.front());
            perThreadQueues_[targetIndex]->tasks.pop();
            return true;
        }
    }

    return false; // No work available to steal
}

void AsyncShaderBundleBuilder::ExecuteBuild(
    ShaderBundleBuilder builder,
    Vixen::EventBus::SenderID sender
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
            sender, std::move(*result.bundle));
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
