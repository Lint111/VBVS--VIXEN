#pragma once

#ifndef UNDERSET_NATIVE_BENCHMARK
#define UNDERSET_NATIVE_BENCHMARK 0
#endif

#if !UNDERSET_NATIVE_BENCHMARK

// The public type stays source-compatible for application code, while normal builds have no
// storage, environment access, CSV state, or out-of-line symbols for the recorder.
class PerfCsvWriter {
public:
    struct WholesaleMetrics {};
    struct PassSource {};

    constexpr PerfCsvWriter() noexcept = default;
    constexpr bool IsEnabled() const noexcept { return false; }
    template <typename... Args>
    constexpr void RecordFrame(Args&&...) noexcept {}
    constexpr void Flush() noexcept {}
    template <typename... Args>
    constexpr void RecordWindowSamples(Args&&...) noexcept {}
};

#else

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
#include <set>
#include <string>
#include <vector>

namespace Vixen::RenderGraph { class GPUPerformanceLogger; }

class PerfCsvWriter {
public:
    struct WholesaleMetrics {
        uint32_t desiredMask = 0;
        uint32_t readyMask = 0;
        uint32_t generation = 0;
        uint64_t allocatedCapacityBytes = 0;
        uint64_t populatedShaderReadableBytes = 0;
        uint64_t reusablePopulatedBytes = 0;
        uint64_t wholeBufferUploadBytes = 0;
        uint64_t channelPoolPopulatedBytes = 0;
        uint64_t brickLookupPopulatedBytes = 0;
        uint64_t mipPoolPopulatedBytes = 0;
        uint64_t tierRefPopulatedBytes = 0;
        uint64_t occupancyGridPopulatedBytes = 0;
        uint64_t residentSignatureFnv64 = 0;
    };
    // One GPU-timed pass to record each frame, named for its CSV column
    // (e.g. {"esvo_traverse_shade_ms", node->GetGPUPerformanceLogger()}).
    struct PassSource {
        std::string columnName;
        Vixen::RenderGraph::GPUPerformanceLogger* logger = nullptr;  // non-owning; may be nullptr (timing unsupported)
    };

    struct WindowMetrics {
        double gpuWaitMs = 0.0;
        double paceWaitMs = 0.0;
        double gpuBoundShare = 0.0;
        uint64_t windowCount = 0;
        double windowPredictedMs = 0.0;
        double windowActualMs = 0.0;
        double windowErrorP95Ms = 0.0;
        double windowErrorP50Relative = 0.0;
        double windowErrorP95Relative = 0.0;
        double windowErrorP50Ms = 0.0;
        // Violation = predicted > actual per window (must be 0); unused = actual - predicted
        // where actual >= predicted; utilisation = sum(min(predicted, actual)) / sum(actual).
        uint64_t windowViolationCount = 0;
        double windowViolationMs = 0.0;
        double windowUnusedMs = 0.0;
        double windowUtilisation = 0.0;
    };

    // One idle window, exported to "<VIXEN_PERF_CSV>.windows.csv" so misses can be attributed.
    struct WindowSample {
        uint64_t frame = 0;
        const char* kind = "";
        uint64_t command = 0;
        bool cold = false;
        double rawPredictedMs = 0.0;
        double predictedMs = 0.0;
        double actualMs = 0.0;
        bool violation = false;
    };

    // Reads VIXEN_PERF_CSV; IsEnabled()==false when unset (the only env access this class does).
    PerfCsvWriter();

    [[nodiscard]] bool IsEnabled() const { return !path_.empty(); }

    // Call once per frame (from PostTick, after RenderFrame — matches CollectResults timing,
    // so GetLastDispatchMs() reflects the frame that just completed). No-op if disabled.
    // wholeFrameGpuSpanMs: Task 0.1 -- first command-buffer start -> last command-buffer end
    // across every GPU-timed pass this frame (computed by the caller from the SAME `passes`
    // loggers' raw timestamps, since only the caller knows the full per-frame pass set and
    // their relative submission order). 0.0 if not computed/unavailable.
    void RecordFrame(double cpuFrameTimeMs, const std::vector<PassSource>& passes,
                     uint64_t bootBytesUploaded, uint64_t steadyStateBytesUploaded,
                     double wholeFrameGpuSpanMs,
                     WindowMetrics windowMetrics,
                     WholesaleMetrics wholesale);

    // Writes the accumulated rows to VIXEN_PERF_CSV. Safe to call once at shutdown; no-op if
    // disabled or already flushed (idempotent — DeInitialize() may run more than once-guarded,
    // but this class doesn't rely on that).
    void Flush();

    // Buffers this frame's window samples (call after RecordFrame; frame = the row just recorded).
    void RecordWindowSamples(const std::vector<WindowSample>& samples);

private:
    struct Row {
        uint64_t frameIndex = 0;
        double cpuFrameTimeMs = 0.0;
        double steadyStateFps = 0.0;
        uint64_t bootBytesUploaded = 0;
        uint64_t steadyStateBytesUploaded = 0;
        double wholeFrameGpuSpanMs = 0.0;  // Task 0.1: first CB start -> last CB end, this frame
        WindowMetrics windowMetrics;
        WholesaleMetrics wholesale;
        std::vector<std::pair<std::string, double>> passMs;  // (columnName, ms) — same set every row
    };

    std::string path_;
    std::vector<Row> rows_;
    std::vector<WindowSample> windowSamples_;
    // Whole-run window totals, every recorded frame from frame 0 (cold windows have no window and
    // are counted separately). Printed as one [WINDOWSUMMARY] line at Flush.
    struct RunWindowSummary {
        uint64_t frames = 0;
        uint64_t windows = 0;       ///< every wait recorded, cold ones included
        uint64_t coldWindows = 0;   ///< waits with no last-frame timing: no window, cannot violate
        uint64_t violations = 0;    ///< predicted > actual, over all frames
        double violationMs = 0.0;
        double actualMs = 0.0;
        double unusedMs = 0.0;
        double gpuWaitMs = 0.0;
    } runSummary_;
    bool flushed_ = false;
    std::set<std::string> warnedNoSlot_;  // columns already reported as slot-less

    // Rolling window for steady-state FPS (matches VulkanApplicationBase's FrameTimer window).
    static constexpr size_t kFpsWindow = 120;
    double recentFrameTimesMs_[kFpsWindow] = {};
    uint64_t frameCounter_ = 0;
};

#endif  // UNDERSET_NATIVE_BENCHMARK
