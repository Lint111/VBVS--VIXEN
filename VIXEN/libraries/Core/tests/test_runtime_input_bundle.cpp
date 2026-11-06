#include "RuntimeInputBundle.h"

#include "VixenHash.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <string>

namespace {

using Json = nlohmann::json;
using Vixen::RuntimeInputBundle;
using Vixen::RuntimeInputBundleStore;
using Vixen::RuntimeInputCompatibilityAdapter;
using Vixen::RuntimeInputValidationOptions;

constexpr char kProductKey[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
constexpr char kCodegenToolKey[] = "1111111111111111111111111111111111111111111111111111111111111111";
constexpr char kContentDigest[] = "2222222222222222222222222222222222222222222222222222222222222222";
constexpr char kLayoutDigest[] = "3333333333333333333333333333333333333333333333333333333333333333";
constexpr char kShaderCompilerDigest[] = "4444444444444444444444444444444444444444444444444444444444444444";
constexpr char kArtifactSchemaDigest[] = "5555555555555555555555555555555555555555555555555555555555555555";

class BundleFixture {
public:
    BundleFixture() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("vixen-runtime-input-test-" + std::to_string(stamp));
        std::filesystem::create_directories(root_ / "undertow");
        std::filesystem::create_directories(root_ / "ui");
        Write("undertow/schemas.json", "{\"schema\":true}\n");
        Write("ui/UndertowFlow.appflow", "model appflow\nshape 0x12345678\n");
        Write("ui/frame.json", "{\"bindings\":[\"color\"]}\n");
        WriteIndex();
    }

    ~BundleFixture() { std::error_code ignored; std::filesystem::remove_all(root_, ignored); }

    const std::filesystem::path& Root() const { return root_; }
    std::filesystem::path Index() const { return root_ / "runtime-data.index.json"; }

    void CorruptFlow() { Write("ui/UndertowFlow.appflow", "model appflow\nshape 0x87654321\n"); }

    void RewriteProductKey(std::string value) {
        Json index = ReadIndex();
        index["productKey"] = std::move(value);
        WriteIndex(index);
    }

    void RewriteIndexWithMissingDependency() {
        Json index = ReadIndex();
        index["artifacts"][1]["dependencies"] = {"missing.json"};
        WriteIndex(index);
    }

private:
    std::filesystem::path root_;

    void Write(std::string_view relative, std::string_view contents) {
        const auto path = root_ / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << contents;
    }

    std::string Digest(std::string_view relative) const {
        std::ifstream input(root_ / relative, std::ios::binary);
        const std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        return Vixen::Hash::ComputeSHA256Hex(data.data(), data.size());
    }

    Json ReadIndex() const {
        std::ifstream input(Index(), std::ios::binary);
        return Json::parse(std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>()));
    }

    void WriteIndex(Json index) {
        std::ofstream output(Index(), std::ios::binary | std::ios::trunc);
        output << index.dump() << '\n';
    }

    void WriteIndex() {
        const std::string schemaDigest = Digest("undertow/schemas.json");
        Json index{
            {"runtimeDataFormat", 2},
            {"productKey", kProductKey},
            {"producer", {
                {"pins", Json::array({{{"name", "kernelPin"}, {"value", "kernel-test"}}})},
                {"codegenToolKey", kCodegenToolKey},
                {"facadeSchemaDigest", schemaDigest},
                {"contentDigest", kContentDigest},
            }},
            {"abi", {
                {"cpuGpuLayoutDigest", kLayoutDigest},
                {"shaderCompilerIdentity", "test-compiler"},
                {"shaderCompilerDigest", kShaderCompilerDigest},
                {"artifactSchemaDigest", kArtifactSchemaDigest},
            }},
            {"artifacts", Json::array()},
        };
        const auto addArtifact = [&](std::string kind, std::string path, std::vector<std::string> deps) {
            std::ifstream input(root_ / path, std::ios::binary);
            const std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            index["artifacts"].push_back({
                {"kind", std::move(kind)},
                {"relativePath", std::move(path)},
                {"formatVersion", 1},
                {"bytes", data.size()},
                {"sha256", Vixen::Hash::ComputeSHA256Hex(data.data(), data.size())},
                {"dependencies", std::move(deps)},
            });
        };
        // Keep canonical path order, matching the export writer's contract ("ui" sorts before
        // "undertow" in the normalized POSIX path order).
        addArtifact("ui.appflow", "ui/UndertowFlow.appflow", {"undertow/schemas.json"});
        addArtifact("frame-target.schema", "ui/frame.json", {"undertow/schemas.json"});
        addArtifact("undertow.schema-catalog", "undertow/schemas.json", {});
        WriteIndex(std::move(index));
    }
};

}  // namespace

TEST(RuntimeInputBundle, LoadsAndOwnsValidatedMembers) {
    BundleFixture fixture;
    EXPECT_EQ(
        Vixen::Hash::ComputeSHA256Hex("abc", 3),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const auto loaded = RuntimeInputBundle::Load(fixture.Root());
    ASSERT_TRUE(loaded) << loaded.error;
    EXPECT_EQ(loaded.bundle->ArtifactCount(), 3u);
    EXPECT_EQ(loaded.bundle->Identity().facadeSchemaDigest,
              Vixen::Hash::ComputeSHA256Hex("{\"schema\":true}\n", 16));
    EXPECT_EQ(loaded.bundle->AppFlowShapeHash(), std::optional<uint32_t>(0x12345678u));
    EXPECT_EQ(loaded.bundle->ArtifactText("ui/UndertowFlow.appflow"),
              "model appflow\nshape 0x12345678\n");
}

TEST(RuntimeInputBundle, RejectsCorruptedMemberBeforeGraphUse) {
    BundleFixture fixture;
    fixture.CorruptFlow();
    const auto loaded = RuntimeInputBundle::Load(fixture.Index());
    EXPECT_FALSE(loaded);
    EXPECT_NE(loaded.error.find("digest mismatch"), std::string::npos);
}

TEST(RuntimeInputBundle, RejectsMismatchedProductAndDependency) {
    BundleFixture fixture;
    fixture.RewriteProductKey("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    RuntimeInputValidationOptions options;
    options.productKey = kProductKey;
    auto loaded = RuntimeInputBundle::Load(fixture.Root(), options);
    EXPECT_FALSE(loaded);
    EXPECT_NE(loaded.error.find("productKey mismatch"), std::string::npos);

    fixture.RewriteProductKey(kProductKey);
    fixture.RewriteIndexWithMissingDependency();
    loaded = RuntimeInputBundle::Load(fixture.Root());
    EXPECT_FALSE(loaded);
    EXPECT_NE(loaded.error.find("invalid artifact dependency"), std::string::npos);
}

TEST(RuntimeInputBundle, HotReloadIsFailClosedAndSwapsAtFrameBoundary) {
    BundleFixture fixture;
    RuntimeInputBundleStore store;
    std::string error;
    ASSERT_TRUE(store.LoadBoot(fixture.Root(), {}, &error)) << error;
    const auto first = store.Active();
    ASSERT_TRUE(first);

    fixture.CorruptFlow();
    EXPECT_FALSE(store.RequestReload(fixture.Root(), &error));
    EXPECT_EQ(store.Active(), first);
    EXPECT_FALSE(store.HasPendingReload());

    BundleFixture candidate;
    ASSERT_TRUE(store.RequestReload(candidate.Root(), &error)) << error;
    EXPECT_EQ(store.Active(), first);
    EXPECT_TRUE(store.HasPendingReload());
    EXPECT_TRUE(store.CommitFrameBoundary());
    EXPECT_NE(store.Active(), first);
    EXPECT_FALSE(store.HasPendingReload());
}

TEST(RuntimeInputBundle, CompatibilityAdapterComparesLegacyIdentities) {
    BundleFixture fixture;
    const auto loaded = RuntimeInputBundle::Load(fixture.Root());
    ASSERT_TRUE(loaded) << loaded.error;
    RuntimeInputCompatibilityAdapter adapter(
        0x12345678u, std::nullopt, std::string(kLayoutDigest));
    EXPECT_TRUE(adapter.Compare(*loaded.bundle));
    RuntimeInputCompatibilityAdapter mismatch(
        0x12345678u, std::nullopt, std::string(64, 'a'));
    std::string error;
    EXPECT_FALSE(mismatch.Compare(*loaded.bundle, &error));
    EXPECT_NE(error.find("layout"), std::string::npos);
}
