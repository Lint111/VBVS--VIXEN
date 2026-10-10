#pragma once

// Shared bounds-checked binary cache codec (audit V-M5..M8, N6, N7).
//
// Three cachers (VoxelSceneCacher, ShaderModuleCacher, PipelineCacher) each hand-rolled their
// own disk deserialization and trusted lengths straight off disk before allocating. This header
// is the one place that discipline lives now: every read validates against both a caller-supplied
// cap and the bytes actually remaining in the stream, and checks size*sizeof(T) for overflow
// before it ever reaches resize()/read(). Nothing here throws — a corrupt or truncated file just
// makes Ok() return false, and callers regenerate the cache from scratch (the uniform failure
// strategy the audit asked for).
//
// The cacher-specific payload is unchanged: CacheWriter emits byte-for-byte what the old
// hand-written out.write() call sequences did. CacheEnvelopeHeader adds a new outer format layer
// for versioning, device/type identity, bounded lengths, and corruption detection.

#include <array>
#include <atomic>
#include <chrono>
#include "RuntimeCachePaths.h"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

namespace CashSystem {

// The outer envelope is shared by every production cacher. Its metadata is checked before the
// existing cacher-specific payload parser sees any bytes. The payload itself remains unchanged.
inline constexpr std::array<char, 8> kCacheEnvelopeMagic{'V', 'X', 'C', 'A', 'C', 'H', 'E', '1'};
inline constexpr std::uint32_t kCacheEnvelopeVersion = 1;
inline constexpr std::uint64_t kMaxCachePayloadBytes = 512ULL * 1024ULL * 1024ULL;

struct CacheEnvelopeHeader {
    char magic[8]{};
    std::uint32_t version = kCacheEnvelopeVersion;
    std::uint32_t reserved = 0;
    std::uint64_t deviceIdentity = 0;
    std::uint64_t cacherTypeHash = 0;
    std::uint64_t payloadSize = 0;
    std::uint64_t payloadChecksum = 0;
};
static_assert(sizeof(CacheEnvelopeHeader) == 48);

enum class CacheFileStatus {
    Loaded,
    Missing,
    Rejected,
    IoError,
};

inline std::uint64_t CacheChecksum64(const void* data, std::size_t size) noexcept {
    return Vixen::Checksum64(data, size);
}

inline std::uint64_t CacheTypeHash(std::string_view value) noexcept {
    return CacheChecksum64(value.data(), value.size());
}

inline std::uint64_t CacheProcessId() noexcept {
    return Vixen::CurrentProcessId();
}

inline std::filesystem::path UniqueCacheTemporaryPath(
    const std::filesystem::path& destination,
    std::string_view purpose
) {
    static std::atomic<std::uint64_t> sequence{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto filename = destination.filename().string() + std::string(purpose) + "." +
        std::to_string(CacheProcessId()) + "." + std::to_string(stamp) + "." +
        std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
    return destination.parent_path() / filename;
}

inline bool WriteCacheEnvelopeBytesAtomically(
    const std::filesystem::path& destination,
    std::uint64_t deviceIdentity,
    std::string_view cacherType,
    const std::vector<char>& payload,
    std::string* error = nullptr
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
    header.payloadChecksum = CacheChecksum64(payload.data(), payload.size());

    std::vector<char> envelope(sizeof(header) + payload.size());
    std::memcpy(envelope.data(), &header, sizeof(header));
    if (!payload.empty()) {
        std::memcpy(envelope.data() + sizeof(header), payload.data(), payload.size());
    }
    return Vixen::AtomicWriteBytes(destination, envelope.data(), envelope.size(), error);
}

inline CacheFileStatus ReadCacheEnvelopeBytes(
    const std::filesystem::path& path,
    std::uint64_t expectedDeviceIdentity,
    std::string_view expectedCacherType,
    std::vector<char>& payload,
    std::string* error = nullptr
) {
    payload.clear();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) {
            if (error) *error = "cannot inspect cache file: " + ec.message();
            return CacheFileStatus::IoError;
        }
        return CacheFileStatus::Missing;
    }

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        if (error) *error = "cannot open cache file";
        return CacheFileStatus::IoError;
    }
    const auto end = in.tellg();
    if (end < static_cast<std::streamoff>(sizeof(CacheEnvelopeHeader)) ||
        static_cast<std::uint64_t>(end) > sizeof(CacheEnvelopeHeader) + kMaxCachePayloadBytes) {
        if (error) *error = "file size is outside the envelope bounds";
        return CacheFileStatus::Rejected;
    }

    in.seekg(0, std::ios::beg);
    CacheEnvelopeHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in) {
        if (error) *error = "truncated cache envelope header";
        return CacheFileStatus::Rejected;
    }
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
    const auto expectedPayloadSize = static_cast<std::uint64_t>(end) - sizeof(header);
    if (header.payloadSize != expectedPayloadSize || header.payloadSize > kMaxCachePayloadBytes) {
        if (error) *error = "cache payload size mismatch";
        return CacheFileStatus::Rejected;
    }

    payload.resize(static_cast<std::size_t>(header.payloadSize));
    if (!payload.empty()) {
        in.read(payload.data(), static_cast<std::streamsize>(payload.size()));
        if (!in) {
            payload.clear();
            if (error) *error = "truncated cache payload";
            return CacheFileStatus::Rejected;
        }
    }
    if (CacheChecksum64(payload.data(), payload.size()) != header.payloadChecksum) {
        payload.clear();
        if (error) *error = "cache payload checksum mismatch";
        return CacheFileStatus::Rejected;
    }
    return CacheFileStatus::Loaded;
}

template <typename Serializer>
bool SerializeCacheFileAtomically(
    const std::filesystem::path& destination,
    std::uint64_t deviceIdentity,
    std::string_view cacherType,
    Serializer&& serialize,
    std::string* error = nullptr
) {
    std::error_code ec;
    if (!destination.parent_path().empty()) {
        std::filesystem::create_directories(destination.parent_path(), ec);
        if (ec) {
            if (error) *error = "create directory failed: " + ec.message();
            return false;
        }
    }

    const auto payloadPath = UniqueCacheTemporaryPath(destination, ".payload");
    bool serialized = false;
    try {
        serialized = serialize(payloadPath);
    } catch (...) {
        if (error) *error = "cacher serializer raised an exception";
    }
    if (!serialized) {
        std::filesystem::remove(payloadPath, ec);
        if (error && error->empty()) *error = "cacher serializer failed";
        return false;
    }

    // Some cache types are intentionally no-op serializers. Preserve that behavior without
    // publishing an empty envelope that would claim the cacher has persistent state.
    if (!std::filesystem::exists(payloadPath, ec)) return !ec;
    std::ifstream in(payloadPath, std::ios::binary | std::ios::ate);
    if (!in) {
        std::filesystem::remove(payloadPath, ec);
        if (error) *error = "cannot read serialized payload";
        return false;
    }
    const auto end = in.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > kMaxCachePayloadBytes) {
        in.close();
        std::filesystem::remove(payloadPath, ec);
        if (error) *error = "serialized payload exceeds cache size limit";
        return false;
    }
    std::vector<char> payload(static_cast<std::size_t>(end));
    in.seekg(0, std::ios::beg);
    if (!payload.empty()) in.read(payload.data(), static_cast<std::streamsize>(payload.size()));
    const bool payloadRead = static_cast<bool>(in) || payload.empty();
    in.close();
    std::filesystem::remove(payloadPath, ec);
    if (!payloadRead) {
        if (error) *error = "failed while reading serialized payload";
        return false;
    }
    return WriteCacheEnvelopeBytesAtomically(destination, deviceIdentity, cacherType, payload, error);
}

template <typename Deserializer>
CacheFileStatus DeserializeCacheFile(
    const std::filesystem::path& path,
    std::uint64_t expectedDeviceIdentity,
    std::string_view expectedCacherType,
    Deserializer&& deserialize,
    std::string* error = nullptr
) {
    std::vector<char> payload;
    const auto readStatus = ReadCacheEnvelopeBytes(
        path, expectedDeviceIdentity, expectedCacherType, payload, error);
    if (readStatus != CacheFileStatus::Loaded) return readStatus;

    const auto payloadPath = UniqueCacheTemporaryPath(path, ".validated");
    std::error_code ec;
    {
        std::ofstream out(payloadPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "cannot create validated payload file";
            return CacheFileStatus::IoError;
        }
        if (!payload.empty()) out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        out.flush();
        if (!out) {
            if (error) *error = "cannot materialize validated payload";
            out.close();
            std::filesystem::remove(payloadPath, ec);
            return CacheFileStatus::IoError;
        }
    }

    bool loaded = false;
    try {
        loaded = deserialize(payloadPath);
    } catch (...) {
        if (error) *error = "cacher deserializer raised an exception";
    }
    std::filesystem::remove(payloadPath, ec);
    if (!loaded) {
        if (error && error->empty()) *error = "cacher payload parser rejected cache";
        return CacheFileStatus::Rejected;
    }
    return CacheFileStatus::Loaded;
}

class CacheWriter {
public:
    explicit CacheWriter(std::ofstream& out) : m_out(out) {}

    template <typename T>
    void WritePod(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "WritePod requires a trivially copyable type");
        m_out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    void WriteString(const std::string& s) {
        WritePod(static_cast<uint64_t>(s.size()));
        if (!s.empty()) {
            m_out.write(s.data(), static_cast<std::streamsize>(s.size()));
        }
    }

    template <typename T>
    void WriteVector(const std::vector<T>& vec) {
        static_assert(std::is_trivially_copyable_v<T>, "WriteVector requires a trivially copyable element type");
        WritePod(static_cast<uint64_t>(vec.size()));
        if (!vec.empty()) {
            m_out.write(reinterpret_cast<const char*>(vec.data()),
                        static_cast<std::streamsize>(vec.size() * sizeof(T)));
        }
    }

    // 32-bit-length-prefixed variants — for cachers whose existing on-disk format used a
    // uint32_t length (ShaderModuleCacher). Byte-identical to the original hand-written
    // out.write() sequences; the 64-bit variants above are for VoxelSceneCacher's format.
    void WriteString32(const std::string& s) {
        WritePod(static_cast<uint32_t>(s.size()));
        if (!s.empty()) {
            m_out.write(s.data(), static_cast<std::streamsize>(s.size()));
        }
    }

    template <typename T>
    void WriteVector32(const std::vector<T>& vec) {
        static_assert(std::is_trivially_copyable_v<T>, "WriteVector32 requires a trivially copyable element type");
        WritePod(static_cast<uint32_t>(vec.size()));
        if (!vec.empty()) {
            m_out.write(reinterpret_cast<const char*>(vec.data()),
                        static_cast<std::streamsize>(vec.size() * sizeof(T)));
        }
    }

    bool Ok() const { return static_cast<bool>(m_out); }

private:
    std::ofstream& m_out;
};

class CacheReader {
public:
    explicit CacheReader(std::ifstream& in) : m_in(in) {
        if (m_in) {
            const auto cur = m_in.tellg();
            m_in.seekg(0, std::ios::end);
            const auto end = m_in.tellg();
            m_in.seekg(cur, std::ios::beg);
            m_remaining = (end >= cur) ? static_cast<uint64_t>(end - cur) : 0;
        }
    }

    template <typename T>
    bool ReadPod(T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "ReadPod requires a trivially copyable type");
        if (!m_ok || sizeof(T) > m_remaining) {
            m_ok = false;
            return false;
        }
        m_in.read(reinterpret_cast<char*>(&value), sizeof(T));
        if (!m_in) {
            m_ok = false;
            return false;
        }
        m_remaining -= sizeof(T);
        return true;
    }

    // maxLen bounds the string length in bytes; a length beyond either maxLen or the bytes left
    // in the file fails immediately, before any allocation.
    bool ReadString(std::string& s, size_t maxLen) {
        uint64_t len = 0;
        if (!ReadPod(len)) return false;
        if (len > maxLen || len > m_remaining) {
            m_ok = false;
            return false;
        }
        s.resize(static_cast<size_t>(len));
        if (len > 0) {
            m_in.read(s.data(), static_cast<std::streamsize>(len));
            if (!m_in) {
                m_ok = false;
                return false;
            }
        }
        m_remaining -= len;
        return true;
    }

    // maxElems bounds the element count; size*sizeof(T) is widened in uint64_t so it can't wrap,
    // and is rejected outright if it would exceed what's left in the file — resize() never runs
    // on an attacker-controlled length larger than the file itself.
    template <typename T>
    bool ReadVector(std::vector<T>& vec, size_t maxElems) {
        static_assert(std::is_trivially_copyable_v<T>, "ReadVector requires a trivially copyable element type");
        uint64_t count = 0;
        if (!ReadPod(count)) return false;
        if (count > maxElems) {
            m_ok = false;
            return false;
        }
        const uint64_t byteSize = count * static_cast<uint64_t>(sizeof(T));
        if (sizeof(T) != 0 && byteSize / sizeof(T) != count) {  // overflow check
            m_ok = false;
            return false;
        }
        if (byteSize > m_remaining) {
            m_ok = false;
            return false;
        }
        vec.resize(static_cast<size_t>(count));
        if (count > 0) {
            m_in.read(reinterpret_cast<char*>(vec.data()), static_cast<std::streamsize>(byteSize));
            if (!m_in) {
                m_ok = false;
                return false;
            }
        }
        m_remaining -= byteSize;
        return true;
    }

    // 32-bit-length-prefixed variants — mirror WriteString32/WriteVector32 above.
    bool ReadString32(std::string& s, size_t maxLen) {
        uint32_t len = 0;
        if (!ReadPod(len)) return false;
        if (len > maxLen || len > m_remaining) {
            m_ok = false;
            return false;
        }
        s.resize(static_cast<size_t>(len));
        if (len > 0) {
            m_in.read(s.data(), static_cast<std::streamsize>(len));
            if (!m_in) {
                m_ok = false;
                return false;
            }
        }
        m_remaining -= len;
        return true;
    }

    template <typename T>
    bool ReadVector32(std::vector<T>& vec, size_t maxElems) {
        static_assert(std::is_trivially_copyable_v<T>, "ReadVector32 requires a trivially copyable element type");
        uint32_t count = 0;
        if (!ReadPod(count)) return false;
        if (count > maxElems) {
            m_ok = false;
            return false;
        }
        const uint64_t byteSize = static_cast<uint64_t>(count) * static_cast<uint64_t>(sizeof(T));
        if (sizeof(T) != 0 && byteSize / sizeof(T) != count) {  // overflow check
            m_ok = false;
            return false;
        }
        if (byteSize > m_remaining) {
            m_ok = false;
            return false;
        }
        vec.resize(static_cast<size_t>(count));
        if (count > 0) {
            m_in.read(reinterpret_cast<char*>(vec.data()), static_cast<std::streamsize>(byteSize));
            if (!m_in) {
                m_ok = false;
                return false;
            }
        }
        m_remaining -= byteSize;
        return true;
    }

    bool Ok() const { return m_ok; }

private:
    std::ifstream& m_in;
    uint64_t m_remaining = 0;
    bool m_ok = true;
};

}  // namespace CashSystem
