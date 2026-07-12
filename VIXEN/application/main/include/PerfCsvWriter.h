#pragma once

// Lazy-Procedural-Delta-Baseline Inc1 M4 Task 6b: a lightweight, always-available (NOT
// demo-gated) perf recorder. Records one row per frame (CPU frame time, per-pass GPU
// dispatch time(s), cumulative bytes-uploaded, running FPS) and writes it out as a CSV on
// Flush() (called from DeInitialize()). No-op unless VIXEN_PERF_CSV is set, so it never
// perturbs a normal run — matches the VIXEN_* env-knob convention used throughout
// VulkanGraphApplication.cpp/BuildRenderGraph.cpp.
//
// Source for the perf ledger's GPU columns: reuses ComputeDispatchNode's existing
// gpuPerfLogger_ (GetGPUPerformanceLogger()->GetLastDispatchMs()) rather than adding new
// vkCmdWriteTimestamp plumbing — every dispatch/render/UI node already exposes one. The
// caller passes in whichever named passes it wants recorded (today: "test_dispatch", the
// single ESVO-traverse+shade compute dispatch; a future split traverse/shade or
// recipe-eval pass just adds another column by construction name).

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace Vixen::RenderGraph { class GPUPerformanceLogger; }

class PerfCsvWriter {
public:
    // One GPU-timed pass to record each frame, named for its CSV column
    // (e.g. {"esvo_traverse_shade_ms", node->GetGPUPerformanceLogger()}).
    struct PassSource {
        std::string columnName;
        Vixen::RenderGraph::GPUPerformanceLogger* logger = nullptr;  // non-owning; may be nullptr (timing unsupported)
    };

    // Reads VIXEN_PERF_CSV; IsEnabled()==false when unset (the only env access this class does).
    PerfCsvWriter();

    [[nodiscard]] bool IsEnabled() const { return !path_.empty(); }

    // Call once per frame (from PostTick, after RenderFrame — matches CollectResults timing,
    // so GetLastDispatchMs() reflects the frame that just completed). No-op if disabled.
    void RecordFrame(double cpuFrameTimeMs, const std::vector<PassSource>& passes,
                     uint64_t bootBytesUploaded, uint64_t steadyStateBytesUploaded);

    // Writes the accumulated rows to VIXEN_PERF_CSV. Safe to call once at shutdown; no-op if
    // disabled or already flushed (idempotent — DeInitialize() may run more than once-guarded,
    // but this class doesn't rely on that).
    void Flush();

private:
    struct Row {
        uint64_t frameIndex = 0;
        double cpuFrameTimeMs = 0.0;
        double steadyStateFps = 0.0;
        uint64_t bootBytesUploaded = 0;
        uint64_t steadyStateBytesUploaded = 0;
        std::vector<std::pair<std::string, double>> passMs;  // (columnName, ms) — same set every row
    };

    std::string path_;
    std::vector<Row> rows_;
    bool flushed_ = false;

    // Rolling window for steady-state FPS (matches VulkanApplicationBase's FrameTimer window).
    static constexpr size_t kFpsWindow = 120;
    double recentFrameTimesMs_[kFpsWindow] = {};
    uint64_t frameCounter_ = 0;
};
