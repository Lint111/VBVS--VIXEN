#include "CacheEnvelope.h"

#include "RuntimeCachePaths.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Vixen {

namespace {

enum class CacheFileReadStatus {
    Loaded,
    Missing,
    TooLarge,
    IoError,
};

CacheFileReadStatus ReadCacheFile(
    const std::filesystem::path& path,
    std::vector<char>& bytes,
    std::string* error
) {
#ifdef _WIN32
    // During FileRenameInfoEx POSIX replacement, Windows briefly reports the old name as
    // delete-pending; CreateFile documents ERROR_ACCESS_DENIED for that transition.  Treat only
    // that state and a direct sharing violation as transient; all other open errors are final.
    constexpr int kMaxReadOpenAttempts = 8;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD openError = ERROR_SUCCESS;
    for (int attempt = 0; attempt < kMaxReadOpenAttempts; ++attempt) {
        file = ::CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_POSIX_SEMANTICS,
            nullptr);
        if (file != INVALID_HANDLE_VALUE) break;

        openError = ::GetLastError();
        if ((openError != ERROR_ACCESS_DENIED && openError != ERROR_SHARING_VIOLATION) ||
            attempt + 1 == kMaxReadOpenAttempts) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (file == INVALID_HANDLE_VALUE) {
        if (openError == ERROR_FILE_NOT_FOUND || openError == ERROR_PATH_NOT_FOUND) {
            return CacheFileReadStatus::Missing;
        }
        if (error) *error = "cannot open cache file (Windows error " +
                            std::to_string(openError) + ")";
        return CacheFileReadStatus::IoError;
    }

    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        static_cast<unsigned long long>(size.QuadPart) >
            static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        if (error) *error = "cannot determine cache file size";
        ::CloseHandle(file);
        return CacheFileReadStatus::IoError;
    }
    if (static_cast<unsigned long long>(size.QuadPart) >
        sizeof(CacheEnvelopeHeader) + kMaxCachePayloadBytes) {
        ::CloseHandle(file);
        return CacheFileReadStatus::TooLarge;
    }

    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD read = 0;
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - offset, std::numeric_limits<DWORD>::max()));
        if (!::ReadFile(file, bytes.data() + offset, requested, &read, nullptr) || read == 0) {
            if (error) *error = "failed while reading cache file (Windows error " +
                                std::to_string(::GetLastError()) + ")";
            ::CloseHandle(file);
            bytes.clear();
            return CacheFileReadStatus::IoError;
        }
        offset += read;
    }
    ::CloseHandle(file);
    return CacheFileReadStatus::Loaded;
#else
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        std::error_code existsError;
        if (!std::filesystem::exists(path, existsError) && !existsError) {
            return CacheFileReadStatus::Missing;
        }
        if (error) *error = "cannot open cache file";
        return CacheFileReadStatus::IoError;
    }
    const auto end = in.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) >
                       static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        if (error) *error = "cannot determine cache file size";
        return CacheFileReadStatus::IoError;
    }
    if (static_cast<std::uint64_t>(end) > sizeof(CacheEnvelopeHeader) + kMaxCachePayloadBytes) {
        return CacheFileReadStatus::TooLarge;
    }
    bytes.resize(static_cast<std::size_t>(end));
    in.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!in) {
            if (error) *error = "failed while reading cache file";
            bytes.clear();
            return CacheFileReadStatus::IoError;
        }
    }
    return CacheFileReadStatus::Loaded;
#endif
}

} // namespace

std::uint64_t CacheTypeHash(std::string_view value) noexcept {
    return Checksum64(value.data(), value.size());
}

bool WriteCacheEnvelopeBytesAtomically(
    const std::filesystem::path& destination,
    std::uint64_t deviceIdentity,
    std::string_view cacherType,
    const std::vector<char>& payload,
    std::string* error
) {
    if (payload.size() > kMaxCachePayloadBytes) {
        if (error) *error = "payload exceeds cache size limit";
        return false;
    }

    CacheEnvelopeHeader header{};
    std::memcpy(header.magic, kCacheEnvelopeMagic.data(), kCacheEnvelopeMagic.size());
    header.deviceIdentity = deviceIdentity;
    header.cacherTypeHash = CacheTypeHash(cacherType);
    header.payloadSize = payload.size();
    header.payloadChecksum = Checksum64(payload.data(), payload.size());

    std::vector<char> envelope(sizeof(header) + payload.size());
    std::memcpy(envelope.data(), &header, sizeof(header));
    if (!payload.empty()) {
        std::memcpy(envelope.data() + sizeof(header), payload.data(), payload.size());
    }
    return AtomicWriteBytes(destination, envelope.data(), envelope.size(), error);
}

CacheFileStatus ReadCacheEnvelopeBytes(
    const std::filesystem::path& path,
    std::uint64_t expectedDeviceIdentity,
    std::string_view expectedCacherType,
    std::vector<char>& payload,
    std::string* error
) {
    payload.clear();
    std::vector<char> fileBytes;
    const auto readStatus = ReadCacheFile(path, fileBytes, error);
    if (readStatus == CacheFileReadStatus::Missing) {
        return CacheFileStatus::Missing;
    }
    if (readStatus == CacheFileReadStatus::IoError) {
        return CacheFileStatus::IoError;
    }
    if (readStatus == CacheFileReadStatus::TooLarge ||
        fileBytes.size() < sizeof(CacheEnvelopeHeader) ||
        fileBytes.size() > sizeof(CacheEnvelopeHeader) + kMaxCachePayloadBytes) {
        if (error) *error = "file size is outside the envelope bounds";
        return CacheFileStatus::Rejected;
    }

    CacheEnvelopeHeader header{};
    std::memcpy(&header, fileBytes.data(), sizeof(header));
    if (std::memcmp(header.magic, kCacheEnvelopeMagic.data(), kCacheEnvelopeMagic.size()) != 0) {
        if (error) *error = "cache envelope magic mismatch";
        return CacheFileStatus::Rejected;
    }
    if (header.version != kCacheEnvelopeVersion) {
        if (error) *error = "cache envelope version mismatch";
        return CacheFileStatus::Rejected;
    }
    if (header.deviceIdentity != expectedDeviceIdentity) {
        if (error) *error = "cache belongs to a different device identity";
        return CacheFileStatus::Rejected;
    }
    if (header.cacherTypeHash != CacheTypeHash(expectedCacherType)) {
        if (error) *error = "cache belongs to a different cacher type";
        return CacheFileStatus::Rejected;
    }
    const auto expectedPayloadSize = static_cast<std::uint64_t>(fileBytes.size()) - sizeof(header);
    if (header.payloadSize != expectedPayloadSize || header.payloadSize > kMaxCachePayloadBytes) {
        if (error) *error = "cache payload size mismatch";
        return CacheFileStatus::Rejected;
    }

    payload.resize(static_cast<std::size_t>(header.payloadSize));
    if (!payload.empty()) {
        std::memcpy(payload.data(), fileBytes.data() + sizeof(header), payload.size());
    }
    if (Checksum64(payload.data(), payload.size()) != header.payloadChecksum) {
        payload.clear();
        if (error) *error = "cache payload checksum mismatch";
        return CacheFileStatus::Rejected;
    }
    return CacheFileStatus::Loaded;
}

} // namespace Vixen
