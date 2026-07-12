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
                                uint64_t bootBytesUploaded, uint64_t steadyStateBytesUploaded) {
    if (!IsEnabled()) return;

    recentFrameTimesMs_[frameCounter_ % kFpsWindow] = cpuFrameTimeMs;
    ++frameCounter_;

    Row row;
    row.frameIndex = frameCounter_;
    row.cpuFrameTimeMs = cpuFrameTimeMs;
    row.bootBytesUploaded = bootBytesUploaded;
    row.steadyStateBytesUploaded = steadyStateBytesUploaded;

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
    out << "frame,cpu_frame_time_ms,steady_state_fps,boot_bytes_uploaded,steady_state_bytes_uploaded";
    if (!rows_.empty()) {
        for (const auto& [name, _] : rows_.front().passMs) {
            out << "," << name << "_ms";
        }
    }
    out << "\n";

    for (const auto& row : rows_) {
        out << row.frameIndex << "," << row.cpuFrameTimeMs << "," << row.steadyStateFps << ","
            << row.bootBytesUploaded << "," << row.steadyStateBytesUploaded;
        for (const auto& [_, ms] : row.passMs) {
            out << "," << ms;
        }
        out << "\n";
    }
}
