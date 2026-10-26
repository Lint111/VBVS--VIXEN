#include "Ui/PanelDock.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace Vixen::RenderGraph {

namespace {

constexpr std::array<std::pair<DockRegion, std::string_view>, 5> kRegionNames{{
    {DockRegion::Left, "left"},
    {DockRegion::Right, "right"},
    {DockRegion::Bottom, "bottom"},
    {DockRegion::Overlay, "overlay"},
    {DockRegion::Floating, "floating"},
}};

constexpr std::array<DockRegion, 3> kEdgeRegions{DockRegion::Left, DockRegion::Right, DockRegion::Bottom};

bool IsEdge(DockRegion region) {
    return region == DockRegion::Left || region == DockRegion::Right || region == DockRegion::Bottom;
}

bool ValidRect(const PanelRect& rect) {
    return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
           std::isfinite(rect.height) && rect.width > 0.0f && rect.height > 0.0f;
}

}  // namespace

std::string_view DockRegionName(DockRegion region) {
    for (const auto& [value, name] : kRegionNames) if (value == region) return name;
    return "left";
}

std::optional<DockRegion> ParseDockRegion(std::string_view name) {
    for (const auto& [value, regionName] : kRegionNames) if (regionName == name) return value;
    return std::nullopt;
}

std::string_view DockResultName(DockResult result) {
    switch (result) {
        case DockResult::Ok: return "Ok";
        case DockResult::UnknownPanel: return "UnknownPanel";
        case DockResult::UnknownRegion: return "UnknownRegion";
        case DockResult::NotClosable: return "NotClosable";
        case DockResult::InvalidSize: return "InvalidSize";
    }
    return "Unknown";
}

bool PanelDock::Declare(PanelDecl decl) {
    if (decl.id.empty() || Find(decl.id)) return false;
    decls_.push_back(std::move(decl));
    ResetToDefaults();
    return true;
}

const PanelDecl* PanelDock::Find(std::string_view panelId) const {
    for (const auto& decl : decls_) if (decl.id == panelId) return &decl;
    return nullptr;
}

PanelRow* PanelDock::RowOf(std::string_view panelId) {
    for (auto& row : rows_) if (row.panel == panelId) return &row;
    return nullptr;
}

const PanelRow* PanelDock::RowOf(std::string_view panelId) const {
    for (const auto& row : rows_) if (row.panel == panelId) return &row;
    return nullptr;
}

RegionRow* PanelDock::RegionOf(DockRegion region) {
    for (auto& row : regions_) if (row.region == region) return &row;
    return nullptr;
}

float PanelDock::RegionSize(DockRegion region) const {
    for (const auto& row : regions_) if (row.region == region) return row.size;
    return 0.0f;
}

void PanelDock::ResetToDefaults() {
    rows_.clear();
    regions_.clear();
    for (const DockRegion region : kEdgeRegions) {
        float size = 0.0f;
        for (const auto& decl : decls_) if (decl.region == region) size = std::max(size, decl.size);
        regions_.push_back({region, size});
    }
    std::map<std::pair<DockRegion, uint32_t>, uint32_t> orders;
    for (const auto& decl : decls_) {
        PanelRow row;
        row.panel = decl.id;
        row.region = decl.region;
        row.stack = IsEdge(decl.region) ? decl.stack : 0;
        row.order = orders[{row.region, row.stack}]++;
        row.visible = true;
        row.floatRect = decl.floatRect;
        if (decl.region == DockRegion::Floating) row.focus = ++focusCounter_;
        rows_.push_back(std::move(row));
    }
    Normalize();
    ++revision_;
}

uint32_t PanelDock::NextStack(DockRegion region) const {
    uint32_t next = 0;
    for (const auto& row : rows_) if (row.region == region) next = std::max(next, row.stack + 1);
    return next;
}

uint32_t PanelDock::NextOrder(DockRegion region, uint32_t stack) const {
    uint32_t next = 0;
    for (const auto& row : rows_) {
        if (row.region == region && row.stack == stack) next = std::max(next, row.order + 1);
    }
    return next;
}

void PanelDock::ActivateOnly(PanelRow& target) {
    for (auto& row : rows_) {
        if (&row != &target && row.region == target.region && row.stack == target.stack) row.active = false;
    }
    target.visible = true;
    target.active = true;
    if (target.region == DockRegion::Floating) target.focus = ++focusCounter_;
}

void PanelDock::Normalize() {
    // Floating and overlay panels are each their own surface: always "active" while visible.
    for (auto& row : rows_) {
        if (!IsEdge(row.region)) {
            row.stack = 0;
            row.active = row.visible;
        }
    }
    for (const DockRegion region : kEdgeRegions) {
        // Compact stack indices to 0..n-1, keeping their relative order.
        std::vector<uint32_t> stacks;
        for (const auto& row : rows_) {
            if (row.region == region && std::find(stacks.begin(), stacks.end(), row.stack) == stacks.end()) {
                stacks.push_back(row.stack);
            }
        }
        std::sort(stacks.begin(), stacks.end());
        for (auto& row : rows_) {
            if (row.region != region) continue;
            row.stack = static_cast<uint32_t>(std::find(stacks.begin(), stacks.end(), row.stack) - stacks.begin());
        }
        // Exactly one active tab among the visible panels of each stack (lowest order wins a tie).
        for (uint32_t stack = 0; stack < stacks.size(); ++stack) {
            PanelRow* active = nullptr;
            PanelRow* first = nullptr;
            for (auto& row : rows_) {
                if (row.region != region || row.stack != stack) continue;
                if (!row.visible) { row.active = false; continue; }
                if (!first || row.order < first->order) first = &row;
                if (row.active) {
                    if (active) row.active = false; else active = &row;
                }
            }
            if (!active && first) first->active = true;
        }
    }
}

DockResult PanelDock::Open(std::string_view panelId) {
    PanelRow* row = RowOf(panelId);
    if (!row) return DockResult::UnknownPanel;
    ActivateOnly(*row);
    Normalize();
    ++revision_;
    return DockResult::Ok;
}

DockResult PanelDock::Close(std::string_view panelId) {
    PanelRow* row = RowOf(panelId);
    if (!row) return DockResult::UnknownPanel;
    if (!Find(panelId)->closable) return DockResult::NotClosable;
    row->visible = false;
    row->active = false;
    Normalize();
    ++revision_;
    return DockResult::Ok;
}

DockResult PanelDock::Focus(std::string_view panelId) {
    return Open(panelId);
}

DockResult PanelDock::Move(std::string_view panelId, DockRegion region) {
    PanelRow* row = RowOf(panelId);
    if (!row) return DockResult::UnknownPanel;
    if (region == DockRegion::Floating) return Float(panelId, row->floatRect);
    // Dock as a tab of the region's last stack (a new first stack when the region is empty).
    uint32_t stack = 0;
    bool regionHasOther = false;
    for (const auto& other : rows_) {
        if (&other == row || other.region != region) continue;
        stack = regionHasOther ? std::max(stack, other.stack) : other.stack;
        regionHasOther = true;
    }
    row->region = region;
    row->stack = stack;
    row->order = NextOrder(region, stack);
    if (IsEdge(region) && RegionSize(region) <= 0.0f) RegionOf(region)->size = Find(panelId)->size;
    ActivateOnly(*row);
    Normalize();
    ++revision_;
    return DockResult::Ok;
}

DockResult PanelDock::Split(std::string_view panelId, DockRegion region) {
    PanelRow* row = RowOf(panelId);
    if (!row) return DockResult::UnknownPanel;
    if (!IsEdge(region)) return DockResult::UnknownRegion;
    row->region = region;
    row->stack = NextStack(region);
    row->order = 0;
    if (RegionSize(region) <= 0.0f) RegionOf(region)->size = Find(panelId)->size;
    ActivateOnly(*row);
    Normalize();
    ++revision_;
    return DockResult::Ok;
}

DockResult PanelDock::Float(std::string_view panelId, PanelRect rect) {
    PanelRow* row = RowOf(panelId);
    if (!row) return DockResult::UnknownPanel;
    if (!ValidRect(rect)) return DockResult::InvalidSize;
    row->region = DockRegion::Floating;
    row->stack = 0;
    row->order = 0;
    row->floatRect = rect;
    ActivateOnly(*row);
    Normalize();
    ++revision_;
    return DockResult::Ok;
}

DockResult PanelDock::Resize(DockRegion region, float size) {
    if (!IsEdge(region)) return DockResult::UnknownRegion;
    if (!std::isfinite(size) || size <= 0.0f) return DockResult::InvalidSize;
    RegionOf(region)->size = size;
    ++revision_;
    return DockResult::Ok;
}

DockLayoutRows PanelDock::Rows() const {
    return DockLayoutRows{regions_, rows_};
}

void PanelDock::ApplyRows(const DockLayoutRows& rows) {
    for (const auto& region : rows.regions) {
        if (RegionRow* own = IsEdge(region.region) ? RegionOf(region.region) : nullptr;
            own && std::isfinite(region.size) && region.size >= 0.0f) {
            own->size = region.size;
        }
    }
    for (const auto& saved : rows.panels) {
        PanelRow* row = RowOf(saved.panel);
        if (!row) continue;  // a row for a panel this app does not declare
        PanelRow next = saved;
        if (!ValidRect(next.floatRect)) next.floatRect = row->floatRect;
        *row = std::move(next);
        focusCounter_ = std::max(focusCounter_, row->focus);
    }
    Normalize();
    ++revision_;
}

std::string PanelDock::RowsJson() const {
    nlohmann::json regions = nlohmann::json::array();
    for (const auto& region : regions_) {
        regions.push_back({{"region", DockRegionName(region.region)}, {"size", region.size}});
    }
    nlohmann::json panels = nlohmann::json::array();
    for (const auto& row : rows_) {
        panels.push_back({{"panel", row.panel}, {"region", DockRegionName(row.region)},
                          {"stack", row.stack}, {"order", row.order}, {"visible", row.visible},
                          {"active", row.active}, {"focus", row.focus},
                          {"float", {row.floatRect.x, row.floatRect.y, row.floatRect.width, row.floatRect.height}}});
    }
    return nlohmann::json{{"version", 1}, {"regions", std::move(regions)}, {"panels", std::move(panels)}}.dump(2);
}

bool PanelDock::ApplyRowsJson(std::string_view text, std::string& error) {
    const nlohmann::json json = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (json.is_discarded() || !json.is_object()) {
        error = "panel layout is not a JSON object";
        return false;
    }
    if (json.value("version", 0) != 1) {
        error = "panel layout version is not 1";
        return false;
    }
    DockLayoutRows rows;
    for (const auto& region : json.value("regions", nlohmann::json::array())) {
        if (!region.is_object()) continue;
        const auto parsed = ParseDockRegion(region.value("region", std::string{}));
        if (!parsed || !region.contains("size") || !region.at("size").is_number()) continue;
        rows.regions.push_back({*parsed, region.at("size").get<float>()});
    }
    for (const auto& panel : json.value("panels", nlohmann::json::array())) {
        if (!panel.is_object() || !panel.contains("panel") || !panel.at("panel").is_string()) continue;
        const auto region = ParseDockRegion(panel.value("region", std::string{}));
        if (!region) continue;
        PanelRow row;
        row.panel = panel.at("panel").get<std::string>();
        row.region = *region;
        row.stack = panel.value("stack", 0u);
        row.order = panel.value("order", 0u);
        row.visible = panel.value("visible", true);
        row.active = panel.value("active", false);
        row.focus = panel.value("focus", 0u);
        if (const auto rect = panel.value("float", nlohmann::json::array());
            rect.is_array() && rect.size() == 4 && std::all_of(rect.begin(), rect.end(),
                                                               [](const auto& v) { return v.is_number(); })) {
            row.floatRect = {rect[0].get<float>(), rect[1].get<float>(), rect[2].get<float>(), rect[3].get<float>()};
        }
        rows.panels.push_back(std::move(row));
    }
    ApplyRows(rows);
    return true;
}

DockFrame PanelDock::Compute(float windowWidth, float windowHeight) const {
    DockFrame frame;
    const float width = std::max(0.0f, windowWidth);
    const float height = std::max(0.0f, windowHeight);

    // Region extents: zero for an empty region, never below the largest visible minimum, and never so
    // large that the viewport drops under kMinViewport (the viewport is never covered).
    std::map<DockRegion, float> extent;
    for (const DockRegion region : kEdgeRegions) {
        bool any = false;
        float minimum = 0.0f;
        for (size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].region != region || !rows_[i].visible) continue;
            any = true;
            minimum = std::max(minimum, decls_[i].minSize);
        }
        extent[region] = any ? std::max(RegionSize(region), minimum) : 0.0f;
    }
    const float sideBudget = std::max(0.0f, width - kMinViewport);
    if (const float sides = extent[DockRegion::Left] + extent[DockRegion::Right]; sides > sideBudget && sides > 0.0f) {
        const float scale = sideBudget / sides;
        extent[DockRegion::Left] *= scale;
        extent[DockRegion::Right] *= scale;
    }
    extent[DockRegion::Bottom] = std::min(extent[DockRegion::Bottom], std::max(0.0f, height - kMinViewport));

    const float left = extent[DockRegion::Left];
    const float right = extent[DockRegion::Right];
    const float bottom = extent[DockRegion::Bottom];
    frame.viewport = {left, 0.0f, std::max(0.0f, width - left - right), std::max(0.0f, height - bottom)};

    const std::map<DockRegion, PanelRect> regionRects{
        {DockRegion::Left, {0.0f, 0.0f, left, height - bottom}},
        {DockRegion::Right, {width - right, 0.0f, right, height - bottom}},
        {DockRegion::Bottom, {0.0f, height - bottom, width, bottom}},
    };

    // Panels sorted by order inside each surface.
    std::vector<size_t> byOrder(rows_.size());
    for (size_t i = 0; i < byOrder.size(); ++i) byOrder[i] = i;
    std::stable_sort(byOrder.begin(), byOrder.end(),
                     [&](size_t a, size_t b) { return rows_[a].order < rows_[b].order; });

    auto emitStack = [&](DockRegion region, uint32_t stack, PanelRect rect, const std::vector<size_t>& members, int z) {
        DockStackFrame stackFrame;
        stackFrame.region = region;
        stackFrame.stack = stack;
        stackFrame.frame = rect;
        const bool headerless = members.size() == 1 && !decls_[members.front()].header;
        stackFrame.headerHeight = headerless ? 0.0f : std::min(kHeaderHeight, rect.height);
        const float tabWidth = members.empty() ? 0.0f
            : std::min(kTabWidth, rect.width / static_cast<float>(members.size()));
        for (size_t t = 0; t < members.size(); ++t) {
            const size_t i = members[t];
            if (!headerless) {
                stackFrame.tabs.push_back({decls_[i].id, decls_[i].title.empty() ? decls_[i].id : decls_[i].title,
                                           {rect.x + tabWidth * static_cast<float>(t), rect.y, tabWidth,
                                            stackFrame.headerHeight},
                                           rows_[i].active, decls_[i].closable});
            }
            frame.panels.push_back({decls_[i].id,
                                    {rect.x, rect.y + stackFrame.headerHeight, rect.width,
                                     std::max(0.0f, rect.height - stackFrame.headerHeight)},
                                    rows_[i].active, z});
        }
        frame.stacks.push_back(std::move(stackFrame));
    };

    for (const DockRegion region : kEdgeRegions) {
        const PanelRect area = regionRects.at(region);
        if (area.width <= 0.0f || area.height <= 0.0f) continue;
        std::map<uint32_t, std::vector<size_t>> stacks;
        for (const size_t i : byOrder) {
            if (rows_[i].region == region && rows_[i].visible) stacks[rows_[i].stack].push_back(i);
        }
        const float count = static_cast<float>(stacks.size());
        uint32_t slot = 0;
        for (const auto& [stack, members] : stacks) {
            PanelRect rect = area;
            if (region == DockRegion::Bottom) {
                rect.width = area.width / count;
                rect.x = area.x + rect.width * static_cast<float>(slot);
            } else {
                rect.height = area.height / count;
                rect.y = area.y + rect.height * static_cast<float>(slot);
            }
            emitStack(region, stack, rect, members, 100);
            ++slot;
        }
    }

    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].region != DockRegion::Overlay) continue;
        frame.panels.push_back({decls_[i].id, frame.viewport, rows_[i].visible, 0});
    }

    std::vector<size_t> floating;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].region == DockRegion::Floating && rows_[i].visible) floating.push_back(i);
    }
    std::sort(floating.begin(), floating.end(), [&](size_t a, size_t b) { return rows_[a].focus < rows_[b].focus; });
    uint32_t floatIndex = 0;
    for (const size_t i : floating) {
        PanelRect rect = rows_[i].floatRect;
        rect.width = std::min(rect.width, width);
        rect.height = std::min(rect.height, height);
        rect.x = std::clamp(rect.x, 0.0f, std::max(0.0f, width - rect.width));
        rect.y = std::clamp(rect.y, 0.0f, std::max(0.0f, height - rect.height));
        emitStack(DockRegion::Floating, floatIndex, rect, {i}, 1000 + static_cast<int>(floatIndex));
        ++floatIndex;
    }

    // Hidden panels still get a row so a host can hide their documents.
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].visible) continue;
        frame.panels.push_back({decls_[i].id, {}, false, 0});
    }
    return frame;
}

}  // namespace Vixen::RenderGraph
