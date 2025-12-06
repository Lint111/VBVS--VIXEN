---
title: Coding Standards
aliases: [Code Style, C++ Guidelines, Standards]
tags: [development, standards, cpp, style]
created: 2025-12-06
related:
  - "[[Overview]]"
  - "[[../01-Architecture/Overview]]"
---

# Coding Standards

C++23 guidelines, naming conventions, and architectural principles for VIXEN development.

---

## 1. Nomenclature

### 1.1 Naming Conventions

| Element | Style | Example |
|---------|-------|---------|
| Classes | PascalCase | `RenderGraph`, `NodeInstance` |
| Functions | camelCase | `castRay()`, `getNodeCount()` |
| Variables | camelCase | `rayOrigin`, `nodeCount` |
| Constants | UPPER_CASE | `MAX_DEPTH`, `BRICK_SIZE` |
| Private members | trailing_ | `device_`, `nodes_` |
| Namespaces | lowercase | `vixen::svo` |
| Files | PascalCase | `RenderGraph.cpp`, `NodeInstance.h` |

### 1.2 Examples

```cpp
namespace vixen::svo {

class LaineKarrasOctree {
public:
    static constexpr int MAX_DEPTH = 23;

    void rebuild(const GaiaVoxelWorld& world);
    size_t getNodeCount() const;

private:
    std::vector<ChildDescriptor> nodes_;
    size_t brickCount_;
};

}  // namespace vixen::svo
```

---

## 2. Functions

### 2.1 Guidelines

| Rule | Description |
|------|-------------|
| Short | < 20 instructions |
| Single purpose | One reason to change |
| Clear names | Verb + noun |
| Early returns | Avoid deep nesting |
| Parameters | Pass by const ref for objects |

### 2.2 Good Example

```cpp
std::optional<HitResult> castRay(const glm::vec3& origin,
                                  const glm::vec3& direction) {
    if (!isValid()) {
        return std::nullopt;
    }

    RayCoefficients coeffs = initCoefficients(origin, direction);
    TraversalState state = initState(coeffs);

    return traverse(state, coeffs);
}
```

### 2.3 Bad Example (Avoid)

```cpp
// BAD: Too long, multiple responsibilities
std::optional<HitResult> doEverything(vec3 o, vec3 d) {
    // 200 lines of nested code...
}
```

---

## 3. Data

### 3.1 Const-Correctness

```cpp
// Prefer const
const std::string& getName() const;

// Use const parameters
void process(const std::vector<Node>& nodes);

// Mark unmodified locals as const
const auto result = compute(input);
```

### 3.2 Immutability

```cpp
// Prefer immutable by default
class Config {
public:
    Config(int width, int height)
        : width_(width), height_(height) {}

    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

private:
    const int width_;
    const int height_;
};
```

### 3.3 Nullable Values

```cpp
// Use std::optional for nullable returns
std::optional<HitResult> castRay(...);

// Check before use
if (auto hit = octree.castRay(origin, dir)) {
    processHit(*hit);
}
```

---

## 4. Classes

### 4.1 SOLID Principles

| Principle | Application |
|-----------|-------------|
| **S**ingle Responsibility | One class, one purpose |
| **O**pen/Closed | Extend via inheritance/composition |
| **L**iskov Substitution | Derived classes substitutable |
| **I**nterface Segregation | Small, focused interfaces |
| **D**ependency Inversion | Depend on abstractions |

### 4.2 Size Limits

| Metric | Limit |
|--------|-------|
| Class size | < 200 instructions |
| Public methods | < 10 |
| File length | < 500 lines |

### 4.3 Composition over Inheritance

```cpp
// GOOD: Composition
class RenderGraph {
    std::unique_ptr<EventBus> eventBus_;
    std::vector<std::unique_ptr<NodeInstance>> nodes_;
};

// AVOID: Deep inheritance
class MySpecialNode : public TypedNode<Config>
                    : public IRenderable
                    : public IUpdatable
                    : public ISeriazable { ... };
```

---

## 5. Memory Management

### 5.1 Smart Pointers

| Type | Use Case |
|------|----------|
| `std::unique_ptr<T>` | Exclusive ownership |
| `std::shared_ptr<T>` | Shared ownership |
| Raw pointer `T*` | Non-owning reference |

### 5.2 RAII

```cpp
class ScopedVulkanBuffer {
public:
    ScopedVulkanBuffer(VkDevice device, VkBufferCreateInfo info)
        : device_(device) {
        vkCreateBuffer(device, &info, nullptr, &buffer_);
    }

    ~ScopedVulkanBuffer() {
        if (buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, buffer_, nullptr);
        }
    }

    // Non-copyable
    ScopedVulkanBuffer(const ScopedVulkanBuffer&) = delete;
    ScopedVulkanBuffer& operator=(const ScopedVulkanBuffer&) = delete;

    // Movable
    ScopedVulkanBuffer(ScopedVulkanBuffer&& other) noexcept
        : device_(other.device_), buffer_(other.buffer_) {
        other.buffer_ = VK_NULL_HANDLE;
    }

private:
    VkDevice device_;
    VkBuffer buffer_;
};
```

### 5.3 No Raw new/delete

```cpp
// GOOD
auto node = std::make_unique<MyNode>();
nodes_.push_back(std::move(node));

// BAD
auto node = new MyNode();  // Who deletes this?
```

---

## 6. Modern C++ Features

### 6.1 C++23 Features Used

| Feature | Usage |
|---------|-------|
| `std::variant` | ResourceHandleVariant |
| `std::optional` | Nullable returns |
| `std::span` | Non-owning arrays |
| Concepts | Template constraints |
| Ranges | Algorithm pipelines |
| `std::format` | String formatting |
| `constexpr` | Compile-time computation |

### 6.2 Examples

```cpp
// Concepts
template<typename T>
concept Compilable = requires(T t) {
    { t.Compile() } -> std::same_as<void>;
};

// Ranges
auto validNodes = nodes_ | std::views::filter([](auto& n) {
    return n->isValid();
});

// std::format
LOG_INFO("Rendered {} rays in {:.2f}ms", rayCount, elapsed);
```

---

## 7. Error Handling

### 7.1 Strategy

| Situation | Approach |
|-----------|----------|
| Programming errors | `assert()` |
| Recoverable errors | `std::optional` / error codes |
| Configuration errors | Exceptions at startup |
| Vulkan errors | Check VkResult |

### 7.2 VkResult Handling

```cpp
VkResult result = vkCreateDevice(physicalDevice, &createInfo,
                                  nullptr, &device);
if (result != VK_SUCCESS) {
    LOG_ERROR("Failed to create device: {}", result);
    return false;
}
```

---

## 8. Documentation

### 8.1 Header Comments

```cpp
/// @brief Brief description of the class.
///
/// Detailed description if needed. Explain the purpose,
/// usage patterns, and any important constraints.
///
/// @see RelatedClass
class MyClass { ... };
```

### 8.2 Function Comments

```cpp
/// @brief Casts a ray through the octree.
///
/// @param origin Ray origin in world space
/// @param direction Normalized ray direction
/// @param tMin Minimum t value (default: 0)
/// @param tMax Maximum t value (default: FLT_MAX)
/// @return Hit result if intersection found, nullopt otherwise
std::optional<HitResult> castRay(const glm::vec3& origin,
                                  const glm::vec3& direction,
                                  float tMin = 0.0f,
                                  float tMax = FLT_MAX);
```

---

## 9. Engineering Philosophy

### 9.1 No Quick Fixes

| Prohibited | Required |
|------------|----------|
| Magic numbers | Named constants |
| Commented-out code | Delete or fix |
| "TODO: fix later" | Fix now or create issue |
| Hardcoded workarounds | Proper solutions |

### 9.2 Example

```cpp
// BAD: Quick fix
if (depth == 23) {  // Magic number
    depth = 8;  // Workaround for broken traversal
}

// GOOD: Proper fix
constexpr int TARGET_DEPTH = 8;
octree.rebuild(world, TARGET_DEPTH);  // Algorithm supports any depth
```

---

## 10. Code References

| Document | Location |
|----------|----------|
| Full Guidelines | `documentation/Standards/cpp-programming-guidelins.md` |
| Smart Pointers Guide | `documentation/Standards/smart-pointers-guide.md` |
| Communication Style | `documentation/Standards/Communication Guidelines.md` |

---

## 11. Related Pages

- [[Overview]] - Development overview
- [[../01-Architecture/Overview|Architecture]] - Design patterns
- [[Testing]] - Test conventions
