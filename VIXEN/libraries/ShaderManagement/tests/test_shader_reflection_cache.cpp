#include <gtest/gtest.h>
#include "ShaderBundleBuilder.h"
#include "ShaderCacheManager.h"
#include "ShaderReflectionCache.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

using namespace ShaderManagement;

namespace {

const char* kVertex = R"(
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(set = 0, binding = 0) uniform CameraUBO { mat4 view; mat4 projection; vec3 eye; } camera;
layout(set = 1, binding = 0) uniform ModelUBO { mat4 model; vec4 color; } model;
layout(set = 2, binding = 3) readonly buffer Data { vec4 values[]; } data;
layout(location = 0) out vec2 uv;
layout(push_constant) uniform PC { uint instanceId; float time; } pc;
void main() {
    uv = inUv;
    gl_Position = camera.projection * camera.view * model.model * vec4(inPosition, 1.0) + data.values[pc.instanceId];
}
)";

const char* kFragment = R"(
#version 450
layout(location = 0) in vec2 uv;
layout(set = 0, binding = 1) uniform sampler2D albedo;
layout(location = 0) out vec4 outColor;
void main() { outColor = texture(albedo, uv); }
)";

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

} // namespace

class ShaderReflectionCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        root = std::filesystem::temp_directory_path() /
               ("shader_reflection_cache_" + std::to_string(::getpid()) + "_" + info->name());
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        ShaderCacheConfig config;
        config.cacheDirectory = root / "spirv";
        spirvCache = std::make_unique<ShaderCacheManager>(config);
    }
    void TearDown() override { std::filesystem::remove_all(root); }

    ShaderBundleBuilder::BuildResult Build(const std::string& sdiSubdir, const char* vertex = kVertex) {
        SdiGeneratorConfig sdi;
        sdi.outputDirectory = root / sdiSubdir;
        ShaderBundleBuilder builder;
        return builder.SetProgramName("ReflectionCacheShader")
            .AddStage(ShaderStage::Vertex, vertex)
            .AddStage(ShaderStage::Fragment, kFragment)
            .SetSdiConfig(sdi)
            .EnableSdiGeneration(true)
            .EnableCaching(spirvCache.get())
            .Build();
    }

    std::filesystem::path root;
    std::unique_ptr<ShaderCacheManager> spirvCache;
};

TEST_F(ShaderReflectionCacheTest, WarmBuildRestoresReflectionAndRegeneratesIdenticalSdi) {
    auto cold = Build("sdi_cold");
    ASSERT_TRUE(cold.success) << cold.errorMessage;
    EXPECT_FALSE(cold.reflectionFromCache);

    auto warm = Build("sdi_warm");
    ASSERT_TRUE(warm.success) << warm.errorMessage;
    EXPECT_TRUE(warm.usedCache);
    EXPECT_TRUE(warm.reflectionFromCache);

    EXPECT_EQ(warm.bundle->uuid, cold.bundle->uuid);
    EXPECT_EQ(warm.bundle->descriptorInterfaceHash, cold.bundle->descriptorInterfaceHash);
    EXPECT_EQ(ShaderReflectionCache::Serialize(*warm.bundle->reflectionData, warm.bundle->descriptorLayout.get()),
              ShaderReflectionCache::Serialize(*cold.bundle->reflectionData, cold.bundle->descriptorLayout.get()));

    // Same SDI bytes, in the same (map-iteration) order, from restored data.
    const auto uuid = cold.bundle->uuid;
    EXPECT_EQ(ReadFile(root / "sdi_warm" / (uuid + "-SDI.g.h")), ReadFile(root / "sdi_cold" / (uuid + "-SDI.g.h")));
    EXPECT_EQ(ReadFile(root / "sdi_warm" / "ReflectionCacheShaderNames.g.h"),
              ReadFile(root / "sdi_cold" / "ReflectionCacheShaderNames.g.h"));
    EXPECT_FALSE(ReadFile(root / "sdi_warm" / (uuid + "-SDI.g.h")).empty());
}

TEST_F(ShaderReflectionCacheTest, ChangedSpirvIsAMiss) {
    ASSERT_TRUE(Build("sdi_a").success);
    std::string changed = kVertex;
    changed.replace(changed.find("vec4 color"), 10, "vec4 color2");
    auto other = Build("sdi_b", changed.c_str());
    ASSERT_TRUE(other.success) << other.errorMessage;
    EXPECT_FALSE(other.reflectionFromCache);
}

TEST_F(ShaderReflectionCacheTest, CachingDisabledNeverRestores) {
    ASSERT_TRUE(Build("sdi_a").success);
    SdiGeneratorConfig sdi;
    sdi.outputDirectory = root / "sdi_b";
    ShaderBundleBuilder builder;
    auto result = builder.SetProgramName("ReflectionCacheShader")
        .AddStage(ShaderStage::Vertex, kVertex)
        .AddStage(ShaderStage::Fragment, kFragment)
        .SetSdiConfig(sdi)
        .DisableCaching()
        .Build();
    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.reflectionFromCache);
}

TEST_F(ShaderReflectionCacheTest, CorruptOrTruncatedPayloadIsRejected) {
    auto built = Build("sdi_a");
    ASSERT_TRUE(built.success);
    auto words = ShaderReflectionCache::Serialize(*built.bundle->reflectionData, built.bundle->descriptorLayout.get());
    ASSERT_TRUE(ShaderReflectionCache::Deserialize(words).has_value());

    auto truncated = words;
    truncated.resize(words.size() / 2);
    EXPECT_FALSE(ShaderReflectionCache::Deserialize(truncated).has_value());

    auto wrongMagic = words;
    wrongMagic[1] ^= 0xFFu;
    EXPECT_FALSE(ShaderReflectionCache::Deserialize(wrongMagic).has_value());

    auto inflatedLength = words;
    inflatedLength[0] = static_cast<uint32_t>(words.size() * 8);
    EXPECT_FALSE(ShaderReflectionCache::Deserialize(inflatedLength).has_value());

    auto trailing = words;  // length covers the payload but a stray byte trails the last field
    trailing[0] -= 1;
    EXPECT_FALSE(ShaderReflectionCache::Deserialize(trailing).has_value());
}
