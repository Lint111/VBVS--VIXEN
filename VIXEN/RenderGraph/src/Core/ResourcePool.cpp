#include "Core/ResourcePool.h"
#include "Core/NodeLogging.h"
#include "Core/ResourceLifetimeAnalyzer.h"
#include "Core/AliasingEngine.h"
#include "Core/ResourceProfiler.h"

namespace Vixen::RenderGraph {

//==============================================================================
// Construction / Destruction
//==============================================================================

ResourcePool::ResourcePool()
    : budgetManager_(std::make_unique<ResourceBudgetManager>())
    , aliasingEngine_(std::make_unique<AliasingEngine>())
    , profiler_(std::make_unique<ResourceProfiler>())
{
    NODE_LOG_INFO("ResourcePool: Initialized with default settings");
    NODE_LOG_INFO("  - Aliasing: disabled (default)");
    NODE_LOG_INFO("  - Profiling: disabled (default)");
    NODE_LOG_INFO("  - Aliasing threshold: 1 MB");
}

ResourcePool::~ResourcePool() {
    NODE_LOG_INFO("ResourcePool: Shutdown");

    // Log final aliasing statistics
    if (aliasingEngine_) {
        auto stats = aliasingEngine_->GetStats();
        NODE_LOG_INFO("  - Aliasing attempts: " + std::to_string(stats.totalAliasAttempts));
        NODE_LOG_INFO("  - Successful aliases: " + std::to_string(stats.successfulAliases));
        NODE_LOG_INFO("  - VRAM saved: " + std::to_string(stats.totalBytesSaved / (1024 * 1024)) + " MB");
        NODE_LOG_INFO("  - Savings rate: " + std::to_string(stats.GetSavingsPercentage()) + "%");
    }

    // Profiler cleanup happens automatically via unique_ptr
    // budgetManager_ cleanup happens automatically via unique_ptr
}

//==============================================================================
// Resource Allocation
//==============================================================================

template<typename T>
Resource* ResourcePool::AllocateResource(
    const typename ResourceTypeTraits<T>::DescriptorT& descriptor,
    ResourceLifetime lifetime,
    ::ResourceManagement::AllocStrategy strategy
) {
    // TODO: Phase I - Full aliasing integration
    // When AliasingEngine is implemented:
    // 1. Check if aliasing is enabled and resource qualifies (size >= threshold)
    // 2. Query AliasingEngine for available alias candidates
    // 3. If alias found, reuse that memory
    // 4. Otherwise, fall through to normal allocation

    // TODO: Phase I - Full profiling integration
    // When ResourceProfiler is implemented:
    // 1. Record allocation request
    // 2. Track allocation time
    // 3. Associate with current node context
    // 4. Update per-frame statistics

    // For now, delegate to budget manager which handles:
    // - Budget checking (soft/strict mode)
    // - Actual resource creation
    // - Usage tracking
    Resource* resource = budgetManager_->CreateResource<T>(descriptor, strategy);

    if (resource && profilingEnabled_) {
        // TODO: profiler_->RecordAllocation(resource, currentFrame_);
        NODE_LOG_DEBUG("ResourcePool: Allocated resource (profiling pending AliasingEngine implementation)");
    }

    return resource;
}

void ResourcePool::ReleaseResource(Resource* resource) {
    if (!resource) {
        return; // Null resource is a no-op
    }

    // TODO: Phase I - Aliasing integration
    // When AliasingEngine is implemented:
    // if (aliasingEnabled_ && resource->GetSize() >= aliasingThreshold_) {
    //     // Register resource as available for aliasing
    //     aliasingEngine_->RegisterAvailableResource(resource);
    //     return; // Don't delete yet - keep for reuse
    // }

    // TODO: Phase I - Profiling integration
    // When ResourceProfiler is implemented:
    // if (profilingEnabled_ && profiler_) {
    //     profiler_->RecordDeallocation(resource, currentFrame_);
    // }

    // Update budget tracking before deletion
    budgetManager_->ReleaseResource(resource);

    // Immediate deletion (when aliasing is disabled or doesn't apply)
    delete resource;
}

//==============================================================================
// Aliasing Control
//==============================================================================

void ResourcePool::EnableAliasing(bool enable) {
    aliasingEnabled_ = enable;

    if (enable) {
        NODE_LOG_INFO("ResourcePool: Aliasing ENABLED");
        NODE_LOG_INFO("  - Threshold: " + std::to_string(aliasingThreshold_ / (1024 * 1024)) + " MB");

        if (!lifetimeAnalyzer_) {
            NODE_LOG_WARN("ResourcePool: Aliasing enabled but no ResourceLifetimeAnalyzer set");
            NODE_LOG_WARN("  - Aliasing will be conservative without lifetime information");
        }

        // TODO: When AliasingEngine is implemented
        // if (aliasingEngine_) {
        //     aliasingEngine_->Enable();
        // }
    } else {
        NODE_LOG_INFO("ResourcePool: Aliasing DISABLED");

        // TODO: When AliasingEngine is implemented
        // if (aliasingEngine_) {
        //     aliasingEngine_->Disable();
        //     aliasingEngine_->FlushAliasPool(); // Release all aliased resources
        // }
    }
}

bool ResourcePool::IsAliasingEnabled() const {
    return aliasingEnabled_;
}

void ResourcePool::SetAliasingThreshold(size_t minBytes) {
    aliasingThreshold_ = minBytes;

    // Log with human-readable units
    double mb = static_cast<double>(minBytes) / (1024.0 * 1024.0);
    NODE_LOG_INFO("ResourcePool: Aliasing threshold set to " +
                  std::to_string(minBytes) + " bytes (" +
                  std::to_string(mb) + " MB)");

    if (minBytes < 256 * 1024) {
        NODE_LOG_WARN("ResourcePool: Aliasing threshold is very low (<256 KB)");
        NODE_LOG_WARN("  - Aliasing small resources may have overhead > benefit");
    }

    // TODO: When AliasingEngine is implemented
    // if (aliasingEngine_) {
    //     aliasingEngine_->SetMinimumSize(minBytes);
    // }
}

//==============================================================================
// Budget Control
//==============================================================================

void ResourcePool::SetBudget(BudgetResourceType type, const ResourceBudget& budget) {
    // Delegate to budget manager
    budgetManager_->SetBudget(type, budget);

    // Log budget configuration for visibility
    const char* typeName = "";
    switch (type) {
        case BudgetResourceType::HostMemory:     typeName = "HostMemory"; break;
        case BudgetResourceType::DeviceMemory:   typeName = "DeviceMemory"; break;
        case BudgetResourceType::CommandBuffers: typeName = "CommandBuffers"; break;
        case BudgetResourceType::Descriptors:    typeName = "Descriptors"; break;
        case BudgetResourceType::UserDefined:    typeName = "UserDefined"; break;
        default: typeName = "Unknown"; break;
    }

    const char* modeName = budget.strict ? "Strict" : "Soft";

    double limitMB = static_cast<double>(budget.maxBytes) / (1024.0 * 1024.0);
    NODE_LOG_INFO("ResourcePool: Budget set for " + std::string(typeName) +
                  " = " + std::to_string(limitMB) + " MB (" + std::string(modeName) + " mode)");
}

std::optional<ResourceBudget> ResourcePool::GetBudget(BudgetResourceType type) const {
    return budgetManager_->GetBudget(type);
}

BudgetResourceUsage ResourcePool::GetUsage(BudgetResourceType type) const {
    return budgetManager_->GetUsage(type);
}

//==============================================================================
// Profiling
//==============================================================================

void ResourcePool::BeginFrameProfiling(uint64_t frameNumber) {
    currentFrame_ = frameNumber;

    if (profilingEnabled_ && profiler_) {
        NODE_LOG_DEBUG("ResourcePool: Begin profiling frame " + std::to_string(frameNumber));
        profiler_->BeginFrame(frameNumber);
    }
}

void ResourcePool::EndFrameProfiling() {
    if (profilingEnabled_ && profiler_) {
        NODE_LOG_DEBUG("ResourcePool: End profiling frame " + std::to_string(currentFrame_));
        profiler_->EndFrame();

        // Optionally log summary statistics
        auto stats = profiler_->GetFrameStats(currentFrame_);
        NODE_LOG_DEBUG("  - Total allocations: " + std::to_string(stats.totals.GetTotalAllocations()));
        NODE_LOG_DEBUG("  - Peak VRAM: " + std::to_string(stats.peakVramUsage / (1024*1024)) + " MB");
        NODE_LOG_DEBUG("  - Aliasing saves: " + std::to_string(stats.totals.bytesSavedViaAliasing / (1024*1024)) + " MB");
    }
}

void ResourcePool::EnableProfiling(bool enable) {
    profilingEnabled_ = enable;

    if (enable) {
        NODE_LOG_INFO("ResourcePool: Profiling ENABLED");
        NODE_LOG_INFO("  - Will track per-node allocations and memory usage");
        if (profiler_) {
            profiler_->EnableDetailedLogging(true);
        }
    } else {
        NODE_LOG_INFO("ResourcePool: Profiling DISABLED");
        if (profiler_) {
            profiler_->EnableDetailedLogging(false);
        }

        // TODO: When ResourceProfiler is implemented
        // if (profiler_) {
        //     // Optionally: Export profiling data before clearing
        //     profiler_->Clear();
        // }
    }
}

bool ResourcePool::IsProfilingEnabled() const {
    return profilingEnabled_;
}

//==============================================================================
// Stack Tracking (Integration with ResourceBudgetManager)
//==============================================================================

void ResourcePool::BeginFrameStackTracking(uint64_t frameNumber) {
    NODE_LOG_DEBUG("ResourcePool: Begin frame stack tracking " + std::to_string(frameNumber));

    // Delegate to budget manager which handles frame-scoped stack allocations
    budgetManager_->BeginFrameStackTracking(frameNumber);

    // Also update current frame for consistency
    currentFrame_ = frameNumber;
}

void ResourcePool::EndFrameStackTracking() {
    NODE_LOG_DEBUG("ResourcePool: End frame stack tracking " + std::to_string(currentFrame_));

    // Delegate to budget manager which automatically cleans up stack allocations
    budgetManager_->EndFrameStackTracking();
}

//==============================================================================
// Lifetime Analyzer Integration
//==============================================================================

void ResourcePool::SetLifetimeAnalyzer(ResourceLifetimeAnalyzer* analyzer) {
    lifetimeAnalyzer_ = analyzer;

    if (analyzer) {
        NODE_LOG_INFO("ResourcePool: ResourceLifetimeAnalyzer connected");

        if (aliasingEnabled_) {
            NODE_LOG_INFO("  - Aliasing can now use lifetime information for optimization");
        }

        // Connect to aliasing engine
        if (aliasingEngine_) {
            aliasingEngine_->SetLifetimeAnalyzer(analyzer);
        }
    } else {
        NODE_LOG_WARN("ResourcePool: ResourceLifetimeAnalyzer disconnected");

        if (aliasingEnabled_) {
            NODE_LOG_WARN("  - Aliasing will be conservative without lifetime data");
        }
    }
}

//==============================================================================
// Template Instantiations
//==============================================================================

// Explicit template instantiations for common resource types
// This allows the template implementation to be in the .cpp file

// TODO: Uncomment these once ResourceTypeTraits is fully defined for each type
// template Resource* ResourcePool::AllocateResource<Texture2D>(
//     const Texture2DDescriptor&, ResourceLifetime, ResourceManagement::AllocStrategy);
//
// template Resource* ResourcePool::AllocateResource<Buffer>(
//     const BufferDescriptor&, ResourceLifetime, ResourceManagement::AllocStrategy);
//
// template Resource* ResourcePool::AllocateResource<RenderTarget>(
//     const RenderTargetDescriptor&, ResourceLifetime, ResourceManagement::AllocStrategy);

} // namespace Vixen::RenderGraph
