// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "Core/SubmitChannel.h"
#include "Nodes/Common/GpuChunkCost.h"

namespace Vixen::Vulkan::Resources {
class VulkanDevice;
class GPUTimestampQuery;
}
namespace Vixen::Log {
class Logger;
}

namespace Vixen::RenderGraph {
class SimpleTaskProfile;

/**
 * @brief R861: the one submit-sizing rule of every compute dispatch node.
 *
 * A pass is a grid of workgroups. This object cuts it into square tiles (one primary command
 * buffer each, `vkCmdDispatchBase`), sized from the pass's own measured cost so that no queue
 * submission runs longer than the device allows:
 *
 *   - measured: `GpuChunkCost::Edge` (one dispatch while the whole pass fits the whole-pass
 *     window, otherwise tiles sized to the submission budget, never below `kLaunchFloor`). A
 *     `Prior::Unknown` pass keeps that work per tile on any grid shape (`TileEdge`) and, while the
 *     device's launch latency is not measured, is refined by 2x to 4x a readback
 *     (`RefinedTileGroups`);
 *   - unmeasured: the node's prior (`Prior`), because a pass that is too long on its first
 *     frame loses the device before any measurement exists.
 *
 * `SubmitRecord` then packs consecutive tiles into submissions by their estimated cost. Tiling
 * changes only where the invocations are launched from, never what they compute.
 *
 * Owned by one node, used on that node's Execute thread only.
 */
class TiledDispatch {
public:
    static constexpr uint32_t kFlights = static_cast<uint32_t>(FrameCommandTimingLedger::kFlightCount);
    /// Smallest tile edge in workgroups. Below it every extra command only adds the device's
    /// launch latency (a translated device lost the device at 3,969 commands, gpuyield-run4).
    static constexpr uint32_t kLaunchFloor = 7;

    /// What the rule assumes about a pass it has not measured yet.
    enum class Prior {
        /// The frame's primary pass, the heaviest by construction: launches at the floor, and on a
        /// translated device is always tiled to the submission budget (its long dispatches lose
        /// that device).
        Heaviest,
        /// Any other pass. Native: launches cut by the square of the watchdog-to-window ratio
        /// (frame budget over submission budget; 81 tiles with the defaults, whatever the grid
        /// shape), so a pass far over the window still returns in time to be measured.
        /// Translated: launches as one dispatch (there the hazard is the
        /// command count). On every device a measured pass that fits the whole-pass window is one
        /// dispatch, as it was before the rule existed.
        Unknown,
    };

    struct Extent {
        uint32_t x = 0, y = 0, z = 1;
    };
    struct Capacity {
        uint64_t frameBudgetNs = 0;
        uint64_t submissionBudgetNs = 0;
    };

    /// Tile edge an unmeasured `Prior::Unknown` pass of `groups` workgroups launches with.
    [[nodiscard]] static uint32_t ColdSplitEdge(Extent groups, Capacity capacity, bool native);

    /// Edge of the tiles that hold `tileGroups` workgroups a layer on a `groups` grid. A grid whose
    /// short side is below the square edge (a strip, a list dispatched along one axis) gets longer
    /// tiles, not more of them.
    [[nodiscard]] static uint32_t TileEdge(Extent groups, uint64_t tileGroups);
    /// The tile size to plan when the rule wants `wanted` workgroups a tile and the last readback
    /// measured tiles of `measured`. While the launch latency is unknown a refinement is kept
    /// between 2x (two tile sizes that far apart are what measures the latency) and 4x (so the
    /// pass is not cut into hundreds of commands before their cost is known).
    [[nodiscard]] static uint64_t RefinedTileGroups(uint64_t wanted, uint64_t measured, bool latencyKnown);

    TiledDispatch();
    ~TiledDispatch();
    TiledDispatch(const TiledDispatch&) = delete;
    TiledDispatch& operator=(const TiledDispatch&) = delete;

    /// Compile-time wiring. `persisted` (may be null) keeps the learned envelope across runs
    /// (R503); `trace` (may be null) receives the native-benchmark `[GPUYIELD*]` lines.
    void Configure(Vixen::Vulkan::Resources::VulkanDevice* device, VkCommandPool pool, std::string pass,
                   Prior prior, SimpleTaskProfile* persisted, Vixen::Log::Logger* trace);

    /// The node's pipeline inputs changed: `tileable` says whether the SPIR-V survives a based
    /// dispatch (no NumWorkgroups dependency). `forgetEnvelope` drops the cost learned so far
    /// (another shader); without it the envelope stays as the seed of the next plan.
    void SetPipeline(bool tileable, bool forgetEnvelope);
    [[nodiscard]] bool Tileable() const { return tileable_; }

    /// Read the timestamps of `flight`'s previous use (call once its completion was waited),
    /// feed the cost envelope and report each measured submission to the submit guard.
    void Collect(uint32_t flight, Vixen::Log::Logger* reporter);

    /// Decide `flight`'s tiles for a dispatch of `groups` workgroups; `first` is the node's own
    /// command buffer, not yet begun. False with `failure` set when Vulkan refused a resource.
    [[nodiscard]] bool Plan(uint32_t flight, VkCommandBuffer first, Extent groups, Capacity capacity,
                            std::string& failure);
    /// `flight` records one command the node cannot split (an external recorder, a disabled dispatch).
    void PlanOpaque(uint32_t flight, VkCommandBuffer only, Capacity capacity);

    /// The primary command buffers of the last plan, in submission order.
    [[nodiscard]] const std::vector<VkCommandBuffer>& Commands() const { return commands_; }

    /// After `first` was begun: open the first tile's interval.
    void OpenFirst(VkCommandBuffer first);
    /// Record every tile. `cmd` enters as the first command buffer with the pipeline state bound
    /// and leaves as the last one; `rebind` restores that state on each following buffer.
    [[nodiscard]] bool Dispatch(VkCommandBuffer& cmd, const std::function<void(VkCommandBuffer)>& rebind,
                                std::string& failure);
    /// Before `last` is ended: close the last tile's interval.
    void CloseLast(VkCommandBuffer last);

    /// Hand the planned commands' cost estimates to the queue owner and remember how it batches them.
    void Describe(uint32_t flight, SubmitRecord& record);

    /// Final readback, then free every tile resource (cleanup, device still alive).
    void Release(Vixen::Log::Logger* reporter);

private:
    struct Flight {
        std::vector<VkCommandBuffer> extraCommands;
        std::unique_ptr<Vixen::Vulkan::Resources::GPUTimestampQuery> query;
        uint32_t queryCapacity = 0;
        uint32_t tiles = 0;            ///< 0: an opaque flight, nothing to read back.
        uint64_t budgetNs = 0;
        uint32_t nominalGroups = 0;    ///< Workgroups of an unclipped tile.
        uint64_t nominalLayerGroups = 0;  ///< The same for one layer (the unit TileEdge sizes).
        std::vector<uint32_t> tileGroups;
        std::vector<SubmitRecord::Batch> batches;
        std::string extentKey;
    };

    void SeedEnvelope(const std::string& extentKey);
    void Trace(const std::string& line) const;

    Vixen::Vulkan::Resources::VulkanDevice* device_ = nullptr;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    std::string pass_;
    Prior prior_ = Prior::Unknown;
    SimpleTaskProfile* persisted_ = nullptr;
    Vixen::Log::Logger* trace_ = nullptr;
    bool tileable_ = false;
    int native_ = -1;                  ///< Device class, resolved at the first plan.

    std::array<Flight, kFlights> flights_;
    FrameCommandTimingLedger timings_;
    GpuChunkCost cost_;
    std::string calibrationKey_;       ///< "WxH" workgroup extent the envelope was learned for.
    uint64_t measuredLayerGroups_ = 0; ///< Tile size (one layer) of the last flight read back.

    // The plan being recorded.
    std::vector<VkCommandBuffer> commands_;
    uint32_t planFlight_ = 0;
    Extent planGroups_;
    uint32_t planEdge_ = 1;

    // Witness: the longest submission and the most submissions of one frame, printed at release.
    static constexpr uint32_t kReportedReads = 16;
    uint32_t reportedReads_ = 0;
    uint64_t reads_ = 0;
    double worstSubmissionMs_ = 0.0;
    std::size_t mostSubmissions_ = 0;
    uint32_t mostTiles_ = 0;
};

}  // namespace Vixen::RenderGraph
