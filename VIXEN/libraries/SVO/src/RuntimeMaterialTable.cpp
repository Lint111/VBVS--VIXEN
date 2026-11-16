#include "RuntimeMaterialTable.h"

#include "ShellOctreeGpu.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace Vixen::SVO {
namespace {

std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1u);
}

std::string ValueAfter(const std::string& line, std::string_view key) {
    const std::string marker = std::string(key) + ":";
    const auto start = line.find(marker);
    if (start == std::string::npos) return {};
    std::string value = line.substr(start + marker.size());
    const auto comma = value.find(',');
    if (comma != std::string::npos) value.resize(comma);
    const auto brace = value.find('}');
    if (brace != std::string::npos) value.resize(brace);
    value = Trim(value);
    if (value.size() >= 2u && value.front() == '"' && value.back() == '"')
        value = value.substr(1u, value.size() - 2u);
    return Trim(value);
}

bool ParseByte(const std::string& text, std::uint8_t& value) {
    try {
        const unsigned long parsed = std::stoul(text);
        if (parsed > 255ul) return false;
        value = static_cast<std::uint8_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseColor(const std::string& text, std::uint32_t& value) {
    if (text.size() != 7u || text.front() != '#') return false;
    try {
        value = static_cast<std::uint32_t>(std::stoul(text.substr(1u), nullptr, 16));
        return value <= 0xFFFFFFu;
    } catch (...) {
        return false;
    }
}

bool ReadRows(const std::filesystem::path& path,
              std::vector<std::string>& ids,
              std::vector<MaterialPackRow>& rows,
              std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot read material table: " + path.string();
        return false;
    }
    const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::string currentNamespace;
    std::istringstream lines(source);
    std::string line;
    while (std::getline(lines, line)) {
        const auto namespaceAt = line.find("namespace");
        if (namespaceAt != std::string::npos) {
            const auto open = line.find('{', namespaceAt);
            std::string name = line.substr(namespaceAt + std::string("namespace").size(),
                                           open == std::string::npos ? std::string::npos : open - namespaceAt - 9u);
            currentNamespace = Trim(name);
        }
        // The pack declaration keeps `type: material` on the declaration header,
        // while the row fields are emitted on the following line.  Match the
        // generated row shape rather than requiring all fields on one line.
        if (line.find("id:") == std::string::npos ||
            line.find("color_hex:") == std::string::npos ||
            line.find("default_edge_smoothness:") == std::string::npos ||
            line.find("visibility:") == std::string::npos)
            continue;
        const std::string rawId = ValueAfter(line, "id");
        const std::string colorText = ValueAfter(line, "color_hex");
        const std::string smoothText = ValueAfter(line, "default_edge_smoothness");
        const std::string visibilityText = ValueAfter(line, "visibility");
        if (rawId.empty() || colorText.empty() || smoothText.empty() || visibilityText.empty()) {
            error = "material row is missing a generated row field: " + path.string();
            return false;
        }
        std::uint32_t color = 0u;
        std::uint8_t smoothness = 0u;
        std::uint8_t visibility = 0u;
        if (!ParseColor(colorText, color) || !ParseByte(smoothText, smoothness) ||
            !ParseByte(visibilityText, visibility) || visibility > 5u) {
            error = "material row has an invalid generated row field: " + path.string();
            return false;
        }
        std::string id = rawId;
        if (id.find(':') == std::string::npos && !currentNamespace.empty())
            id = currentNamespace + ":" + id;
        ids.push_back(std::move(id));
        rows.push_back(MaterialPackRow{ids.back().c_str(), color, smoothness, visibility});
    }
    return true;
}

}  // namespace

RuntimeMaterialTable RuntimeMaterialTable::Neutral(std::string diagnostic) {
    RuntimeMaterialTable result;
    result.diagnostic_ = std::move(diagnostic);
    return result;
}

RuntimeMaterialTable RuntimeMaterialTable::LoadFromPackRoot(const std::string& packRoot) {
    RuntimeMaterialTable result;
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::weakly_canonical(packRoot, ec);
    if (ec || !std::filesystem::is_directory(root))
        return Neutral("material table unavailable: pack root is not a directory: " + packRoot);

    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().filename() == "materials.utdl") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) return Neutral("material table unavailable: no materials.utdl under " + root.string());

    std::string error;
    for (const auto& file : files) {
        if (!ReadRows(file, result.ids_, result.rawRows_, error)) return Neutral(std::move(error));
    }
    if (result.rawRows_.empty()) return Neutral("material table unavailable: no material rows under " + root.string());

    // MaterialPackRow carries borrowed ids.  Rebind them after all strings have
    // been collected so vector growth cannot leave stale pointers in the rows.
    for (std::size_t i = 0; i < result.rawRows_.size(); ++i)
        result.rawRows_[i].id = result.ids_[i].c_str();

    result.rows_.reserve(result.rawRows_.size());
    for (std::size_t i = 0; i < result.rawRows_.size(); ++i)
        result.rows_.push_back(MaterialPaletteSpecFromPackRow(static_cast<std::uint32_t>(i + 1u), result.rawRows_[i]));

    // The pack table is declaration-ordered for GPU palette access, but authored MaterialRef
    // operands are the global 1-based content-id handles. A core pack carries the frozen table
    // beside its content directory; keep the mapping parallel to the runtime rows so the editor
    // can convert once at the consumer boundary instead of changing the serialized contract.
    const std::filesystem::path idCandidates[] = {
        root / "content" / "content-ids.g.json",
        root / "content-ids.g.json",
        root.parent_path() / "content-ids.g.json",
    };
    for (const auto& candidate : idCandidates) {
        std::ifstream idsFile(candidate, std::ios::binary);
        if (!idsFile) continue;
        const std::string source((std::istreambuf_iterator<char>(idsFile)), std::istreambuf_iterator<char>());
        const std::size_t markerAt = source.find("\"ids\"");
        const std::size_t begin = markerAt == std::string::npos
            ? std::string::npos : source.find('[', markerAt + 5u);
        if (begin == std::string::npos) continue;
        std::vector<std::string> globalIds;
        for (std::size_t at = begin + 1u; at < source.size() && source[at] != ']';) {
            if (source[at] != '"') {
                ++at;
                continue;
            }
            const std::size_t end = source.find('"', at + 1u);
            if (end == std::string::npos) break;
            globalIds.emplace_back(source.substr(at + 1u, end - at - 1u));
            at = end + 1u;
        }
        if (globalIds.empty()) continue;
        result.globalRefs_.reserve(result.ids_.size());
        for (const auto& id : result.ids_) {
            const auto found = std::find(globalIds.begin(), globalIds.end(), id);
            result.globalRefs_.push_back(found == globalIds.end()
                ? kMaterialRefNone
                : static_cast<std::uint32_t>(found - globalIds.begin()) + 1u);
        }
        break;
    }
    result.loaded_ = true;
    return result;
}

std::uint32_t RuntimeMaterialTable::RefFor(std::string_view id) const noexcept {
    for (std::size_t i = 0; i < ids_.size(); ++i)
        if (ids_[i] == id) return static_cast<std::uint32_t>(i + 1u);
    return kMaterialRefNone;
}

std::uint32_t RuntimeMaterialTable::GlobalRefFor(std::string_view id) const noexcept {
    for (std::size_t i = 0; i < ids_.size(); ++i) {
        if (ids_[i] == id && i < globalRefs_.size()) return globalRefs_[i];
    }
    return kMaterialRefNone;
}

std::uint32_t RuntimeMaterialTable::RuntimeRefForGlobal(std::uint32_t globalRef) const noexcept {
    if (globalRef == kMaterialRefNone) return kMaterialRefNone;
    for (std::size_t i = 0; i < globalRefs_.size(); ++i) {
        if (globalRefs_[i] == globalRef) return static_cast<std::uint32_t>(i + 1u);
    }
    return kMaterialRefNone;
}

std::vector<std::uint8_t> RuntimeMaterialTable::BuildGpuBuffer() const {
    std::vector<GPUMaterial> palette(64u,
        GPUMaterial{{0.5f, 0.5f, 0.5f}, 0.5f, 0.0f, 0.0f, {0.0f, 0.0f}});
    for (const MaterialPaletteSpec& row : rows_) {
        if (row.id >= palette.size()) break;
        palette[row.id] = GPUMaterial{{row.linearAlbedo[0], row.linearAlbedo[1], row.linearAlbedo[2]},
                                       0.5f, 0.0f, 0.0f, {0.0f, 0.0f}};
    }
    std::vector<std::uint8_t> bytes(palette.size() * sizeof(GPUMaterial));
    std::memcpy(bytes.data(), palette.data(), bytes.size());
    return bytes;
}

}  // namespace Vixen::SVO
