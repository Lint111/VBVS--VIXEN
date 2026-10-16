#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <vector>

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

struct Store {
    std::mutex mutex;
    std::map<std::string, Series> execute;
    std::map<std::string, Series> waits;
};

inline Store& GetStore() {
    static Store store;
    return store;
}

inline void Add(std::map<std::string, Series> Store::*which, const std::string& key, double ms) {
    Store& store = GetStore();
    std::lock_guard<std::mutex> lock(store.mutex);
    Series& series = (store.*which)[key];
    series.ms.push_back(ms);
    series.total += ms;
}

inline void AddExecute(const std::string& node, double ms) { Add(&Store::execute, node, ms); }
inline void AddWait(const std::string& site, double ms) { Add(&Store::waits, site, ms); }

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
    if (!Enabled()) return;
    Store& store = GetStore();
    std::lock_guard<std::mutex> lock(store.mutex);
    PrintSeries("NODEEXEC", store.execute);
    PrintSeries("HOSTWAIT", store.waits);
    store.execute.clear();
    store.waits.clear();
}

class ScopedWait {
public:
    explicit ScopedWait(const char* site) : site_(site), start_(Enabled() ? Clock::now() : Clock::time_point{}) {}
    ~ScopedWait() { if (Enabled()) AddWait(site_, MsSince(start_)); }
    ScopedWait(const ScopedWait&) = delete;
    ScopedWait& operator=(const ScopedWait&) = delete;
private:
    const char* site_;
    Clock::time_point start_;
};

}  // namespace Vixen::RenderGraph::PhaseTiming
