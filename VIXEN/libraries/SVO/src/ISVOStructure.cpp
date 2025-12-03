#include "pch.h"
#include "ISVOStructure.h"
#include <fstream>

namespace Vixen::SVO {

// Default implementation for file I/O
bool ISVOStructure::saveToFile(const std::string& filename) const {
    // Serialize to binary
    std::vector<uint8_t> data = serialize();

    // Write to file
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return file.good();
}

bool ISVOStructure::loadFromFile(const std::string& filename) {
    // Read file
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }

    // Get file size
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read into buffer
    std::vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        return false;
    }

    // Deserialize
    return deserialize(data);
}

} // namespace Vixen::SVO
