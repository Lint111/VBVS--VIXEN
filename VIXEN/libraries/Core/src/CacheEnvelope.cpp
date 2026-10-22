#include "CacheEnvelope.h"

#include "RuntimeCachePaths.h"

#include <cstring>
#include <fstream>
#include <system_error>

namespace Vixen {

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
    if (Checksum64(payload.data(), payload.size()) != header.payloadChecksum) {
        payload.clear();
        if (error) *error = "cache payload checksum mismatch";
        return CacheFileStatus::Rejected;
    }
    return CacheFileStatus::Loaded;
}

} // namespace Vixen
