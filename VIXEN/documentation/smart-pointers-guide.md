# Smart Pointers Guide for C++ Beginners

## The Problem: Manual Memory Management

In C++, when you use `new`, you must manually call `delete`:

```cpp
// Old style - DANGEROUS
MyClass* obj = new MyClass();
// ... use obj ...
delete obj;  // Easy to forget! Memory leak if you forget!
```

**Problems:**
- Forget `delete` → Memory leak
- Delete twice → Crash
- Exception thrown before `delete` → Memory leak
- Complex ownership → Who should delete?

## The Solution: Smart Pointers

Smart pointers automatically manage memory using **RAII** (Resource Acquisition Is Initialization):
- Memory allocated in constructor
- Memory freed in destructor
- No manual `delete` needed!

---

## std::unique_ptr - Exclusive Ownership

**Use when:** One object owns the resource exclusively.

### Basic Usage

```cpp
#include <memory>

// Create unique_ptr (modern way)
std::unique_ptr<MyClass> obj = std::make_unique<MyClass>();

// Use like regular pointer
obj->DoSomething();
obj->value = 42;

// Automatically deleted when obj goes out of scope
// No delete needed!
```

### Key Properties

```cpp
// ✅ OWNERSHIP - Only one owner
std::unique_ptr<MyClass> obj1 = std::make_unique<MyClass>();

// ❌ CANNOT COPY - This won't compile
// std::unique_ptr<MyClass> obj2 = obj1;  // ERROR!

// ✅ CAN MOVE - Transfer ownership
std::unique_ptr<MyClass> obj2 = std::move(obj1);
// Now obj1 is nullptr, obj2 owns the object

// ✅ GET RAW POINTER - For non-owning access
MyClass* rawPtr = obj2.get();  // Doesn't transfer ownership
```

### Real Example from Our Code

```cpp
class VulkanApplication {
    // VulkanApplication OWNS the device and renderer
    std::unique_ptr<VulkanDevice> deviceObj;
    std::unique_ptr<VulkanRenderer> renderObj;

    void Initialize() {
        // Create device - VulkanApplication takes ownership
        deviceObj = std::make_unique<VulkanDevice>(gpu);

        // Create renderer - VulkanApplication takes ownership
        renderObj = std::make_unique<VulkanRenderer>(this, deviceObj.get());
        //                                                   ^^^^^^^^^^
        //                                  Pass raw pointer (non-owning)
    }

    // Destructor automatically deletes deviceObj and renderObj
    ~VulkanApplication() {
        // No manual delete needed!
    }
};
```

### When to Use unique_ptr

- **Class members**: When your class owns another object
- **Factory functions**: Return ownership to caller
- **Replacing `new`**: Always prefer unique_ptr over raw `new`

---

## std::shared_ptr - Shared Ownership

**Use when:** Multiple objects need to share ownership of a resource.

### Basic Usage

```cpp
#include <memory>

// Create shared_ptr
std::shared_ptr<MyClass> obj1 = std::make_shared<MyClass>();

// ✅ CAN COPY - Multiple owners
std::shared_ptr<MyClass> obj2 = obj1;  // Both own the object
std::shared_ptr<MyClass> obj3 = obj1;  // Three owners now

// Object deleted when LAST owner is destroyed
// Reference counting tracks how many owners exist
```

### Reference Counting

```cpp
{
    std::shared_ptr<MyClass> ptr1 = std::make_shared<MyClass>();
    // Reference count: 1

    {
        std::shared_ptr<MyClass> ptr2 = ptr1;
        // Reference count: 2

        std::shared_ptr<MyClass> ptr3 = ptr1;
        // Reference count: 3

    } // ptr2 and ptr3 destroyed, count drops to 1

    // ptr1 still valid here

} // ptr1 destroyed, count drops to 0, object deleted
```

### Real-World Example

```cpp
class Document {
    std::string content;
};

class Editor {
    std::shared_ptr<Document> doc;  // Editor shares document
};

class Viewer {
    std::shared_ptr<Document> doc;  // Viewer shares document
};

// Usage
auto document = std::make_shared<Document>();

Editor editor;
editor.doc = document;  // Editor gets shared ownership

Viewer viewer;
viewer.doc = document;  // Viewer gets shared ownership

// Document deleted only when editor, viewer, AND document are destroyed
```

### When to Use shared_ptr

- **Shared resources**: Multiple objects need the same resource
- **Caching**: Multiple caches hold references to same data
- **Observer pattern**: Multiple observers watch same subject
- **Graphs/Trees**: Nodes with multiple parents

---

## Raw Pointers - Non-Owning References

Raw pointers still have their place for **non-owning access**:

```cpp
class VulkanRenderer {
    VulkanApplication* appObj;  // Doesn't own, just observes
    VulkanDevice* deviceObj;    // Doesn't own, just uses

    VulkanRenderer(VulkanApplication* app, VulkanDevice* device)
        : appObj(app), deviceObj(device)
    {
        // We don't own these, so we don't delete them
    }
};

// Usage
auto app = std::make_unique<VulkanApplication>();
auto device = std::make_unique<VulkanDevice>();

// Pass raw pointers for non-owning access
VulkanRenderer renderer(app.get(), device.get());
//                      ^^^^^^^^^   ^^^^^^^^^^^^^
//                      Non-owning references
```

**Rule:** Use raw pointers when you **don't own** the object.

---

## Comparison Table

| Feature | unique_ptr | shared_ptr | Raw Pointer |
|---------|-----------|-----------|-------------|
| **Ownership** | Exclusive | Shared | None |
| **Copy** | ❌ No | ✅ Yes | ✅ Yes |
| **Move** | ✅ Yes | ✅ Yes | ✅ Yes |
| **Auto Delete** | ✅ Yes | ✅ Yes (when last owner dies) | ❌ No |
| **Overhead** | None | Reference counting | None |
| **Thread Safe** | No | Yes (ref count) | No |
| **Use For** | Exclusive ownership | Shared ownership | Non-owning reference |

---

## Common Patterns

### Pattern 1: Factory Function

```cpp
// Return unique_ptr to transfer ownership
std::unique_ptr<Weapon> CreateWeapon(const std::string& type) {
    if (type == "sword") {
        return std::make_unique<Sword>();
    } else if (type == "bow") {
        return std::make_unique<Bow>();
    }
    return nullptr;
}

// Caller takes ownership
auto weapon = CreateWeapon("sword");
```

### Pattern 2: Container of Smart Pointers

```cpp
class Scene {
    // Scene owns all entities
    std::vector<std::unique_ptr<Entity>> entities;

    void AddEntity(std::unique_ptr<Entity> entity) {
        entities.push_back(std::move(entity));  // Transfer ownership
    }

    // Destructor automatically deletes all entities
    ~Scene() = default;
};
```

### Pattern 3: Passing to Functions

```cpp
// By value - Takes ownership
void TakeOwnership(std::unique_ptr<MyClass> obj);

// By reference - Borrows temporarily
void UseTemporarily(const std::unique_ptr<MyClass>& obj);

// Raw pointer - Non-owning observation
void Observe(MyClass* obj);

// Usage
auto obj = std::make_unique<MyClass>();

TakeOwnership(std::move(obj));  // obj is now nullptr
// OR
UseTemporarily(obj);            // obj still valid
// OR
Observe(obj.get());             // obj still valid
```

---

## Our Codebase Examples

### Before (Manual Memory Management)

```cpp
class VulkanApplication {
    VulkanDevice* deviceObj;
    VulkanRenderer* renderObj;

    void Initialize() {
        deviceObj = new VulkanDevice(gpu);     // Manual new
        renderObj = new VulkanRenderer(this, deviceObj);
    }

    ~VulkanApplication() {
        delete renderObj;   // Must remember!
        delete deviceObj;   // Must remember!
        renderObj = nullptr;
        deviceObj = nullptr;
    }
};
```

**Problems:**
- Must manually delete in correct order
- Easy to forget
- Exception before delete = memory leak
- Delete order matters (renderObj uses deviceObj)

### After (Smart Pointers)

```cpp
class VulkanApplication {
    std::unique_ptr<VulkanDevice> deviceObj;
    std::unique_ptr<VulkanRenderer> renderObj;

    void Initialize() {
        deviceObj = std::make_unique<VulkanDevice>(gpu);
        renderObj = std::make_unique<VulkanRenderer>(this, deviceObj.get());
    }

    ~VulkanApplication() {
        // Automatic cleanup in correct order!
        // renderObj deleted first, then deviceObj
        // No manual delete needed!
    }
};
```

**Benefits:**
- Automatic deletion in reverse construction order
- Exception-safe
- Clear ownership semantics
- Impossible to forget deletion

---

## Common Mistakes to Avoid

### Mistake 1: Don't Mix new/delete with Smart Pointers

```cpp
// ❌ BAD - Don't do this
MyClass* raw = new MyClass();
std::unique_ptr<MyClass> smart(raw);
delete raw;  // CRASH! Smart pointer will try to delete again

// ✅ GOOD - Let smart pointer own from start
std::unique_ptr<MyClass> smart = std::make_unique<MyClass>();
```

### Mistake 2: Don't Store Raw Pointers from Smart Pointers

```cpp
// ❌ BAD - Dangerous dangling pointer
MyClass* dangling = nullptr;
{
    auto smart = std::make_unique<MyClass>();
    dangling = smart.get();
} // smart deleted here

dangling->DoSomething();  // CRASH! Dangling pointer

// ✅ GOOD - Keep smart pointer alive
std::unique_ptr<MyClass> smart = std::make_unique<MyClass>();
MyClass* safe = smart.get();
safe->DoSomething();  // OK, smart still owns it
```

### Mistake 3: Don't Create shared_ptr from same Raw Pointer Twice

```cpp
MyClass* raw = new MyClass();

// ❌ BAD - Two independent reference counts!
std::shared_ptr<MyClass> ptr1(raw);
std::shared_ptr<MyClass> ptr2(raw);
// Both will try to delete, CRASH!

// ✅ GOOD - Copy shared_ptr
auto ptr1 = std::make_shared<MyClass>();
auto ptr2 = ptr1;  // Share ownership correctly
```

---

## Decision Tree

```
Do I need to OWN this object?
│
├─ YES → Use Smart Pointer
│  │
│  ├─ Only ONE owner? → std::unique_ptr
│  │   Example: Class member that class exclusively owns
│  │
│  └─ MULTIPLE owners? → std::shared_ptr
│      Example: Cached resource used by many objects
│
└─ NO → Use Raw Pointer or Reference
    Example: Function parameter, observer, non-owning reference
```

---

## Best Practices

1. **Default to unique_ptr** - Use it for ownership
2. **Use shared_ptr sparingly** - Only when truly needed for shared ownership
3. **Raw pointers for observation** - When you don't own the object
4. **Never manual new/delete** - Always use smart pointers
5. **Use make_unique/make_shared** - Safer and more efficient
6. **Pass by reference when possible** - Avoid copying smart pointers unnecessarily

---

## Further Reading

- **std::weak_ptr**: Breaks circular references in shared_ptr
- **Custom deleters**: For special cleanup logic
- **Array support**: `unique_ptr<int[]>` for arrays
- **Performance**: Smart pointers have minimal overhead

---

## Quick Reference Card

```cpp
// Creation
auto ptr = std::make_unique<T>(args...);
auto ptr = std::make_shared<T>(args...);

// Access
ptr->member();      // Call member function
*ptr;              // Dereference
ptr.get();         // Get raw pointer (non-owning)

// Ownership
ptr.reset();                    // Delete owned object, set to nullptr
ptr.reset(new T());            // Delete old, own new
ptr.release();                 // Give up ownership, return raw pointer

// Checking
if (ptr) { ... }               // Check if not nullptr
if (ptr == nullptr) { ... }

// Moving (unique_ptr only)
auto ptr2 = std::move(ptr1);   // ptr1 becomes nullptr

// Copying (shared_ptr only)
auto ptr2 = ptr1;              // Both own the object
```

---

Remember: **Smart pointers make your code safer, cleaner, and less error-prone!**
