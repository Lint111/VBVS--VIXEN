#pragma once

#include <type_traits>
#include <vector>
#include <array>
#include <memory>
#include <functional>
#include <variant>
#include <typeindex>
#include <unordered_set>

namespace Vixen::RenderGraph {

// ============================================================================
// COMPOSABLE TYPE WRAPPERS - Build complex types through composition
// ============================================================================

/**
 * @brief Base wrapper template for composable type modifiers
 *
 * Usage examples:
 * - RefW<VkImage>                    // VkImage&
 * - ConstW<RefW<VkImage>>            // const VkImage&
 * - PtrW<ConstW<VkImage>>            // const VkImage*
 * - VectorW<PtrW<VkImage>>           // std::vector<VkImage*>
 * - RefW<VectorW<VkImage>>           // std::vector<VkImage>&
 * - SharedW<VkImage>                 // std::shared_ptr<VkImage>
 * - ConstW<RefW<VectorW<VkImage>>>   // const std::vector<VkImage>&
 */

// Forward declarations
template<typename T> struct RefW;
template<typename T> struct PtrW;
template<typename T> struct ConstW;
template<typename T> struct VectorW;
template<typename T, size_t N> struct ArrayW;
template<typename T> struct SharedW;
template<typename T> struct UniqueW;
template<typename T> struct RefWrapW;

// ============================================================================
// TYPE TRAITS FOR WRAPPER DETECTION
// ============================================================================

template<typename T>
struct IsWrapper : std::false_type {};

template<typename T> struct IsWrapper<RefW<T>> : std::true_type {};
template<typename T> struct IsWrapper<PtrW<T>> : std::true_type {};
template<typename T> struct IsWrapper<ConstW<T>> : std::true_type {};
template<typename T> struct IsWrapper<VectorW<T>> : std::true_type {};
template<typename T, size_t N> struct IsWrapper<ArrayW<T, N>> : std::true_type {};
template<typename T> struct IsWrapper<SharedW<T>> : std::true_type {};
template<typename T> struct IsWrapper<UniqueW<T>> : std::true_type {};
template<typename T> struct IsWrapper<RefWrapW<T>> : std::true_type {};

template<typename T>
inline constexpr bool IsWrapper_v = IsWrapper<T>::value;

// ============================================================================
// WRAPPER UNWRAPPING - Get the actual C++ type
// ============================================================================

/**
 * @brief Convert wrapper type to actual C++ type
 *
 * Examples:
 * - UnwrapType<RefW<VkImage>>                  → VkImage&
 * - UnwrapType<ConstW<RefW<VkImage>>>          → const VkImage&
 * - UnwrapType<VectorW<PtrW<VkImage>>>         → std::vector<VkImage*>
 */
template<typename T>
struct UnwrapType {
    using type = T;  // Base case: non-wrapped types return themselves
};

// ============================================================================
// REFERENCE WRAPPER
// ============================================================================

template<typename T>
struct RefW {
    using wrapped_type = T;
    using unwrapped_type = typename UnwrapType<T>::type;
    using actual_type = unwrapped_type&;

    static constexpr bool is_wrapper = true;
    static constexpr bool is_ref = true;

    // Storage: store reference as pointer internally
    unwrapped_type* ptr = nullptr;

    RefW() = default;
    explicit RefW(unwrapped_type& ref) : ptr(&ref) {}

    // Conversion operators
    operator unwrapped_type&() const {
        if (!ptr) throw std::runtime_error("Null reference");
        return *ptr;
    }
    unwrapped_type& get() const {
        if (!ptr) throw std::runtime_error("Null reference");
        return *ptr;
    }

    // Assignment
    RefW& operator=(unwrapped_type& ref) {
        ptr = &ref;
        return *this;
    }
};

template<typename T>
struct UnwrapType<RefW<T>> {
    using type = typename UnwrapType<T>::type&;
};

// ============================================================================
// POINTER WRAPPER
// ============================================================================

template<typename T>
struct PtrW {
    using wrapped_type = T;
    using unwrapped_type = typename UnwrapType<T>::type;
    using actual_type = unwrapped_type*;

    static constexpr bool is_wrapper = true;
    static constexpr bool is_ptr = true;

    // Storage
    unwrapped_type* ptr = nullptr;

    PtrW() = default;
    explicit PtrW(unwrapped_type* p) : ptr(p) {}

    // Conversion operators
    operator unwrapped_type*() const { return ptr; }
    unwrapped_type* get() const { return ptr; }
    unwrapped_type& operator*() const {
        if (!ptr) throw std::runtime_error("Null pointer dereference");
        return *ptr;
    }
    unwrapped_type* operator->() const { return ptr; }

    // Assignment
    PtrW& operator=(unwrapped_type* p) {
        ptr = p;
        return *this;
    }
};

template<typename T>
struct UnwrapType<PtrW<T>> {
    using type = typename UnwrapType<T>::type*;
};

// ============================================================================
// CONST WRAPPER
// ============================================================================

template<typename T>
struct ConstW {
    using wrapped_type = T;
    using unwrapped_type = typename UnwrapType<T>::type;
    using actual_type = const unwrapped_type;

    static constexpr bool is_wrapper = true;
    static constexpr bool is_const = true;

    // Storage (delegates to wrapped type)
    T wrapped;

    ConstW() = default;
    explicit ConstW(const T& w) : wrapped(w) {}

    // Const access only
    const unwrapped_type& get() const {
        if constexpr (IsWrapper_v<T>) {
            return wrapped.get();
        } else {
            return wrapped;
        }
    }

    operator const unwrapped_type&() const { return get(); }
};

template<typename T>
struct UnwrapType<ConstW<T>> {
    using type = const typename UnwrapType<T>::type;
};

// ============================================================================
// VECTOR WRAPPER
// ============================================================================

template<typename T>
struct VectorW {
    using wrapped_type = T;
    using element_type = typename UnwrapType<T>::type;
    using actual_type = std::vector<element_type>;

    static constexpr bool is_wrapper = true;
    static constexpr bool is_vector = true;

    // Storage
    actual_type data;

    VectorW() = default;
    explicit VectorW(const actual_type& v) : data(v) {}
    explicit VectorW(actual_type&& v) : data(std::move(v)) {}

    // Vector interface
    operator actual_type&() { return data; }
    operator const actual_type&() const { return data; }
    actual_type& get() { return data; }
    const actual_type& get() const { return data; }

    // Common vector operations
    size_t size() const { return data.size(); }
    bool empty() const { return data.empty(); }
    void push_back(const element_type& val) { data.push_back(val); }
    element_type& operator[](size_t i) { return data[i]; }
    const element_type& operator[](size_t i) const { return data[i]; }
};

template<typename T>
struct UnwrapType<VectorW<T>> {
    using type = std::vector<typename UnwrapType<T>::type>;
};

// ============================================================================
// ARRAY WRAPPER
// ============================================================================

template<typename T, size_t N>
struct ArrayW {
    using wrapped_type = T;
    using element_type = typename UnwrapType<T>::type;
    using actual_type = std::array<element_type, N>;

    static constexpr bool is_wrapper = true;
    static constexpr bool is_array = true;
    static constexpr size_t array_size = N;

    // Storage
    actual_type data;

    ArrayW() = default;
    explicit ArrayW(const actual_type& a) : data(a) {}

    // Array interface
    operator actual_type&() { return data; }
    operator const actual_type&() const { return data; }
    actual_type& get() { return data; }
    const actual_type& get() const { return data; }

    // Common array operations
    constexpr size_t size() const { return N; }
    element_type& operator[](size_t i) { return data[i]; }
    const element_type& operator[](size_t i) const { return data[i]; }
};

template<typename T, size_t N>
struct UnwrapType<ArrayW<T, N>> {
    using type = std::array<typename UnwrapType<T>::type, N>;
};

// ============================================================================
// SHARED POINTER WRAPPER
// ============================================================================

template<typename T>
struct SharedW {
    using wrapped_type = T;
    using element_type = typename UnwrapType<T>::type;
    using actual_type = std::shared_ptr<element_type>;

    static constexpr bool is_wrapper = true;
    static constexpr bool is_shared = true;

    // Storage
    actual_type ptr;

    SharedW() = default;
    explicit SharedW(const actual_type& p) : ptr(p) {}
    explicit SharedW(actual_type&& p) : ptr(std::move(p)) {}

    // Shared pointer interface
    operator actual_type&() { return ptr; }
    operator const actual_type&() const { return ptr; }
    actual_type& get() { return ptr; }
    const actual_type& get() const { return ptr; }

    element_type* operator->() const { return ptr.get(); }
    element_type& operator*() const { return *ptr; }
    explicit operator bool() const { return ptr != nullptr; }
};

template<typename T>
struct UnwrapType<SharedW<T>> {
    using type = std::shared_ptr<typename UnwrapType<T>::type>;
};

// ============================================================================
// UNIQUE POINTER WRAPPER
// ============================================================================

template<typename T>
struct UniqueW {
    using wrapped_type = T;
    using element_type = typename UnwrapType<T>::type;
    using actual_type = std::unique_ptr<element_type>;

    static constexpr bool is_wrapper = true;
    static constexpr bool is_unique = true;

    // Storage
    actual_type ptr;

    UniqueW() = default;
    explicit UniqueW(actual_type&& p) : ptr(std::move(p)) {}

    // Move-only semantics
    UniqueW(const UniqueW&) = delete;
    UniqueW& operator=(const UniqueW&) = delete;
    UniqueW(UniqueW&&) = default;
    UniqueW& operator=(UniqueW&&) = default;

    // Unique pointer interface
    actual_type& get() { return ptr; }
    const actual_type& get() const { return ptr; }

    element_type* operator->() const { return ptr.get(); }
    element_type& operator*() const { return *ptr; }
    explicit operator bool() const { return ptr != nullptr; }

    element_type* release() { return ptr.release(); }
    void reset(element_type* p = nullptr) { ptr.reset(p); }
};

template<typename T>
struct UnwrapType<UniqueW<T>> {
    using type = std::unique_ptr<typename UnwrapType<T>::type>;
};

// ============================================================================
// REFERENCE WRAPPER (std::reference_wrapper equivalent)
// ============================================================================

template<typename T>
struct RefWrapW {
    using wrapped_type = T;
    using element_type = typename UnwrapType<T>::type;
    using actual_type = std::reference_wrapper<element_type>;

    static constexpr bool is_wrapper = true;
    static constexpr bool is_ref_wrap = true;

    // Storage
    actual_type ref;

    explicit RefWrapW(element_type& r) : ref(r) {}

    // Reference wrapper interface
    operator element_type&() const { return ref.get(); }
    element_type& get() const { return ref.get(); }
};

template<typename T>
struct UnwrapType<RefWrapW<T>> {
    using type = std::reference_wrapper<typename UnwrapType<T>::type>;
};

// ============================================================================
// TYPE REGISTRY WITH WRAPPER SUPPORT
// ============================================================================

/**
 * @brief Registry that accepts base types and all wrapped variations
 *
 * Register VkImage once, automatically accept:
 * - VkImage
 * - RefW<VkImage>, PtrW<VkImage>
 * - ConstW<VkImage>, ConstW<RefW<VkImage>>
 * - VectorW<VkImage>, VectorW<PtrW<VkImage>>
 * - Any composition of wrappers
 */
class WrapperTypeRegistry {
public:
    static WrapperTypeRegistry& Instance() {
        static WrapperTypeRegistry instance;
        return instance;
    }

    // Register a base type
    template<typename T>
    void RegisterBaseType() {
        static_assert(!IsWrapper_v<T>, "Register base types, not wrapped types");
        registeredTypes.insert(std::type_index(typeid(T)));
    }

    // Check if a wrapped type is acceptable
    template<typename T>
    bool IsTypeAcceptable() const {
        return CheckType<T>();
    }

private:
    WrapperTypeRegistry() = default;

    // Recursive type checking
    template<typename T>
    bool CheckType() const {
        if constexpr (IsWrapper_v<T>) {
            // Wrapped type: recursively check the wrapped type
            return CheckType<typename T::wrapped_type>();
        } else {
            // Base type: check if registered
            return registeredTypes.count(std::type_index(typeid(T))) > 0;
        }
    }

    std::unordered_set<std::type_index> registeredTypes;
};

// ============================================================================
// VARIANT WITH WRAPPER SUPPORT
// ============================================================================

/**
 * @brief Type-erased storage that works with wrapped types
 */
class WrappedVariant {
public:
    WrappedVariant() = default;

    // Set value using wrapped type
    template<typename WrappedType>
    void Set(const typename UnwrapType<WrappedType>::type& value) {
        using ActualType = typename UnwrapType<WrappedType>::type;

        if (!WrapperTypeRegistry::Instance().IsTypeAcceptable<WrappedType>()) {
            throw std::runtime_error("Type not acceptable");
        }

        // Store type-erased value
        data = std::make_shared<TypedHolder<ActualType>>(value);
        typeInfo = std::type_index(typeid(ActualType));
    }

    // Get value using wrapped type
    template<typename WrappedType>
    typename UnwrapType<WrappedType>::type Get() const {
        using ActualType = typename UnwrapType<WrappedType>::type;

        if (typeInfo != std::type_index(typeid(ActualType))) {
            throw std::runtime_error("Type mismatch");
        }

        auto* holder = dynamic_cast<TypedHolder<ActualType>*>(data.get());
        if (!holder) {
            throw std::runtime_error("Cast failed");
        }

        return holder->value;
    }

    bool IsValid() const { return data != nullptr; }

private:
    struct HolderBase {
        virtual ~HolderBase() = default;
    };

    template<typename T>
    struct TypedHolder : HolderBase {
        T value;
        explicit TypedHolder(const T& v) : value(v) {}
    };

    std::shared_ptr<HolderBase> data;
    std::type_index typeInfo = std::type_index(typeid(void));
};

// ============================================================================
// CONVENIENCE ALIASES
// ============================================================================

// Common type patterns as aliases
template<typename T>
using ConstRef = ConstW<RefW<T>>;  // const T&

template<typename T>
using ConstPtr = PtrW<ConstW<T>>;  // const T*

template<typename T>
using RefVector = RefW<VectorW<T>>;  // std::vector<T>&

template<typename T>
using ConstRefVector = ConstW<RefW<VectorW<T>>>;  // const std::vector<T>&

template<typename T>
using VectorOfPtrs = VectorW<PtrW<T>>;  // std::vector<T*>

template<typename T>
using VectorOfRefs = VectorW<RefWrapW<T>>;  // std::vector<std::reference_wrapper<T>>

// ============================================================================
// USAGE EXAMPLES
// ============================================================================

/**
 * Example usage:
 *
 * // Register base type
 * WrapperTypeRegistry::Instance().RegisterBaseType<VkImage>();
 *
 * // Now all these are automatically valid:
 * using ImageRef = RefW<VkImage>;                        // VkImage&
 * using ConstImageRef = ConstW<RefW<VkImage>>;          // const VkImage&
 * using ImagePtr = PtrW<VkImage>;                       // VkImage*
 * using ConstImagePtr = PtrW<ConstW<VkImage>>;          // const VkImage*
 * using ImageVector = VectorW<VkImage>;                 // std::vector<VkImage>
 * using ImagePtrVector = VectorW<PtrW<VkImage>>;        // std::vector<VkImage*>
 * using ImageVectorRef = RefW<VectorW<VkImage>>;        // std::vector<VkImage>&
 *
 * // Use in variant
 * WrappedVariant variant;
 * VkImage img = ...;
 * variant.Set<RefW<VkImage>>(img);
 * VkImage& ref = variant.Get<RefW<VkImage>>();
 */

} // namespace Vixen::RenderGraph