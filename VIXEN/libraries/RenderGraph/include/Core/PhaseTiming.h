#pragma once

#ifndef UNDERSET_NATIVE_BENCHMARK
#define UNDERSET_NATIVE_BENCHMARK 0
#endif

#if !UNDERSET_NATIVE_BENCHMARK

#include <cstdint>
#include <string>

namespace Vixen::RenderGraph::PhaseTiming {

// Keep the call surface available to graph code, but make the normal-build implementation
// constexpr/no-state: no environment read, clock read, storage, or per-frame branch remains.
struct DisabledClock {
    using time_point = uint64_t;
    static constexpr time_point now() noexcept { return 0; }
};

inline constexpr bool Enabled() noexcept { return false; }
using Clock = DisabledClock;
inline constexpr double MsSince(Clock::time_point) noexcept { return 0.0; }
inline constexpr void AddExecute(const std::string&, double) noexcept {}
inline constexpr void AddWait(const std::string&, double) noexcept {}
inline constexpr void PrintSummary() noexcept {}

class ScopedWait {
public:
    explicit constexpr ScopedWait(const char*) noexcept {}
    ScopedWait(const ScopedWait&) = delete;
    ScopedWait& operator=(const ScopedWait&) = delete;
};

}  // namespace Vixen::RenderGraph::PhaseTiming

#else

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "FrameTrace.h"
#include "PerThreadLog.h"

// VIXEN_PHASE_TIMING=1: host wall-time census for compile phases, per-node Execute cost and
// blocking waits. Output goes to stderr as [PHASE] / [NODEEXEC] / [HOSTWAIT] lines; the summary
// (p50/p95/max per node and per wait site) prints once when the graph is destroyed.
namespace Vixen::RenderGraph::PhaseTiming {

inline bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("VIXEN_PHASE_TIMING");
        return value && *value && *value != '0';
    }();
    return enabled;
}

using Clock = std::chrono::steady_clock;

inline double MsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Series {
    std::vector<double> ms;
    double total = 0.0;
};

// Thread-local + merge (no-stall pattern 4): every thread appends its samples to its own log, the
// summary merges all logs once when the graph is destroyed. Recording never takes a lock.
struct Sample {
    bool isWait = false;
    std::string key;
    double ms = 0.0;
};

inline Vixen::PerThreadLog<Sample>& GetLog() {
    static Vixen::PerThreadLog<Sample> log;
    return log;
}

inline void AddExecute(const std::string& node, double ms) { GetLog().Append(Sample{false, node, ms}); }
inline void AddWait(const std::string& site, double ms) { GetLog().Append(Sample{true, site, ms}); }

inline double Percentile(std::vector<double> samples, double fraction) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    const std::size_t index = std::min(samples.size() - 1,
        static_cast<std::size_t>(fraction * static_cast<double>(samples.size() - 1) + 0.5));
    return samples[index];
}

inline void PrintSeries(const char* tag, const std::map<std::string, Series>& all) {
    for (const auto& [name, series] : all) {
        std::fprintf(stderr, "[%s] name=%s count=%zu total_ms=%.3f p50_ms=%.3f p95_ms=%.3f max_ms=%.3f\n",
            tag, name.c_str(), series.ms.size(), series.total,
            Percentile(series.ms, 0.50), Percentile(series.ms, 0.95), Percentile(series.ms, 1.0));
    }
}

inline void PrintSummary() {
    FrameTrace::Write();  // no-op unless VIXEN_FRAME_TRACE is set
    if (!Enabled()) return;
    std::map<std::string, Series> execute;
    std::map<std::string, Series> waits;
    GetLog().ForEach([&](const Sample& sample) {
        Series& series = (sample.isWait ? waits : execute)[sample.key];
        series.ms.push_back(sample.ms);
        series.total += sample.ms;
    });
    GetLog().Clear();
    PrintSeries("NODEEXEC", execute);
    PrintSeries("HOSTWAIT", waits);
}

class ScopedWait {
public:
    explicit ScopedWait(const char* site)
        : site_(site), start_(Enabled() ? Clock::now() : Clock::time_point{}), trace_("wait", site) {}
    ~ScopedWait() { if (Enabled()) AddWait(site_, MsSince(start_)); }
    ScopedWait(const ScopedWait&) = delete;
    ScopedWait& operator=(const ScopedWait&) = delete;
private:
    const char* site_;
    Clock::time_point start_;
    FrameTrace::Scope trace_;  // category "wait": the same interval on the frame timeline
};

}  // namespace Vixen::RenderGraph::PhaseTiming

#endif  // UNDERSET_NATIVE_BENCHMARK
