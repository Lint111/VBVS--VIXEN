#pragma once

#include <type_traits>
#include <variant>
#include <vector>
#include <array>
#include <memory>
#include <functional>

namespace Vixen::RenderGraph {

// ============================================================================
// TYPE PATTERN SYSTEM - Extract base type and modifiers from complex types
// ============================================================================

/**
 * @brief Type modifiers that can be applied to a base type
 *
 * These are bitflags so we can combine them (e.g., const pointer to vector)
 */
enum class TypeModifier : uint32_t {
    None           = 0,
    Pointer        = 1 << 0,  // T*
    Reference      = 1 << 1,  // T&
    Const          = 1 << 2,  // const T
    Vector         = 1 << 3,  // std::vector<T>
    Array          = 1 << 4,  // std::array<T, N> or T[N]
    SharedPtr      = 1 << 5,  // std::shared_ptr<T>
    UniquePtr      = 1 << 6,  // std::unique_ptr<T>
    ReferenceWrapper = 1 << 7, // std::reference_wrapper<T>
};

inline TypeModifier operator|(TypeModifier a, TypeModifier b) {
    return static_cast<TypeModifier>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline TypeModifier operator&(TypeModifier a, TypeModifier b) {
    return static_cast<TypeModifier>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool operator!(TypeModifier mod) {
    return mod == TypeModifier::None;
}

/**
 * @brief Extract the base type and modifiers from a complex type
 *
 * Examples:
 * - const VkImage* → base=VkImage, modifiers=Const|Pointer
 * - std::vector<VkBuffer>& → base=VkBuffer, modifiers=Reference|Vector
 * - const std::shared_ptr<VkDevice>& → base=VkDevice, modifiers=Const|Reference|SharedPtr
 */
template<typename T>
struct TypePattern {
    // Start with the full type
    using FullType = T;

    // Strip reference first
    static constexpr bool isReference = std::is_reference_v<T>;
    using NoRef = std::remove_reference_t<T>;

    // Check and strip const
    static constexpr bool isConst = std::is_const_v<NoRef>;
    using NoConstRef = std::remove_const_t<NoRef>;

    // Check for pointer and strip
    static constexpr bool isPointer = std::is_pointer_v<NoConstRef>;
    using NoPtr = std::conditional_t<isPointer,
                                     std::remove_pointer_t<NoConstRef>,
                                     NoConstRef>;

    // Check for const on pointee
    static constexpr bool isConstPointee = isPointer && std::is_const_v<NoPtr>;
    using NoPtrConst = std::conditional_t<isPointer,
                                          std::remove_const_t<NoPtr>,
                                          NoPtr>;

    // Check for containers and smart pointers
    template<typename U> struct IsVector : std::false_type {};
    template<typename U> struct IsVector<std::vector<U>> : std::true_type { using element = U; };

    template<typename U> struct IsArray : std::false_type {};
    template<typename U, size_t N> struct IsArray<std::array<U, N>> : std::true_type {
        using element = U;
        static constexpr size_t size = N;
    };
    template<typename U, size_t N> struct IsArray<U[N]> : std::true_type {
        using element = U;
        static constexpr size_t size = N;
    };

    template<typename U> struct IsSharedPtr : std::false_type {};
    template<typename U> struct IsSharedPtr<std::shared_ptr<U>> : std::true_type { using element = U; };

    template<typename U> struct IsUniquePtr : std::false_type {};
    template<typename U, typename D> struct IsUniquePtr<std::unique_ptr<U, D>> : std::true_type { using element = U; };

    template<typename U> struct IsRefWrapper : std::false_type {};
    template<typename U> struct IsRefWrapper<std::reference_wrapper<U>> : std::true_type { using element = U; };

    // Determine what kind of wrapper we have
    static constexpr bool isVector = IsVector<NoPtrConst>::value;
    static constexpr bool isArray = IsArray<NoPtrConst>::value;
    static constexpr bool isSharedPtr = IsSharedPtr<NoPtrConst>::value;
    static constexpr bool isUniquePtr = IsUniquePtr<NoPtrConst>::value;
    static constexpr bool isRefWrapper = IsRefWrapper<NoPtrConst>::value;

    // Extract base type by unwrapping containers
    using BaseType = std::conditional_t<isVector, typename IsVector<NoPtrConst>::element,
                     std::conditional_t<isArray, typename IsArray<NoPtrConst>::element,
                     std::conditional_t<isSharedPtr, typename IsSharedPtr<NoPtrConst>::element,
                     std::conditional_t<isUniquePtr, typename IsUniquePtr<NoPtrConst>::element,
                     std::conditional_t<isRefWrapper, typename IsRefWrapper<NoPtrConst>::element,
                     NoPtrConst>>>>>;

    // Array size (0 for non-arrays or dynamic containers)
    static constexpr size_t arraySize = isArray ? IsArray<NoPtrConst>::size : 0;

    // Build modifier flags
    static constexpr TypeModifier modifiers =
        (isReference ? TypeModifier::Reference : TypeModifier::None) |
        (isConst ? TypeModifier::Const : TypeModifier::None) |
        (isPointer ? TypeModifier::Pointer : TypeModifier::None) |
        (isVector ? TypeModifier::Vector : TypeModifier::None) |
        (isArray ? TypeModifier::Array : TypeModifier::None) |
        (isSharedPtr ? TypeModifier::SharedPtr : TypeModifier::None) |
        (isUniquePtr ? TypeModifier::UniquePtr : TypeModifier::None) |
        (isRefWrapper ? TypeModifier::ReferenceWrapper : TypeModifier::None);

    // Helper to check if this is a container type
    static constexpr bool isContainer = isVector || isArray;

    // Helper to check if this is a smart pointer
    static constexpr bool isSmartPointer = isSharedPtr || isUniquePtr;
};

// ============================================================================
// TYPE ERASURE FOR RUNTIME STORAGE
// ============================================================================

/**
 * @brief Type-erased storage that can hold values, pointers, or references
 *
 * This is what actually gets stored in the variant, avoiding the need to
 * register every possible pointer/reference variation.
 */
class TypeErasedStorage {
public:
    enum class StorageMode {
        Value,      // Owns the value
        Pointer,    // Non-owning pointer
        Reference,  // Non-owning reference (stored as pointer internally)
        SharedPtr,  // Shared ownership
        UniquePtr   // Unique ownership
    };

    TypeErasedStorage() = default;

    // Store a value (copy)
    template<typename T>
    static TypeErasedStorage StoreValue(const T& value) {
        TypeErasedStorage storage;
        storage.mode = StorageMode::Value;
        storage.data = std::make_shared<TypedHolder<T>>(value);
        return storage;
    }

    // Store a pointer (non-owning)
    template<typename T>
    static TypeErasedStorage StorePointer(T* ptr) {
        TypeErasedStorage storage;
        storage.mode = StorageMode::Pointer;
        if (ptr) {
            storage.data = std::make_shared<TypedHolder<T*>>(ptr);
        }
        return storage;
    }

    // Store a reference (non-owning, stored as pointer)
    template<typename T>
    static TypeErasedStorage StoreReference(T& ref) {
        TypeErasedStorage storage;
        storage.mode = StorageMode::Reference;
        storage.data = std::make_shared<TypedHolder<T*>>(std::addressof(ref));
        return storage;
    }

    // Store a shared_ptr
    template<typename T>
    static TypeErasedStorage StoreShared(std::shared_ptr<T> ptr) {
        TypeErasedStorage storage;
        storage.mode = StorageMode::SharedPtr;
        storage.data = std::make_shared<TypedHolder<std::shared_ptr<T>>>(std::move(ptr));
        return storage;
    }

    // Store a unique_ptr (transfers ownership)
    template<typename T>
    static TypeErasedStorage StoreUnique(std::unique_ptr<T> ptr) {
        TypeErasedStorage storage;
        storage.mode = StorageMode::UniquePtr;
        storage.data = std::make_shared<TypedHolder<std::unique_ptr<T>>>(std::move(ptr));
        return storage;
    }

    // Get value (throws if wrong type or mode)
    template<typename T>
    T GetValue() const {
        if (mode != StorageMode::Value) {
            throw std::runtime_error("Storage does not contain a value");
        }
        auto* holder = dynamic_cast<TypedHolder<T>*>(data.get());
        if (!holder) {
            throw std::runtime_error("Type mismatch");
        }
        return holder->value;
    }

    // Get pointer (works for Pointer, Reference, SharedPtr, UniquePtr modes)
    template<typename T>
    T* GetPointer() const {
        switch (mode) {
            case StorageMode::Pointer:
            case StorageMode::Reference: {
                auto* holder = dynamic_cast<TypedHolder<T*>*>(data.get());
                return holder ? holder->value : nullptr;
            }
            case StorageMode::SharedPtr: {
                auto* holder = dynamic_cast<TypedHolder<std::shared_ptr<T>>*>(data.get());
                return holder ? holder->value.get() : nullptr;
            }
            case StorageMode::UniquePtr: {
                auto* holder = dynamic_cast<TypedHolder<std::unique_ptr<T>>*>(data.get());
                return holder ? holder->value.get() : nullptr;
            }
            case StorageMode::Value: {
                auto* holder = dynamic_cast<TypedHolder<T>*>(data.get());
                return holder ? &holder->value : nullptr;
            }
            default:
                return nullptr;
        }
    }

    // Get reference (throws if null)
    template<typename T>
    T& GetReference() const {
        T* ptr = GetPointer<T>();
        if (!ptr) {
            throw std::runtime_error("Null reference");
        }
        return *ptr;
    }

    StorageMode GetMode() const { return mode; }
    bool IsValid() const { return data != nullptr; }

private:
    // Base holder for type erasure
    struct HolderBase {
        virtual ~HolderBase() = default;
    };

    // Typed holder
    template<typename T>
    struct TypedHolder : HolderBase {
        T value;
        explicit TypedHolder(T v) : value(std::move(v)) {}
    };

    StorageMode mode = StorageMode::Value;
    std::shared_ptr<HolderBase> data;
};

// ============================================================================
// RULE-BASED TYPE REGISTRY
// ============================================================================

/**
 * @brief Registry that stores base types and applies rules to accept variations
 *
 * Instead of registering VkImage, VkImage*, const VkImage*, vector<VkImage>, etc.,
 * we just register VkImage and the system automatically accepts all variations.
 */
class TypeRegistry {
public:
    // Register a base type
    template<typename T>
    void RegisterType() {
        static_assert(!std::is_pointer_v<T>, "Register base types, not pointers");
        static_assert(!std::is_reference_v<T>, "Register base types, not references");
        registeredTypes.insert(typeid(T).hash_code());
    }

    // Check if a type (with all its modifiers) is acceptable
    template<typename T>
    bool IsTypeAccepted() const {
        using Pattern = TypePattern<T>;
        using BaseType = typename Pattern::BaseType;

        // Check if the base type is registered
        return registeredTypes.count(typeid(BaseType).hash_code()) > 0;
    }

    // Create storage for a value
    template<typename T>
    TypeErasedStorage CreateStorage(T&& value) const {
        using Pattern = TypePattern<std::decay_t<T>>;

        if constexpr (Pattern::isPointer) {
            return TypeErasedStorage::StorePointer(value);
        } else if constexpr (Pattern::isReference) {
            return TypeErasedStorage::StoreReference(value);
        } else if constexpr (Pattern::isSharedPtr) {
            return TypeErasedStorage::StoreShared(std::forward<T>(value));
        } else if constexpr (Pattern::isUniquePtr) {
            return TypeErasedStorage::StoreUnique(std::forward<T>(value));
        } else {
            return TypeErasedStorage::StoreValue(value);
        }
    }

private:
    std::unordered_set<size_t> registeredTypes;
};

// ============================================================================
// FLEXIBLE VARIANT WITH RULE-BASED ACCEPTANCE
// ============================================================================

/**
 * @brief A variant that accepts any type matching registered patterns
 *
 * Instead of a fixed std::variant<...> with all possible types, this uses
 * type erasure internally but provides type-safe access through templates.
 */
class FlexibleVariant {
public:
    FlexibleVariant() = default;

    // Set value (accepts any type that matches a registered pattern)
    template<typename T>
    void Set(T&& value, const TypeRegistry& registry) {
        using DecayedType = std::decay_t<T>;

        if (!registry.IsTypeAccepted<DecayedType>()) {
            throw std::runtime_error("Type not accepted by registry");
        }

        storage = registry.CreateStorage(std::forward<T>(value));
        typeInfo = typeid(DecayedType).hash_code();
    }

    // Get value (type-safe access)
    template<typename T>
    T Get() const {
        using Pattern = TypePattern<T>;

        if constexpr (Pattern::isPointer) {
            return storage.GetPointer<std::remove_pointer_t<T>>();
        } else if constexpr (Pattern::isReference) {
            return storage.GetReference<std::remove_reference_t<T>>();
        } else {
            return storage.GetValue<T>();
        }
    }

    // Check if variant holds a specific type
    template<typename T>
    bool HoldsType() const {
        return typeInfo == typeid(T).hash_code();
    }

    bool IsValid() const { return storage.IsValid(); }

private:
    TypeErasedStorage storage;
    size_t typeInfo = 0;
};

} // namespace Vixen::RenderGraph