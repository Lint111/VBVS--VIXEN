// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file ConcurrentRunRegistry.h
 * @brief Records which VIXEN processes ran at the same time against one cache directory
 *
 * A process that measures timings while other VIXEN processes share the GPU measures the
 * contention, not the machine. CalibrationStore uses this registry to learn, at save time,
 * whether any other process overlapped this run, so a contended measurement never replaces a clean
 * one.
 *
 * Each run owns one marker file in `<dir>`: `<pid>-<startMs>.run` while it is running, renamed to
 * `<pid>-<startMs>-<endMs>.done` when it leaves cleanly. A `.run` file whose process is gone (a
 * crash) counts as having lasted one second. Markers are best-effort: an I/O failure makes the
 * registry report no contention, never throws. Wall-clock milliseconds are used so unrelated
 * processes agree on the timeline.
 */

#include "RuntimeCachePaths.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace Vixen::RenderGraph {

class ConcurrentRunRegistry {
public:
    explicit ConcurrentRunRegistry(std::filesystem::path dir, std::uint64_t processId = Vixen::CurrentProcessId())
        : dir_(std::move(dir)), pid_(processId) {}

    ~ConcurrentRunRegistry() { Leave(); }

    ConcurrentRunRegistry(const ConcurrentRunRegistry&) = delete;
    ConcurrentRunRegistry& operator=(const ConcurrentRunRegistry&) = delete;
    ConcurrentRunRegistry(ConcurrentRunRegistry&&) = delete;
    ConcurrentRunRegistry& operator=(ConcurrentRunRegistry&&) = delete;

    /// Announce this run. Idempotent.
    void Enter() {
        if (entered_) return;
        entered_ = true;
        startMs_ = NowMs();
        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);
        std::ofstream marker(RunPath(), std::ios::binary | std::ios::trunc);
    }

    /// End this run (clean exit). Idempotent.
    void Leave() noexcept {
        if (!entered_ || left_) return;
        left_ = true;
        std::error_code ec;
        std::filesystem::rename(RunPath(), DonePath(NowMs()), ec);
        if (ec) std::filesystem::remove(RunPath(), ec);
    }

    /// Number of OTHER runs whose lifetime overlaps [Enter(), now]. Also prunes markers that ended
    /// more than a day ago.
    [[nodiscard]] std::size_t OverlappingRuns() const {
        if (!entered_) return 0;
        std::size_t overlapping = 0;
        const std::int64_t now = NowMs();
        std::error_code ec;
        std::filesystem::directory_iterator it(dir_, ec);
        const std::filesystem::directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            const auto& path = it->path();
            const std::string ext = path.extension().string();
            if (ext != ".run" && ext != ".done") continue;
            Marker m;
            if (!Parse(path.stem().string(), m)) continue;
            if (m.pid == pid_ && m.startMs == startMs_) continue;  // self

            std::int64_t endMs = m.endMs;
            if (ext == ".run") {
                if (Vixen::IsProcessAlive(m.pid)) {
                    endMs = now;  // still running
                } else {
                    // Crashed without leaving: its end is unknown. Assume it lived a second, so a
                    // run that started during this one counts as overlapping.
                    endMs = m.startMs + 1000;
                }
            }
            if (endMs < now - kPruneMs) {
                std::error_code removeError;
                std::filesystem::remove(path, removeError);
                continue;
            }
            if (m.startMs <= now && endMs >= startMs_) ++overlapping;
        }
        return overlapping;
    }

    [[nodiscard]] std::int64_t StartMs() const { return startMs_; }

private:
    struct Marker { std::uint64_t pid = 0; std::int64_t startMs = 0; std::int64_t endMs = 0; };
    static constexpr std::int64_t kPruneMs = 24LL * 60 * 60 * 1000;

    static std::int64_t NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // "<pid>-<startMs>" or "<pid>-<startMs>-<endMs>"
    static bool Parse(const std::string& stem, Marker& out) {
        try {
            const auto a = stem.find('-');
            if (a == std::string::npos) return false;
            const auto b = stem.find('-', a + 1);
            out.pid = std::stoull(stem.substr(0, a));
            out.startMs = std::stoll(stem.substr(a + 1, b == std::string::npos ? b : b - a - 1));
            out.endMs = b == std::string::npos ? 0 : std::stoll(stem.substr(b + 1));
            return true;
        } catch (...) {
            return false;
        }
    }

    std::filesystem::path RunPath() const {
        return dir_ / (std::to_string(pid_) + "-" + std::to_string(startMs_) + ".run");
    }
    std::filesystem::path DonePath(std::int64_t endMs) const {
        return dir_ / (std::to_string(pid_) + "-" + std::to_string(startMs_) + "-" + std::to_string(endMs) + ".done");
    }

    std::filesystem::path dir_;
    std::uint64_t pid_;
    std::int64_t startMs_ = 0;
    bool entered_ = false;
    bool left_ = false;
};

} // namespace Vixen::RenderGraph
