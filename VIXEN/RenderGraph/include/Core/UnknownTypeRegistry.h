#pragma once

#include "Headers.h"
#include "Data/VariantDescriptors.h"
#include <unordered_map>
#include <functional>
#include <iostream>

namespace Vixen::RenderGraph {

/**
 * @brief Hash computation for struct layouts
 *
 * Computes deterministic hash from field metadata (name, offset, size, type).
 * Used for discovering unknown types at startup.
 */
class LayoutHasher {
public:
    /**
     * @brief Compute layout hash from RuntimeStructDescriptor
     */
    static uint64_t ComputeHash(const RuntimeStructDescriptor& desc) {
        uint64_t hash = 0xcbf29ce484222325ULL;  // FNV-1a offset basis

        // Hash struct name
        hash = HashString(hash, desc.structName);

        // Hash total size
        hash = HashValue(hash, desc.totalSize);

        // Hash each field
        for (const auto& field : desc.fields) {
            hash = HashString(hash, field.name);
            hash = HashValue(hash, field.offset);
            hash = HashValue(hash, field.size);
            hash = HashValue(hash, static_cast<uint8_t>(field.baseType));
            hash = HashValue(hash, field.componentCount);
            hash = HashValue(hash, field.isArray ? 1 : 0);
            hash = HashValue(hash, field.arraySize);
        }

        return hash;
    }

private:
    // FNV-1a string hashing
    static uint64_t HashString(uint64_t hash, const std::string& str) {
        const uint64_t prime = 0x100000001b3ULL;
        for (char c : str) {
            hash ^= static_cast<uint64_t>(c);
            hash *= prime;
        }
        return hash;
    }

    // FNV-1a value hashing
    template<typename T>
    static uint64_t HashValue(uint64_t hash, T value) {
        const uint64_t prime = 0x100000001b3ULL;
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
        for (size_t i = 0; i < sizeof(T); ++i) {
            hash ^= static_cast<uint64_t>(bytes[i]);
            hash *= prime;
        }
        return hash;
    }
};

/**
 * @brief Registry for runtime-discovered descriptor types
 *
 * Phase H: Hybrid discovery system
 * - At startup, scans all SDI files for layout hashes
 * - Compares against known compile-time types
 * - Creates RuntimeStructDescriptor for unknown types
 * - Logs to user with registration instructions
 *
 * User can then choose to:
 * 1. Keep runtime binding (flexible, slower)
 * 2. Promote to compile-time by adding to RESOURCE_TYPE_REGISTRY (fast, rigid)
 */
class UnknownTypeRegistry {
public:
    /**
     * @brief Get singleton instance
     */
    static UnknownTypeRegistry& GetInstance() {
        static UnknownTypeRegistry instance;
        return instance;
    }

    /**
     * @brief Register known compile-time type hash
     *
     * Called during startup for each type in RESOURCE_TYPE_REGISTRY.
     * Prevents false positives in discovery scan.
     */
    void RegisterKnownType(uint64_t layoutHash, const std::string& typeName) {
        knownTypes[layoutHash] = typeName;
    }

    /**
     * @brief Check if type is known at compile-time
     */
    bool IsKnownType(uint64_t layoutHash) const {
        return knownTypes.find(layoutHash) != knownTypes.end();
    }

    /**
     * @brief Register runtime-discovered type
     *
     * Called during discovery scan when SDI hash doesn't match any known type.
     * Stores descriptor for runtime use and logs to user.
     */
    void RegisterUnknownType(const RuntimeStructDescriptor& desc) {
        uint64_t hash = LayoutHasher::ComputeHash(desc);

        if (IsKnownType(hash)) {
            return;  // Already registered as compile-time type
        }

        if (unknownTypes.find(hash) != unknownTypes.end()) {
            return;  // Already registered as runtime type
        }

        unknownTypes[hash] = desc;

        // Log to user
        std::cout << "[UnknownTypeRegistry] Discovered new runtime struct: "
                  << desc.structName << " (hash: 0x" << std::hex << hash << std::dec << ")\n"
                  << "  Total size: " << desc.totalSize << " bytes\n"
                  << "  Fields: " << desc.fields.size() << "\n";

        for (const auto& field : desc.fields) {
            std::cout << "    - " << field.name
                      << " (offset: " << field.offset
                      << ", size: " << field.size << ")\n";
        }

        std::cout << "  To promote to compile-time, add to RESOURCE_TYPE_REGISTRY in ResourceVariant.h\n";
    }

    /**
     * @brief Lookup runtime descriptor by hash
     */
    std::optional<RuntimeStructDescriptor> FindUnknownType(uint64_t layoutHash) const {
        auto it = unknownTypes.find(layoutHash);
        if (it != unknownTypes.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * @brief Get all registered runtime types
     */
    const std::unordered_map<uint64_t, RuntimeStructDescriptor>& GetUnknownTypes() const {
        return unknownTypes;
    }

    /**
     * @brief Clear all runtime types (for testing)
     */
    void ClearUnknownTypes() {
        unknownTypes.clear();
    }

private:
    UnknownTypeRegistry() = default;
    ~UnknownTypeRegistry() = default;
    UnknownTypeRegistry(const UnknownTypeRegistry&) = delete;
    UnknownTypeRegistry& operator=(const UnknownTypeRegistry&) = delete;

    // Known compile-time types (from RESOURCE_TYPE_REGISTRY)
    std::unordered_map<uint64_t, std::string> knownTypes;

    // Runtime-discovered types (from SDI discovery scan)
    std::unordered_map<uint64_t, RuntimeStructDescriptor> unknownTypes;
};

} // namespace Vixen::RenderGraph
