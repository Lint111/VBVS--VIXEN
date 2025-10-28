#pragma once

// Compatibility shim: forward shader-management-style calls to the global
// project hash implementation (Vixen::Hash). This keeps existing call sites
// that include ShaderManagement/Hash.h working while the canonical
// implementation lives in include/Hash.h.

#include "Hash.h"

namespace ShaderManagement {

inline std::string ComputeSHA256Hex(const void* data, size_t len) {
    return Vixen::Hash::ComputeSHA256Hex(data, len);
}

inline std::string ComputeSHA256Hex(const std::vector<uint8_t>& data) {
    return Vixen::Hash::ComputeSHA256Hex(data);
}

inline std::string ComputeSHA256HexFromUint32Vec(const std::vector<uint32_t>& data) {
    return Vixen::Hash::ComputeSHA256HexFromUint32Vec(data);
}

} // namespace ShaderManagement
