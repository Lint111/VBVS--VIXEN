#include "VulkanGraphApplication.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/StorageBufferNode.h"
#include "Nodes/PhotonCellParamsConfigNode.h"
#include "Nodes/PhotonCellTableNode.h"
#include "RunDataPaths.h"

#include <filesystem>
#include <fstream>

void VulkanGraphApplication::DeInitialize() {
    // Prevent double cleanup (called from both main and destructor)
    if (deinitialized) {
        return;
    }
    deinitialized = true;

    // A terminal render tick has no following frame to process asynchronous off-graph readbacks.
    // Flush the graph-owned submission queue while the device and render target are still alive,
    // then poll the completed capture handles before teardown can discard their staging buffers.
    if (renderGraph && (!pendingHudCapturePath_.empty() || pendingHudCapture_ ||
                        !pendingOffscreenCaptures_.empty())) {
        // Offscreen captures were begun on offscreenDeviceNode_ (CaptureOffscreenFrameToPng); a graph
        // whose device is not named main_device (the offscreen UI graph's ui_device) must still flush.
        auto* deviceInst = pendingOffscreenCaptures_.empty()
            ? nullptr
            : static_cast<DeviceNode*>(renderGraph->GetInstance(offscreenDeviceNode_));
        if (!deviceInst) deviceInst = static_cast<DeviceNode*>(renderGraph->GetInstanceByName("main_device"));
        if (deviceInst) {
            if (auto* device = deviceInst->GetVulkanDevice()) {
                const auto waited = renderGraph->FlushSubmittedWork(device);
                PollPendingCaptures();
                if (!waited.Ok() && mainLogger) {
                    mainLogger->Error("[DeInitialize] pending capture queue did not retire: " +
                                      std::to_string(waited.result));
                }
            }
        }
    }

    // Inc1 M4 Task 6b: flush the perf CSV before any teardown — RecordFrame already copied
    // every value it needs out of the nodes each tick, so this has no ordering dependency on
    // renderGraph/engine_ below; no-op unless VIXEN_PERF_CSV was set.
    if (hitAccumEnabled_ && std::getenv("VIXEN_HIT_ACCUM_PROBE_LOG")) {
        RunHitAccumDiagReadback();
    }
    if (auto* photonParams = static_cast<PhotonCellParamsConfigNode*>(
            renderGraph ? renderGraph->GetInstance(photonCellParamsBuffer_) : nullptr);
        photonParams && photonParams->ProbeLogEnabled()) {
        RunPhotonCellDiagReadback();
    }
    perfCsvWriter_.Flush();

    // Sprint 6.3: Publish shutdown event BEFORE any cleanup
    // CalibrationStore subscribes and saves automatically
    if (messageBus) {
        auto shutdownEvent = std::make_unique<Vixen::EventBus::ApplicationShuttingDownEvent>(0);
        messageBus->PublishImmediate(*shutdownEvent);  // Immediate - no queue processing
        mainLogger->Info("Published ApplicationShuttingDownEvent");
    }

    // Extract logs BEFORE destroying the render graph
    // With shared_ptr ownership:
    // 1. Nodes still alive → refcount = 2 (node + parent)
    // 2. Extract logs safely
    // 3. Destroy render graph → node destructors drop their refs (refcount 2→1)
    // 4. Loggers stay alive under main logger (refcount = 1)
    // 5. Clear children → refcount 1→0, automatic cleanup
    if (mainLogger && mainLogger->IsEnabled()) {
        try {
            std::string logs = mainLogger->ExtractLogs();
            // Write logs into the run-data dir (VIXEN_RUN_DATA_DIR, else the executable's build dir).
            const std::filesystem::path logPath = Vixen::App::AppLogPath(Vixen::App::RunDataDir());
            std::error_code logPathError;
            std::filesystem::create_directories(logPath.parent_path(), logPathError);
            std::ofstream logFile(logPath);
            if (logFile.is_open()) {
                logFile << logs;
                logFile.close();
                mainLogger->Info("Logs written to " + logPath.string());
            }
        } catch (...) {
            // Best-effort: don't throw during cleanup
        }
    }

    // Clear all child logger references (drops refcount 1→0, automatic cleanup)
    if (mainLogger) {
        mainLogger->ClearChildren();
    }

    // CRITICAL: Destroy render graph (triggers CleanupStack execution) BEFORE base class destroys device
    // NOTE: RenderGraph destructor handles cache saving automatically
    // This ensures all node-owned Vulkan resources (buffers, images, views, shaders) are destroyed
    // while the VkDevice is still valid.
    // Note: ConstantNode's cleanup callback will destroy the shader via registered callback
    // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: destroy the specialized per-recipe Vulkan
    // objects (VkShaderModule/VkDescriptorPool -- destroying the pool also frees every
    // VkDescriptorSet allocated from it) BEFORE the render graph/device teardown below, same
    // "while the VkDevice is still valid" ordering rule as every node's own CleanupImpl.
    // These are NOT node-owned resources (built directly via Vulkan calls in PreTick, outside
    // the RenderGraph node system entirely -- see RunRecipeBucketedDispatchPreTick's own
    // comment for why), so nothing else in the graph's teardown chain destroys them; skipping
    // this was a real gap found live (VUID-vkDestroyDevice-device-05137, one per leaked
    // pool/set, during this milestone's own gate run).
    if ((!recipeSpecializedPipelineCache_.empty() || !bucketShadePipelineCache_.empty()) && renderGraph) {
        if (auto* deviceInst = static_cast<DeviceNode*>(renderGraph->GetInstanceByName("main_device"))) {
            if (auto* device = deviceInst->GetVulkanDevice()) {
                // Wait for every in-flight command buffer to finish before destroying these
                // pools -- found live (VUID-vkDestroyDescriptorPool-descriptorPool-00303) that
                // the last frame's dispatch could still be executing when teardown reaches here.
                vkDeviceWaitIdle(device->device);
                for (auto& [recipeId, cached] : recipeSpecializedPipelineCache_) {
                    if (cached.descriptorPool != VK_NULL_HANDLE) {
                        vkDestroyDescriptorPool(device->device, cached.descriptorPool, nullptr);
                    }
                    if (cached.shaderModule != VK_NULL_HANDLE) {
                        vkDestroyShaderModule(device->device, cached.shaderModule, nullptr);
                    }
                    // descriptorSetLayout/pipeline/pipelineLayout are owned by CashSystem's
                    // caches (DescriptorSetLayoutCacher's BuildDescriptorSetLayoutFromReflection
                    // is a one-off convenience helper -- its returned layout is NOT cacher-owned,
                    // but ComputePipelineCacher DOES own pipeline/pipelineLayout via MainCacher,
                    // torn down by the graph's own MainCacher cleanup below). The layout built by
                    // the one-off helper has no owner anywhere else, so destroy it here too.
                    if (cached.descriptorSetLayout != VK_NULL_HANDLE) {
                        vkDestroyDescriptorSetLayout(device->device, cached.descriptorSetLayout, nullptr);
                    }
                }
                for (auto& [recipeId, cached] : bucketShadePipelineCache_) {
                    if (cached.descriptorPool != VK_NULL_HANDLE) {
                        vkDestroyDescriptorPool(device->device, cached.descriptorPool, nullptr);
                    }
                    if (cached.shaderModule != VK_NULL_HANDLE) {
                        vkDestroyShaderModule(device->device, cached.shaderModule, nullptr);
                    }
                    if (cached.descriptorSetLayout != VK_NULL_HANDLE) {
                        vkDestroyDescriptorSetLayout(device->device, cached.descriptorSetLayout, nullptr);
                    }
                }
                if (mainLogger) mainLogger->Info("[DeInitialize] Destroyed " +
                    std::to_string(recipeSpecializedPipelineCache_.size()) +
                    " specialized recipe pipeline(s) and " +
                    std::to_string(bucketShadePipelineCache_.size()) +
                    " bucket-shade pipeline(s) (shader modules + descriptor pools/layouts)");
            }
        }
        recipeSpecializedPipelineCache_.clear();
        bucketShadePipelineCache_.clear();
    }

    // E11-T1: PolicyStencilTileBuffer readback — app-level (not a node's own
    // CleanupImpl) because the tile buffer and RayTraceBuffer's [PolicyStencil]
    // print (VoxelGridNode::CleanupImpl) are owned by different nodes with no
    // shared access; renderGraph->GetInstanceByName is still valid here, the
    // same pattern RunRecipeBucketedDispatchPreTick's skipMaskNode CPU publication
    // uses (this file, ~line 750), and this is the last point before
    // engine_.reset() destroys every node (including this one).
    if (std::getenv("VIXEN_POLICY_STENCIL_TILES") && renderGraph) {
        if (auto* deviceInst = static_cast<DeviceNode*>(renderGraph->GetInstanceByName("main_device"))) {
            if (auto* device = deviceInst->GetVulkanDevice()) {
                if (auto* tileNode = static_cast<StorageBufferNode*>(
                        renderGraph->GetInstanceByName("policy_stencil_tile_buffer"))) {
                    vkDeviceWaitIdle(device->device);
                    if (void* mapped = tileNode->MapForReadback(device)) {
                        auto* words = reinterpret_cast<uint32_t*>(mapped);
                        const size_t wordCount = static_cast<size_t>(tileNode->GetSizeBytes() / 4);
                        const uint32_t header = wordCount > 0 ? words[0] : 0u;
                        const uint32_t skipped = wordCount > 1 ? words[1] : 0u;
                        uint32_t nonZeroTiles = 0u;
                        for (size_t i = 2; i < wordCount; ++i) {
                            if (words[i] != 0u) ++nonZeroTiles;
                        }
                        std::cout << "[PolicyStencilTiles] imgWidth=" << (header >> 16u)
                                   << " imgHeight=" << (header & 0xFFFFu)
                                   << " skippedPixels=" << skipped
                                   << " nonZeroTiles=" << nonZeroTiles
                                   << " totalTileSlots=" << (wordCount > 2 ? wordCount - 2 : 0)
                                   << std::endl;
                        tileNode->UnmapReadback(device);
                    }
                }
            }
        }
    }

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[DeInitialize] Destroying render graph...");
    }
    // AR#7: one reset tears down the whole EngineContext in order — calibration -> graph
    // (node cleanup, while the base-class VkDevice below is still valid) -> bus -> registry.
    engine_.reset();
    nodeRegistry = nullptr;
    messageBus = nullptr;
    renderGraph = nullptr;
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[DeInitialize] EngineContext (graph + registry + bus + calibration) destroyed");
    }

    // Graph nodes handle their own cleanup (including window)

    // Call base class cleanup (destroys device and instance)
    if (mainLogger) {
        mainLogger->Info("[DeInitialize] Calling base class DeInitialize...");
    }
    VulkanApplicationBase::DeInitialize();
    if (mainLogger) {
        mainLogger->Info("[DeInitialize] Base class DeInitialize complete");
    }

    if (mainLogger) {
        mainLogger->Info("VulkanGraphApplication deinitialized");
    }
}
