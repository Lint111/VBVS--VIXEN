#include "PerfCsvWriter.h"
#include "Core/GPUPerformanceLogger.h"

#include <cstdlib>  // std::getenv
#include <numeric>  // std::accumulate

PerfCsvWriter::PerfCsvWriter() {
    if (const char* env = std::getenv("VIXEN_PERF_CSV")) {
        path_ = env;
    }
}

void PerfCsvWriter::RecordFrame(double cpuFrameTimeMs, const std::vector<PassSource>& passes,
                                uint64_t bootBytesUploaded, uint64_t steadyStateBytesUploaded,
                                double wholeFrameGpuSpanMs,
                                WholesaleMetrics wholesale) {
    if (!IsEnabled()) return;

    recentFrameTimesMs_[frameCounter_ % kFpsWindow] = cpuFrameTimeMs;
    ++frameCounter_;

    Row row;
    row.frameIndex = frameCounter_;
    row.cpuFrameTimeMs = cpuFrameTimeMs;
    row.bootBytesUploaded = bootBytesUploaded;
    row.steadyStateBytesUploaded = steadyStateBytesUploaded;
    row.wholeFrameGpuSpanMs = wholeFrameGpuSpanMs;
    row.wholesale = wholesale;

    // Steady-state FPS: rolling average over the last kFpsWindow frames (or however many
    // have run so far, before the window fills) — mirrors VulkanApplicationBase::FrameTimer's
    // avg-FPS window so the two "frames per second" numbers in the ledger agree.
    const size_t sampleCount = frameCounter_ < kFpsWindow ? static_cast<size_t>(frameCounter_) : kFpsWindow;
    const double sum = std::accumulate(recentFrameTimesMs_, recentFrameTimesMs_ + sampleCount, 0.0);
    const double avgMs = sampleCount > 0 ? sum / static_cast<double>(sampleCount) : 0.0;
    row.steadyStateFps = avgMs > 0.0 ? 1000.0 / avgMs : 0.0;

    row.passMs.reserve(passes.size());
    for (const auto& pass : passes) {
        const double ms = pass.logger ? static_cast<double>(pass.logger->GetLastDispatchMs()) : 0.0;
        row.passMs.emplace_back(pass.columnName, ms);
    }

    rows_.push_back(std::move(row));
}

void PerfCsvWriter::Flush() {
    if (!IsEnabled() || flushed_) return;
    flushed_ = true;

    std::ofstream out(path_);
    if (!out.is_open()) return;  // best-effort: don't throw during shutdown

    // Header. Per-pass columns are named from the first row (every row records the same
    // pass set — RecordFrame's caller passes the same `passes` vector every frame).
    // E21-S1: keep the wholesale ledger schema stable across the slice ladder.  The
    // node-specific values are supplied by later admission slices; zero is an honest
    // value for a field which has not been admitted on a given frame, and avoids
    // changing the CSV shape between S1-S5.
    out << "scene_leg,frame,cpu_frame_time_ms,steady_state_fps,boot_bytes_uploaded,"
           "steady_state_bytes_uploaded,whole_frame_gpu_span_ms,wholesale_desired_mask,"
           "wholesale_ready_mask,wholesale_generation,allocated_capacity_bytes,"
           "populated_shader_readable_bytes,reusable_populated_bytes,boot_bytes_uploaded_ledger,"
           "steady_state_bytes_uploaded_ledger,whole_buffer_upload_bytes,"
           "channel_pool_populated_bytes,brick_lookup_populated_bytes,mip_pool_populated_bytes,"
           "tier_ref_populated_bytes,occupancy_grid_populated_bytes,resident_signature_fnv64";
    if (!rows_.empty()) {
        for (const auto& [name, _] : rows_.front().passMs) {
            out << "," << name << "_ms";
        }
    }
    out << "\n";

    for (const auto& row : rows_) {
        out << "S1," << row.frameIndex << "," << row.cpuFrameTimeMs << "," << row.steadyStateFps << ","
            << row.bootBytesUploaded << "," << row.steadyStateBytesUploaded << ","
            << row.wholeFrameGpuSpanMs
            << "," << row.wholesale.desiredMask << "," << row.wholesale.readyMask << ","
            << row.wholesale.generation << "," << row.wholesale.allocatedCapacityBytes << ","
            << row.wholesale.populatedShaderReadableBytes << "," << row.wholesale.reusablePopulatedBytes << ","
            << row.bootBytesUploaded << "," << row.steadyStateBytesUploaded << ","
            << row.wholesale.wholeBufferUploadBytes << ","
            << row.wholesale.channelPoolPopulatedBytes << "," << row.wholesale.brickLookupPopulatedBytes << ","
            << row.wholesale.mipPoolPopulatedBytes << "," << row.wholesale.tierRefPopulatedBytes << ","
            << row.wholesale.occupancyGridPopulatedBytes << "," << row.wholesale.residentSignatureFnv64;
        for (const auto& [_, ms] : row.passMs) {
            out << "," << ms;
        }
        out << "\n";
    }
}
