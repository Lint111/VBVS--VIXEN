#include "AsyncShaderBundleBuilder.h"
#include "Logger.h"
#include <algorithm>
#include <utility>

namespace ShaderManagement {

namespace {

Vixen::KernelDispatch::DispatcherProfile MakeShaderDispatcherProfile(uint32_t workerCount) {
    Vixen::KernelDispatch::DispatcherProfile profile;
    profile.blockingIO.workerCount = workerCount;
    return profile;
}

} // namespace

// ===== AsyncShaderBundleBuilder Implementation =====

AsyncShaderBundleBuilder::AsyncShaderBundleBuilder(
    Vixen::EventBus::MessageBus* messageBus,
    uint32_t workerThreadCount
)
    : messageBus_(messageBus)
    , taskExecutor_(MakeShaderDispatcherProfile(workerThreadCount))
{
}

AsyncShaderBundleBuilder::~AsyncShaderBundleBuilder() {
    // Wait before member destruction because queued callbacks publish through messageBus_ and
    // retain active-build handles.
    WaitForAll();
    BuildNode* node = buildsHead_.exchange(nullptr, std::memory_order_acq_rel);
    while (node) {
        BuildNode* older = node->older;
        delete node;
        node = older;
    }
}

AsyncShaderBundleBuilder::AsyncConfigurator AsyncShaderBundleBuilder::BuildAsync(
    Vixen::EventBus::SenderID sender
) {
    return AsyncConfigurator(this, sender);
}

AsyncShaderBundleBuilder::BuildNode* AsyncShaderBundleBuilder::FindBuild(const std::string& uuid) const {
    for (BuildNode* node = buildsHead_.load(std::memory_order_acquire); node; node = node->older) {
        if (node->handle->uuid != uuid) continue;
        // The newest node for a uuid shadows older ones, as the old map's overwrite did.
        return node->removed.load(std::memory_order_acquire) ? nullptr : node;
    }
    return nullptr;
}

template <typename Fn>
void AsyncShaderBundleBuilder::ForEachLiveBuild(Fn&& fn) const {
    std::vector<const std::string*> seen;
    for (BuildNode* node = buildsHead_.load(std::memory_order_acquire); node; node = node->older) {
        const std::string& uuid = node->handle->uuid;
        const bool shadowed = std::any_of(seen.begin(), seen.end(),
            [&](const std::string* other) { return *other == uuid; });
        if (shadowed) continue;
        seen.push_back(&uuid);
        if (!node->removed.load(std::memory_order_acquire)) fn(*node);
    }
}

bool AsyncShaderBundleBuilder::CancelBuild(const std::string& uuid) {
    if (BuildNode* node = FindBuild(uuid)) {
        node->handle->cancelled = true;
        node->handle->stopSource.request_stop();
        return true;
    }

    return false;
}

bool AsyncShaderBundleBuilder::IsBuildComplete(const std::string& uuid) const {
    if (BuildNode* node = FindBuild(uuid)) {
        return node->handle->completion.IsReady();
    }

    return true; // Not found = already completed/cleaned up
}

bool AsyncShaderBundleBuilder::WaitForBuild(
    const std::string& uuid,
    std::chrono::milliseconds timeout
) {
    BuildNode* node = FindBuild(uuid);
    if (!node) return true;
    const std::shared_ptr<AsyncBuildHandle> handle = node->handle;

    // Off-frame join (load / shutdown / tests); never called from the frame path.
    if (timeout.count() == 0) {
        (void)handle->completion.Wait();
        return true;
    }
    return handle->completion.WaitFor(timeout);
}

bool AsyncShaderBundleBuilder::WaitForAll(std::chrono::milliseconds timeout) {
    std::vector<std::shared_ptr<AsyncBuildHandle>> handles;
    ForEachLiveBuild([&](BuildNode& node) { handles.push_back(node.handle); });

    const auto start = std::chrono::steady_clock::now();
    for (const auto& handle : handles) {
        if (timeout.count() == 0) {
            (void)handle->completion.Wait();
            continue;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        if (elapsed >= timeout || !handle->completion.WaitFor(timeout - elapsed)) return false;
    }
    return true;
}

size_t AsyncShaderBundleBuilder::GetActiveBuildCount() const {
    size_t count = 0;
    ForEachLiveBuild([&](BuildNode& node) {
        if (!node.handle->completion.IsReady()) ++count;
    });
    return count;
}

std::vector<std::string> AsyncShaderBundleBuilder::GetActiveBuilds() const {
    std::vector<std::string> uuids;
    ForEachLiveBuild([&](BuildNode& node) {
        if (!node.handle->completion.IsReady()) uuids.push_back(node.handle->uuid);
    });
    return uuids;
}

uint32_t AsyncShaderBundleBuilder::CleanupCompleted() {
    uint32_t count = 0;
    ForEachLiveBuild([&](BuildNode& node) {
        if (node.handle->completion.IsReady()) {
            node.removed.store(true, std::memory_order_release);
            ++count;
        }
    });

    return count;
}

void AsyncShaderBundleBuilder::SubmitBuildInternal(
    ShaderBundleBuilder builder,
    Vixen::EventBus::SenderID sender
) {
    // Create build handle
    auto handle = std::make_shared<AsyncBuildHandle>(builder.GetUuid());

    auto* node = new BuildNode();
    node->handle = handle;
    node->older = buildsHead_.load(std::memory_order_relaxed);
    while (!buildsHead_.compare_exchange_weak(node->older, node,
               std::memory_order_release, std::memory_order_relaxed)) {
        // node->older refreshed by the failed exchange; retry the push
    }

    auto builderPtr = std::make_shared<ShaderBundleBuilder>(std::move(builder));
    handle->completion = taskExecutor_.SubmitBlocking(
        [this, builderPtr, sender, handle]() mutable {
            const bool success = ExecuteBuild(std::move(*builderPtr), sender, handle);
            handle->completed = true;
            return success;
        }, handle->stopSource.get_token());
}

bool AsyncShaderBundleBuilder::ExecuteBuild(
    ShaderBundleBuilder builder,
    Vixen::EventBus::SenderID sender,
    const std::shared_ptr<AsyncBuildHandle>& handle
) {
    auto startTime = std::chrono::steady_clock::now();

    std::string uuid = builder.GetUuid();
    std::string programName = builder.GetProgramName();
    uint32_t stageCount = static_cast<uint32_t>(builder.GetStageCount());

    // Check if cancelled
    if (handle->cancelled) return false; // Silently abort

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
        return true;
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
        return false;
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
