# MCP Architecture Implementation Example

This document provides concrete implementation examples for the MCP architecture, focusing on a complete workflow: creating a node, connecting it, and compiling the graph.

---

## Example: Implementing NodeController as a Controller Point

### Model Layer: NodeModel

```cpp
// File: RenderGraphEditor/include/Model/NodeModel.h
#pragma once

#include <string>
#include <vector>
#include <map>
#include <any>
#include "Observable.h"
#include "RenderGraph/Core/NodeInstance.h"
#include "SlotModel.h"

namespace VIXEN::GraphEditor {

enum class NodeState {
    Unconnected,    // Not all required slots connected
    Ready,          // All required slots connected, not compiled
    Compiled,       // Successfully compiled
    Error           // Compilation or validation error
};

class NodeModel {
public:
    // Constructor wraps existing NodeInstance
    NodeModel(NodeInstance* instance, const std::string& typeName);

    // Identity
    NodeHandle GetHandle() const { return handle_; }
    const std::string& GetInstanceName() const { return instanceName_; }
    const std::string& GetTypeName() const { return typeName_; }

    // Configuration
    const std::vector<SlotModel>& GetInputSlots() const { return inputSlots_; }
    const std::vector<SlotModel>& GetOutputSlots() const { return outputSlots_; }
    const SlotModel& GetInputSlot(uint32_t index) const;
    const SlotModel& GetOutputSlot(uint32_t index) const;

    // Visual properties
    Vec2 GetPosition() const { return position_.Get(); }
    void SetPosition(Vec2 position) { position_.Set(position); }

    Vec2 GetSize() const { return size_.Get(); }
    void SetSize(Vec2 size) { size_.Set(size); }

    Color GetColor() const { return color_; }
    void SetColor(Color color) { color_ = color; }

    // State management
    NodeState GetState() const { return state_.Get(); }
    void SetState(NodeState state) { state_.Set(state); }

    const std::vector<std::string>& GetErrors() const { return errors_.Get(); }
    void AddError(const std::string& error);
    void ClearErrors();

    // Parameters (runtime configuration)
    bool HasParameter(const std::string& name) const;
    std::any GetParameter(const std::string& name) const;
    void SetParameter(const std::string& name, const std::any& value);

    // Connection tracking
    void MarkSlotConnected(uint32_t slotIndex, bool isInput, bool connected);
    bool IsSlotConnected(uint32_t slotIndex, bool isInput) const;
    bool AreAllRequiredSlotsConnected() const;

    // Underlying node instance
    NodeInstance* GetNodeInstance() const { return nodeInstance_; }

    // Observables (Views subscribe to these)
    Observable<Vec2>& PositionObservable() { return position_; }
    Observable<Vec2>& SizeObservable() { return size_; }
    Observable<NodeState>& StateObservable() { return state_; }
    Observable<std::vector<std::string>>& ErrorsObservable() { return errors_; }

private:
    // Core data
    NodeInstance* nodeInstance_;  // Non-owning (RenderGraph owns)
    NodeHandle handle_;
    std::string instanceName_;
    std::string typeName_;

    // Slot metadata
    std::vector<SlotModel> inputSlots_;
    std::vector<SlotModel> outputSlots_;

    // Visual properties (observable)
    Observable<Vec2> position_;
    Observable<Vec2> size_;
    Color color_;

    // State (observable)
    Observable<NodeState> state_;
    Observable<std::vector<std::string>> errors_;

    // Runtime parameters
    std::map<std::string, std::any> parameters_;

    // Connection state
    std::vector<bool> inputSlotConnected_;
    std::vector<bool> outputSlotConnected_;
};

} // namespace VIXEN::GraphEditor
```

---

### Controller Layer: NodeController

```cpp
// File: RenderGraphEditor/include/Controller/NodeController.h
#pragma once

#include "Model/GraphModel.h"
#include "Model/ConfigModelRegistry.h"
#include "Command/Command.h"
#include "Event.h"
#include <memory>

namespace VIXEN::GraphEditor {

class NodeController {
public:
    NodeController(GraphModel* graphModel, ConfigModelRegistry* registry);

    // Node creation
    CommandResult CreateNode(
        const std::string& nodeTypeName,
        const std::string& instanceName,
        Vec2 position = Vec2(0, 0)
    );

    // Node deletion
    CommandResult DeleteNode(NodeHandle handle);
    CommandResult DeleteNodes(const std::vector<NodeHandle>& handles);

    // Node modification
    CommandResult SetNodePosition(NodeHandle handle, Vec2 position);
    CommandResult SetNodeParameter(
        NodeHandle handle,
        const std::string& paramName,
        const std::any& value
    );
    CommandResult RenameNode(NodeHandle handle, const std::string& newName);

    // Node duplication
    CommandResult DuplicateNode(NodeHandle handle, Vec2 offset = Vec2(50, 50));

    // Bulk operations
    CommandResult MoveNodes(const std::vector<NodeHandle>& handles, Vec2 delta);

    // Validation
    ValidationResult ValidateNodeName(const std::string& name) const;
    bool CanDeleteNode(NodeHandle handle) const;
    bool NodeExists(const std::string& name) const;

    // Undo/Redo
    void Undo();
    void Redo();
    bool CanUndo() const;
    bool CanRedo() const;

    // Events
    Event<NodeModel*> onNodeCreated;
    Event<NodeModel*> onNodeDeleted;
    Event<NodeModel*> onNodeModified;

private:
    GraphModel* graphModel_;
    ConfigModelRegistry* registry_;
    CommandHistory commandHistory_;

    // Helper methods
    std::string GenerateUniqueNodeName(const std::string& baseName) const;
    NodeModel* CreateNodeModel(
        const std::string& nodeTypeName,
        const std::string& instanceName
    );
};

// Command implementations
class CreateNodeCommand : public Command {
public:
    CreateNodeCommand(
        GraphModel* graphModel,
        ConfigModelRegistry* registry,
        const std::string& nodeTypeName,
        const std::string& instanceName,
        Vec2 position
    );

    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override;

private:
    GraphModel* graphModel_;
    ConfigModelRegistry* registry_;
    std::string nodeTypeName_;
    std::string instanceName_;
    Vec2 position_;
    NodeHandle createdHandle_;  // Stored for undo
};

class DeleteNodeCommand : public Command {
public:
    DeleteNodeCommand(GraphModel* graphModel, NodeHandle handle);

    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override;

private:
    GraphModel* graphModel_;
    NodeHandle handle_;

    // Store state for undo
    std::unique_ptr<NodeModel> deletedNode_;
    std::vector<ConnectionModel*> deletedConnections_;
};

class SetNodePositionCommand : public Command {
public:
    SetNodePositionCommand(GraphModel* graphModel, NodeHandle handle, Vec2 newPosition);

    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override;

private:
    GraphModel* graphModel_;
    NodeHandle handle_;
    Vec2 newPosition_;
    Vec2 oldPosition_;  // Stored during Execute
};

} // namespace VIXEN::GraphEditor
```

---

### Controller Implementation

```cpp
// File: RenderGraphEditor/src/Controller/NodeController.cpp
#include "Controller/NodeController.h"
#include "Model/NodeModel.h"
#include <sstream>

namespace VIXEN::GraphEditor {

NodeController::NodeController(GraphModel* graphModel, ConfigModelRegistry* registry)
    : graphModel_(graphModel)
    , registry_(registry)
{
}

CommandResult NodeController::CreateNode(
    const std::string& nodeTypeName,
    const std::string& instanceName,
    Vec2 position
) {
    // Validate node type exists
    if (!registry_->IsTypeRegistered(nodeTypeName)) {
        return CommandResult::Error("Node type '" + nodeTypeName + "' not registered");
    }

    // Validate name uniqueness
    auto validationResult = ValidateNodeName(instanceName);
    if (!validationResult.isValid) {
        return CommandResult::Error(validationResult.errorMessage);
    }

    // Create command
    auto command = std::make_unique<CreateNodeCommand>(
        graphModel_,
        registry_,
        nodeTypeName,
        instanceName,
        position
    );

    // Execute command
    command->Execute();

    // Add to history
    commandHistory_.Add(std::move(command));

    return CommandResult::Success();
}

CommandResult NodeController::DeleteNode(NodeHandle handle) {
    // Validate node exists
    NodeModel* node = graphModel_->FindNode(handle);
    if (!node) {
        return CommandResult::Error("Node not found");
    }

    // Check if node can be deleted
    if (!CanDeleteNode(handle)) {
        return CommandResult::Error("Node cannot be deleted (critical node)");
    }

    // Create command
    auto command = std::make_unique<DeleteNodeCommand>(graphModel_, handle);

    // Execute command
    command->Execute();

    // Add to history
    commandHistory_.Add(std::move(command));

    return CommandResult::Success();
}

CommandResult NodeController::SetNodePosition(NodeHandle handle, Vec2 position) {
    // Validate node exists
    NodeModel* node = graphModel_->FindNode(handle);
    if (!node) {
        return CommandResult::Error("Node not found");
    }

    // Create command
    auto command = std::make_unique<SetNodePositionCommand>(
        graphModel_,
        handle,
        position
    );

    // Execute command
    command->Execute();

    // Add to history
    commandHistory_.Add(std::move(command));

    // Emit event
    onNodeModified.Emit(node);

    return CommandResult::Success();
}

ValidationResult NodeController::ValidateNodeName(const std::string& name) const {
    // Check if name is empty
    if (name.empty()) {
        return ValidationResult::Error("Node name cannot be empty");
    }

    // Check if name already exists
    if (NodeExists(name)) {
        return ValidationResult::Error("Node name '" + name + "' already exists");
    }

    // Check for invalid characters
    if (name.find(' ') != std::string::npos) {
        return ValidationResult::Error("Node name cannot contain spaces");
    }

    return ValidationResult::Success();
}

bool NodeController::CanDeleteNode(NodeHandle handle) const {
    // Example: Prevent deletion of root nodes (e.g., DeviceNode)
    NodeModel* node = graphModel_->FindNode(handle);
    if (!node) return false;

    // Check if node is a critical system node
    if (node->GetTypeName() == "DeviceNode" ||
        node->GetTypeName() == "WindowNode") {
        return false;
    }

    return true;
}

bool NodeController::NodeExists(const std::string& name) const {
    return graphModel_->FindNode(name) != nullptr;
}

std::string NodeController::GenerateUniqueNodeName(const std::string& baseName) const {
    std::string candidateName = baseName;
    int suffix = 1;

    while (NodeExists(candidateName)) {
        std::ostringstream oss;
        oss << baseName << "_" << suffix++;
        candidateName = oss.str();
    }

    return candidateName;
}

void NodeController::Undo() {
    if (CanUndo()) {
        commandHistory_.Undo();
    }
}

void NodeController::Redo() {
    if (CanRedo()) {
        commandHistory_.Redo();
    }
}

bool NodeController::CanUndo() const {
    return commandHistory_.CanUndo();
}

bool NodeController::CanRedo() const {
    return commandHistory_.CanRedo();
}

// ============================================================================
// CreateNodeCommand Implementation
// ============================================================================

CreateNodeCommand::CreateNodeCommand(
    GraphModel* graphModel,
    ConfigModelRegistry* registry,
    const std::string& nodeTypeName,
    const std::string& instanceName,
    Vec2 position
)
    : graphModel_(graphModel)
    , registry_(registry)
    , nodeTypeName_(nodeTypeName)
    , instanceName_(instanceName)
    , position_(position)
    , createdHandle_(INVALID_NODE_HANDLE)
{
}

void CreateNodeCommand::Execute() {
    // Get node type descriptor
    const NodeTypeDescriptor* descriptor = registry_->GetNodeType(nodeTypeName_);
    if (!descriptor) {
        throw std::runtime_error("Node type not found: " + nodeTypeName_);
    }

    // Create node instance using factory
    NodeInstance* nodeInstance = descriptor->factoryFunction();

    // Add to RenderGraph
    createdHandle_ = graphModel_->GetRenderGraph()->AddNode(
        instanceName_,
        nodeInstance
    );

    // Create NodeModel wrapper
    auto nodeModel = std::make_unique<NodeModel>(nodeInstance, nodeTypeName_);
    nodeModel->SetPosition(position_);
    nodeModel->SetColor(descriptor->defaultColor);

    // Add to GraphModel
    graphModel_->AddNode(std::move(nodeModel));

    // Mark graph dirty
    graphModel_->MarkDirty();
}

void CreateNodeCommand::Undo() {
    // Remove from GraphModel
    graphModel_->RemoveNode(createdHandle_);

    // Remove from RenderGraph (if supported)
    // Note: Current RenderGraph doesn't support removal during Build phase
    // This would require extending RenderGraph API

    // Mark graph dirty
    graphModel_->MarkDirty();

    createdHandle_ = INVALID_NODE_HANDLE;
}

std::string CreateNodeCommand::GetDescription() const {
    return "Create node '" + instanceName_ + "' of type '" + nodeTypeName_ + "'";
}

// ============================================================================
// DeleteNodeCommand Implementation
// ============================================================================

DeleteNodeCommand::DeleteNodeCommand(GraphModel* graphModel, NodeHandle handle)
    : graphModel_(graphModel)
    , handle_(handle)
{
}

void DeleteNodeCommand::Execute() {
    // Store node state for undo
    NodeModel* node = graphModel_->FindNode(handle_);
    if (!node) {
        throw std::runtime_error("Node not found");
    }

    // Store connections that will be deleted
    deletedConnections_ = graphModel_->GetConnectionsForNode(handle_);

    // Remove connections first
    for (auto* connection : deletedConnections_) {
        graphModel_->RemoveConnection(connection->GetId());
    }

    // Remove node
    deletedNode_ = graphModel_->RemoveNode(handle_);

    // Mark graph dirty
    graphModel_->MarkDirty();
}

void DeleteNodeCommand::Undo() {
    // Re-add node
    graphModel_->AddNode(std::move(deletedNode_));

    // Re-add connections
    for (auto* connection : deletedConnections_) {
        graphModel_->AddConnection(connection);
    }

    // Mark graph dirty
    graphModel_->MarkDirty();
}

std::string DeleteNodeCommand::GetDescription() const {
    return "Delete node (handle: " + std::to_string(handle_) + ")";
}

// ============================================================================
// SetNodePositionCommand Implementation
// ============================================================================

SetNodePositionCommand::SetNodePositionCommand(
    GraphModel* graphModel,
    NodeHandle handle,
    Vec2 newPosition
)
    : graphModel_(graphModel)
    , handle_(handle)
    , newPosition_(newPosition)
{
}

void SetNodePositionCommand::Execute() {
    NodeModel* node = graphModel_->FindNode(handle_);
    if (!node) {
        throw std::runtime_error("Node not found");
    }

    // Store old position for undo
    oldPosition_ = node->GetPosition();

    // Set new position
    node->SetPosition(newPosition_);
}

void SetNodePositionCommand::Undo() {
    NodeModel* node = graphModel_->FindNode(handle_);
    if (!node) {
        throw std::runtime_error("Node not found");
    }

    // Restore old position
    node->SetPosition(oldPosition_);
}

std::string SetNodePositionCommand::GetDescription() const {
    return "Move node (handle: " + std::to_string(handle_) + ")";
}

} // namespace VIXEN::GraphEditor
```

---

### View Layer: NodeView

```cpp
// File: RenderGraphEditor/include/View/NodeView.h
#pragma once

#include "Model/NodeModel.h"
#include "Controller/NodeController.h"
#include "View/SlotView.h"
#include <vector>
#include <memory>

namespace VIXEN::GraphEditor {

class NodeView {
public:
    NodeView(NodeModel* model, NodeController* controller);
    ~NodeView();

    // Rendering
    void Render(ImDrawList* drawList, const Mat3& worldToScreen);

    // Layout
    Rect GetBounds() const;
    Vec2 GetScreenPosition() const { return screenPosition_; }

    // Input handling
    bool HitTest(Vec2 screenPosition) const;
    void OnMouseDown(Vec2 position, MouseButton button);
    void OnMouseMove(Vec2 position);
    void OnMouseUp(Vec2 position, MouseButton button);
    void OnDragStart(Vec2 position);
    void OnDrag(Vec2 delta);
    void OnDragEnd();

    // State
    void SetSelected(bool selected);
    bool IsSelected() const { return isSelected_; }
    void SetHovered(bool hovered);
    bool IsHovered() const { return isHovered_; }

    // Slot access
    SlotView* GetSlotAtPosition(Vec2 screenPosition) const;
    SlotView* GetInputSlot(uint32_t index) const;
    SlotView* GetOutputSlot(uint32_t index) const;

    // Model access
    NodeModel* GetModel() const { return model_; }

private:
    NodeModel* model_;
    NodeController* controller_;

    // Visual state
    Vec2 screenPosition_;
    Vec2 size_;
    bool isSelected_;
    bool isHovered_;
    bool isDragging_;

    // Child views
    std::vector<std::unique_ptr<SlotView>> inputSlots_;
    std::vector<std::unique_ptr<SlotView>> outputSlots_;

    // Rendering helpers
    void RenderFrame(ImDrawList* drawList);
    void RenderHeader(ImDrawList* drawList);
    void RenderBody(ImDrawList* drawList);
    void RenderSlots(ImDrawList* drawList);
    void RenderSelectionHighlight(ImDrawList* drawList);
    void RenderErrorBadge(ImDrawList* drawList);

    // Layout helpers
    void UpdateLayout();
    void UpdateSlotPositions();

    // Model observation
    void OnPositionChanged(Vec2 newPosition);
    void OnStateChanged(NodeState newState);
    void OnErrorsChanged(const std::vector<std::string>& errors);

    // Subscriptions (stored for cleanup)
    std::vector<ObserverHandle> observerHandles_;
};

} // namespace VIXEN::GraphEditor
```

---

### View Implementation

```cpp
// File: RenderGraphEditor/src/View/NodeView.cpp
#include "View/NodeView.h"
#include <imgui.h>

namespace VIXEN::GraphEditor {

NodeView::NodeView(NodeModel* model, NodeController* controller)
    : model_(model)
    , controller_(controller)
    , isSelected_(false)
    , isHovered_(false)
    , isDragging_(false)
{
    // Subscribe to model changes
    observerHandles_.push_back(
        model_->PositionObservable().Subscribe([this](Vec2 pos) {
            OnPositionChanged(pos);
        })
    );

    observerHandles_.push_back(
        model_->StateObservable().Subscribe([this](NodeState state) {
            OnStateChanged(state);
        })
    );

    observerHandles_.push_back(
        model_->ErrorsObservable().Subscribe([this](const std::vector<std::string>& errors) {
            OnErrorsChanged(errors);
        })
    );

    // Create slot views
    for (size_t i = 0; i < model_->GetInputSlots().size(); ++i) {
        inputSlots_.push_back(
            std::make_unique<SlotView>(&model_->GetInputSlots()[i], this, true)
        );
    }

    for (size_t i = 0; i < model_->GetOutputSlots().size(); ++i) {
        outputSlots_.push_back(
            std::make_unique<SlotView>(&model_->GetOutputSlots()[i], this, false)
        );
    }

    // Initial layout
    UpdateLayout();
}

NodeView::~NodeView() {
    // Unsubscribe from model (handles cleaned up automatically)
}

void NodeView::Render(ImDrawList* drawList, const Mat3& worldToScreen) {
    // Transform world position to screen space
    Vec2 worldPos = model_->GetPosition();
    screenPosition_ = TransformPoint(worldToScreen, worldPos);

    // Update layout
    UpdateLayout();

    // Render node
    RenderFrame(drawList);
    RenderHeader(drawList);
    RenderBody(drawList);
    RenderSlots(drawList);

    // Render state indicators
    if (isSelected_) {
        RenderSelectionHighlight(drawList);
    }

    if (!model_->GetErrors().empty()) {
        RenderErrorBadge(drawList);
    }
}

void NodeView::RenderFrame(ImDrawList* drawList) {
    Rect bounds = GetBounds();

    // Background color based on state
    Color backgroundColor;
    switch (model_->GetState()) {
        case NodeState::Unconnected:
            backgroundColor = Color(60, 60, 60, 255);
            break;
        case NodeState::Ready:
            backgroundColor = Color(70, 70, 70, 255);
            break;
        case NodeState::Compiled:
            backgroundColor = Color(50, 80, 50, 255);
            break;
        case NodeState::Error:
            backgroundColor = Color(100, 50, 50, 255);
            break;
    }

    // Draw background
    drawList->AddRectFilled(
        ImVec2(bounds.min.x, bounds.min.y),
        ImVec2(bounds.max.x, bounds.max.y),
        ImGui::ColorConvertFloat4ToU32(backgroundColor.ToImVec4()),
        4.0f  // Corner radius
    );

    // Draw border
    Color borderColor = isHovered_ ? Color(200, 200, 200, 255) : Color(100, 100, 100, 255);
    drawList->AddRect(
        ImVec2(bounds.min.x, bounds.min.y),
        ImVec2(bounds.max.x, bounds.max.y),
        ImGui::ColorConvertFloat4ToU32(borderColor.ToImVec4()),
        4.0f,  // Corner radius
        0,
        isSelected_ ? 3.0f : 1.5f  // Border thickness
    );
}

void NodeView::RenderHeader(ImDrawList* drawList) {
    Rect bounds = GetBounds();
    float headerHeight = 30.0f;

    // Header background
    drawList->AddRectFilled(
        ImVec2(bounds.min.x, bounds.min.y),
        ImVec2(bounds.max.x, bounds.min.y + headerHeight),
        ImGui::ColorConvertFloat4ToU32(model_->GetColor().ToImVec4()),
        4.0f,
        ImDrawFlags_RoundCornersTop
    );

    // Node name text
    drawList->AddText(
        ImVec2(bounds.min.x + 10, bounds.min.y + 8),
        IM_COL32(255, 255, 255, 255),
        model_->GetInstanceName().c_str()
    );
}

void NodeView::RenderBody(ImDrawList* drawList) {
    // Node type text
    Rect bounds = GetBounds();
    drawList->AddText(
        ImVec2(bounds.min.x + 10, bounds.min.y + 40),
        IM_COL32(180, 180, 180, 255),
        model_->GetTypeName().c_str()
    );
}

void NodeView::RenderSlots(ImDrawList* drawList) {
    // Render input slots
    for (auto& slot : inputSlots_) {
        slot->Render(drawList);
    }

    // Render output slots
    for (auto& slot : outputSlots_) {
        slot->Render(drawList);
    }
}

void NodeView::RenderSelectionHighlight(ImDrawList* drawList) {
    Rect bounds = GetBounds();
    drawList->AddRect(
        ImVec2(bounds.min.x - 4, bounds.min.y - 4),
        ImVec2(bounds.max.x + 4, bounds.max.y + 4),
        IM_COL32(255, 200, 0, 255),  // Orange highlight
        6.0f,
        0,
        2.0f
    );
}

void NodeView::RenderErrorBadge(ImDrawList* drawList) {
    Rect bounds = GetBounds();
    Vec2 badgePos(bounds.max.x - 20, bounds.min.y + 5);

    // Error badge (red circle with exclamation mark)
    drawList->AddCircleFilled(
        ImVec2(badgePos.x, badgePos.y),
        10.0f,
        IM_COL32(200, 50, 50, 255)
    );

    drawList->AddText(
        ImVec2(badgePos.x - 3, badgePos.y - 8),
        IM_COL32(255, 255, 255, 255),
        "!"
    );
}

bool NodeView::HitTest(Vec2 screenPosition) const {
    Rect bounds = GetBounds();
    return bounds.Contains(screenPosition);
}

void NodeView::OnMouseDown(Vec2 position, MouseButton button) {
    if (button == MouseButton::Left) {
        // Begin drag
        OnDragStart(position);
    }
}

void NodeView::OnDragStart(Vec2 position) {
    isDragging_ = true;
}

void NodeView::OnDrag(Vec2 delta) {
    if (isDragging_) {
        Vec2 newPosition = model_->GetPosition() + delta;
        controller_->SetNodePosition(model_->GetHandle(), newPosition);
    }
}

void NodeView::OnDragEnd() {
    isDragging_ = false;
}

void NodeView::UpdateLayout() {
    // Calculate node size based on slot count
    float headerHeight = 30.0f;
    float slotHeight = 20.0f;
    float padding = 10.0f;

    size_t maxSlots = std::max(inputSlots_.size(), outputSlots_.size());
    float bodyHeight = maxSlots * slotHeight + padding * 2;

    size_ = Vec2(200.0f, headerHeight + bodyHeight);

    // Update slot positions
    UpdateSlotPositions();
}

void NodeView::UpdateSlotPositions() {
    float slotHeight = 20.0f;
    float headerHeight = 30.0f;
    float startY = screenPosition_.y + headerHeight + 10.0f;

    // Position input slots (left side)
    for (size_t i = 0; i < inputSlots_.size(); ++i) {
        Vec2 slotPos(screenPosition_.x, startY + i * slotHeight);
        inputSlots_[i]->SetPosition(slotPos);
    }

    // Position output slots (right side)
    for (size_t i = 0; i < outputSlots_.size(); ++i) {
        Vec2 slotPos(screenPosition_.x + size_.x, startY + i * slotHeight);
        outputSlots_[i]->SetPosition(slotPos);
    }
}

Rect NodeView::GetBounds() const {
    return Rect(screenPosition_, screenPosition_ + size_);
}

void NodeView::OnPositionChanged(Vec2 newPosition) {
    // Position updated, layout will be recalculated on next Render()
}

void NodeView::OnStateChanged(NodeState newState) {
    // State updated, visual appearance will change on next Render()
}

void NodeView::OnErrorsChanged(const std::vector<std::string>& errors) {
    // Errors updated, badge will appear on next Render()
}

void NodeView::SetSelected(bool selected) {
    isSelected_ = selected;
}

void NodeView::SetHovered(bool hovered) {
    isHovered_ = hovered;
}

SlotView* NodeView::GetSlotAtPosition(Vec2 screenPosition) const {
    // Check input slots
    for (auto& slot : inputSlots_) {
        if (slot->HitTest(screenPosition)) {
            return slot.get();
        }
    }

    // Check output slots
    for (auto& slot : outputSlots_) {
        if (slot->HitTest(screenPosition)) {
            return slot.get();
        }
    }

    return nullptr;
}

SlotView* NodeView::GetInputSlot(uint32_t index) const {
    if (index < inputSlots_.size()) {
        return inputSlots_[index].get();
    }
    return nullptr;
}

SlotView* NodeView::GetOutputSlot(uint32_t index) const {
    if (index < outputSlots_.size()) {
        return outputSlots_[index].get();
    }
    return nullptr;
}

} // namespace VIXEN::GraphEditor
```

---

## Complete Workflow Example

### User Creates and Connects Two Nodes

```cpp
// Application code example
#include "Controller/GraphController.h"
#include "Controller/NodeController.h"
#include "Controller/ConnectionController.h"
#include "View/GraphEditorView.h"

int main() {
    // Initialize MCP architecture
    auto graphModel = std::make_unique<GraphModel>();
    auto registry = std::make_unique<ConfigModelRegistry>();

    // Register all node types
    registry->RegisterAllNodeTypes();

    // Create controllers
    auto graphController = std::make_unique<GraphController>(graphModel.get());
    auto nodeController = std::make_unique<NodeController>(graphModel.get(), registry.get());
    auto connectionController = std::make_unique<ConnectionController>(graphModel.get());

    // Create view
    auto editorView = std::make_unique<GraphEditorView>(
        graphController.get(),
        nodeController.get(),
        connectionController.get(),
        graphModel.get()
    );

    // ========================================================================
    // WORKFLOW: User creates WindowNode
    // ========================================================================

    // User drags WindowNode from palette to canvas at position (100, 100)
    auto result1 = nodeController->CreateNode("WindowNode", "main_window", Vec2(100, 100));
    if (!result1.success) {
        std::cerr << "Failed to create WindowNode: " << result1.errorMessage << std::endl;
        return 1;
    }

    // View automatically receives notification via Observer pattern
    // GraphEditorView::OnNodeAdded() creates NodeView and renders it

    // ========================================================================
    // WORKFLOW: User creates SwapChainNode
    // ========================================================================

    // User drags SwapChainNode from palette to canvas at position (400, 100)
    auto result2 = nodeController->CreateNode("SwapChainNode", "swapchain_main", Vec2(400, 100));
    if (!result2.success) {
        std::cerr << "Failed to create SwapChainNode: " << result2.errorMessage << std::endl;
        return 1;
    }

    // ========================================================================
    // WORKFLOW: User connects WindowNode.OUTPUT_SURFACE to SwapChainNode.INPUT_SURFACE
    // ========================================================================

    // Get node handles
    NodeModel* windowNode = graphModel->FindNode("main_window");
    NodeModel* swapChainNode = graphModel->FindNode("swapchain_main");

    // User drags from WindowNode output slot 2 (SURFACE) to SwapChainNode input slot 1 (SURFACE)
    auto connectionResult = connectionController->CreateConnection(
        windowNode->GetHandle(),
        2,  // WindowNodeConfig::OUTPUT_SURFACE
        swapChainNode->GetHandle(),
        1   // SwapChainNodeConfig::INPUT_SURFACE
    );

    if (!connectionResult.success) {
        std::cerr << "Failed to create connection: " << connectionResult.errorMessage << std::endl;
        return 1;
    }

    // View automatically receives notification
    // GraphEditorView::OnConnectionAdded() creates ConnectionView and renders wire

    // ========================================================================
    // WORKFLOW: User compiles graph
    // ========================================================================

    // User clicks "Compile" button
    graphController->Compile();

    // CompilationController performs:
    // 1. ValidationController validates graph
    // 2. TopologyModel analyzes dependencies
    // 3. RenderGraph.Compile() called
    // 4. ExecutionModel updated with results

    // If errors occur, they're displayed in NodeView error badges

    // ========================================================================
    // WORKFLOW: User executes graph
    // ========================================================================

    // User clicks "Play" button
    graphController->Execute();

    // Main render loop
    while (true) {
        // Process input
        editorView->ProcessInput();

        // Update and render
        editorView->Render();

        // Execute graph (if playing)
        if (graphController->IsExecuting()) {
            graphModel->GetRenderGraph()->Execute();
        }

        // Present frame
        // ...
    }

    return 0;
}
```

---

## Key Takeaways

### 1. **Model Layer**: Single Source of Truth
- `NodeModel` wraps `NodeInstance` with observable properties
- Views subscribe to model changes automatically
- No manual synchronization required

### 2. **Controller Layer**: Command Pattern + Validation
- All user actions are commands (undoable)
- Controllers validate before modifying model
- Events emitted for cross-cutting concerns

### 3. **View Layer**: Passive Rendering
- Views observe models and render state
- Input handling translates to controller calls
- No business logic in views

### 4. **Workflow**: User Action → Controller → Model → View Update
```
User drags node
  → NodeView.OnDragStart()
  → NodeController.SetNodePosition()
  → CreateCommand + Execute
  → NodeModel.SetPosition()
  → NodeModel.position_.Notify()
  → NodeView.OnPositionChanged()
  → NodeView.Render() (updated position)
```

This architecture ensures:
- ✅ Clear separation of concerns
- ✅ Automatic UI synchronization
- ✅ Full undo/redo support
- ✅ Type-safe validation
- ✅ Testability (each layer independent)
- ✅ Extensibility (easy to add new node types)

---

## Next Steps

1. Implement Observable pattern (`Observable<T>`)
2. Implement Command pattern (`Command`, `CommandHistory`)
3. Implement Event system (`Event<T>`)
4. Prototype NodeModel + NodeController + NodeView
5. Test complete workflow end-to-end
