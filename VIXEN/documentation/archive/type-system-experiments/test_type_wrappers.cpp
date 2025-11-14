/**
 * @file test_type_wrappers.cpp
 * @brief Tests for the composable type wrapper system
 *
 * Demonstrates how to use the wrapper system to create complex type patterns
 * without registry explosion.
 */

#include <gtest/gtest.h>
#include "../include/Data/Core/TypeWrappers.h"
#include <vulkan/vulkan.h>

using namespace Vixen::RenderGraph;

// Test fixture
class TypeWrapperTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Register base types
        auto& registry = WrapperTypeRegistry::Instance();
        registry.RegisterBaseType<VkImage>();
        registry.RegisterBaseType<VkBuffer>();
        registry.RegisterBaseType<uint32_t>();
        registry.RegisterBaseType<float>();
    }
};

// ============================================================================
// Basic Wrapper Tests
// ============================================================================

TEST_F(TypeWrapperTest, ReferenceWrapper) {
    VkImage image = reinterpret_cast<VkImage>(0x1234);
    RefW<VkImage> ref(image);

    // Test conversion
    VkImage& imgRef = ref;
    EXPECT_EQ(imgRef, image);

    // Test get()
    EXPECT_EQ(ref.get(), image);
}

TEST_F(TypeWrapperTest, PointerWrapper) {
    VkImage image = reinterpret_cast<VkImage>(0x1234);
    PtrW<VkImage> ptr(&image);

    // Test conversion
    VkImage* imgPtr = ptr;
    EXPECT_EQ(imgPtr, &image);

    // Test dereference
    EXPECT_EQ(*ptr, image);

    // Test arrow operator
    EXPECT_EQ(ptr.get(), &image);
}

TEST_F(TypeWrapperTest, VectorWrapper) {
    VectorW<VkImage> vec;
    vec.push_back(reinterpret_cast<VkImage>(0x1234));
    vec.push_back(reinterpret_cast<VkImage>(0x5678));

    EXPECT_EQ(vec.size(), 2);
    EXPECT_EQ(vec[0], reinterpret_cast<VkImage>(0x1234));
    EXPECT_EQ(vec[1], reinterpret_cast<VkImage>(0x5678));

    // Test conversion to std::vector
    std::vector<VkImage>& stdVec = vec;
    EXPECT_EQ(stdVec.size(), 2);
}

// ============================================================================
// Composed Wrapper Tests
// ============================================================================

TEST_F(TypeWrapperTest, ConstReference) {
    // ConstW<RefW<VkImage>> represents const VkImage&
    using ConstImageRef = ConstW<RefW<VkImage>>;

    VkImage image = reinterpret_cast<VkImage>(0x1234);
    RefW<VkImage> ref(image);
    ConstImageRef constRef(ref);

    // Test that we get const access
    const VkImage& imgRef = constRef.get();
    EXPECT_EQ(imgRef, image);
}

TEST_F(TypeWrapperTest, VectorOfPointers) {
    // VectorW<PtrW<VkImage>> represents std::vector<VkImage*>
    using ImagePtrVector = VectorW<PtrW<VkImage>>;

    VkImage img1 = reinterpret_cast<VkImage>(0x1234);
    VkImage img2 = reinterpret_cast<VkImage>(0x5678);

    ImagePtrVector vec;
    vec.data.push_back(&img1);
    vec.data.push_back(&img2);

    EXPECT_EQ(vec.size(), 2);
    EXPECT_EQ(vec[0], &img1);
    EXPECT_EQ(vec[1], &img2);
}

TEST_F(TypeWrapperTest, ReferenceToVector) {
    // RefW<VectorW<VkImage>> represents std::vector<VkImage>&
    using VectorRef = RefW<VectorW<VkImage>>;

    std::vector<VkImage> images = {
        reinterpret_cast<VkImage>(0x1234),
        reinterpret_cast<VkImage>(0x5678)
    };

    VectorW<VkImage> vecWrapper(images);
    VectorRef vecRef(vecWrapper);

    // Access through reference
    std::vector<VkImage>& ref = vecRef.get();
    EXPECT_EQ(ref.size(), 2);
    EXPECT_EQ(ref[0], images[0]);
}

// ============================================================================
// Type Unwrapping Tests
// ============================================================================

TEST_F(TypeWrapperTest, UnwrapSimpleTypes) {
    // Test that unwrapping produces correct C++ types
    static_assert(std::is_same_v<
        UnwrapType<RefW<VkImage>>::type,
        VkImage&
    >);

    static_assert(std::is_same_v<
        UnwrapType<PtrW<VkImage>>::type,
        VkImage*
    >);

    static_assert(std::is_same_v<
        UnwrapType<VectorW<VkImage>>::type,
        std::vector<VkImage>
    >);
}

TEST_F(TypeWrapperTest, UnwrapComposedTypes) {
    // Test complex compositions
    static_assert(std::is_same_v<
        UnwrapType<ConstW<RefW<VkImage>>>::type,
        const VkImage&
    >);

    static_assert(std::is_same_v<
        UnwrapType<VectorW<PtrW<VkImage>>>::type,
        std::vector<VkImage*>
    >);

    static_assert(std::is_same_v<
        UnwrapType<RefW<VectorW<VkImage>>>::type,
        std::vector<VkImage>&
    >);

    static_assert(std::is_same_v<
        UnwrapType<ConstW<RefW<VectorW<VkImage>>>>::type,
        const std::vector<VkImage>&
    >);
}

// ============================================================================
// Registry Tests
// ============================================================================

TEST_F(TypeWrapperTest, RegistryAcceptance) {
    auto& registry = WrapperTypeRegistry::Instance();

    // Base type should be accepted
    EXPECT_TRUE(registry.IsTypeAcceptable<VkImage>());

    // Wrapped types should be accepted
    EXPECT_TRUE(registry.IsTypeAcceptable<RefW<VkImage>>());
    EXPECT_TRUE(registry.IsTypeAcceptable<PtrW<VkImage>>());
    EXPECT_TRUE(registry.IsTypeAcceptable<VectorW<VkImage>>());

    // Composed wrappers should be accepted
    EXPECT_TRUE(registry.IsTypeAcceptable<ConstW<RefW<VkImage>>>());
    EXPECT_TRUE(registry.IsTypeAcceptable<VectorW<PtrW<VkImage>>>());
    EXPECT_TRUE(registry.IsTypeAcceptable<RefW<VectorW<VkImage>>>());

    // Unregistered types should be rejected
    struct UnregisteredType {};
    EXPECT_FALSE(registry.IsTypeAcceptable<UnregisteredType>());
    EXPECT_FALSE(registry.IsTypeAcceptable<RefW<UnregisteredType>>());
    EXPECT_FALSE(registry.IsTypeAcceptable<VectorW<UnregisteredType>>());
}

// ============================================================================
// Variant Storage Tests
// ============================================================================

TEST_F(TypeWrapperTest, VariantWithWrappedTypes) {
    WrappedVariant variant;

    // Store a simple value
    VkImage image = reinterpret_cast<VkImage>(0x1234);
    variant.Set<VkImage>(image);
    EXPECT_EQ(variant.Get<VkImage>(), image);

    // Store through reference wrapper
    variant.Set<RefW<VkImage>>(image);
    VkImage retrieved = variant.Get<RefW<VkImage>>();
    EXPECT_EQ(retrieved, image);
}

TEST_F(TypeWrapperTest, VariantWithVectorWrapper) {
    WrappedVariant variant;

    std::vector<VkImage> images = {
        reinterpret_cast<VkImage>(0x1234),
        reinterpret_cast<VkImage>(0x5678)
    };

    variant.Set<VectorW<VkImage>>(images);
    auto retrieved = variant.Get<VectorW<VkImage>>();

    EXPECT_EQ(retrieved.size(), 2);
    EXPECT_EQ(retrieved[0], images[0]);
    EXPECT_EQ(retrieved[1], images[1]);
}

// ============================================================================
// Convenience Alias Tests
// ============================================================================

TEST_F(TypeWrapperTest, ConvenienceAliases) {
    // Test that convenience aliases produce expected types
    static_assert(std::is_same_v<
        UnwrapType<ConstRef<VkImage>>::type,
        const VkImage&
    >);

    static_assert(std::is_same_v<
        UnwrapType<ConstPtr<VkImage>>::type,
        const VkImage*
    >);

    static_assert(std::is_same_v<
        UnwrapType<RefVector<VkImage>>::type,
        std::vector<VkImage>&
    >);

    static_assert(std::is_same_v<
        UnwrapType<ConstRefVector<VkImage>>::type,
        const std::vector<VkImage>&
    >);

    static_assert(std::is_same_v<
        UnwrapType<VectorOfPtrs<VkImage>>::type,
        std::vector<VkImage*>
    >);
}

// ============================================================================
// Practical Usage Example
// ============================================================================

TEST_F(TypeWrapperTest, PracticalExample) {
    // Simulate a render graph slot that accepts const ref to vector of images
    using SlotType = ConstW<RefW<VectorW<VkImage>>>;

    // Create some images
    std::vector<VkImage> swapchainImages = {
        reinterpret_cast<VkImage>(0x1000),
        reinterpret_cast<VkImage>(0x2000),
        reinterpret_cast<VkImage>(0x3000)
    };

    // Wrap them appropriately
    VectorW<VkImage> vecWrapper(swapchainImages);
    RefW<VectorW<VkImage>> refWrapper(vecWrapper);
    SlotType slotValue(refWrapper);

    // Access the data
    const std::vector<VkImage>& images = slotValue.get();
    EXPECT_EQ(images.size(), 3);
    EXPECT_EQ(images[0], reinterpret_cast<VkImage>(0x1000));

    // Store in variant
    WrappedVariant variant;
    variant.Set<SlotType>(swapchainImages);
    auto retrieved = variant.Get<SlotType>();
    EXPECT_EQ(retrieved.size(), 3);
}

// ============================================================================
// Complex Composition Test
// ============================================================================

TEST_F(TypeWrapperTest, ComplexComposition) {
    // Test a very complex type: const ref to vector of shared pointers
    using ComplexType = ConstW<RefW<VectorW<SharedW<VkImage>>>>;

    // This should unwrap to: const std::vector<std::shared_ptr<VkImage>>&
    static_assert(std::is_same_v<
        UnwrapType<ComplexType>::type,
        const std::vector<std::shared_ptr<VkImage>>&
    >);

    // Registry should accept it since VkImage is registered
    auto& registry = WrapperTypeRegistry::Instance();
    EXPECT_TRUE(registry.IsTypeAcceptable<ComplexType>());
}