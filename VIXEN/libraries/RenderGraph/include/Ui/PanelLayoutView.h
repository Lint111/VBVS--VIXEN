#pragma once
// R766: the dock chrome's data model — the generated PanelLayout [View] (codegen/view-schemas/
// PanelLayout.cs) filled from a computed DockFrame. Includes RmlUi's data-model headers, so only
// gaia-free TUs (PanelDockHost.cpp, tests) include it; apps reach it through PanelDockHost.
#include "Ui/IView.h"
#include "Ui/PanelDock.h"
#include "Ui/PanelLayout.g.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <vector>

namespace Vixen::RenderGraph {

class PanelLayoutView final : public IView {
public:
    const char* ModelName() const override { return "panel_layout"; }
    const char* DocumentPath() const override { return "assets/ui/dock.rml"; }
    void Register(Rml::DataModelConstructor& c) override {
        Vixen::Views::BindPanelLayoutModel(c, Vixen::Views::PanelLayoutBind{
            &stacks_, &panels_, &viewportLeft_, &viewportTop_, &viewportWidth_, &viewportHeight_});
        model_ = c.GetModelHandle();
    }

    // Rebuilds the rows from the dock's declarations and one computed frame.
    void Populate(const PanelDock& dock, const DockFrame& frame);

    const std::vector<Vixen::Views::PanelLayoutStack>& Stacks() const { return stacks_; }
    const std::vector<Vixen::Views::PanelLayoutPanel>& Panels() const { return panels_; }
    PanelRect Viewport() const { return {viewportLeft_, viewportTop_, viewportWidth_, viewportHeight_}; }

private:
    std::vector<Vixen::Views::PanelLayoutStack> stacks_;
    std::vector<Vixen::Views::PanelLayoutPanel> panels_;
    float viewportLeft_ = 0.0f;
    float viewportTop_ = 0.0f;
    float viewportWidth_ = 0.0f;
    float viewportHeight_ = 0.0f;
    Rml::DataModelHandle model_;
};

}  // namespace Vixen::RenderGraph
