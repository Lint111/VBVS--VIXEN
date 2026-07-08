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
// On-disk format is unchanged: CacheWriter emits byte-for-byte what the old hand-written
// out.write() call sequences did, so this is a mechanics refactor, not a format bump.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace CashSystem {

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
