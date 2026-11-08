#pragma once

#include "DescriptorLayoutSpec.h"
#include "ShaderCacheManager.h"
#include "ShaderProgram.h"
#include "SpirvReflectionData.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ShaderManagement {

/**
 * @brief What ShaderBundleBuilder derives from SPIR-V alone: the reflection data and the legacy
 * descriptor layout (null when the layout could not be extracted).
 */
struct CachedReflection {
    std::unique_ptr<SpirvReflectionData> reflection;
    std::unique_ptr<DescriptorLayoutSpec> descriptorLayout;
};

/**
 * @brief Persistent cache of SPIR-V reflection, keyed by the SPIR-V content.
 *
 * Reflection is a pure function of the stages' SPIR-V, entry points, the program name and the
 * pipeline type, so a warm launch skips SPIRV-Reflect after every SPIR-V cache hit. SDI headers
 * are not cached: the builder regenerates them from the restored reflection data, so the
 * generator's own write-if-changed check stays the single authority on the files.
 *
 * Entries live in `<spirv cache dir>/reflection`. Bump kReflectionCacheVersion whenever
 * SpirvReflector, ReflectDescriptorLayout or the reflection data structs change.
 */
class ShaderReflectionCache {
public:
    static constexpr uint32_t kReflectionCacheVersion = 1;

    /// Stores under `spirvCache.GetCacheDirectory() / "reflection"`; disabled with `spirvCache`.
    explicit ShaderReflectionCache(const ShaderCacheManager& spirvCache);

    static std::string MakeKey(const CompiledProgram& program);
    static std::vector<uint32_t> Serialize(const SpirvReflectionData& reflection,
                                           const DescriptorLayoutSpec* descriptorLayout);
    /// nullopt for any malformed payload or one whose descriptor-set iteration order differs
    /// from the recorded one (SDI output order depends on it).
    static std::optional<CachedReflection> Deserialize(const std::vector<uint32_t>& payload);

    std::optional<CachedReflection> Lookup(const CompiledProgram& program);
    bool Store(const CompiledProgram& program, const SpirvReflectionData& reflection,
               const DescriptorLayoutSpec* descriptorLayout);

private:
    ShaderCacheManager store_;
};

} // namespace ShaderManagement
