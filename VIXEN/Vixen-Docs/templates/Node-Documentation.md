---
title: Node Documentation Template
aliases: [Node Template]
tags: [template, node]
created: {{date}}
---

# {{NodeName}}

Brief description of what this node does.

---

## 1. Purpose

One paragraph explaining the node's role in the render graph.

---

## 2. Slot Configuration

### 2.1 Inputs

| Slot | Type | Nullability | Role | Description |
|------|------|-------------|------|-------------|
| DEVICE | VkDevice | Required | Dependency | Vulkan logical device |
| INPUT_IMAGE | VkImageView | Required | Execute | Image to process |

### 2.2 Outputs

| Slot | Type | Description |
|------|------|-------------|
| OUTPUT_IMAGE | VkImageView | Processed result |

---

## 3. Config Struct

```cpp
struct {{NodeName}}Config {
    AUTO_INPUT(DEVICE, VkDevice,
               SlotNullability::Required,
               SlotRole::Dependency,
               SlotMutability::ReadOnly);

    AUTO_INPUT(INPUT_IMAGE, VkImageView,
               SlotNullability::Required,
               SlotRole::Execute,
               SlotMutability::ReadOnly);

    AUTO_OUTPUT(OUTPUT_IMAGE, VkImageView,
                SlotNullability::Required,
                SlotRole::Dependency,
                SlotMutability::WriteOnly);
};
```

---

## 4. Lifecycle

### 4.1 Setup

```cpp
void SetupImpl(Context& ctx) override {
    // Subscribe to events
    eventBus->Subscribe(EventType::ResourceChanged, this);
}
```

### 4.2 Compile

```cpp
void CompileImpl(Context& ctx) override {
    auto device = In(Config::DEVICE);
    auto input = In(Config::INPUT_IMAGE);

    // Create resources
    // ...

    Out(Config::OUTPUT_IMAGE, result);
}
```

### 4.3 Execute

```cpp
void ExecuteImpl(Context& ctx) override {
    // Record commands or no-op
}
```

### 4.4 Cleanup

```cpp
void CleanupImpl(Context& ctx) override {
    // Destroy resources (reverse of Compile)
}
```

---

## 5. Events

| Event | Action |
|-------|--------|
| ResourceChanged | SetDirty(true), recompile |

---

## 6. Usage Example

```cpp
auto myNode = graph.AddNode({{NodeName}}Type, "MyNodeInstance");
graph.ConnectNodes(deviceNode, DeviceConfig::DEVICE, myNode, {{NodeName}}Config::DEVICE);
graph.ConnectNodes(sourceNode, SourceConfig::OUTPUT, myNode, {{NodeName}}Config::INPUT_IMAGE);
```

---

## 7. Code References

| Component | Location |
|-----------|----------|
| Header | `libraries/RenderGraph/include/Nodes/{{NodeName}}.h` |
| Implementation | `libraries/RenderGraph/src/Nodes/{{NodeName}}.cpp` |
| Config | `libraries/RenderGraph/include/Data/Nodes/{{NodeName}}Config.h` |

---

## 8. Related Pages

- [[../01-Architecture/RenderGraph-System|RenderGraph System]]
- [[Related-Node-1]]
- [[Related-Node-2]]
