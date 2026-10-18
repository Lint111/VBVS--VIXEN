#include <gtest/gtest.h>

#include "Textures/STBTextureLoader.h"
#include "Textures/TextureCacher.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>

namespace {

std::filesystem::path Fixture(const char* name) {
    return std::filesystem::path(TEXTURES_FIXTURE_DIR) / name;
}

class ProbeTextureCacher final : public CashSystem::TextureCacher {
public:
    int createCount = 0;

protected:
    std::shared_ptr<CashSystem::TextureWrapper> Create(
        const CashSystem::TextureCreateParams& params) override {
        ++createCount;
        auto wrapper = std::make_shared<CashSystem::TextureWrapper>();
        wrapper->filePath = params.filePath;
        wrapper->format = params.format;
        return wrapper;
    }

    std::uint64_t ComputeKey(
        const CashSystem::TextureCreateParams& params) const override {
        return std::hash<std::string>{}(
            params.filePath + "|" + std::to_string(static_cast<uint32_t>(params.format)));
    }
};

} // namespace

TEST(TextureDecode, CheckedInPngProducesRgba8ExtentAndBytes) {
    const auto result = Vixen::TextureHandling::STBTextureLoader::DecodeFile(
        Fixture("texture-loader-fixture.png").string().c_str());

    ASSERT_TRUE(result.has_value()) << result.error().message;
    const auto& image = result.value();
    EXPECT_EQ(image.width, 500u);
    EXPECT_EQ(image.height, 500u);
    ASSERT_EQ(image.pixels.size(), 500u * 500u * 4u);

    // The checked-in capture's first four RGB bytes are [5, 5, 15]; STB adds
    // the requested opaque alpha channel and expands the RGB PNG to RGBA8.
    const std::array<uint8_t, 16> expected = {
        5, 5, 15, 255, 5, 5, 15, 255,
        5, 5, 15, 255, 5, 5, 15, 255};
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), image.pixels.begin()));
}

TEST(TextureDecode, MissingFileReportsErrorWithoutThrowing) {
    EXPECT_NO_THROW({
        const auto result = Vixen::TextureHandling::STBTextureLoader::DecodeFile(
            Fixture("does-not-exist.png").string().c_str());
        EXPECT_FALSE(result.has_value());
    });
}

TEST(TextureCacher, ReusesPublishedEntryOnHit) {
    ProbeTextureCacher cacher;
    CashSystem::TextureCreateParams params{};
    params.filePath = "fixture.png";

    auto first = cacher.GetOrCreate(params);
    auto second = cacher.GetOrCreate(params);

    ASSERT_NE(first, nullptr);
    EXPECT_EQ(second, first);
    EXPECT_EQ(cacher.createCount, 1);
    EXPECT_EQ(cacher.EntryCount(), 1u);
}
