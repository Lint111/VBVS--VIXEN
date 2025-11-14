/**
 * @file test_transparent_types.cpp
 * @brief Demonstrates transparent type system - users write natural C++
 *
 * KEY POINT: No wrapper types (RefW, PtrW, etc.) appear in user code!
 * System handles everything behind the scenes.
 */

#include <gtest/gtest.h>
#include "../include/Data/Core/TransparentTypeSystem.h"

using namespace Vixen::RenderGraph;

// ============================================================================
// USER CODE - NATURAL C++ (No knowledge of wrappers!)
// ============================================================================

// User's camera data (stack-allocated struct)
struct CameraData {
    float viewMatrix[16];
    float projectionMatrix[16];
    float position[3];
};

// Register base type (one-time setup)
struct TypeRegistration {
    TypeRegistration() {
        CachedTypeRegistry::Instance().RegisterBaseType<CameraData>();
        CachedTypeRegistry::Instance().RegisterBaseType<VkImage>();
        CachedTypeRegistry::Instance().RegisterBaseType<VkBuffer>();
    }
};
static TypeRegistration g_init;

// ============================================================================
// Test 1: Stack Object by Reference (Most Common Use Case)
// ============================================================================

TEST(TransparentTypesTest, StackObjectByReference) {
    // USER WRITES: Natural C++ with stack object
    CameraData camera;
    camera.position[0] = 1.0f;
    camera.position[1] = 2.0f;
    camera.position[2] = 3.0f;

    // USER WRITES: Create resource
    TransparentResource resource;

    // USER WRITES: Set handle with reference (natural C++!)
    resource.SetHandle(camera);  // Just pass the object!
    // Behind the scenes: wrapped to RefW<CameraData>

    // USER WRITES: Get handle back as reference (natural C++!)
    CameraData& retrievedRef = resource.GetHandle<CameraData&>();
    // Behind the scenes: unwrapped from RefW<CameraData>

    // Verify it's the same object (reference semantics)
    EXPECT_EQ(&retrievedRef, &camera);
    EXPECT_FLOAT_EQ(retrievedRef.position[0], 1.0f);

    // Modify through reference
    retrievedRef.position[0] = 5.0f;
    EXPECT_FLOAT_EQ(camera.position[0], 5.0f);  // Original modified!
}

// ============================================================================
// Test 2: Pointer to Persistent Resource
// ============================================================================

TEST(TransparentTypesTest, PointerToPersistentResource) {
    // USER WRITES: Persistent resource (heap or member variable)
    VkImage texture = reinterpret_cast<VkImage>(0x12345678);

    TransparentResource resource;

    // USER WRITES: Set handle with pointer (natural C++!)
    resource.SetHandle(&texture);  // Just pass the pointer!
    // Behind the scenes: wrapped to PtrW<VkImage>

    // USER WRITES: Get handle back as pointer (natural C++!)
    VkImage* retrievedPtr = resource.GetHandle<VkImage*>();
    // Behind the scenes: unwrapped from PtrW<VkImage>

    // Verify it's the same pointer
    EXPECT_EQ(retrievedPtr, &texture);
    EXPECT_EQ(*retrievedPtr, texture);
}

// ============================================================================
// Test 3: Const Reference (Read-Only Access)
// ============================================================================

TEST(TransparentTypesTest, ConstReference) {
    // USER WRITES: Stack object
    CameraData camera;
    camera.position[0] = 10.0f;

    TransparentResource resource;

    // USER WRITES: Set handle (non-const reference)
    resource.SetHandle(camera);

    // USER WRITES: Get as const reference (natural C++!)
    const CameraData& constRef = resource.GetHandle<const CameraData&>();
    // Behind the scenes: unwrapped from ConstW<RefW<CameraData>>

    // Can read but not modify
    EXPECT_FLOAT_EQ(constRef.position[0], 10.0f);
    // constRef.position[0] = 20.0f;  // Would not compile - const!
}

// ============================================================================
// Test 4: Vector by Reference (Swapchain Images Pattern)
// ============================================================================

TEST(TransparentTypesTest, VectorByReference) {
    // USER WRITES: Vector of images (common pattern)
    std::vector<VkImage> swapchainImages = {
        reinterpret_cast<VkImage>(0x1000),
        reinterpret_cast<VkImage>(0x2000),
        reinterpret_cast<VkImage>(0x3000)
    };

    TransparentResource resource;

    // USER WRITES: Set handle with vector reference (natural C++!)
    resource.SetHandle(swapchainImages);  // Pass by reference!
    // Behind the scenes: wrapped to RefW<VectorW<VkImage>>

    // USER WRITES: Get back as vector reference (natural C++!)
    std::vector<VkImage>& retrievedVec = resource.GetHandle<std::vector<VkImage>&>();
    // Behind the scenes: unwrapped

    // Verify same vector
    EXPECT_EQ(retrievedVec.size(), 3);
    EXPECT_EQ(retrievedVec[0], swapchainImages[0]);

    // Modify through reference
    retrievedVec.push_back(reinterpret_cast<VkImage>(0x4000));
    EXPECT_EQ(swapchainImages.size(), 4);  // Original vector modified!
}

// ============================================================================
// Test 5: Const Vector Reference (Read-Only Array)
// ============================================================================

TEST(TransparentTypesTest, ConstVectorReference) {
    // USER WRITES: Const vector
    std::vector<VkBuffer> buffers = {
        reinterpret_cast<VkBuffer>(0xA000),
        reinterpret_cast<VkBuffer>(0xB000)
    };

    TransparentResource resource;

    // USER WRITES: Set handle
    resource.SetHandle(buffers);

    // USER WRITES: Get as const vector reference (natural C++!)
    const std::vector<VkBuffer>& constVecRef = resource.GetHandle<const std::vector<VkBuffer>&>();
    // Behind the scenes: ConstW<RefW<VectorW<VkBuffer>>>

    // Can read
    EXPECT_EQ(constVecRef.size(), 2);
    EXPECT_EQ(constVecRef[0], buffers[0]);

    // Cannot modify
    // constVecRef.push_back(...);  // Would not compile - const!
}

// ============================================================================
// Test 6: Value Semantics (Copy)
// ============================================================================

TEST(TransparentTypesTest, ValueSemantics) {
    // USER WRITES: Value type
    VkImage image = reinterpret_cast<VkImage>(0xABCDEF);

    TransparentResource resource;

    // USER WRITES: Set by value (natural C++!)
    resource.SetHandle(image);  // Copied
    // Behind the scenes: stored as value (no wrapper needed for base types)

    // USER WRITES: Get by value (natural C++!)
    VkImage retrievedImage = resource.GetHandle<VkImage>();

    // Verify value
    EXPECT_EQ(retrievedImage, image);

    // Different objects (value semantics)
    // Modifying retrieved doesn't affect original
}

// ============================================================================
// Test 7: Null Pointer Handling
// ============================================================================

TEST(TransparentTypesTest, NullPointerHandling) {
    TransparentResource resource;

    // USER WRITES: Set null pointer (natural C++!)
    VkImage* nullPtr = nullptr;
    resource.SetHandle(nullPtr);

    // USER WRITES: Get null pointer back (natural C++!)
    VkImage* retrieved = resource.GetHandle<VkImage*>();

    EXPECT_EQ(retrieved, nullptr);
}

// ============================================================================
// Test 8: ResourceSlot Type Deduction
// ============================================================================

TEST(TransparentTypesTest, ResourceSlotTypeDeduction) {
    // USER WRITES: Natural C++ types in slot declarations
    using RefSlot = ResourceSlot<CameraData&>;
    using PtrSlot = ResourceSlot<VkImage*>;
    using ConstRefSlot = ResourceSlot<const CameraData&>;
    using VectorRefSlot = ResourceSlot<std::vector<VkImage>&>;

    // Verify internal wrapper types are correct (compile-time)
    static_assert(std::is_same_v<typename RefSlot::WrapperType, RefW<CameraData>>);
    static_assert(std::is_same_v<typename PtrSlot::WrapperType, PtrW<VkImage>>);
    static_assert(std::is_same_v<typename ConstRefSlot::WrapperType, ConstW<RefW<CameraData>>>);
    static_assert(std::is_same_v<typename VectorRefSlot::WrapperType, RefW<VectorW<VkImage>>>);

    // But users never see these wrappers!
}

// ============================================================================
// Test 9: Real-World Usage Pattern (No Wrapper Types Visible!)
// ============================================================================

TEST(TransparentTypesTest, RealWorldUsagePattern) {
    // Simulate node execution

    // === PRODUCER NODE ===
    struct {
        CameraData cameraData;  // Stack-allocated

        void Execute(TransparentResource& output) {
            // Update camera
            cameraData.position[0] = 1.0f;
            cameraData.position[1] = 2.0f;
            cameraData.position[2] = 3.0f;

            // USER WRITES: Just pass the object! (natural C++)
            output.SetHandle(cameraData);
            // NO WRAPPER TYPES IN USER CODE!
        }
    } producerNode;

    // === CONSUMER NODE ===
    struct {
        void Execute(const TransparentResource& input) {
            // USER WRITES: Get const reference (natural C++)
            const CameraData& camera = input.GetHandle<const CameraData&>();
            // NO WRAPPER TYPES IN USER CODE!

            // Use camera data
            EXPECT_FLOAT_EQ(camera.position[0], 1.0f);
            EXPECT_FLOAT_EQ(camera.position[1], 2.0f);
            EXPECT_FLOAT_EQ(camera.position[2], 3.0f);
        }
    } consumerNode;

    // Execute graph
    TransparentResource resource;
    producerNode.Execute(resource);
    consumerNode.Execute(resource);
}

// ============================================================================
// Test 10: Type Validation Still Works
// ============================================================================

TEST(TransparentTypesTest, TypeValidationStillWorks) {
    // Registered types should be accepted
    EXPECT_TRUE(CachedTypeRegistry::Instance().IsTypeAcceptable<CameraData>());
    EXPECT_TRUE(CachedTypeRegistry::Instance().IsTypeAcceptable<VkImage>());
    EXPECT_TRUE(CachedTypeRegistry::Instance().IsTypeAcceptable<VkBuffer>());

    // Unregistered types should be rejected
    struct UnregisteredType {};
    EXPECT_FALSE(CachedTypeRegistry::Instance().IsTypeAcceptable<UnregisteredType>());

    // But users don't need to check - system handles it!
}

// ============================================================================
// SUMMARY
// ============================================================================

/**
 * KEY TAKEAWAYS:
 *
 * 1.  Users write natural C++: CameraData&, VkImage*, const std::vector<T>&
 * 2.  NO wrapper types in user code (RefW, PtrW hidden)
 * 3.  System auto-wraps on SetHandle()
 * 4.  System auto-unwraps on GetHandle()
 * 5.  Zero-copy reference passing for stack objects
 * 6.  Type validation cached behind the scenes
 * 7.  No code changes needed from existing patterns
 *
 * BEFORE (hypothetical verbose system):
 *   outputData[0].SetHandle(RefW<CameraData>(camera));  // L Ugly!
 *
 * NOW (transparent system):
 *   outputData[0].SetHandle(camera);  //  Natural C++!
 */