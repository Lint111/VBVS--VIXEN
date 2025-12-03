#pragma once

#include "TypeWrappers.h"
#include "TypePattern.h"
#include "TypeValidation.h"

namespace Vixen::RenderGraph {

// ============================================================================
// AUTOMATIC TYPE DECOMPOSITION - Native C++ → Wrapper Mapping
// ============================================================================

/**
 * @brief Automatically decompose native C++ types to wrapper equivalents
 *
 * Users write natural C++:
 *   std::vector<ResourceSlot<CameraData&>>
 *   std::vector<ResourceSlot<VkImage*>>
 *   std::vector<ResourceSlot<const std::vector<VkBuffer>&>>
 *
 * System automatically maps to:
 *   std::vector<ResourceSlot<RefW<CameraData>>>
 *   std::vector<ResourceSlot<PtrW<VkImage>>>
 *   std::vector<ResourceSlot<ConstW<RefW<VectorW<VkBuffer>>>>>
 *
 * The wrapper types are implementation details - users never see them!
 */

// ============================================================================
// TYPE NORMALIZATION - C++ Type → Wrapper Type
// ============================================================================

/**
 * @brief Map native C++ type to wrapper equivalent
 *
 * Examples:
 * - T                          → T (base type, no wrapper)
 * - T&                         → RefW<T>
 * - T*                         → PtrW<T>
 * - const T&                   → ConstW<RefW<T>>
 * - const T*                   → PtrW<ConstW<T>>
 * - std::vector<T>             → VectorW<T>
 * - std::vector<T>&            → RefW<VectorW<T>>
 * - std::vector<T*>            → VectorW<PtrW<T>>
 * - const std::vector<T>&      → ConstW<RefW<VectorW<T>>>
 */
template<typename T>
struct NormalizeToWrapper {
    // Start by stripping reference
    static constexpr bool isReference = std::is_reference_v<T>;
    using NoRef = std::remove_reference_t<T>;

    // Check for const
    static constexpr bool isConst = std::is_const_v<NoRef>;
    using NoConstRef = std::remove_const_t<NoRef>;

    // Check for pointer
    static constexpr bool isPointer = std::is_pointer_v<NoConstRef>;
    using NoPtr = std::conditional_t<isPointer,
                                     std::remove_pointer_t<NoConstRef>,
                                     NoConstRef>;

    // Check for const on pointee
    static constexpr bool isConstPointee = isPointer && std::is_const_v<NoPtr>;
    using NoPtrConst = std::conditional_t<isPointer,
                                          std::remove_const_t<NoPtr>,
                                          NoPtr>;

    // Check for containers
    template<typename U> struct IsVector : std::false_type {};
    template<typename U, typename A> struct IsVector<std::vector<U, A>> : std::true_type {
        using element = U;
    };

    template<typename U> struct IsArray : std::false_type {};
    template<typename U, size_t N> struct IsArray<std::array<U, N>> : std::true_type {
        using element = U;
        static constexpr size_t size = N;
    };

    template<typename U> struct IsPair : std::false_type {};
    template<typename T1, typename T2> struct IsPair<std::pair<T1, T2>> : std::true_type {
        using first = T1;
        using second = T2;
    };

    template<typename U> struct IsTuple : std::false_type {};
    template<typename... Ts> struct IsTuple<std::tuple<Ts...>> : std::true_type {
        using types = std::tuple<Ts...>;
    };

    template<typename U> struct IsVariant : std::false_type {};
    template<typename... Ts> struct IsVariant<std::variant<Ts...>> : std::true_type {
        using types = std::variant<Ts...>;
    };

    template<typename U> struct IsOptional : std::false_type {};
    template<typename U> struct IsOptional<std::optional<U>> : std::true_type {
        using element = U;
    };

    template<typename U> struct IsSharedPtr : std::false_type {};
    template<typename U> struct IsSharedPtr<std::shared_ptr<U>> : std::true_type {
        using element = U;
    };

    template<typename U> struct IsUniquePtr : std::false_type {};
    template<typename U, typename D> struct IsUniquePtr<std::unique_ptr<U, D>> : std::true_type {
        using element = U;
    };

    // Detect container types
    static constexpr bool isVector = IsVector<NoPtrConst>::value;
    static constexpr bool isArray = IsArray<NoPtrConst>::value;
    static constexpr bool isPair = IsPair<NoPtrConst>::value;
    static constexpr bool isTuple = IsTuple<NoPtrConst>::value;
    static constexpr bool isVariant = IsVariant<NoPtrConst>::value;
    static constexpr bool isOptional = IsOptional<NoPtrConst>::value;
    static constexpr bool isSharedPtr = IsSharedPtr<NoPtrConst>::value;
    static constexpr bool isUniquePtr = IsUniquePtr<NoPtrConst>::value;

    // Recursively normalize
    using InnerType =
        std::conditional_t<isVector,
            VectorW<typename NormalizeToWrapper<typename IsVector<NoPtrConst>::element>::type>,
        std::conditional_t<isArray,
            ArrayW<typename NormalizeToWrapper<typename IsArray<NoPtrConst>::element>::type,
                   IsArray<NoPtrConst>::size>,
        std::conditional_t<isPair,
            PairW<typename NormalizeToWrapper<typename IsPair<NoPtrConst>::first>::type,
                  typename NormalizeToWrapper<typename IsPair<NoPtrConst>::second>::type>,
        std::conditional_t<isTuple,
            typename NormalizeTuple<typename IsTuple<NoPtrConst>::types>::type,
        std::conditional_t<isVariant,
            typename NormalizeVariant<typename IsVariant<NoPtrConst>::types>::type,
        std::conditional_t<isOptional,
            OptionalW<typename NormalizeToWrapper<typename IsOptional<NoPtrConst>::element>::type>,
        std::conditional_t<isSharedPtr,
            SharedW<typename NormalizeToWrapper<typename IsSharedPtr<NoPtrConst>::element>::type>,
        std::conditional_t<isUniquePtr,
            UniqueW<typename NormalizeToWrapper<typename IsUniquePtr<NoPtrConst>::element>::type>,
        NoPtrConst  // Base case: no container
        >>>>>>>>;

    // Apply pointer wrapper if needed
    using WithPointer = std::conditional_t<isPointer,
        PtrW<std::conditional_t<isConstPointee, ConstW<InnerType>, InnerType>>,
        InnerType>;

    // Apply const wrapper if needed (but not if we already have pointer)
    using WithConst = std::conditional_t<isConst && !isPointer,
        ConstW<WithPointer>,
        WithPointer>;

    // Apply reference wrapper if needed
    using type = std::conditional_t<isReference,
        RefW<WithConst>,
        WithConst>;
};

// Helper for normalizing tuple types
template<typename Tuple>
struct NormalizeTuple;

template<typename... Ts>
struct NormalizeTuple<std::tuple<Ts...>> {
    using type = TupleW<typename NormalizeToWrapper<Ts>::type...>;
};

// Helper for normalizing variant types
template<typename Variant>
struct NormalizeVariant;

template<typename... Ts>
struct NormalizeVariant<std::variant<Ts...>> {
    using type = VariantW<typename NormalizeToWrapper<Ts>::type...>;
};

// Convenience alias
template<typename T>
using NormalizeToWrapper_t = typename NormalizeToWrapper<T>::type;

// ============================================================================
// REVERSE MAPPING - Wrapper Type → Native C++ Type
// ============================================================================

/**
 * @brief Convert wrapper type back to native C++ type
 *
 * Used for type deduction in slot declarations
 */
template<typename W>
struct DenormalizeFromWrapper {
    using type = W;  // Base case: not a wrapper
};

template<typename T>
struct DenormalizeFromWrapper<RefW<T>> {
    using type = typename DenormalizeFromWrapper<T>::type&;
};

template<typename T>
struct DenormalizeFromWrapper<PtrW<T>> {
    using type = typename DenormalizeFromWrapper<T>::type*;
};

template<typename T>
struct DenormalizeFromWrapper<ConstW<T>> {
    using type = const typename DenormalizeFromWrapper<T>::type;
};

template<typename T>
struct DenormalizeFromWrapper<VectorW<T>> {
    using type = std::vector<typename DenormalizeFromWrapper<T>::type>;
};

template<typename T, size_t N>
struct DenormalizeFromWrapper<ArrayW<T, N>> {
    using type = std::array<typename DenormalizeFromWrapper<T>::type, N>;
};

template<typename T1, typename T2>
struct DenormalizeFromWrapper<PairW<T1, T2>> {
    using type = std::pair<
        typename DenormalizeFromWrapper<T1>::type,
        typename DenormalizeFromWrapper<T2>::type
    >;
};

template<typename... Ts>
struct DenormalizeFromWrapper<TupleW<Ts...>> {
    using type = std::tuple<typename DenormalizeFromWrapper<Ts>::type...>;
};

template<typename... Ts>
struct DenormalizeFromWrapper<VariantW<Ts...>> {
    using type = std::variant<typename DenormalizeFromWrapper<Ts>::type...>;
};

template<typename T>
struct DenormalizeFromWrapper<OptionalW<T>> {
    using type = std::optional<typename DenormalizeFromWrapper<T>::type>;
};

template<typename T>
using DenormalizeFromWrapper_t = typename DenormalizeFromWrapper<T>::type;

// ============================================================================
// AUTO-WRAPPING CONVERSIONS
// ============================================================================

/**
 * @brief Automatically wrap native C++ value to wrapper type
 */
template<typename NativeType>
auto WrapValue(NativeType&& value) {
    using WrapperType = NormalizeToWrapper_t<NativeType>;

    if constexpr (std::is_same_v<WrapperType, std::decay_t<NativeType>>) {
        // No wrapping needed - base type
        return std::forward<NativeType>(value);
    } else {
        // Construct wrapper
        return WrapperType(std::forward<NativeType>(value));
    }
}

/**
 * @brief Automatically unwrap wrapper type to native C++ value
 */
template<typename WrapperType>
auto UnwrapValue(const WrapperType& wrapper) {
    if constexpr (IsWrapper_v<WrapperType>) {
        return wrapper.get();
    } else {
        return wrapper;
    }
}

// ============================================================================
// TRANSPARENT RESOURCE SLOT - Natural C++ syntax
// ============================================================================

/**
 * @brief ResourceSlot that accepts native C++ types and auto-converts
 *
 * Usage:
 *   std::vector<ResourceSlot<CameraData&>> GetOutputs() {  // User writes natural C++
 *       return {{"camera", ResourceLifetime::Transient}};
 *   }
 *
 * Internally stored as:
 *   ResourceSlot<RefW<CameraData>>  // Implementation detail
 */
template<typename UserType>
class ResourceSlot {
public:
    using UserT = UserType;
    using InternalT = NormalizeToWrapper_t<UserType>;
    using Type = InternalT;  // For template argument deduction

    std::string name;
    ResourceLifetime lifetime;
    bool optional = false;
    uint32_t index = 0;
    SlotRole role = SlotRole::None;

    // Static metadata
    static constexpr ResourceType resourceType = ResourceType::Buffer;  // TODO: deduce from type

    ResourceSlot() = default;

    ResourceSlot(const std::string& n,
                 ResourceLifetime lt = ResourceLifetime::Transient,
                 bool opt = false,
                 SlotRole r = SlotRole::None)
        : name(n), lifetime(lt), optional(opt), role(r) {}

    // Automatic conversion from native C++ type
    template<typename T>
    void SetValue(ResourceV2& resource, T&& value) const {
        // Wrap if needed
        auto wrapped = WrapValue(std::forward<T>(value));
        resource.SetHandle(wrapped);
    }

    // Automatic unwrapping to native C++ type
    UserType GetValue(const ResourceV2& resource) const {
        auto wrapped = resource.GetHandle<InternalT>();
        return UnwrapValue(wrapped);
    }
};

// ============================================================================
// COMPILE-TIME VERIFICATION
// ============================================================================

// Test normalization produces expected results
static_assert(std::is_same_v<
    NormalizeToWrapper_t<CameraData&>,
    RefW<CameraData>
>);

static_assert(std::is_same_v<
    NormalizeToWrapper_t<VkImage*>,
    PtrW<VkImage>
>);

static_assert(std::is_same_v<
    NormalizeToWrapper_t<const VkImage*>,
    PtrW<ConstW<VkImage>>
>);

static_assert(std::is_same_v<
    NormalizeToWrapper_t<const CameraData&>,
    ConstW<RefW<CameraData>>
>);

static_assert(std::is_same_v<
    NormalizeToWrapper_t<std::vector<VkImage>>,
    VectorW<VkImage>
>);

static_assert(std::is_same_v<
    NormalizeToWrapper_t<std::vector<VkImage>&>,
    RefW<VectorW<VkImage>>
>);

static_assert(std::is_same_v<
    NormalizeToWrapper_t<const std::vector<VkImage>&>,
    ConstW<RefW<VectorW<VkImage>>>
>);

static_assert(std::is_same_v<
    NormalizeToWrapper_t<std::vector<VkImage*>>,
    VectorW<PtrW<VkImage>>
>);

// Test denormalization reverses normalization
static_assert(std::is_same_v<
    DenormalizeFromWrapper_t<RefW<CameraData>>,
    CameraData&
>);

static_assert(std::is_same_v<
    DenormalizeFromWrapper_t<PtrW<VkImage>>,
    VkImage*
>);

// ============================================================================
// USAGE EXAMPLES
// ============================================================================

/**
 * Example: Natural C++ syntax
 *
 * struct CameraNode : public NodeInstance {
 *     CameraData cameraData;  // Stack-allocated
 *
 *     // User writes natural C++ - references!
 *     std::vector<ResourceSlot<CameraData&>> GetOutputs() override {
 *         return {{"camera", ResourceLifetime::Transient}};
 *     }
 *
 *     void Execute() override {
 *         UpdateCameraMatrices(cameraData);
 *
 *         // Just pass the reference - system auto-wraps!
 *         outputSlots[0].SetValue(outputData[0], cameraData);
 *         // Internally: ResourceV2.SetHandle(RefW<CameraData>(cameraData))
 *     }
 * };
 *
 * struct RenderNode : public NodeInstance {
 *     // User writes natural C++ - const reference!
 *     std::vector<ResourceSlot<const CameraData&>> GetInputs() override {
 *         return {{"camera", ResourceLifetime::Transient}};
 *     }
 *
 *     void Execute() override {
 *         // System auto-unwraps to const reference
 *         const CameraData& camera = inputSlots[0].GetValue(inputData[0]);
 *         // Internally: auto wrapped = ResourceV2.GetHandle<ConstW<RefW<CameraData>>>();
 *         //             return wrapped.get();  // Returns const CameraData&
 *
 *         RenderScene(camera);
 *     }
 * };
 *
 * Example: Pointers
 *
 * struct TextureNode : public NodeInstance {
 *     VkImage texture = VK_NULL_HANDLE;
 *
 *     // User writes: pointer type
 *     std::vector<ResourceSlot<VkImage*>> GetOutputs() override {
 *         return {{"texture", ResourceLifetime::Persistent}};
 *     }
 *
 *     void Execute() override {
 *         texture = CreateTexture(...);
 *
 *         // Just pass the pointer - system auto-wraps!
 *         outputSlots[0].SetValue(outputData[0], &texture);
 *         // Internally: ResourceV2.SetHandle(PtrW<VkImage>(&texture))
 *     }
 * };
 *
 * Example: Const vector reference
 *
 * struct SwapChainNode : public NodeInstance {
 *     std::vector<VkImage> swapchainImages;
 *
 *     // User writes: const reference to vector
 *     std::vector<ResourceSlot<const std::vector<VkImage>&>> GetOutputs() override {
 *         return {{"images", ResourceLifetime::Persistent}};
 *     }
 *
 *     void Execute() override {
 *         swapchainImages = AcquireSwapchainImages();
 *
 *         // Pass const reference - system auto-wraps!
 *         outputSlots[0].SetValue(outputData[0], swapchainImages);
 *         // Internally: ConstW<RefW<VectorW<VkImage>>>(swapchainImages)
 *     }
 * };
 */

} // namespace Vixen::RenderGraph