// R766: the RmlUi side of the dock host — the only TU (besides tests) that instantiates the generated
// PanelLayout binder. Gaia-free, like the HUD/inspector bridges.
#include "Ui/PanelDockHost.h"
#include "Ui/PanelLayoutView.h"

#include "RuntimeCachePaths.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace Vixen::RenderGraph {

namespace {

constexpr int kChromeZ = 500;  // above docked panels (headers/borders only), below floating panels

std::string ElementId(std::string_view panel, std::string_view suffix) {
    return "panel-" + std::string(panel) + "-" + std::string(suffix);
}

}  // namespace

void PanelLayoutView::Populate(const PanelDock& dock, const DockFrame& frame) {
    stacks_.clear();
    panels_.clear();
    for (const auto& stack : frame.stacks) {
        Vixen::Views::PanelLayoutStack row{};
        row.region = Rml::String(DockRegionName(stack.region));
        row.stack = static_cast<int>(stack.stack);
        row.left = stack.frame.x;
        row.top = stack.frame.y;
        row.width = stack.frame.width;
        row.height = stack.frame.height;
        row.headerHeight = stack.headerHeight;
        stacks_.push_back(std::move(row));
    }
    const DockLayoutRows rows = dock.Rows();
    for (const auto& decl : dock.Declarations()) {
        Vixen::Views::PanelLayoutPanel row{};
        row.panel = Rml::String(decl.id);
        row.title = Rml::String(decl.title.empty() ? decl.id : decl.title);
        row.closable = decl.closable;
        for (const auto& placed : rows.panels) {
            if (placed.panel != decl.id) continue;
            row.region = Rml::String(DockRegionName(placed.region));
            row.stack = static_cast<int>(placed.stack);
            row.visible = placed.visible;
            row.active = placed.active;
        }
        for (const auto& stack : frame.stacks) {
            for (const auto& tab : stack.tabs) {
                if (tab.panel != decl.id) continue;
                row.hasTab = true;
                row.tabLeft = tab.tab.x;
                row.tabTop = tab.tab.y;
                row.tabWidth = tab.tab.width;
                row.tabHeight = tab.tab.height;
            }
        }
        row.tabId = ElementId(decl.id, "tab");
        row.closeId = ElementId(decl.id, "close");
        row.toLeftId = ElementId(decl.id, "to-left");
        row.toRightId = ElementId(decl.id, "to-right");
        row.toBottomId = ElementId(decl.id, "to-bottom");
        panels_.push_back(std::move(row));
    }
    viewportLeft_ = frame.viewport.x;
    viewportTop_ = frame.viewport.y;
    viewportWidth_ = frame.viewport.width;
    viewportHeight_ = frame.viewport.height;
    if (model_) {
        model_.DirtyVariable("stacks");
        model_.DirtyVariable("panels");
        model_.DirtyVariable("viewportLeft");
        model_.DirtyVariable("viewportTop");
        model_.DirtyVariable("viewportWidth");
        model_.DirtyVariable("viewportHeight");
    }
}

PanelDockHost::PanelDockHost() : chromeView_(std::make_unique<PanelLayoutView>()) {}
PanelDockHost::~PanelDockHost() = default;

bool PanelDockHost::LoadLayout(std::string& error) {
    if (layoutPath_.empty()) return true;
    std::ifstream in(layoutPath_, std::ios::binary);
    if (!in) return true;  // nothing saved yet: declared defaults
    std::stringstream text;
    text << in.rdbuf();
    return dock_.ApplyRowsJson(text.str(), error);
}

bool PanelDockHost::SaveLayout(std::string& error) const {
    if (layoutPath_.empty()) return true;
    std::error_code ec;
    std::filesystem::create_directories(layoutPath_.parent_path(), ec);
    if (ec) {
        error = "cannot create " + layoutPath_.parent_path().string() + ": " + ec.message();
        return false;
    }
    return Vixen::AtomicWriteText(layoutPath_, dock_.RowsJson(), &error);
}

void PanelDockHost::Attach(IUiCompositionHost& host) {
    host_ = &host;
    chromeHandle_ = host.Mount(*chromeView_);
    for (const auto& decl : dock_.Declarations()) {
        if (!decl.view) continue;
        if (decl.mount == PanelMount::Primary) {
            host.BindPrimary(*decl.view);
            handles_[decl.id] = IUiCompositionHost::kPrimaryDocument;
        } else {
            handles_[decl.id] = host.Mount(*decl.view, decl.document.c_str());
        }
    }
    placedRevision_ = ~uint64_t{0};
}

bool PanelDockHost::Update() {
    if (!host_) return false;
    float width = 0.0f, height = 0.0f;
    if (!host_->Extent(width, height)) return false;
    if (dock_.Revision() == placedRevision_ && width == placedWidth_ && height == placedHeight_) return false;
    frame_ = dock_.Compute(width, height);
    chromeView_->Populate(dock_, frame_);
    host_->Place(chromeHandle_, {0.0f, 0.0f, width, height}, true, kChromeZ);
    for (const auto& panel : frame_.panels) {
        const auto handle = handles_.find(panel.panel);
        if (handle == handles_.end() || handle->second == 0) continue;
        host_->Place(handle->second, panel.content, panel.visible, panel.z);
    }
    placedRevision_ = dock_.Revision();
    placedWidth_ = width;
    placedHeight_ = height;
    return true;
}

IUiCompositionHost::MountHandle PanelDockHost::HandleOf(std::string_view panelId) const {
    const auto found = handles_.find(std::string(panelId));
    return found == handles_.end() ? 0 : found->second;
}

std::string PanelDockHost::LayoutJson() const {
    const auto rect = [](const PanelRect& r) {
        return nlohmann::json{{"left", r.x}, {"top", r.y}, {"width", r.width}, {"height", r.height}};
    };
    nlohmann::json stacks = nlohmann::json::array();
    for (const auto& stack : chromeView_->Stacks()) {
        stacks.push_back({{"region", stack.region}, {"stack", stack.stack},
                          {"left", stack.left}, {"top", stack.top}, {"width", stack.width},
                          {"height", stack.height}, {"headerHeight", stack.headerHeight}});
    }
    nlohmann::json panels = nlohmann::json::array();
    for (const auto& panel : chromeView_->Panels()) {
        nlohmann::json row{{"panel", panel.panel}, {"title", panel.title}, {"region", panel.region},
                           {"stack", panel.stack}, {"visible", panel.visible}, {"active", panel.active},
                           {"hasTab", panel.hasTab}, {"closable", panel.closable}};
        for (const auto& placed : frame_.panels) {
            if (placed.panel == panel.panel) row["content"] = rect(placed.content);
        }
        panels.push_back(std::move(row));
    }
    return nlohmann::json{
        {"viewport", rect(chromeView_->Viewport())},
        {"stacks", std::move(stacks)},
        {"panels", std::move(panels)},
        {"rows", nlohmann::json::parse(dock_.RowsJson())},
        {"layoutPath", layoutPath_.string()},
        {"revision", dock_.Revision()},
    }.dump();
}

}  // namespace Vixen::RenderGraph
