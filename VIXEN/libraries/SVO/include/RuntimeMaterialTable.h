#pragma once

#include "VoxelChannelFormat.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Vixen::SVO {

// Runtime material rows are deliberately not generated into the consumer. The generated
// vocabulary owns the row shape; this class only reads pack-owned instances and resolves
// declaration-order references for the renderer/editor seams.
class RuntimeMaterialTable {
public:
    static RuntimeMaterialTable Neutral(std::string diagnostic = {});
    static RuntimeMaterialTable LoadFromPackRoot(const std::string& packRoot);

    bool Loaded() const noexcept { return loaded_; }
    const std::string& Diagnostic() const noexcept { return diagnostic_; }
    const std::vector<MaterialPaletteSpec>& Rows() const noexcept { return rows_; }

    // Look-dev presets have no authored document ids. They use this pack-owned declaration
    // order seam; authored documents use the frozen global content-id handle and are converted
    // to this row reference at the runtime consumer.
    std::uint32_t RefAt(std::size_t declarationIndex) const noexcept {
        return declarationIndex < rows_.size() ? static_cast<std::uint32_t>(declarationIndex + 1u)
                                                : kMaterialRefNone;
    }

    // Material references reserve zero for "none"; pack declaration row i is reference i+1.
    std::uint32_t RefFor(std::string_view id) const noexcept;

    // The authoring/bake contract uses the frozen global 1-based content-id table. These two
    // methods are the one explicit bridge at the runtime consumer: serialized MaterialRef values
    // remain global handles, while the GPU palette and stored coverage use pack declaration rows.
    std::uint32_t GlobalRefFor(std::string_view id) const noexcept;
    std::uint32_t RuntimeRefForGlobal(std::uint32_t globalRef) const noexcept;

    // Returns the existing fixed-capacity material SSBO shape. Unloaded tables contain only
    // the neutral fallback; loaded rows occupy their declaration-order references.
    std::vector<std::uint8_t> BuildGpuBuffer() const;

private:
    bool loaded_ = false;
    std::string diagnostic_;
    std::vector<std::string> ids_;
    std::vector<MaterialPackRow> rawRows_;
    std::vector<MaterialPaletteSpec> rows_;
    std::vector<std::uint32_t> globalRefs_;
};

}  // namespace Vixen::SVO
