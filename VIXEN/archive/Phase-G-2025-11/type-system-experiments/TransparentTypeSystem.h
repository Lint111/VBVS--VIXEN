#pragma once

#include "AutoTypeDecomposition.h"
#include "ResourceVariantV2Integration.h"

namespace Vixen::RenderGraph {

// ============================================================================
// TRANSPARENT TYPE SYSTEM - Zero user code changes
// ============================================================================

/**
 * @brief Completely transparent type handling - users code naturally
 *
 * USER WRITES (natural C++):
 *   struct MyNode {
 *       CameraData camera;
 *
 *       std::vector<ResourceSlot<CameraData&>> GetOutputs() {
 *           return {{"camera"}};
 *       }
 *
 *       void Execute() {
 *           outputData[0].SetHandle(camera);  // Natural reference
 *       }
 *   };
 *
 * SYSTEM HANDLES (behind the scenes):
 *   - Validates CameraData is registered ✓
 *   - Auto-wraps to RefW<CameraData> ✓
 *   - Stores non-owning reference ✓
 *   - Auto-unwraps on retrieval ✓
 *
 * USER NEVER SEES WRAPPERS - THEY'RE IMPLEMENTATION DETAILS!
 */

// ============================================================================
// ENHANCED RESOURCE WITH AUTOMATIC WRAPPING
// ============================================================================

/**
 * @brief Resource that automatically wraps/unwraps native C++ types
 */
class TransparentResource {
public:
    TransparentResource() = default;

    // ========================================================================
    // TRANSPARENT SETHANDLE - Accepts ANY native C++ type
    // ========================================================================

    /**
     * @brief Set handle from native C++ type (automatic wrapping)
     *
     * Examples:
     *   VkImage img;
     *   res.SetHandle(img);           // Value
     *   res.SetHandle(&img);          // Pointer (auto-wraps to PtrW)
     *   res.SetHandle(camera);        // Stack object (auto-wraps to RefW)
     *
     * std::vector<VkImage> images;
     *   res.SetHandle(images);        // Vector by value (auto-wraps to VectorW)
     */
    template<typename NativeType>
    void SetHandle(NativeType&& value) {
        using DecayedNative = std::decay_t<NativeType>;
        using WrapperType = NormalizeToWrapper_t<NativeType>;

        // Validate base type is registered (not wrapper!)
        using Pattern = TypePattern<DecayedNative>;
        using BaseType = typename Pattern::BaseType;

        if (!CachedTypeRegistry::Instance().IsTypeAcceptable<BaseType>()) {
            throw std::runtime_error("Base type not registered");
        }

        // Handle different patterns
        if constexpr (std::is_lvalue_reference_v<NativeType>) {
            // Lvalue reference T& → RefW<T>
            SetHandleReference(value);
        } else if constexpr (std::is_rvalue_reference_v<NativeType>) {
            // Rvalue reference T&& → move into storage
            SetHandleMove(std::move(value));
        } else if constexpr (std::is_pointer_v<DecayedNative>) {
            // Pointer T* → PtrW<T>
            SetHandlePointer(value);
        } else {
            // Value T → store by value
            SetHandleValue(value);
        }

        isSet_ = true;
    }

    // ========================================================================
    // TRANSPARENT GETHANDLE - Returns native C++ type
    // ========================================================================

    /**
     * @brief Get handle as native C++ type (automatic unwrapping)
     *
     * Examples:
     *   VkImage img = res.GetHandle<VkImage>();        // Value
     *   VkImage* ptr = res.GetHandle<VkImage*>();      // Pointer
     *   CameraData& ref = res.GetHandle<CameraData&>(); // Reference
     */
    template<typename NativeType>
    NativeType GetHandle() const {
        if (!isSet_) {
            if constexpr (std::is_pointer_v<NativeType>) {
                return nullptr;
            } else if constexpr (std::is_reference_v<NativeType>) {
                throw std::runtime_error("Cannot return null reference");
            } else {
                return NativeType{};
            }
        }

        // Determine how to retrieve based on requested type
        if constexpr (std::is_pointer_v<NativeType>) {
            return GetHandleAsPointer<NativeType>();
        } else if constexpr (std::is_reference_v<NativeType>) {
            return GetHandleAsReference<NativeType>();
        } else {
            return GetHandleAsValue<NativeType>();
        }
    }

    bool IsValid() const { return isSet_; }

    // Allow ResourceV2 adapter
    ResourceV2& GetInternalResource() { return impl_; }
    const ResourceV2& GetInternalResource() const { return impl_; }

private:
    ResourceV2 impl_;  // Underlying resource implementation
    bool isSet_ = false;

    // ========================================================================
    // INTERNAL STORAGE STRATEGIES
    // ========================================================================

    // Store lvalue reference (non-owning)
    template<typename T>
    void SetHandleReference(T& value) {
        using BaseType = std::remove_cv_t<std::remove_reference_t<T>>;
        using WrapperType = RefW<BaseType>;

        WrapperType wrapper(value);
        impl_.SetHandle(wrapper);
    }

    // Store rvalue (move into owned storage)
    template<typename T>
    void SetHandleMove(T&& value) {
        using BaseType = std::remove_cv_t<std::remove_reference_t<T>>;
        impl_.SetHandle(std::forward<T>(value));
    }

    // Store pointer (non-owning)
    template<typename T>
    void SetHandlePointer(T ptr) {
        using PointeeType = std::remove_pointer_t<T>;
        using BaseType = std::remove_cv_t<PointeeType>;
        using WrapperType = PtrW<BaseType>;

        WrapperType wrapper(const_cast<BaseType*>(ptr));
        impl_.SetHandle(wrapper);
    }

    // Store value (owned)
    template<typename T>
    void SetHandleValue(const T& value) {
        impl_.SetHandle(value);
    }

    // Retrieve as pointer
    template<typename PtrType>
    PtrType GetHandleAsPointer() const {
        using PointeeType = std::remove_pointer_t<PtrType>;
        using BaseType = std::remove_cv_t<PointeeType>;
        using WrapperType = PtrW<BaseType>;

        auto wrapper = impl_.GetHandle<WrapperType>();
        return wrapper.get();
    }

    // Retrieve as reference
    template<typename RefType>
    RefType GetHandleAsReference() const {
        using BaseType = std::remove_cv_t<std::remove_reference_t<RefType>>;
        using WrapperType = RefW<BaseType>;

        auto wrapper = impl_.GetHandle<WrapperType>();
        return wrapper.get();
    }

    // Retrieve as value
    template<typename ValueType>
    ValueType GetHandleAsValue() const {
        return impl_.GetHandle<ValueType>();
    }
};

// ============================================================================
// TRANSPARENT RESOURCE SLOT - Natural C++ types in declarations
// ============================================================================

/**
 * @brief ResourceSlot with automatic type normalization
 *
 * Users write natural C++ types, system handles conversion
 */
template<typename UserType>
class TransparentResourceSlot {
public:
    using NativeType = UserType;
    using WrapperType = NormalizeToWrapper_t<UserType>;
    using Type = WrapperType;  // For internal type deduction

    std::string name;
    ResourceLifetime lifetime = ResourceLifetime::Transient;
    bool optional = false;
    uint32_t index = 0;
    SlotRole role = SlotRole::None;

    // Static metadata
    static constexpr ResourceType resourceType = ResourceType::Buffer;

    TransparentResourceSlot() = default;

    TransparentResourceSlot(const std::string& n,
                           ResourceLifetime lt = ResourceLifetime::Transient,
                           bool opt = false,
                           SlotRole r = SlotRole::None)
        : name(n), lifetime(lt), optional(opt), role(r) {}

    // No special methods needed - users interact directly with TransparentResource!
};

// ============================================================================
// DROP-IN REPLACEMENT ALIASES
// ============================================================================

// Make TransparentResource the default
using Resource = TransparentResource;

// Make TransparentResourceSlot the default
template<typename T>
using ResourceSlot = TransparentResourceSlot<T>;

// ============================================================================
// COMPILE-TIME VALIDATION
// ============================================================================

// Ensure natural types compile correctly
namespace CompileTimeTests {
    // These should all compile and validate correctly
    using SlotByValue = ResourceSlot<VkImage>;
    using SlotByPtr = ResourceSlot<VkImage*>;
    using SlotByRef = ResourceSlot<CameraData&>;
    using SlotByConstRef = ResourceSlot<const CameraData&>;
    using SlotVector = ResourceSlot<std::vector<VkImage>>;
    using SlotVectorRef = ResourceSlot<std::vector<VkImage>&>;
    using SlotConstVectorRef = ResourceSlot<const std::vector<VkImage>&>;

    static_assert(std::is_same_v<
        typename SlotByRef::WrapperType,
        RefW<CameraData>
    >, "Reference should map to RefW");

    static_assert(std::is_same_v<
        typename SlotByPtr::WrapperType,
        PtrW<VkImage>
    >, "Pointer should map to PtrW");

    static_assert(std::is_same_v<
        typename SlotConstVectorRef::WrapperType,
        ConstW<RefW<VectorW<VkImage>>>
    >, "Const vector ref should map to ConstW<RefW<VectorW>>");
}

// ============================================================================
// REAL-WORLD USAGE EXAMPLES (UNCHANGED USER CODE)
// ============================================================================

/**
 * Example 1: Stack object output (zero-copy reference)
 *
 * struct CameraNode : public NodeInstance {
 *     CameraData cameraData;  // Stack-allocated
 *
 *     // USER WRITES NATURAL C++:
 *     std::vector<ResourceSlot<CameraData&>> GetOutputs() override {
 *         return {{"camera", ResourceLifetime::Transient}};
 *     }
 *
 *     void Execute() override {
 *         UpdateCameraMatrices(cameraData);
 *
 *         // USER WRITES NATURAL C++:
 *         outputData[0].SetHandle(cameraData);  // Just pass the object!
 *         // System automatically wraps to RefW<CameraData> behind the scenes
 *     }
 * };
 *
 * struct RenderNode : public NodeInstance {
 *     // USER WRITES NATURAL C++:
 *     std::vector<ResourceSlot<const CameraData&>> GetInputs() override {
 *         return {{"camera", ResourceLifetime::Transient}};
 *     }
 *
 *     void Execute() override {
 *         // USER WRITES NATURAL C++:
 *         const CameraData& camera = inputData[0].GetHandle<const CameraData&>();
 *         // System automatically unwraps from ConstW<RefW<CameraData>>
 *
 *         RenderScene(camera);
 *     }
 * };
 *
 * Example 2: Pointer to persistent resource
 *
 * struct TextureManager : public NodeInstance {
 *     VkImage texture = VK_NULL_HANDLE;
 *
 *     // USER WRITES NATURAL C++:
 *     std::vector<ResourceSlot<VkImage*>> GetOutputs() override {
 *         return {{"texture", ResourceLifetime::Persistent}};
 *     }
 *
 *     void Execute() override {
 *         if (!texture) {
 *             texture = LoadTexture(...);
 *         }
 *
 *         // USER WRITES NATURAL C++:
 *         outputData[0].SetHandle(&texture);  // Just pass the pointer!
 *         // System automatically wraps to PtrW<VkImage>
 *     }
 * };
 *
 * Example 3: Vector reference (swap chain images)
 *
 * struct SwapChainNode : public NodeInstance {
 *     std::vector<VkImage> swapchainImages;
 *
 *     // USER WRITES NATURAL C++:
 *     std::vector<ResourceSlot<const std::vector<VkImage>&>> GetOutputs() override {
 *         return {{"images", ResourceLifetime::Persistent}};
 *     }
 *
 *     void Execute() override {
 *         swapchainImages = AcquireSwapchainImages();
 *
 *         // USER WRITES NATURAL C++:
 *         outputData[0].SetHandle(swapchainImages);  // Pass vector by reference!
 *         // System wraps to ConstW<RefW<VectorW<VkImage>>>
 *     }
 * };
 *
 * struct FramebufferNode : public NodeInstance {
 *     // USER WRITES NATURAL C++:
 *     std::vector<ResourceSlot<const std::vector<VkImage>&>> GetInputs() override {
 *         return {{"images", ResourceLifetime::Persistent}};
 *     }
 *
 *     void Execute() override {
 *         // USER WRITES NATURAL C++:
 *         const std::vector<VkImage>& images = inputData[0].GetHandle<const std::vector<VkImage>&>();
 *         // System unwraps automatically
 *
 *         CreateFramebuffers(images);
 *     }
 * };
 *
 * NO WRAPPER TYPES IN USER CODE!
 * NO MANUAL WRAPPING/UNWRAPPING!
 * JUST NATURAL C++!
 */

} // namespace Vixen::RenderGraph