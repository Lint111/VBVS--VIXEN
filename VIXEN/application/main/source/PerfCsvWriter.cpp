#if UNDERSET_NATIVE_BENCHMARK
#include "PerfCsvWriter.h"
#include "Core/GPUPerformanceLogger.h"

#include <cstdio>
#include <fstream>
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
                                WindowMetrics windowMetrics,
                                WholesaleMetrics wholesale,
                                const FrameCounters& counters) {
    if (!IsEnabled()) return;

    recentFrameTimesMs_[frameCounter_ % kFpsWindow] = cpuFrameTimeMs;
    ++frameCounter_;

    Row row;
    row.frameIndex = frameCounter_;
    row.cpuFrameTimeMs = cpuFrameTimeMs;
    row.bootBytesUploaded = bootBytesUploaded;
    row.steadyStateBytesUploaded = steadyStateBytesUploaded;
    row.wholeFrameGpuSpanMs = wholeFrameGpuSpanMs;
    row.windowMetrics = windowMetrics;
    row.wholesale = wholesale;
    row.counters = counters;
    ++runSummary_.frames;
    runSummary_.windows += windowMetrics.windowCount;
    runSummary_.violations += windowMetrics.windowViolationCount;
    runSummary_.violationMs += windowMetrics.windowViolationMs;
    runSummary_.actualMs += windowMetrics.windowActualMs;
    runSummary_.unusedMs += windowMetrics.windowUnusedMs;
    runSummary_.gpuWaitMs += windowMetrics.gpuWaitMs;

    // Steady-state FPS: rolling average over the last kFpsWindow frames (or however many
    // have run so far, before the window fills) — mirrors VulkanApplicationBase::FrameTimer's
    // avg-FPS window so the two "frames per second" numbers in the ledger agree.
    const size_t sampleCount = frameCounter_ < kFpsWindow ? static_cast<size_t>(frameCounter_) : kFpsWindow;
    const double sum = std::accumulate(recentFrameTimesMs_, recentFrameTimesMs_ + sampleCount, 0.0);
    const double avgMs = sampleCount > 0 ? sum / static_cast<double>(sampleCount) : 0.0;
    row.steadyStateFps = avgMs > 0.0 ? 1000.0 / avgMs : 0.0;

    row.passMs.reserve(passes.size());
    for (const auto& pass : passes) {
        if (pass.logger && !pass.logger->HasQuerySlot() && warnedNoSlot_.insert(pass.columnName).second) {
            std::fprintf(stderr, "[PerfCsvWriter] column '%s' has no GPU query slot, so its 0 means unmeasured, "
                                 "not fast; raise maxQueryConsumers in DeviceNode.cpp\n", pass.columnName.c_str());
        }
        const double ms = pass.logger ? static_cast<double>(pass.logger->GetLastDispatchMs()) : 0.0;
        row.passMs.emplace_back(pass.columnName, ms);
    }

    rows_.push_back(std::move(row));
}

void PerfCsvWriter::RecordWindowSamples(const std::vector<WindowSample>& samples) {
    if (!IsEnabled()) return;
    for (WindowSample sample : samples) {
        sample.frame = frameCounter_;
        if (sample.cold) ++runSummary_.coldWindows;
        windowSamples_.push_back(sample);
    }
}

void PerfCsvWriter::Flush() {
    if (!IsEnabled() || flushed_) return;
    flushed_ = true;

    // used = actual - unused: a violating window has unused 0 and used = actual.
    const double used = runSummary_.actualMs - runSummary_.unusedMs;
    std::fprintf(stderr,
        "[WINDOWSUMMARY] frames=%llu windows=%llu cold=%llu violations=%llu violation_ms=%.6f "
        "actual_ms=%.6f used_ms=%.6f utilisation=%.6f gpu_wait_ms=%.6f\n",
        static_cast<unsigned long long>(runSummary_.frames),
        static_cast<unsigned long long>(runSummary_.windows),
        static_cast<unsigned long long>(runSummary_.coldWindows),
        static_cast<unsigned long long>(runSummary_.violations), runSummary_.violationMs,
        runSummary_.actualMs, used,
        runSummary_.actualMs > 0.0 ? used / runSummary_.actualMs : 0.0, runSummary_.gpuWaitMs);

    if (std::ofstream windows(path_ + ".windows.csv"); windows.is_open()) {
        windows << "frame,kind,command,cold,raw_predicted_ms,predicted_ms,actual_ms,violation\n";
        for (const auto& w : windowSamples_) {
            windows << w.frame << "," << w.kind << "," << w.command << "," << (w.cold ? 1 : 0) << ","
                    << w.rawPredictedMs << "," << w.predictedMs << "," << w.actualMs << ","
                    << (w.violation ? 1 : 0) << "\n";
        }
    }

    std::ofstream out(path_);
    if (!out.is_open()) return;  // best-effort: don't throw during shutdown

    // Header. Per-pass columns are named from the first row (every row records the same
    // pass set — RecordFrame's caller passes the same `passes` vector every frame).
    // E22-S2: keep the wholesale ledger schema stable across the slice ladder.  The
    // node-specific values are supplied by the admission node; reusable_populated_bytes
    // is the retained-payload reuse event metric, and zero is honest when absent, avoiding
    // changing the CSV shape between S1-S5.
    out << "scene_leg,frame,cpu_frame_time_ms,steady_state_fps,boot_bytes_uploaded,"
           "steady_state_bytes_uploaded,whole_frame_gpu_span_ms,wholesale_desired_mask,"
           "wholesale_ready_mask,wholesale_generation,allocated_capacity_bytes,"
           "populated_shader_readable_bytes,reusable_populated_bytes,boot_bytes_uploaded_ledger,"
           "steady_state_bytes_uploaded_ledger,whole_buffer_upload_bytes,"
           "channel_pool_populated_bytes,brick_lookup_populated_bytes,mip_pool_populated_bytes,"
           "tier_ref_populated_bytes,occupancy_grid_populated_bytes,resident_signature_fnv64,"
           "gpu_wait_ms,pace_wait_ms,gpu_bound_share,window_count,window_predicted_ms,"
           "window_actual_ms,window_error_p95_ms,window_error_p50_relative,"
           "window_error_p95_relative,window_error_p50_ms,window_violation_count,"
           "window_violation_ms,window_unused_ms,window_utilisation";
    if (!rows_.empty()) {
        for (const auto& [name, _] : rows_.front().passMs) {
            out << "," << name << "_ms";
        }
    }
    // Frame-pipeline counters sit after the variable pass columns so positional readers of the
    // fixed columns above keep working.
    out << ",mapped_buffer_accesses,mapped_buffer_bytes,descriptor_updates,command_recordings,"
           "queue_submits,gpu_retire_waits,frame_pace_waits";
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
            << row.wholesale.occupancyGridPopulatedBytes << "," << row.wholesale.residentSignatureFnv64
            << "," << row.windowMetrics.gpuWaitMs << "," << row.windowMetrics.paceWaitMs
            << "," << row.windowMetrics.gpuBoundShare << "," << row.windowMetrics.windowCount
            << "," << row.windowMetrics.windowPredictedMs << "," << row.windowMetrics.windowActualMs
            << "," << row.windowMetrics.windowErrorP95Ms
            << "," << row.windowMetrics.windowErrorP50Relative
            << "," << row.windowMetrics.windowErrorP95Relative
            << "," << row.windowMetrics.windowErrorP50Ms
            << "," << row.windowMetrics.windowViolationCount
            << "," << row.windowMetrics.windowViolationMs
            << "," << row.windowMetrics.windowUnusedMs
            << "," << row.windowMetrics.windowUtilisation;
        for (const auto& [_, ms] : row.passMs) {
            out << "," << ms;
        }
        out << "," << row.counters.mappedBufferAccesses << "," << row.counters.mappedBufferBytes
            << "," << row.counters.descriptorUpdates << "," << row.counters.commandRecordings
            << "," << row.counters.queueSubmits << "," << row.counters.gpuRetireWaits
            << "," << row.counters.framePaceWaits;
        out << "\n";
    }
}
#endif  // UNDERSET_NATIVE_BENCHMARK
