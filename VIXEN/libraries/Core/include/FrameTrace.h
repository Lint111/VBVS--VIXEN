#pragma once

// FrameTrace: a per-frame CPU/GPU interval log for measuring where a frame's CPU time goes.
//
// Benchmark builds only (UNDERSET_NATIVE_BENCHMARK=1): in every other build the call surface is
// empty inline functions and an empty macro, so no clock read, branch or storage remains. At run
// time it is also off unless VIXEN_FRAME_TRACE is set ("1" writes frame-trace.csv into
// VIXEN_LOOKDEV_CAPTURE_DIR, anything else is taken as the output path).
//
// Every record is one interval {frame, thread, depth, category, name, t0, t1} on the steady clock
// in nanoseconds. Nested scopes on one thread carry their nesting depth, so a reader can derive
// self time. GPU pass intervals are mapped to the same clock through calibrated timestamps.
// Recording is thread-local append (PerThreadLog); nothing is locked on the hot path.

#include <cstdint>
#include <optional>
#include <string_view>

#ifndef UNDERSET_NATIVE_BENCHMARK
#define UNDERSET_NATIVE_BENCHMARK 0
#endif

#if UNDERSET_NATIVE_BENCHMARK

namespace Vixen::FrameTrace {

[[nodiscard]] bool Enabled();
[[nodiscard]] uint64_t NowNs();

/// The frame number stamped on records that follow (set once per tick by the application loop).
void SetFrame(uint64_t frame);

/// Append one interval at the current thread's nesting depth.
void Add(const char* category, std::string_view name, uint64_t t0Ns, uint64_t t1Ns);

/// Append a GPU pass interval as raw device ticks (category "gpuraw"). A reader maps it to the
/// steady clock with the calibration pairs below.
void AddGpuRaw(std::string_view name, uint64_t t0DeviceTicks, uint64_t t1DeviceTicks);

/// Calibrated-timestamp support: 0 none, 1 VK_KHR_calibrated_timestamps, 2 VK_EXT_calibrated_timestamps.
void SetCalibratedTimestampExtension(int extension);
[[nodiscard]] int CalibratedTimestampExtension();

/// Record one calibration pair (category "calib"): a device tick value and the steady-clock ns
/// taken together. The reader fits host ns = a + b * ticks over all pairs.
void AddCalibration(uint64_t deviceTicks, uint64_t hostNs);
/// Steady-clock ns of a Windows QueryPerformanceCounter reading (identity elsewhere).
[[nodiscard]] uint64_t HostCounterToNs(uint64_t counter);

/// Write frame-trace.csv and the per-thread CPU census; no-op unless Enabled().
void Write();

/// Per-frame event counters (design fb-framepipe1 section 5.1, R851.7). Always counted in benchmark
/// builds, independent of Enabled(), because the perf CSV reads them: a relaxed atomic add per
/// event, no allocation, no lock.
enum class Counter : uint32_t {
    MappedBufferAccesses,  ///< PerFrameResources::GetUniformBufferMapped calls (proxy for buffers written)
    MappedBufferBytes,     ///< size of the buffers those calls returned
    DescriptorUpdates,     ///< vkUpdateDescriptorSets calls
    CommandRecordings,     ///< vkBeginCommandBuffer calls on node paths
    QueueSubmits,          ///< vkQueueSubmit2 calls through the submit channel
    Count_
};
inline constexpr uint32_t kCounterCount = static_cast<uint32_t>(Counter::Count_);

void CountAdd(Counter counter, uint64_t amount);

/// GPU ledger hooks (sn-gpumem1). Forwarded to VK_LAYER_VIXEN_gpu_ledger when that layer is loaded in this
/// process (tools/gpu-ledger-layer, enabled by VIXEN_GPU_LEDGER=1); a no-op otherwise. The scope names the node
/// whose Setup/Compile/Execute is running, so the layer can attribute allocations and uploads; Touch marks a
/// mapped ring slot the CPU may write this frame; SetFrame (above) closes the previous frame in the layer.
void LedgerPushScope(const char* name);
void LedgerPopScope();
void LedgerTouch(const void* mapped, uint64_t bytes);
/// The bytes a node actually copied into a mapped slot (reported by the sites that copy a variable amount).
void LedgerWritten(const void* mapped, uint64_t bytes);
void LedgerFlush();

class LedgerScope {
public:
    explicit LedgerScope(const char* name) { LedgerPushScope(name); }
    LedgerScope(const LedgerScope&) = delete;
    LedgerScope& operator=(const LedgerScope&) = delete;
    ~LedgerScope() { LedgerPopScope(); }
};

/// The counts since the previous call; resets them. Called once per frame by the perf CSV recorder.
struct FrameCounts {
    uint64_t value[kCounterCount] = {};
};
[[nodiscard]] FrameCounts TakeFrameCounts();

class Scope {
public:
    Scope(const char* category, std::string_view name);
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope();

private:
    const char* category_;
    char name_[48];
    uint64_t t0_;
    bool active_;
};

}  // namespace Vixen::FrameTrace

#define VIXEN_FRAME_TRACE_CONCAT_INNER(a, b) a##b
#define VIXEN_FRAME_TRACE_CONCAT(a, b) VIXEN_FRAME_TRACE_CONCAT_INNER(a, b)
#define VIXEN_FRAME_TRACE_SCOPE(category, name) \
    ::Vixen::FrameTrace::Scope VIXEN_FRAME_TRACE_CONCAT(vixenFrameTraceScope, __LINE__)((category), (name))
// A span that does not match a C++ block: open with BEGIN(var, ...), close with END(var).
#define VIXEN_FRAME_TRACE_BEGIN(var, category, name) \
    std::optional<::Vixen::FrameTrace::Scope> var; \
    (var).emplace((category), (name))
#define VIXEN_FRAME_TRACE_END(var) (var).reset()
#define VIXEN_LEDGER_SCOPE(name) \
    ::Vixen::FrameTrace::LedgerScope VIXEN_FRAME_TRACE_CONCAT(vixenLedgerScope, __LINE__)((name))
#define VIXEN_LEDGER_TOUCH(mapped, bytes) ::Vixen::FrameTrace::LedgerTouch((mapped), static_cast<uint64_t>(bytes))
#define VIXEN_LEDGER_WRITTEN(mapped, bytes) ::Vixen::FrameTrace::LedgerWritten((mapped), static_cast<uint64_t>(bytes))
#define VIXEN_FRAME_COUNT(counter, amount) \
    ::Vixen::FrameTrace::CountAdd(::Vixen::FrameTrace::Counter::counter, static_cast<uint64_t>(amount))

#else

#define VIXEN_FRAME_TRACE_SCOPE(category, name) static_cast<void>(0)
#define VIXEN_FRAME_TRACE_BEGIN(var, category, name) static_cast<void>(0)
#define VIXEN_FRAME_TRACE_END(var) static_cast<void>(0)
#define VIXEN_FRAME_COUNT(counter, amount) static_cast<void>(0)
#define VIXEN_LEDGER_SCOPE(name) static_cast<void>(0)
#define VIXEN_LEDGER_TOUCH(mapped, bytes) static_cast<void>(0)
#define VIXEN_LEDGER_WRITTEN(mapped, bytes) static_cast<void>(0)

namespace Vixen::FrameTrace {
inline constexpr bool Enabled() noexcept { return false; }
inline constexpr void SetFrame(uint64_t) noexcept {}
}  // namespace Vixen::FrameTrace

#endif  // UNDERSET_NATIVE_BENCHMARK
