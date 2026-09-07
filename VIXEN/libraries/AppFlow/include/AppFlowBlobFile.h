#pragma once

#include "generated/AppFlow.g.h"
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vixen::AppFlow {

// Runtime .appflow parser. It owns every string, parameter table, and row referenced by the
// returned AppFlowContainerView. Parse/Load are fail-closed and never throw to callers.
class AppFlowBlobFile {
public:
    static std::optional<AppFlowBlobFile> Parse(std::string_view text);
    static std::optional<AppFlowBlobFile> Load(const std::string& path);
    // Reads only the declared shape line so a consumer can distinguish a rejected facade
    // interface/graph from an otherwise malformed or unreadable artifact. Full validation still
    // belongs to Parse()/Load().
    static std::optional<uint32_t> ReadDeclaredShapeHash(const std::string& path);

    const Generated::AppFlowContainerView& View() const { return view_; }
    uint32_t ShapeHash() const { return shapeHash_; }

    AppFlowBlobFile(AppFlowBlobFile&& other) noexcept;
    AppFlowBlobFile& operator=(AppFlowBlobFile&& other) noexcept;
    AppFlowBlobFile(const AppFlowBlobFile&) = delete;
    AppFlowBlobFile& operator=(const AppFlowBlobFile&) = delete;

private:
    AppFlowBlobFile() = default;

    std::deque<std::string> strings_;
    std::deque<std::vector<Generated::FlowParamSchema>> paramArrays_;
    std::vector<Generated::AppFlowActionDecl> actions_;
    std::vector<Generated::AppFlowTransition> transitions_;
    std::vector<Generated::AppFlowElementTrigger> elementTriggers_;
    std::vector<Generated::AppFlowKeyDefault> keyDefaults_;
    std::vector<Generated::AppFlowReturnEdge> returnEdges_;
    std::vector<Generated::AppFlowDataTarget> dataTargets_;
    Generated::AppFlowContainerView view_;
    uint32_t shapeHash_ = 0;

    void RebindView() noexcept;
};

} // namespace Vixen::AppFlow
