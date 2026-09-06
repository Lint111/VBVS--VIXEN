# Transforming Node Configs into Controller Points

This document demonstrates how to transform every existing node configuration into a controller point within the MCP architecture, enabling GUI-driven graph editing.

---

## Overview: Config as Controller Metadata

In the existing system:
- **Compile-time**: `CONSTEXPR_NODE_CONFIG` defines node structure
- **Runtime**: `NodeInstance` uses config for type-safe slot access

In the MCP system:
- **Model**: Config becomes runtime metadata (NodeTypeDescriptor)
- **Controller**: Config drives validation and connection logic
- **View**: Config drives visual representation (slot layout, colors)

---

## Transformation Pipeline

```
Compile-Time Config → Runtime Descriptor → Controller Logic → View Representation
```

### Step 1: Extract Config to Descriptor

For each node config (e.g., `WindowNodeConfig`), create a runtime descriptor:

```cpp
// Existing compile-time config
CONSTEXPR_NODE_CONFIG(WindowNodeConfig, 1, 5) {
    // Input: Instance
    INPUT_SLOT(INSTANCE, VkInstance, 0, Required, Dependency, ReadOnly, NodeLevel);

    // Outputs
    OUTPUT_SLOT(SURFACE, VkSurfaceKHR, 0, Required, WriteOnly);
    OUTPUT_SLOT(WIDTH, uint32_t, 1, Required, WriteOnly);
    OUTPUT_SLOT(HEIGHT, uint32_t, 2, Required, WriteOnly);
    OUTPUT_SLOT(WINDOW_HANDLE, void*, 3, Required, WriteOnly);
    OUTPUT_SLOT(SHOULD_CLOSE, bool, 4, Required, WriteOnly);
};
```

**Transforms to:**

```cpp
// Runtime descriptor for MCP architecture
NodeTypeDescriptor windowDescriptor {
    .typeName = "WindowNode",
    .category = "Resource",
    .description = "Creates a windowed surface for rendering",

    .inputs = {
        SlotDescriptor {
            .index = 0,
            .name = "Instance",
            .typeName = "VkInstance",
            .nullability = SlotNullability::Required,
            .role = SlotRole::Dependency,
            .mutability = SlotMutability::ReadOnly,
            .scope = SlotScope::NodeLevel,
            .typeColor = Color(100, 150, 255),  // Blue for Vulkan types
            .tooltip = "Vulkan instance handle"
        }
    },

    .outputs = {
        SlotDescriptor {
            .index = 0,
            .name = "Surface",
            .typeName = "VkSurfaceKHR",
            .nullability = SlotNullability::Required,
            .mutability = SlotMutability::WriteOnly,
            .scope = SlotScope::NodeLevel,
            .typeColor = Color(100, 150, 255),
            .tooltip = "Vulkan surface for presentation"
        },
        SlotDescriptor {
            .index = 1,
            .name = "Width",
            .typeName = "uint32_t",
            .nullability = SlotNullability::Required,
            .mutability = SlotMutability::WriteOnly,
            .scope = SlotScope::NodeLevel,
            .typeColor = Color(255, 200, 100),  // Orange for primitives
            .tooltip = "Window width in pixels"
        },
        // ... more outputs
    },

    .factoryFunction = []() -> NodeInstance* {
        return new TypedNode<WindowNodeType, WindowNodeConfig>();
    },

    .defaultColor = Color(80, 120, 180),
    .iconPath = "icons/window.png"
};
```

---

## Step 2: Config Registry as Controller Point

The `ConfigModelRegistry` acts as the central controller point for all node types:

```cpp
class ConfigModelRegistry {
public:
    // Registration (called at startup)
    void RegisterAllNodeTypes() {
        RegisterWindowNode();
        RegisterDeviceNode();
        RegisterSwapChainNode();
        RegisterComputePipelineNode();
        // ... register all 45+ node types
    }

    // Query interface (used by controllers)
    const NodeTypeDescriptor* GetNodeType(const std::string& typeName) const;
    std::vector<std::string> GetAllNodeTypes() const;
    std::vector<std::string> GetNodeTypesByCategory(const std::string& category) const;

    // Validation (used by ConnectionController)
    bool AreTypesCompatible(const std::string& sourceType, const std::string& targetType) const;
    bool CanConnectSlots(const SlotDescriptor& source, const SlotDescriptor& target) const;

private:
    std::map<std::string, NodeTypeDescriptor> nodeTypes_;
    std::map<std::string, TypeCompatibilityRule> compatibilityRules_;

    // Registration helpers
    void RegisterWindowNode();
    void RegisterDeviceNode();
    void RegisterSwapChainNode();
    // ... one method per node type
};
```

---

## Step 3: Per-Node Registration Functions

Each node type gets its own registration function that serves as a "controller point":

### Example: WindowNode Controller Point

```cpp
void ConfigModelRegistry::RegisterWindowNode() {
    NodeTypeDescriptor descriptor;
    descriptor.typeName = "WindowNode";
    descriptor.category = "Resource";
    descriptor.description = "Creates a windowed surface for rendering";

    // Input slots (from WindowNodeConfig)
    descriptor.inputs.push_back({
        .index = 0,
        .name = "Instance",
        .typeName = "VkInstance",
        .nullability = SlotNullability::Required,
        .role = SlotRole::Dependency,
        .mutability = SlotMutability::ReadOnly,
        .scope = SlotScope::NodeLevel,
        .typeColor = GetTypeColor("VkInstance"),
        .tooltip = "Vulkan instance handle",
        .defaultValue = std::nullopt
    });

    // Output slots
    descriptor.outputs = {
        {
            .index = 0,
            .name = "Surface",
            .typeName = "VkSurfaceKHR",
            .nullability = SlotNullability::Required,
            .mutability = SlotMutability::WriteOnly,
            .scope = SlotScope::NodeLevel,
            .typeColor = GetTypeColor("VkSurfaceKHR"),
            .tooltip = "Vulkan surface for presentation"
        },
        {
            .index = 1,
            .name = "Width",
            .typeName = "uint32_t",
            .nullability = SlotNullability::Required,
            .mutability = SlotMutability::WriteOnly,
            .scope = SlotScope::NodeLevel,
            .typeColor = GetTypeColor("uint32_t"),
            .tooltip = "Window width in pixels"
        },
        {
            .index = 2,
            .name = "Height",
            .typeName = "uint32_t",
            .nullability = SlotNullability::Required,
            .mutability = SlotMutability::WriteOnly,
            .scope = SlotScope::NodeLevel,
            .typeColor = GetTypeColor("uint32_t"),
            .tooltip = "Window height in pixels"
        },
        {
            .index = 3,
            .name = "WindowHandle",
            .typeName = "void*",
            .nullability = SlotNullability::Required,
            .mutability = SlotMutability::WriteOnly,
            .scope = SlotScope::NodeLevel,
            .typeColor = GetTypeColor("void*"),
            .tooltip = "Platform-specific window handle"
        },
        {
            .index = 4,
            .name = "ShouldClose",
            .typeName = "bool",
            .nullability = SlotNullability::Required,
            .mutability = SlotMutability::WriteOnly,
            .scope = SlotScope::NodeLevel,
            .typeColor = GetTypeColor("bool"),
            .tooltip = "True if window close requested"
        }
    };

    // Factory function
    descriptor.factoryFunction = []() -> NodeInstance* {
        return new TypedNode<WindowNodeType, WindowNodeConfig>();
    };

    // Visual metadata
    descriptor.defaultColor = Color(80, 120, 180);
    descriptor.iconPath = "icons/window.png";

    // Runtime parameters (editable in Inspector)
    descriptor.parameters = {
        {
            .name = "title",
            .type = "std::string",
            .defaultValue = std::string("VIXEN Window"),
            .description = "Window title bar text"
        },
        {
            .name = "width",
            .type = "uint32_t",
            .defaultValue = uint32_t(1920),
            .description = "Initial window width"
        },
        {
            .name = "height",
            .type = "uint32_t",
            .defaultValue = uint32_t(1080),
            .description = "Initial window height"
        },
        {
            .name = "resizable",
            .type = "bool",
            .defaultValue = true,
            .description = "Allow window resizing"
        }
    };

    // Register
    RegisterNodeType(descriptor);
}
```

---

### Example: ComputePipelineNode Controller Point

```cpp
void ConfigModelRegistry::RegisterComputePipelineNode() {
    NodeTypeDescriptor descriptor;
    descriptor.typeName = "ComputePipelineNode";
    descriptor.category = "Pipeline";
    descriptor.description = "Creates a compute pipeline for GPU computation";

    // Inputs (from ComputePipelineNodeConfig)
    descriptor.inputs = {
        {
            .index = 0,
            .name = "Device",
            .typeName = "VkDevice",
            .nullability = SlotNullability::Required,
            .role = SlotRole::Dependency,
            .mutability = SlotMutability::ReadOnly,
            .scope = SlotScope::NodeLevel,
            .typeColor = GetTypeColor("VkDevice"),
            .tooltip = "Logical device handle"
        },
        {
            .index = 1,
            .name = "ShaderModule",
            .typeName = "VkShaderModule",
            .nullability = SlotNullability::Required,
            .role = SlotRole::Dependency,
            .mutability = SlotMutability::ReadOnly,
            .scope = SlotScope::NodeLevel,
            .typeColor = GetTypeColor("VkShaderModule"),
            .tooltip = "Compiled shader module"
        },
        {
            .index = 2,
            .name = "DescriptorSetLayout",
            .typeName = "VkDescriptorSetLayout",
            .nullability = SlotNullability::Optional,
            .role = SlotRole::Dependency,
            .mutability = SlotMutability::ReadOnly,
            .scope = SlotScope::NodeLevel,
            .typeColor = GetTypeColor("VkDescriptorSetLayout"),
            .tooltip = "Descriptor set layout (optional)"
        }
    };

    // Outputs
    descriptor.outputs = {
        {
            .index = 0,
            .name = "Pipeline",
            .typeName = "VkPipeline",
            .nullability = SlotNullability::Required,
            .mutability = SlotMutability::WriteOnly,
            .scope = SlotScope::NodeLevel,
            .typeColor = GetTypeColor("VkPipeline"),
            .tooltip = "Compute pipeline handle"
        },
        {
            .index = 1,
            .name = "PipelineLayout",
            .typeName = "VkPipelineLayout",
            .nullability = SlotNullability::Required,
            .mutability = SlotMutability::WriteOnly,
            .scope = SlotScope::NodeLevel,
            .typeColor = GetTypeColor("VkPipelineLayout"),
            .tooltip = "Pipeline layout handle"
        }
    };

    // Factory
    descriptor.factoryFunction = []() -> NodeInstance* {
        return new TypedNode<ComputePipelineNodeType, ComputePipelineNodeConfig>();
    };

    // Visual
    descriptor.defaultColor = Color(150, 100, 180);  // Purple for pipelines
    descriptor.iconPath = "icons/compute_pipeline.png";

    // Parameters
    descriptor.parameters = {
        {
            .name = "entryPoint",
            .type = "std::string",
            .defaultValue = std::string("main"),
            .description = "Shader entry point function name"
        },
        {
            .name = "workgroupSizeX",
            .type = "uint32_t",
            .defaultValue = uint32_t(8),
            .description = "Workgroup size in X dimension"
        },
        {
            .name = "workgroupSizeY",
            .type = "uint32_t",
            .defaultValue = uint32_t(8),
            .description = "Workgroup size in Y dimension"
        },
        {
            .name = "workgroupSizeZ",
            .type = "uint32_t",
            .defaultValue = uint32_t(1),
            .description = "Workgroup size in Z dimension"
        }
    };

    RegisterNodeType(descriptor);
}
```

---

## Step 4: Type Compatibility Rules as Controller Logic

The registry also defines how types can connect (controller logic):

```cpp
class ConfigModelRegistry {
private:
    void InitializeTypeCompatibilityRules() {
        // Direct type matching
        AddCompatibilityRule("VkInstance", "VkInstance", CompatibilityType::Exact);
        AddCompatibilityRule("VkDevice", "VkDevice", CompatibilityType::Exact);
        AddCompatibilityRule("VkImage", "VkImage", CompatibilityType::Exact);

        // Implicit conversions
        AddCompatibilityRule("uint32_t", "int32_t", CompatibilityType::ImplicitCast);
        AddCompatibilityRule("float", "double", CompatibilityType::ImplicitCast);

        // PassThroughStorage compatibility
        AddCompatibilityRule("PassThroughStorage", "*", CompatibilityType::TypeErasure);

        // Array element compatibility
        AddCompatibilityRule("VkImage", "VkImage[]", CompatibilityType::ArrayElement);

        // Struct field extraction
        AddCompatibilityRule("CameraData", "Mat4", CompatibilityType::FieldExtraction, "viewMatrix");
        AddCompatibilityRule("CameraData", "Vec3", CompatibilityType::FieldExtraction, "position");
    }

public:
    bool AreTypesCompatible(
        const std::string& sourceType,
        const std::string& targetType,
        CompatibilityReason* outReason = nullptr
    ) const {
        // Exact match
        if (sourceType == targetType) {
            if (outReason) *outReason = CompatibilityReason::ExactMatch;
            return true;
        }

        // Check compatibility rules
        auto key = std::make_pair(sourceType, targetType);
        auto it = compatibilityRules_.find(key);
        if (it != compatibilityRules_.end()) {
            if (outReason) *outReason = it->second.reason;
            return true;
        }

        // PassThroughStorage accepts any type
        if (targetType == "PassThroughStorage") {
            if (outReason) *outReason = CompatibilityReason::TypeErasure;
            return true;
        }

        if (outReason) *outReason = CompatibilityReason::Incompatible;
        return false;
    }

    bool CanConnectSlots(const SlotDescriptor& source, const SlotDescriptor& target) const {
        // Type compatibility
        if (!AreTypesCompatible(source.typeName, target.typeName)) {
            return false;
        }

        // Mutability check (can't write to ReadOnly)
        if (target.mutability == SlotMutability::ReadOnly) {
            return false;
        }

        // Role check (Dependency slots must connect before Execute slots)
        if (source.role == SlotRole::Execute && target.role == SlotRole::Dependency) {
            return false;
        }

        // Scope compatibility
        if (source.scope != target.scope && target.scope != SlotScope::NodeLevel) {
            return false;
        }

        return true;
    }
};
```

---

## Step 5: Auto-Generate Registration from Existing Configs

To avoid manual translation, create a code generator:

```cpp
// Tool: ConfigToDescriptorGenerator
// Reads existing node config headers and generates registration functions

class ConfigToDescriptorGenerator {
public:
    void GenerateRegistrationCode(const std::filesystem::path& configHeaderPath) {
        // Parse CONSTEXPR_NODE_CONFIG macro
        ConfigParseResult parseResult = ParseNodeConfig(configHeaderPath);

        // Generate registration function
        std::ofstream output("Generated_" + parseResult.nodeName + "_Registration.cpp");

        output << "void ConfigModelRegistry::Register" << parseResult.nodeName << "() {\n";
        output << "    NodeTypeDescriptor descriptor;\n";
        output << "    descriptor.typeName = \"" << parseResult.nodeName << "\";\n";
        output << "    descriptor.category = \"" << InferCategory(parseResult) << "\";\n";
        output << "\n";

        // Generate input slots
        output << "    // Input slots\n";
        for (const auto& slot : parseResult.inputSlots) {
            output << "    descriptor.inputs.push_back({\n";
            output << "        .index = " << slot.index << ",\n";
            output << "        .name = \"" << slot.name << "\",\n";
            output << "        .typeName = \"" << slot.typeName << "\",\n";
            output << "        .nullability = SlotNullability::" << slot.nullability << ",\n";
            output << "        .role = SlotRole::" << slot.role << ",\n";
            output << "        .mutability = SlotMutability::" << slot.mutability << ",\n";
            output << "        .scope = SlotScope::" << slot.scope << ",\n";
            output << "        .typeColor = GetTypeColor(\"" << slot.typeName << "\"),\n";
            output << "        .tooltip = \"" << GenerateTooltip(slot) << "\"\n";
            output << "    });\n";
        }

        // Generate output slots
        output << "\n    // Output slots\n";
        for (const auto& slot : parseResult.outputSlots) {
            // Similar generation
        }

        // Generate factory
        output << "\n    // Factory function\n";
        output << "    descriptor.factoryFunction = []() -> NodeInstance* {\n";
        output << "        return new TypedNode<" << parseResult.nodeTypeName
               << ", " << parseResult.configName << ">();\n";
        output << "    };\n";

        // Visual metadata
        output << "\n    // Visual metadata\n";
        output << "    descriptor.defaultColor = " << InferColor(parseResult) << ";\n";
        output << "    descriptor.iconPath = \"icons/" << parseResult.nodeName << ".png\";\n";

        output << "\n    RegisterNodeType(descriptor);\n";
        output << "}\n";
    }
};
```

### Usage:

```bash
# Generate registration code for all node configs
./ConfigToDescriptorGenerator \
    --input VIXEN/libraries/RenderGraph/include/Data/Nodes/*.h \
    --output VIXEN/libraries/RenderGraphEditor/src/Generated/NodeRegistrations.cpp
```

---

## Step 6: Controller Usage Example

Now that all configs are transformed to descriptors, controllers use them:

### ConnectionController using Registry

```cpp
CommandResult ConnectionController::CreateConnection(
    NodeHandle sourceHandle,
    uint32_t sourceSlotIndex,
    NodeHandle targetHandle,
    uint32_t targetSlotIndex
) {
    // Get nodes
    NodeModel* sourceNode = graphModel_->FindNode(sourceHandle);
    NodeModel* targetNode = graphModel_->FindNode(targetHandle);

    if (!sourceNode || !targetNode) {
        return CommandResult::Error("Node not found");
    }

    // Get slot descriptors
    const SlotModel& sourceSlot = sourceNode->GetOutputSlot(sourceSlotIndex);
    const SlotModel& targetSlot = targetNode->GetInputSlot(targetSlotIndex);

    // Validate using registry (CONTROLLER POINT)
    if (!registry_->CanConnectSlots(sourceSlot, targetSlot)) {
        CompatibilityReason reason;
        registry_->AreTypesCompatible(
            sourceSlot.typeName,
            targetSlot.typeName,
            &reason
        );

        return CommandResult::Error(
            "Cannot connect " + sourceSlot.typeName + " to " + targetSlot.typeName +
            " (reason: " + ToString(reason) + ")"
        );
    }

    // Create connection
    auto connection = std::make_unique<ConnectionModel>(
        sourceNode, sourceSlotIndex,
        targetNode, targetSlotIndex
    );

    // Register in graph model
    graphModel_->AddConnection(std::move(connection));

    return CommandResult::Success();
}
```

---

## Step 7: View Layer Uses Descriptors

Views query the registry to render node palettes and tooltips:

```cpp
class ToolbarView {
private:
    void RenderNodePalette() {
        // Group nodes by category
        for (const auto& category : registry_->GetAllCategories()) {
            if (ImGui::TreeNode(category.c_str())) {
                // Get nodes in this category
                auto nodeTypes = registry_->GetNodeTypesByCategory(category);

                for (const auto& nodeTypeName : nodeTypes) {
                    const NodeTypeDescriptor* descriptor = registry_->GetNodeType(nodeTypeName);

                    // Render node button with icon and color
                    ImGui::PushID(nodeTypeName.c_str());

                    // Color indicator
                    ImGui::ColorButton("##color", descriptor->defaultColor.ToImVec4(),
                                       ImGuiColorEditFlags_NoTooltip);
                    ImGui::SameLine();

                    // Node button
                    if (ImGui::Selectable(nodeTypeName.c_str())) {
                        // Begin drag-and-drop
                        BeginNodeDrag(descriptor);
                    }

                    // Tooltip
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::Text("%s", descriptor->description.c_str());
                        ImGui::Separator();
                        ImGui::Text("Inputs: %zu", descriptor->inputs.size());
                        ImGui::Text("Outputs: %zu", descriptor->outputs.size());
                        ImGui::EndTooltip();
                    }

                    ImGui::PopID();
                }

                ImGui::TreePop();
            }
        }
    }
};
```

---

## Complete Transformation Example

### Before (Compile-Time Only):

```cpp
// WindowNodeConfig.h
CONSTEXPR_NODE_CONFIG(WindowNodeConfig, 1, 5) {
    INPUT_SLOT(INSTANCE, VkInstance, 0, Required, Dependency, ReadOnly, NodeLevel);
    OUTPUT_SLOT(SURFACE, VkSurfaceKHR, 0, Required, WriteOnly);
    OUTPUT_SLOT(WIDTH, uint32_t, 1, Required, WriteOnly);
    OUTPUT_SLOT(HEIGHT, uint32_t, 2, Required, WriteOnly);
    OUTPUT_SLOT(WINDOW_HANDLE, void*, 3, Required, WriteOnly);
    OUTPUT_SLOT(SHOULD_CLOSE, bool, 4, Required, WriteOnly);
};

// Usage (compile-time)
auto windowNode = renderGraph.AddNode<WindowNodeType, WindowNodeConfig>("main_window");
```

### After (MCP Architecture):

```cpp
// Model: Runtime representation
NodeModel* windowNode = graphModel->FindNode("main_window");
windowNode->GetOutputSlot(0);  // SURFACE slot

// Controller: Validation and operations
nodeController->CreateNode("WindowNode", "main_window", Vec2(100, 100));
connectionController->CreateConnection(windowNode->GetHandle(), 0, swapChainNode->GetHandle(), 1);

// View: Visual representation
nodeView->Render(drawList, worldToScreen);
slotView->RenderTooltip();  // Shows "Vulkan surface for presentation"
```

**Result:**
- ✅ Compile-time config preserved (no changes to existing code)
- ✅ Runtime descriptor generated (enables GUI)
- ✅ Controller validates using descriptor metadata
- ✅ View renders using descriptor visual hints
- ✅ All 45+ node types become controller points

---

## Summary: Every Config is Now a Controller Point

| Component | Role | Example |
|-----------|------|---------|
| **Config (Compile-Time)** | Type-safe slot definitions | `WindowNodeConfig` with `INPUT_SLOT` macros |
| **Descriptor (Runtime)** | Model metadata | `NodeTypeDescriptor` with slot metadata |
| **Registry (Controller)** | Validation + Factory | `ConfigModelRegistry::RegisterWindowNode()` |
| **NodeController (Controller)** | CRUD operations | `CreateNode("WindowNode", "main_window")` |
| **ConnectionController (Controller)** | Connection validation | `CanConnectSlots(sourceSlot, targetSlot)` |
| **NodeView (View)** | Visual rendering | Renders node with slots based on descriptor |

**Every node config is transformed into:**
1. A runtime descriptor (model)
2. A registration function (controller point)
3. Factory function (instantiation controller)
4. Validation rules (connection controller)
5. Visual metadata (view hints)

This architecture enables **full GUI graph editing** while preserving the existing compile-time type safety and zero-overhead runtime performance.
