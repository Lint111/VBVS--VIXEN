#include "RuntimeCachePaths.h"

#include <atomic>
#include <chrono>
#include <string>
#include <cstdlib>
#include <fstream>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace Vixen {

std::filesystem::path RuntimeCacheDirectory() {
    if (const char* overridePath = std::getenv("VIXEN_CACHE_DIR");
        overridePath && *overridePath) {
        return std::filesystem::path(overridePath);
    }
#ifdef VIXEN_RUNTIME_CACHE_DIR
    return std::filesystem::path(VIXEN_RUNTIME_CACHE_DIR);
#else
    return std::filesystem::path("cache");
#endif
}

std::uint64_t CurrentProcessId() noexcept {
#ifdef _WIN32
    return static_cast<std::uint64_t>(::_getpid());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

std::filesystem::path ProcessTemporaryDirectory(std::string_view applicationName) {
    return std::filesystem::temp_directory_path() /
        ("vixen_" + std::string(applicationName) + "_" + std::to_string(CurrentProcessId()));
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
