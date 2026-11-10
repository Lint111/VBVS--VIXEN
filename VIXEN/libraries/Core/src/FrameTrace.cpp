#include "FrameTrace.h"

#if UNDERSET_NATIVE_BENCHMARK

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

#include "PerThreadLog.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace Vixen::FrameTrace {
namespace {

struct Record {
    uint64_t t0 = 0;
    uint64_t t1 = 0;
    uint64_t frame = 0;
    uint32_t thread = 0;
    uint32_t depth = 0;
    char category[12] = {};
    char name[48] = {};
};

PerThreadLog<Record>& Log() {
    static PerThreadLog<Record> log;
    return log;
}

std::atomic<uint64_t> gFrame{0};
std::atomic<int> gCalibratedExtension{0};

thread_local uint32_t tDepth = 0;

constexpr uint32_t kMaxThreads = 256;
std::atomic<uint32_t> gOsThreadIds[kMaxThreads];

uint32_t CurrentOsThreadId() {
#if defined(_WIN32)
    return static_cast<uint32_t>(GetCurrentThreadId());
#else
    return static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

uint32_t ThreadIndex() {
    static std::atomic<uint32_t> next{0};
    thread_local const uint32_t index = [] {
        const uint32_t assigned = next.fetch_add(1, std::memory_order_relaxed);
        if (assigned < kMaxThreads) gOsThreadIds[assigned].store(CurrentOsThreadId(), std::memory_order_relaxed);
        return assigned;
    }();
    return index;
}

void CopyName(char* destination, std::size_t capacity, std::string_view source) {
    const std::size_t count = std::min(capacity - 1, source.size());
    std::memcpy(destination, source.data(), count);
    destination[count] = '\0';
}

}  // namespace

bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("VIXEN_FRAME_TRACE");
        return value && *value && *value != '0';
    }();
    return enabled;
}

uint64_t NowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void SetFrame(uint64_t frame) { gFrame.store(frame, std::memory_order_relaxed); }

void Add(const char* category, std::string_view name, uint64_t t0Ns, uint64_t t1Ns) {
    if (!Enabled()) return;
    Record record;
    record.t0 = t0Ns;
    record.t1 = t1Ns;
    record.frame = gFrame.load(std::memory_order_relaxed);
    record.thread = ThreadIndex();
    record.depth = tDepth;
    CopyName(record.category, sizeof(record.category), category);
    CopyName(record.name, sizeof(record.name), name);
    Log().Append(record);
}

void AddGpuRaw(std::string_view name, uint64_t t0DeviceTicks, uint64_t t1DeviceTicks) {
    if (!Enabled()) return;
    Record record;
    record.t0 = t0DeviceTicks;
    record.t1 = t1DeviceTicks;
    record.frame = gFrame.load(std::memory_order_relaxed);
    record.thread = ThreadIndex();
    record.depth = 0;
    CopyName(record.category, sizeof(record.category), "gpuraw");
    CopyName(record.name, sizeof(record.name), name);
    Log().Append(record);
}

void SetCalibratedTimestampExtension(int extension) { gCalibratedExtension.store(extension); }
int CalibratedTimestampExtension() { return gCalibratedExtension.load(); }

void AddCalibration(uint64_t deviceTicks, uint64_t hostNs) {
    if (!Enabled()) return;
    Record record;
    record.t0 = deviceTicks;
    record.t1 = hostNs;
    record.frame = gFrame.load(std::memory_order_relaxed);
    record.thread = ThreadIndex();
    record.depth = 0;
    CopyName(record.category, sizeof(record.category), "calib");
    CopyName(record.name, sizeof(record.name), "pair");
    Log().Append(record);
}

uint64_t HostCounterToNs(uint64_t counter) {
#if defined(_WIN32)
    // MSVC's steady_clock is QueryPerformanceCounter scaled to nanoseconds, so the two agree.
    static const uint64_t frequency = [] {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        return static_cast<uint64_t>(value.QuadPart);
    }();
    return (counter / frequency) * 1'000'000'000ull + (counter % frequency) * 1'000'000'000ull / frequency;
#else
    return counter;
#endif
}

Scope::Scope(const char* category, std::string_view name)
    : category_(category), t0_(0), active_(Enabled()) {
    name_[0] = '\0';
    if (!active_) return;
    CopyName(name_, sizeof(name_), name);
    ++tDepth;
    t0_ = NowNs();
}

Scope::~Scope() {
    if (!active_) return;
    const uint64_t t1 = NowNs();
    --tDepth;
    Add(category_, name_, t0_, t1);
}

namespace {

#if defined(_WIN32)
// One line per thread of this process: CPU time (user + kernel) and the name it set, if any.
// Shows which threads burn CPU and so which work runs off the render thread.
void WriteThreadCensus(std::FILE* out) {
    const DWORD processId = GetCurrentProcessId();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    const auto toMs = [](const FILETIME& time) {
        ULARGE_INTEGER value;
        value.LowPart = time.dwLowDateTime;
        value.HighPart = time.dwHighDateTime;
        return static_cast<double>(value.QuadPart) / 10000.0;
    };
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != processId) continue;
            const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
            if (!thread) continue;
            FILETIME created{}, exited{}, kernel{}, user{};
            std::string name;
            if (GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
                PWSTR description = nullptr;
                if (SUCCEEDED(GetThreadDescription(thread, &description)) && description) {
                    for (const wchar_t* c = description; *c; ++c) name += *c < 128 ? static_cast<char>(*c) : '?';
                    LocalFree(description);
                }
                std::fprintf(out, "thread,%lu,%.3f,%.3f,%s\n",
                    static_cast<unsigned long>(entry.th32ThreadID), toMs(user), toMs(kernel), name.c_str());
            }
            CloseHandle(thread);
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
}
#endif

}  // namespace

void Write() {
    if (!Enabled()) return;
    static std::atomic<bool> written{false};
    if (written.exchange(true)) return;
    const char* setting = std::getenv("VIXEN_FRAME_TRACE");
    std::string path;
    if (std::strcmp(setting, "1") == 0) {
        const char* dir = std::getenv("VIXEN_LOOKDEV_CAPTURE_DIR");
        path = std::string(dir ? dir : ".") + "/frame-trace.csv";
    } else {
        path = setting;
    }
    std::FILE* out = std::fopen(path.c_str(), "w");
    if (!out) return;
    std::fprintf(out, "frame,thread,depth,category,name,t0_ns,t1_ns\n");
    Log().ForEach([&](const Record& record) {
        std::fprintf(out, "%llu,%u,%u,%s,%s,%llu,%llu\n",
            static_cast<unsigned long long>(record.frame), record.thread, record.depth,
            record.category, record.name,
            static_cast<unsigned long long>(record.t0), static_cast<unsigned long long>(record.t1));
    });
    for (uint32_t i = 0; i < kMaxThreads; ++i) {
        const uint32_t osId = gOsThreadIds[i].load(std::memory_order_relaxed);
        if (osId) std::fprintf(out, "threadmap,%u,%u\n", i, osId);
    }
#if defined(_WIN32)
    std::fprintf(out, "# thread,os_thread_id,user_ms,kernel_ms,name\n");
    WriteThreadCensus(out);
#endif
    std::fclose(out);
}

}  // namespace Vixen::FrameTrace

#endif  // UNDERSET_NATIVE_BENCHMARK
