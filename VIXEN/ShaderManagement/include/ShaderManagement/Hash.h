#pragma once

#include <string>
#include <vector>
#pragma once

// Redirect to project-level hash header.
#include "Hash.h"


inline std::string ComputeSHA256HexFromUint32Vec(const std::vector<uint32_t>& data) {
    if (data.empty()) return ComputeSHA256Hex(nullptr, 0);
    return ComputeSHA256Hex(reinterpret_cast<const void*>(data.data()), data.size() * sizeof(uint32_t));
}

} // namespace ShaderManagement
