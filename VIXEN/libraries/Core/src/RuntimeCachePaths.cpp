#include "RuntimeCachePaths.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef _WIN32
#include <process.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

namespace Vixen {

namespace {

constexpr auto kStaleTemporaryDirectoryAge = std::chrono::hours(24);

bool IsProcessAliveImpl(std::uint64_t processId) noexcept {
    if (processId == 0) return false;

#ifdef _WIN32
    if (processId > std::numeric_limits<DWORD>::max()) return false;
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                   static_cast<DWORD>(processId));
    if (!process) {
        return ::GetLastError() == ERROR_ACCESS_DENIED;
    }

    DWORD exitCode = 0;
    const bool queried = ::GetExitCodeProcess(process, &exitCode) != FALSE;
    ::CloseHandle(process);
    return queried && exitCode == STILL_ACTIVE;
#else
    if (processId > static_cast<std::uint64_t>(std::numeric_limits<pid_t>::max())) {
        return false;
    }
    if (::kill(static_cast<pid_t>(processId), 0) == 0) return true;
    return errno == EPERM;
#endif
}

bool ParseTemporaryDirectoryName(
    const std::filesystem::path& path,
    std::uint64_t& processId
) {
    const std::string name = path.filename().string();
    constexpr std::string_view prefix = "vixen_";
    if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }

    const std::size_t separator = name.rfind('_');
    if (separator <= prefix.size() || separator + 1 >= name.size()) return false;

    const std::string_view processIdText(name.data() + separator + 1,
                                         name.size() - separator - 1);
    for (const char digit : processIdText) {
        if (!std::isdigit(static_cast<unsigned char>(digit))) return false;
    }

    try {
        processId = std::stoull(std::string(processIdText));
    } catch (...) {
        return false;
    }
    return processId != 0;
}

void SweepStaleTemporaryDirectories() noexcept {
    std::error_code ec;
    const auto temporaryRoot = std::filesystem::temp_directory_path(ec);
    if (ec) return;

    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::directory_iterator entries(temporaryRoot, ec);
    const std::filesystem::directory_iterator end;
    for (; !ec && entries != end; entries.increment(ec)) {
        const auto& entry = *entries;
        std::error_code entryError;
        if (entry.is_symlink(entryError) || entryError || !entry.is_directory(entryError) || entryError) {
            continue;
        }

        const auto modifiedAt = entry.last_write_time(entryError);
        if (entryError || now - modifiedAt < kStaleTemporaryDirectoryAge) continue;

        std::uint64_t processId = 0;
        if (!ParseTemporaryDirectoryName(entry.path(), processId) || IsProcessAliveImpl(processId)) {
            continue;
        }

        std::filesystem::remove_all(entry.path(), entryError);
    }
}

const char* ExplicitTemporaryRoot(std::string_view applicationName) noexcept {
    const char* environmentName = nullptr;
    if (applicationName == "main") {
        environmentName = "VIXEN_HUD_CAPTURE_DIR";
    } else if (applicationName == "editor") {
        environmentName = "VIXEN_EDITOR_CAPTURE_DIR";
    }
    if (!environmentName) return nullptr;

    const char* root = std::getenv(environmentName);
    return root && *root ? root : nullptr;
}

class TemporaryDirectoryLifetime {
public:
    TemporaryDirectoryLifetime() noexcept {
        SweepStaleTemporaryDirectories();
    }

    ~TemporaryDirectoryLifetime() noexcept {
        for (const auto& directory : ownedDirectories_) {
            std::error_code ec;
            std::filesystem::remove_all(directory, ec);
        }
    }

    std::filesystem::path Acquire(std::string_view applicationName) {
        if (const char* explicitRoot = ExplicitTemporaryRoot(applicationName)) {
            return std::filesystem::path(explicitRoot);
        }

        const auto directory = std::filesystem::temp_directory_path() /
            ("vixen_" + std::string(applicationName) + "_" + std::to_string(CurrentProcessId()));
        std::error_code createError;
        std::filesystem::create_directories(directory, createError);
        if (std::find(ownedDirectories_.begin(), ownedDirectories_.end(), directory) ==
            ownedDirectories_.end()) {
            ownedDirectories_.push_back(directory);
        }
        return directory;
    }

private:
    std::vector<std::filesystem::path> ownedDirectories_;
};

TemporaryDirectoryLifetime& ProcessTemporaryDirectoryLifetime() {
    static TemporaryDirectoryLifetime lifetime;
    return lifetime;
}

} // namespace

std::filesystem::path RuntimeCacheDirectory() {
    if (const char* overridePath = std::getenv("VIXEN_CACHE_DIR");
        overridePath && *overridePath) {
        return std::filesystem::path(overridePath);
    }
#ifdef VIXEN_RUNTIME_CACHE_DIR
    return std::filesystem::path(VIXEN_RUNTIME_CACHE_DIR);
#else
    return UserCacheDirectory();
#endif
}

std::filesystem::path SharedDeviceCacheDirectory() {
    const auto environmentPath = [](const char* name) -> std::filesystem::path {
        const char* value = std::getenv(name);
        return value && *value ? std::filesystem::path(value) : std::filesystem::path();
    };

    if (auto explicitRoot = environmentPath("VIXEN_SHARED_CACHE_DIR"); !explicitRoot.empty()) {
        return explicitRoot;
    }
    if (auto overriddenRuntime = environmentPath("VIXEN_CACHE_DIR"); !overriddenRuntime.empty()) {
        return overriddenRuntime / "shared";
    }
#ifdef _WIN32
    if (auto localAppData = environmentPath("LOCALAPPDATA"); !localAppData.empty()) {
        return localAppData / "VIXEN" / "cache";
    }
#else
    if (auto xdgCache = environmentPath("XDG_CACHE_HOME"); !xdgCache.empty()) {
        return xdgCache / "vixen";
    }
    if (auto home = environmentPath("HOME"); !home.empty()) {
        return home / ".cache" / "vixen";
    }
#endif
    return RuntimeCacheDirectory() / "shared";
}

std::filesystem::path UserCacheDirectory() {
    // Absolute and independent of the working directory: two processes started from different
    // directories (or the same build directory) must agree on where shared state lives.
    std::error_code ec;
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local) {
        return std::filesystem::path(local) / "vixen";
    }
#else
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "vixen";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".cache" / "vixen";
    }
#endif
    return std::filesystem::absolute(std::filesystem::temp_directory_path(ec) / "vixen-cache", ec);
}

bool IsProcessAlive(std::uint64_t processId) noexcept {
    return ::Vixen::IsProcessAliveImpl(processId);
}

std::uint64_t CurrentProcessId() noexcept {
#ifdef _WIN32
    return static_cast<std::uint64_t>(::_getpid());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

std::filesystem::path ProcessTemporaryDirectory(std::string_view applicationName) {
    return ProcessTemporaryDirectoryLifetime().Acquire(applicationName);
}

std::uint64_t Checksum64(const void* data, std::size_t size) noexcept {
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    auto hash = offset;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= prime;
    }
    return hash;
}

bool AtomicWriteFile(
    const std::filesystem::path& destination,
    const std::function<bool(std::ostream&)>& writer,
    std::string* error
) {
    std::error_code ec;
    if (!destination.parent_path().empty()) {
        std::filesystem::create_directories(destination.parent_path(), ec);
        if (ec) {
            if (error) *error = "create directory failed: " + ec.message();
            return false;
        }
    }

    static std::atomic<std::uint64_t> sequence{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary = destination.parent_path() /
        (destination.filename().string() + ".tmp." + std::to_string(CurrentProcessId()) + "." +
         std::to_string(stamp) + "." + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "failed to open temporary file";
            return false;
        }
        bool writerSucceeded = false;
        try {
            writerSucceeded = writer(out);
        } catch (...) {
            if (error) *error = "writer raised an exception";
        }
        out.flush();
        if (!out || !writerSucceeded) {
            if (error && error->empty()) *error = "failed while writing temporary file";
            out.close();
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }

#ifdef _WIN32
    if (!::MoveFileExW(temporary.c_str(), destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (error) *error = "MoveFileExW failed with error " + std::to_string(::GetLastError());
        std::filesystem::remove(temporary, ec);
        return false;
    }
#else
    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        if (error) *error = ec.message();
        std::filesystem::remove(temporary, ec);
        return false;
    }
#endif
    return true;
}

bool AtomicWriteBytes(
    const std::filesystem::path& destination,
    const char* bytes,
    std::size_t size,
    std::string* error
) {
    if (size > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        if (error) *error = "byte payload exceeds stream size limit";
        return false;
    }
    return AtomicWriteFile(destination, [bytes, size](std::ostream& out) {
        if (size != 0) out.write(bytes, static_cast<std::streamsize>(size));
        return static_cast<bool>(out);
    }, error);
}

bool AtomicWriteText(
    const std::filesystem::path& destination,
    std::string_view text,
    std::string* error
) {
    return AtomicWriteBytes(destination, text.data(), text.size(), error);
}

} // namespace Vixen
