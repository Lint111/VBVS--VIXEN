#include <gtest/gtest.h>

#include "MainCacher.h"
#include "RecipeBucketDataCacher.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <typeindex>

namespace {

class RecipeBucketDataCacherTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto type = std::type_index(typeid(CashSystem::RecipeBucketSnapshot));
        mainCacher_.RegisterCacher<
            CashSystem::RecipeBucketDataCacher,
            CashSystem::RecipeBucketSnapshot,
            CashSystem::RecipeBucketCacheCreateInfo
        >(type, "RecipeBucketData", /*isDeviceDependent=*/false);
        cacher_ = mainCacher_.GetCacher<
            CashSystem::RecipeBucketDataCacher,
            CashSystem::RecipeBucketSnapshot,
            CashSystem::RecipeBucketCacheCreateInfo
        >(type);
    }

    CashSystem::MainCacher mainCacher_;
    CashSystem::RecipeBucketDataCacher* cacher_ = nullptr;
};

TEST_F(RecipeBucketDataCacherTest, GenerationPairReusesCanonicalSnapshot) {
    ASSERT_NE(cacher_, nullptr);

    CashSystem::RecipeBucketCacheCreateInfo ci;
    ci.registryGeneration = 4;
    ci.instanceGeneration = 9;
    ci.boundSpheres[3].radius = 2.0f;
    ci.bucketMeta[3].memberCount = 8;
    ci.instanceSkipMask[0] = 0x0000000fu;

    const auto first = cacher_->GetOrCreate(ci);
    const auto second = cacher_->GetOrCreate(ci);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_EQ(first->registryGeneration, 4u);
    EXPECT_EQ(first->instanceGeneration, 9u);
}

TEST_F(RecipeBucketDataCacherTest, DirtyRangesAreByteExactAndIgnoreB1MaskWords) {
    CashSystem::RecipeBucketCacheCreateInfo beforeInfo;
    beforeInfo.registryGeneration = 4;
    beforeInfo.instanceGeneration = 9;
    beforeInfo.boundSpheres[3].radius = 2.0f;
    beforeInfo.bucketMeta[3].memberCount = 8;
    beforeInfo.instanceSkipMask[0] = 0x0000000fu;

    CashSystem::RecipeBucketCacheCreateInfo afterInfo = beforeInfo;
    afterInfo.registryGeneration = 5;
    afterInfo.boundSpheres[7].radius = 3.0f;
    afterInfo.bucketMeta[3].memberCount = 12;
    afterInfo.instanceSkipMask[0] = 0x000000f0u;
    afterInfo.instanceSkipMask[6] = 0xffffffffu; // B1-owned; CPU publisher must ignore it.

    const auto before = cacher_->GetOrCreate(beforeInfo);
    const auto after = cacher_->GetOrCreate(afterInfo);
    ASSERT_NE(before, nullptr);
    ASSERT_NE(after, nullptr);

    const auto dirty = CashSystem::ComputeRecipeBucketDirtyRanges(*before, *after);
    ASSERT_EQ(dirty.boundSpheres.size(), 1u);
    EXPECT_EQ(dirty.boundSpheres[0].offset, 7u * sizeof(CashSystem::RecipeBoundSphereGpu));
    EXPECT_EQ(dirty.boundSpheres[0].size, sizeof(CashSystem::RecipeBoundSphereGpu));
    ASSERT_EQ(dirty.bucketMeta.size(), 1u);
    EXPECT_EQ(dirty.bucketMeta[0].offset, 3u * sizeof(CashSystem::RecipeBucketMetaGpu));
    EXPECT_EQ(dirty.bucketMeta[0].size, sizeof(CashSystem::RecipeBucketMetaGpu));
    ASSERT_EQ(dirty.instanceSkipMask.size(), 1u);
    EXPECT_EQ(dirty.instanceSkipMask[0].offset, 0u);
    EXPECT_EQ(dirty.instanceSkipMask[0].size, sizeof(std::uint32_t));

    auto uploadedSpheres = before->boundSpheres;
    auto uploadedMeta = before->bucketMeta;
    auto uploadedMask = before->instanceSkipMask;
    for (const auto& range : dirty.boundSpheres) {
        std::memcpy(reinterpret_cast<std::uint8_t*>(uploadedSpheres.data()) + range.offset,
                    reinterpret_cast<const std::uint8_t*>(after->boundSpheres.data()) + range.offset,
                    range.size);
    }
    for (const auto& range : dirty.bucketMeta) {
        std::memcpy(reinterpret_cast<std::uint8_t*>(uploadedMeta.data()) + range.offset,
                    reinterpret_cast<const std::uint8_t*>(after->bucketMeta.data()) + range.offset,
                    range.size);
    }
    for (const auto& range : dirty.instanceSkipMask) {
        std::memcpy(reinterpret_cast<std::uint8_t*>(uploadedMask.data()) + range.offset,
                    reinterpret_cast<const std::uint8_t*>(after->instanceSkipMask.data()) + range.offset,
                    range.size);
    }
    EXPECT_EQ(std::memcmp(uploadedSpheres.data(), after->boundSpheres.data(), sizeof(uploadedSpheres)), 0);
    EXPECT_EQ(std::memcmp(uploadedMeta.data(), after->bucketMeta.data(), sizeof(uploadedMeta)), 0);
    EXPECT_EQ(uploadedMask[0], after->instanceSkipMask[0]);
    EXPECT_EQ(uploadedMask[6], before->instanceSkipMask[6]);

    const auto unchanged = CashSystem::ComputeRecipeBucketDirtyRanges(*after, *after);
    EXPECT_TRUE(unchanged.Empty());
}

} // namespace
