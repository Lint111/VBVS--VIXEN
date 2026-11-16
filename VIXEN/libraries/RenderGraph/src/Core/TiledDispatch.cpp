// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include "Core/TiledDispatch.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "Core/GPUPerformanceLogger.h"
#include "Core/TaskProfiles/SimpleTaskProfile.h"
#include "FrameTrace.h"
#include "GPUTimestampQuery.h"
#include "Logger.h"
#include "Nodes/Common/GpuDeviceClass.h"
#include "VulkanDevice.h"

namespace Vixen::RenderGraph {

namespace {
// VIXEN_GPUYIELD_EDGE=<n> pins the tile edge in workgroups (0 = one untiled dispatch); a
// diagnostic for separating tiling cost from shader cost, not a production control.
long EdgeOverride() {
    static const long value = [] {
        const char* text = std::getenv("VIXEN_GPUYIELD_EDGE");
        return text && *text ? std::strtol(text, nullptr, 10) : -1L;
    }();
    return value;
}

uint64_t SubmissionBudgetNs(const TiledDispatch::Capacity& capacity) {
    return std::max(uint64_t{1}, std::min(capacity.frameBudgetNs, capacity.submissionBudgetNs));
}

}  // namespace

// The cold split of an unmeasured pass. Native: each axis is cut by the ratio the whole-pass
// window already uses (watchdog : window == frame budget : submission budget), never below the
// launch floor. Translated: one dispatch.
uint32_t TiledDispatch::ColdSplitEdge(uint32_t wholeEdge, Capacity capacity, bool native) {
    if (!native) return std::max(1u, wholeEdge);
    const uint64_t submission = std::max(uint64_t{1}, capacity.submissionBudgetNs);
    const uint64_t cuts = std::max(uint64_t{1}, (capacity.frameBudgetNs + submission - 1) / submission);
    return std::max(kLaunchFloor, static_cast<uint32_t>((uint64_t{wholeEdge} + cuts - 1) / cuts));
}

TiledDispatch::TiledDispatch() = default;
TiledDispatch::~TiledDispatch() = default;

void TiledDispatch::Configure(Vixen::Vulkan::Resources::VulkanDevice* device, VkCommandPool pool,
                              std::string pass, ColdLaunch cold, SimpleTaskProfile* persisted,
                              Vixen::Log::Logger* trace) {
    device_ = device;
    pool_ = pool;
    pass_ = std::move(pass);
    cold_ = cold;
    persisted_ = persisted;
    trace_ = trace;
    native_ = -1;
    calibrationKey_.clear();
}

void TiledDispatch::SetPipeline(bool tileable) {
    tileable_ = tileable;
    cost_ = {};
    calibrationKey_.clear();  // re-enter from the persisted envelope (R503)
}

void TiledDispatch::Trace(const std::string& line) const {
    if (trace_ && trace_->IsEnabled() &&
        Vixen::Log::Logger::GetGlobalMinLevel() <= Vixen::Log::LogLevel::LOG_INFO)
        trace_->Info(line);
}

// Seed order: this extent's persisted envelope, then this process's envelope from the previous
// extent, then the nearest persisted extent. Per-workgroup cost belongs to the device, so a
// known device never restarts from the cold launch (frames before the first ring readback).
void TiledDispatch::SeedEnvelope(const std::string& extentKey) {
    calibrationKey_ = extentKey;
    const GpuChunkCost previous = cost_;
    cost_ = {};
    std::string seed = "cold";
    if (persisted_) {
        if (const auto restored = persisted_->GetChunkCalibration(extentKey)) {
            cost_.Import(restored->anchorUnits, restored->unitNs, restored->fixedNs,
                         restored->meanUnitNs, restored->meanFixedNs);
            seed = "persisted";
        }
    }
    if (!cost_.Calibrated() && previous.Calibrated()) {
        cost_.Import(previous.AnchorUnits(), previous.UnitNs(), previous.FixedNs(),
                     previous.MeanUnitNs(), previous.MeanFixedNs());
        seed = "previous-extent";
    }
    if (!cost_.Calibrated() && persisted_) {
        if (const auto neighbour = persisted_->NearestChunkCalibration(extentKey)) {
            cost_.Import(neighbour->anchorUnits, neighbour->unitNs, neighbour->fixedNs,
                         neighbour->meanUnitNs, neighbour->meanFixedNs);
            seed = "nearest-persisted";
        }
    }
#if UNDERSET_NATIVE_BENCHMARK
    Trace("[GPUYIELD_CALIBRATION] node=" + pass_ + " extent=" + extentKey +
          (cost_.Calibrated() ? " start=warm seed=" + seed + " anchor_units=" + std::to_string(cost_.AnchorUnits()) +
              " unit_ns=" + std::to_string(cost_.UnitNs()) + " fixed_ns=" + std::to_string(cost_.FixedNs())
              : std::string(" start=cold")));
#else
    (void)seed;
#endif
}

void TiledDispatch::Collect(uint32_t flight, Vixen::Log::Logger* reporter) {
    if (flight >= kFlights) return;
    Flight& state = flights_[flight];
    if (!state.tiles || !state.query || !state.query->ReadResults(0)) return;
    auto& query = *state.query;

    timings_.Begin(flight);
    // The full-tile mean feeds the yield rule (not a measurement hook), so it is never gated.
    double fullTileMs = 0;
    uint32_t fullTiles = 0;
    for (uint32_t i = 0; i < state.tiles; ++i) {
        const double ms = query.GetElapsedMs(0, i * 4, i * 4 + 3);
#if UNDERSET_NATIVE_BENCHMARK
        if (trace_ && std::getenv("VIXEN_GPUYIELD_TRACE")) {
            Trace("[GPUYIELD_CHUNK] node=" + pass_ +
                " flight=" + std::to_string(flight) + " chunk=" + std::to_string(i) +
                " start_ticks=" + std::to_string(query.GetRawTimestampTicks(0, i * 4)) +
                " end_ticks=" + std::to_string(query.GetRawTimestampTicks(0, i * 4 + 3)) +
                " dispatch_start_ticks=" + std::to_string(query.GetRawTimestampTicks(0, i * 4 + 1)) +
                " dispatch_end_ticks=" + std::to_string(query.GetRawTimestampTicks(0, i * 4 + 2)) +
                " dispatch_gpu_ms=" + std::to_string(query.GetElapsedMs(0, i * 4 + 1, i * 4 + 2)) +
                " elapsed_gpu_ms=" + std::to_string(ms));
        }
#endif
        if (i >= state.tileGroups.size() || !state.tileGroups[i]) continue;
        const uint32_t groups = state.tileGroups[i];
        timings_.Record(flight, i, groups, query.GetElapsedNs(0, i * 4, i * 4 + 3));
        if (groups == state.nominalGroups) {
            fullTileMs += ms;
            ++fullTiles;
        }
        cost_.Observe(groups, state.nominalGroups, ms * 1.0e6);
    }
    if (fullTiles) cost_.ObserveScale(state.nominalGroups, fullTileMs * 1.0e6 / fullTiles);
    // R503: publish the learned envelope to the task profile; CalibrationStore persists it per
    // device at shutdown, keyed by this flight's extent.
    if (persisted_ && cost_.Calibrated() && state.extentKey == calibrationKey_)
        persisted_->SetChunkCalibration(state.extentKey,
            {cost_.AnchorUnits(), cost_.UnitNs(), cost_.FixedNs(), cost_.MeanUnitNs(), cost_.MeanFixedNs()});

    double worstMs = 0;
#if UNDERSET_NATIVE_BENCHMARK
    double totalMs = 0;
#endif
    for (const auto& batch : state.batches) {
        if (!batch.count || batch.first + batch.count > state.tiles) continue;
        const double elapsed = query.GetElapsedMs(0, static_cast<uint32_t>(batch.first * 4),
            static_cast<uint32_t>((batch.first + batch.count) * 4 - 1));
        worstMs = std::max(worstMs, elapsed);
        if (reporter) GPUPerformanceLogger::RecordSubmission(*reporter, pass_, elapsed);
        uint64_t groups = 0;
        for (std::size_t i = batch.first; i < batch.first + batch.count && i < state.tileGroups.size(); ++i)
            groups += state.tileGroups[i];
        if (groups) cost_.ObserveBatch(groups, batch.count, elapsed * 1.0e6);
#if UNDERSET_NATIVE_BENCHMARK
        totalMs += elapsed;
        if (trace_ && std::getenv("VIXEN_GPUYIELD_TRACE")) {
            Trace("[GPUYIELD_SUBMISSION] node=" + pass_ +
                " first_chunk=" + std::to_string(batch.first) +
                " chunks=" + std::to_string(batch.count) +
                " elapsed_gpu_ms=" + std::to_string(elapsed));
        }
#endif
    }
    ++reads_;
    if (worstMs > worstSubmissionMs_) worstSubmissionMs_ = worstMs;
    if (state.batches.size() > mostSubmissions_) {
        mostSubmissions_ = state.batches.size();
        mostTiles_ = state.tiles;
    }
    // stdout, not the node logger: capture executables print only the application logger at INFO.
    if (reportedReads_ < kReportedReads) {
        ++reportedReads_;
        std::printf("[GPU_SUBMIT_TIME] pass=%s submissions=%zu chunks=%u gpu_ms=%.3f\n",
            pass_.c_str(), state.batches.size(), state.tiles, worstMs);
        std::fflush(stdout);
    }
#if UNDERSET_NATIVE_BENCHMARK
    Trace("[GPUYIELD] node=" + pass_ +
        " submissions=" + std::to_string(state.batches.size()) +
        " chunks=" + std::to_string(state.tiles) +
        " total_gpu_ms=" + std::to_string(totalMs) +
        " max_submission_ms=" + std::to_string(worstMs) +
        " budget_ms=" + std::to_string(state.budgetNs / 1.0e6) +
        " launch_latency_ms=" + std::to_string(cost_.LaunchLatencyNs() / 1.0e6) +
        " device_class=" + GpuDeviceClassName(*device_));
#endif
}

void TiledDispatch::PlanOpaque(uint32_t flight, VkCommandBuffer only, Capacity capacity) {
    commands_.assign(1, only);
    if (flight >= kFlights) return;
    planFlight_ = flight;
    Flight& state = flights_[flight];
    // The node's own whole-pass interval measures an opaque command; an earlier tiled recording
    // on this flight must not be read back again.
    state.tiles = 0;
    state.tileGroups.clear();
    state.batches.clear();
    state.budgetNs = SubmissionBudgetNs(capacity);
}

bool TiledDispatch::Plan(uint32_t flight, VkCommandBuffer first, Extent groups, Capacity capacity,
                         std::string& failure) {
    commands_.assign(1, first);
    if (flight >= kFlights || !device_ || !groups.x || !groups.y || !groups.z) {
        failure = "tile plan without a device, flight or dispatch extent";
        return false;
    }
    planFlight_ = flight;
    planGroups_ = groups;
    Flight& state = flights_[flight];

    // Keep the frame budget separate: smaller chunks do not make an expensive pass cheap.
    const uint64_t budgetNs = SubmissionBudgetNs(capacity);
    // R503: the envelope is learned per extent, so a known resolution starts warm.
    const std::string extentKey = std::to_string(groups.x) + "x" + std::to_string(groups.y);
    if (extentKey != calibrationKey_) SeedEnvelope(extentKey);
    state.extentKey = extentKey;

    if (native_ < 0) native_ = GpuDeviceClassIsNative(GpuDeviceClassName(*device_)) ? 1 : 0;
    const uint32_t wholeEdge = std::max(1u, std::max(groups.x, groups.y));
    // Native devices untile a pass whose projected whole time is far below the watchdog window;
    // translated devices keep the submission-budget tiling. Same rule for every dispatch node.
    const GpuChunkCost::WholePass whole{uint64_t(groups.x) * groups.y * groups.z,
        NativeWholePassWindowNs(capacity.submissionBudgetNs, capacity.frameBudgetNs)};
    const uint32_t launch = cold_ == ColdLaunch::Floor ? 0u : ColdSplitEdge(wholeEdge, capacity, native_ == 1);
    const long edgeOverride = EdgeOverride();
    // A tile of edge e holds e*e*z workgroups, so the per-layer budget sizes the edge.
    planEdge_ = !tileable_ || edgeOverride == 0
        ? wholeEdge
        : edgeOverride > 0
            ? static_cast<uint32_t>(edgeOverride)
            : cost_.Edge(std::max(uint64_t{1}, budgetNs / groups.z), wholeEdge, kLaunchFloor,
                         native_ ? &whole : nullptr, launch);
    const uint32_t edge = planEdge_;
    state.nominalGroups = std::min(edge, groups.x) * std::min(edge, groups.y) * groups.z;
    const uint32_t count = std::max(1u, ((groups.x + edge - 1) / edge) * ((groups.y + edge - 1) / edge));

    auto& extras = state.extraCommands;
    if (extras.size() + 1 < count) {
        const uint32_t needed = count - 1 - static_cast<uint32_t>(extras.size());
        std::vector<VkCommandBuffer> allocated(needed);
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = pool_;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = needed;
        if (vkAllocateCommandBuffers(device_->device, &allocation, allocated.data()) != VK_SUCCESS) {
            failure = "tile command allocation failed";
            return false;
        }
        extras.insert(extras.end(), allocated.begin(), allocated.end());
    }
    if (count > 1) commands_.insert(commands_.end(), extras.begin(), extras.begin() + (count - 1));
    if (state.queryCapacity < count) {
        if (state.query) {
            // Release recorded pool references before replacing their backing storage.
            bool reset = vkResetCommandBuffer(first, 0) == VK_SUCCESS;
            for (VkCommandBuffer buffer : extras) reset = vkResetCommandBuffer(buffer, 0) == VK_SUCCESS && reset;
            if (!reset) {
                failure = "query-growth command reset failed";
                return false;
            }
        }
        state.queryCapacity = std::max(count, state.queryCapacity * 2u);
        state.query = std::make_unique<Vixen::Vulkan::Resources::GPUTimestampQuery>(
            device_, 1, state.queryCapacity * 4);
    }
    state.tiles = count;
    state.budgetNs = cost_.SubmissionBudgetNs(budgetNs);
    state.tileGroups.clear();
    state.batches.clear();
    return true;
}

void TiledDispatch::OpenFirst(VkCommandBuffer first) {
    Flight& state = flights_[planFlight_];
    if (!state.tiles || !state.query) return;
    state.query->ResetQueryRange(first, 0, 0, 4);
    state.query->WriteTimestamp(first, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
}

bool TiledDispatch::Dispatch(VkCommandBuffer& cmd, const std::function<void(VkCommandBuffer)>& rebind,
                             std::string& failure) {
    Flight& state = flights_[planFlight_];
    auto& query = *state.query;
    const Extent groups = planGroups_;
    const uint32_t edge = planEdge_;
    const uint32_t count = state.tiles;
    const VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    uint32_t tile = 0;
    for (uint32_t baseY = 0; baseY < groups.y; baseY += edge) {
        for (uint32_t baseX = 0; baseX < groups.x; baseX += edge) {
            const uint32_t tileX = std::min(edge, groups.x - baseX);
            const uint32_t tileY = std::min(edge, groups.y - baseY);
            state.tileGroups.push_back(tileable_ ? tileX * tileY * groups.z : groups.x * groups.y * groups.z);
            query.WriteTimestamp(cmd, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, tile * 4 + 1);
            if (tileable_) {
                vkCmdDispatchBase(cmd, baseX, baseY, 0, tileX, tileY, groups.z);
            } else {
                vkCmdDispatch(cmd, groups.x, groups.y, groups.z);
            }
            query.WriteTimestamp(cmd, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, tile * 4 + 2);
            if (++tile != count) {
                query.WriteTimestamp(cmd, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, tile * 4 - 1);
                if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
                    failure = "failed to end tile commands";
                    return false;
                }
                cmd = commands_[tile];
                VIXEN_FRAME_COUNT(CommandRecordings, 1);
                if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
                    failure = "failed to begin tile commands";
                    return false;
                }
                // Each retired command owns four queries. Resetting the entire
                // capacity in chunk zero makes instrumentation grow with tile count.
                query.ResetQueryRange(cmd, 0, tile * 4, 4);
                query.WriteTimestamp(cmd, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, tile * 4);
                rebind(cmd);
            }
        }
    }
    return true;
}

void TiledDispatch::CloseLast(VkCommandBuffer last) {
    Flight& state = flights_[planFlight_];
    if (!state.tiles || !state.query) return;
    state.query->WriteTimestamp(last, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, state.tiles * 4 - 1);
}

void TiledDispatch::Describe(uint32_t flight, SubmitRecord& record) {
    if (flight >= kFlights) return;
    Flight& state = flights_[flight];
    record.submissionBudgetNs = state.budgetNs;
    for (std::size_t command = 0; command < state.tileGroups.size(); ++command) {
        const uint32_t groups = state.tileGroups[command];
        record.commandCostUpperBoundsNs.push_back(cost_.Estimate(groups));
        const uint64_t lastFrame = timings_.Last(flight, command, groups);
        record.commandCostMeanNs.push_back(lastFrame ? lastFrame : cost_.MeanEstimate(groups));
    }
    if (state.tiles) state.batches = record.SubmissionBatches();
}

void TiledDispatch::Release(Vixen::Log::Logger* reporter) {
    if (device_ && device_->device != VK_NULL_HANDLE) {
        for (uint32_t flight = 0; flight < kFlights; ++flight) Collect(flight, reporter);
        for (Flight& state : flights_) {
            if (!state.extraCommands.empty() && pool_ != VK_NULL_HANDLE)
                vkFreeCommandBuffers(device_->device, pool_,
                                     static_cast<uint32_t>(state.extraCommands.size()), state.extraCommands.data());
        }
    }
    for (Flight& state : flights_) state = Flight{};
    cost_ = {};
    commands_.clear();
    if (reads_) {
        // One line per pass and compile: the worst submission the guard saw and the most
        // submissions one frame needed (the cost of the bound).
        std::printf("[GPU_SUBMIT_WORST] pass=%s reads=%llu worst_gpu_ms=%.3f most_submissions=%zu chunks=%u\n",
            pass_.c_str(), static_cast<unsigned long long>(reads_), worstSubmissionMs_, mostSubmissions_, mostTiles_);
        std::fflush(stdout);
    }
    reads_ = 0;
    reportedReads_ = 0;
    worstSubmissionMs_ = 0.0;
    mostSubmissions_ = 0;
    mostTiles_ = 0;
}

}  // namespace Vixen::RenderGraph
