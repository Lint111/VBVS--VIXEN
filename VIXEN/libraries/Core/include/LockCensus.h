#pragma once

// ============================================================================
// LockCensus — build-flagged mutex census for the lock-free federation, phase 0.
//
// Vixen-Docs/01-Architecture/2026-09-08-lockfree-federation-architecture.md §5 phase 0 / §9:
// before any lock is removed, the steady frame path must be measurable — acquisitions per
// frame per inventory family, wait/hold time, and the wave width the executor actually ran.
// This header is that instrument. It is a TYPE wrapper, not a call-site macro: a declaring
// site swaps `std::mutex m;` for `Vixen::LockCensus::Mutex<Family::XX> m;` and every existing
// `std::lock_guard`/`std::unique_lock` acquisition keeps working unchanged (CTAD or an alias).
//
// Family IDs are the inventory's (2026-09-08-runtime-lock-inventory-and-classification.md);
// nothing is minted here. The wrapper is also the engine-side "declared" spelling that
// scripts/check-no-new-mutex.sh recognises (a LockCensus::Mutex<Family::XX> declaration names
// its inventory row; a bare std::mutex must be pinned in scripts/no-new-mutex.allowlist).
//
// Cost: with VIXEN_LOCK_CENSUS unset/0 (the default), Mutex<F, M> IS M — an alias, no wrapper,
// no counters, no code. With VIXEN_LOCK_CENSUS=1 (CMake -DVIXEN_LOCK_CENSUS=ON), each
// acquisition does a try_lock probe (contention detection) plus two steady_clock reads.
//
// Output (census builds only): RenderGraph samples the counters once per frame; a summary is
// printed at graph destruction, and VIXEN_LOCK_CENSUS_CSV=<path> writes one row per frame.
// ============================================================================

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <shared_mutex>
#include <string_view>

#ifndef VIXEN_LOCK_CENSUS
#define VIXEN_LOCK_CENSUS 0
#endif

namespace Vixen::LockCensus {

/// Inventory families instrumented on the steady frame path. The IDs and the objects they name
/// are the inventory's rows verbatim; the dormant/cold families (SH*, SVO*, GA1, RM2-6, RM13, C2,
/// C5, C6, RG5-7, KD1, APP1) are census-listed in scripts/no-new-mutex.allowlist but not timed.
enum class Family : uint8_t {
    VK1,   // VulkanDevice::submitMutexMapLock_        (queue -> submit-mutex directory lookup)
    VK2,   // VulkanDevice::submitMutexes_ elements    (per-physical-queue host serialization)
    EB1,   // MessageBus::queueMutex                   (publish queue)
    EB2,   // MessageBus::subscriptionMutex            (dispatch + registry)
    EB3,   // MessageBus::statsMutex                   (bus statistics)
    RM1,   // DeferredDestructionQueue::mutex_         (retirement ring)
    RM7,   // StagingBufferPool::SizeClassBucket::mutex (per-size-class deque)
    RM8,   // StagingBufferPool::recordsMutex_         (handle -> record map)
    RM9,   // BatchedUploader::cmdBufferMutex_         (command-buffer FIFO)
    RM10,  // BatchedUploader::pendingMutex_           (pending uploads)
    RM11,  // BatchedUploader::submittedMutex_         (submitted FIFO / completion)
    RM12,  // BatchedUploader::statusMutex_            (upload status map)
    C1,    // TypedCacher::m_lock (shared)             (cache hit / publication, 15 cachers)
    C3,    // MainCacher::m_globalRegistryMutex (shared)
    C4,    // MainCacher::m_deviceRegistriesMutex (shared)
    RG2,   // ITaskProfile::samplesMutex_              (profile samples)
    RG3,   // InputNode::eventMutex_                   (input pump vs fold)
    RG4,   // WindowNode::eventMutex (recursive)       (window pump vs drain)
    Count
};

constexpr std::size_t kFamilyCount = static_cast<std::size_t>(Family::Count);

constexpr std::string_view FamilyName(Family family) noexcept {
    constexpr std::string_view names[kFamilyCount] = {
        "VK1", "VK2", "EB1", "EB2", "EB3", "RM1", "RM7", "RM8", "RM9", "RM10", "RM11", "RM12",
        "C1", "C3", "C4", "RG2", "RG3", "RG4",
    };
    return names[static_cast<std::size_t>(family)];
}

#if VIXEN_LOCK_CENSUS

using Clock = std::chrono::steady_clock;

/// Per-family cumulative counters (process lifetime). Relaxed atomics: the census wants totals,
/// never ordering, and a sample that straddles an in-flight acquisition is off by at most one.
struct Counters {
    std::atomic<uint64_t> acquisitions{0};        // exclusive lock() / successful try_lock()
    std::atomic<uint64_t> sharedAcquisitions{0};  // lock_shared() / successful try_lock_shared()
    std::atomic<uint64_t> contended{0};           // lock() whose try_lock probe failed
    std::atomic<uint64_t> waitNs{0};              // time blocked in lock()/lock_shared() after a failed probe
    std::atomic<uint64_t> holdNs{0};              // exclusive hold time (lock() -> unlock())
};

inline std::array<Counters, kFamilyCount>& Table() noexcept {
    static std::array<Counters, kFamilyCount> table;
    return table;
}

inline Counters& CountersFor(Family family) noexcept {
    return Table()[static_cast<std::size_t>(family)];
}

inline uint64_t ElapsedNs(Clock::time_point since) noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - since).count());
}

/// Counting wrapper over an exclusive mutex (std::mutex / std::recursive_mutex). Satisfies
/// Lockable, so std::lock_guard / std::unique_lock / std::scoped_lock bind to it directly.
/// `depth_`/`lockedAt_` are touched only by the holding thread, so they need no atomics; for a
/// recursive mutex hold time spans the outermost acquisition.
template <Family F, class M = std::mutex>
class Mutex {
public:
    Mutex() = default;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void lock() {
        Counters& c = CountersFor(F);
        if (!m_.try_lock()) {
            const auto start = Clock::now();
            m_.lock();
            c.waitNs.fetch_add(ElapsedNs(start), std::memory_order_relaxed);
            c.contended.fetch_add(1, std::memory_order_relaxed);
        }
        Acquired(c);
    }

    bool try_lock() {
        if (!m_.try_lock()) return false;
        Acquired(CountersFor(F));
        return true;
    }

    void unlock() {
        if (--depth_ == 0) {
            CountersFor(F).holdNs.fetch_add(ElapsedNs(lockedAt_), std::memory_order_relaxed);
        }
        m_.unlock();
    }

    /// The wrapped mutex, for the rare caller that must hand a std type to a foreign API
    /// (std::condition_variable requires std::mutex). Acquisitions through it are NOT counted.
    M& Underlying() noexcept { return m_; }

private:
    void Acquired(Counters& c) noexcept {
        c.acquisitions.fetch_add(1, std::memory_order_relaxed);
        if (depth_++ == 0) lockedAt_ = Clock::now();
    }

    M m_;
    uint32_t depth_ = 0;
    Clock::time_point lockedAt_{};
};

/// Counting wrapper over std::shared_mutex. Exclusive acquisitions are timed like Mutex; shared
/// acquisitions are counted and wait-timed but not hold-timed (a shared hold has no single owner
/// slot to store its start in — a per-thread record is a later refinement if the number is needed).
template <Family F>
class SharedMutex {
public:
    SharedMutex() = default;
    SharedMutex(const SharedMutex&) = delete;
    SharedMutex& operator=(const SharedMutex&) = delete;

    void lock() {
        Counters& c = CountersFor(F);
        if (!m_.try_lock()) {
            const auto start = Clock::now();
            m_.lock();
            c.waitNs.fetch_add(ElapsedNs(start), std::memory_order_relaxed);
            c.contended.fetch_add(1, std::memory_order_relaxed);
        }
        c.acquisitions.fetch_add(1, std::memory_order_relaxed);
        lockedAt_ = Clock::now();
    }

    bool try_lock() {
        if (!m_.try_lock()) return false;
        CountersFor(F).acquisitions.fetch_add(1, std::memory_order_relaxed);
        lockedAt_ = Clock::now();
        return true;
    }

    void unlock() {
        CountersFor(F).holdNs.fetch_add(ElapsedNs(lockedAt_), std::memory_order_relaxed);
        m_.unlock();
    }

    void lock_shared() {
        Counters& c = CountersFor(F);
        if (!m_.try_lock_shared()) {
            const auto start = Clock::now();
            m_.lock_shared();
            c.waitNs.fetch_add(ElapsedNs(start), std::memory_order_relaxed);
            c.contended.fetch_add(1, std::memory_order_relaxed);
        }
        c.sharedAcquisitions.fetch_add(1, std::memory_order_relaxed);
    }

    bool try_lock_shared() {
        if (!m_.try_lock_shared()) return false;
        CountersFor(F).sharedAcquisitions.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void unlock_shared() { m_.unlock_shared(); }

    std::shared_mutex& Underlying() noexcept { return m_; }

private:
    std::shared_mutex m_;
    Clock::time_point lockedAt_{};
};

/// One family's counters as plain integers (a copy of the atomics at sample time).
struct FamilySample {
    uint64_t acquisitions = 0;
    uint64_t sharedAcquisitions = 0;
    uint64_t contended = 0;
    uint64_t waitNs = 0;
    uint64_t holdNs = 0;

    FamilySample operator-(const FamilySample& rhs) const noexcept {
        return {acquisitions - rhs.acquisitions, sharedAcquisitions - rhs.sharedAcquisitions,
                contended - rhs.contended, waitNs - rhs.waitNs, holdNs - rhs.holdNs};
    }
};

inline FamilySample Read(Family family) noexcept {
    const Counters& c = CountersFor(family);
    return {c.acquisitions.load(std::memory_order_relaxed),
            c.sharedAcquisitions.load(std::memory_order_relaxed),
            c.contended.load(std::memory_order_relaxed),
            c.waitNs.load(std::memory_order_relaxed),
            c.holdNs.load(std::memory_order_relaxed)};
}

#endif  // VIXEN_LOCK_CENSUS

/// What the executor actually ran this frame — design §9 measurement 3 (wave width) and the
/// N-worker scaling input for measurement 5. Filled by RenderGraph, zero when the sequential
/// (non-lowered) path ran (waves == 0 then; rows == nodes executed).
struct FrameWaveStats {
    bool lowered = false;       // ExecuteLoweredFrame (KernelDispatch::TaskExecutor waves) vs sequential
    uint32_t workerCount = 1;   // VIXEN_GRAPH_WORKERS resolved value
    uint32_t waves = 0;         // wave count in the executed plan
    uint32_t rows = 0;          // tasks (rows) across all waves
    uint32_t maxWaveWidth = 0;  // widest wave (rows)
    uint32_t wholeSlotRows = 0; // rows with no per-item access set (every graph node task today)
    double frameCpuMs = 0.0;    // RenderFrame wall time
};

#if VIXEN_LOCK_CENSUS

/// Per-frame sampler owned by the frame driver (RenderGraph). Sample() diffs the cumulative
/// counters against the previous call, accumulates the running summary, and appends a CSV row
/// when VIXEN_LOCK_CENSUS_CSV names a file. Report() prints the summary (mean per frame).
class FrameSampler {
public:
    FrameSampler() {
        if (const char* path = std::getenv("VIXEN_LOCK_CENSUS_CSV")) {
            csv_ = std::fopen(path, "w");
            if (csv_) {
                std::fputs("frame,lowered,workers,waves,rows,max_wave_width,whole_slot_rows,frame_cpu_ms", csv_);
                for (std::size_t i = 0; i < kFamilyCount; ++i) {
                    const auto name = FamilyName(static_cast<Family>(i));
                    std::fprintf(csv_, ",%.*s_acq,%.*s_shared,%.*s_contended,%.*s_wait_us,%.*s_hold_us",
                                 static_cast<int>(name.size()), name.data(), static_cast<int>(name.size()), name.data(),
                                 static_cast<int>(name.size()), name.data(), static_cast<int>(name.size()), name.data(),
                                 static_cast<int>(name.size()), name.data());
                }
                std::fputc('\n', csv_);
            }
        }
        for (std::size_t i = 0; i < kFamilyCount; ++i) previous_[i] = Read(static_cast<Family>(i));
    }

    ~FrameSampler() {
        if (csv_) std::fclose(csv_);
    }

    FrameSampler(const FrameSampler&) = delete;
    FrameSampler& operator=(const FrameSampler&) = delete;

    void Sample(uint64_t frameIndex, const FrameWaveStats& wave) {
        ++frames_;
        waveTotals_.waves += wave.waves;
        waveTotals_.rows += wave.rows;
        waveTotals_.wholeSlotRows += wave.wholeSlotRows;
        if (wave.maxWaveWidth > waveTotals_.maxWaveWidth) waveTotals_.maxWaveWidth = wave.maxWaveWidth;
        waveTotals_.frameCpuMs += wave.frameCpuMs;
        waveTotals_.lowered = wave.lowered;
        waveTotals_.workerCount = wave.workerCount;

        if (csv_) {
            std::fprintf(csv_, "%llu,%d,%u,%u,%u,%u,%u,%.4f",
                         static_cast<unsigned long long>(frameIndex), wave.lowered ? 1 : 0, wave.workerCount,
                         wave.waves, wave.rows, wave.maxWaveWidth, wave.wholeSlotRows, wave.frameCpuMs);
        }
        for (std::size_t i = 0; i < kFamilyCount; ++i) {
            const FamilySample now = Read(static_cast<Family>(i));
            const FamilySample delta = now - previous_[i];
            previous_[i] = now;
            FamilySample& total = totals_[i];
            total.acquisitions += delta.acquisitions;
            total.sharedAcquisitions += delta.sharedAcquisitions;
            total.contended += delta.contended;
            total.waitNs += delta.waitNs;
            total.holdNs += delta.holdNs;
            if (delta.acquisitions + delta.sharedAcquisitions > peakPerFrame_[i]) {
                peakPerFrame_[i] = delta.acquisitions + delta.sharedAcquisitions;
            }
            if (csv_) {
                std::fprintf(csv_, ",%llu,%llu,%llu,%.3f,%.3f",
                             static_cast<unsigned long long>(delta.acquisitions),
                             static_cast<unsigned long long>(delta.sharedAcquisitions),
                             static_cast<unsigned long long>(delta.contended),
                             static_cast<double>(delta.waitNs) / 1000.0,
                             static_cast<double>(delta.holdNs) / 1000.0);
            }
        }
        if (csv_) std::fputc('\n', csv_);
    }

    /// Mean-per-frame summary. Lines are prefixed so a log grep (`[LockCensus]`) isolates them.
    void Report(std::FILE* out = stderr) const {
        if (!out) return;
        const double frames = frames_ > 0 ? static_cast<double>(frames_) : 1.0;
        std::fprintf(out, "[LockCensus] frames=%llu lowered=%d workers=%u mean_waves/frame=%.2f "
                          "mean_rows/frame=%.2f max_wave_width=%u whole_slot_rows/frame=%.2f "
                          "mean_frame_cpu_ms=%.4f\n",
                     static_cast<unsigned long long>(frames_), waveTotals_.lowered ? 1 : 0,
                     waveTotals_.workerCount, static_cast<double>(waveTotals_.waves) / frames,
                     static_cast<double>(waveTotals_.rows) / frames, waveTotals_.maxWaveWidth,
                     static_cast<double>(waveTotals_.wholeSlotRows) / frames, waveTotals_.frameCpuMs / frames);
        std::fprintf(out, "[LockCensus] %-5s %12s %12s %12s %10s %12s %12s\n", "fam", "acq/frame",
                     "shared/frame", "peak/frame", "contended", "wait_us/fr", "hold_us/fr");
        uint64_t totalAcq = 0;
        for (std::size_t i = 0; i < kFamilyCount; ++i) {
            const FamilySample& t = totals_[i];
            totalAcq += t.acquisitions + t.sharedAcquisitions;
            const auto name = FamilyName(static_cast<Family>(i));
            std::fprintf(out, "[LockCensus] %-5.*s %12.3f %12.3f %12llu %10llu %12.3f %12.3f\n",
                         static_cast<int>(name.size()), name.data(),
                         static_cast<double>(t.acquisitions) / frames,
                         static_cast<double>(t.sharedAcquisitions) / frames,
                         static_cast<unsigned long long>(peakPerFrame_[i]),
                         static_cast<unsigned long long>(t.contended),
                         static_cast<double>(t.waitNs) / 1000.0 / frames,
                         static_cast<double>(t.holdNs) / 1000.0 / frames);
        }
        std::fprintf(out, "[LockCensus] total mutex acquisitions/frame (instrumented families) = %.3f\n",
                     static_cast<double>(totalAcq) / frames);
    }

private:
    struct WaveTotals {
        bool lowered = false;
        uint32_t workerCount = 1;
        uint64_t waves = 0;
        uint64_t rows = 0;
        uint64_t wholeSlotRows = 0;
        uint32_t maxWaveWidth = 0;
        double frameCpuMs = 0.0;
    };

    std::FILE* csv_ = nullptr;
    uint64_t frames_ = 0;
    WaveTotals waveTotals_;
    std::array<FamilySample, kFamilyCount> previous_{};
    std::array<FamilySample, kFamilyCount> totals_{};
    std::array<uint64_t, kFamilyCount> peakPerFrame_{};
};

#else  // !VIXEN_LOCK_CENSUS — the wrappers ARE the std types; the sampler is a no-op.

template <Family F, class M = std::mutex>
using Mutex = M;

template <Family F>
using SharedMutex = std::shared_mutex;

class FrameSampler {
public:
    void Sample(uint64_t, const FrameWaveStats&) noexcept {}
    void Report(std::FILE* = nullptr) const noexcept {}
};

#endif  // VIXEN_LOCK_CENSUS

/// The per-physical-queue host serialization mutex (inventory VK2). Named once so the borrowed
/// pointers (BatchedUploader, UIRenderNode, VixenRmlRenderInterface, CommandBufferMgr) and the
/// owning VulkanDevice map agree on the type without including VulkanDevice.h.
using QueueSubmitMutex = Mutex<Family::VK2>;

}  // namespace Vixen::LockCensus
