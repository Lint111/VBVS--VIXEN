// std::getenv is C4996 under MSVC /WX.
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "Text/GlyphAtlas.h"
#include "Text/GlyphCellStore.h"
#include "Text/RmlFontEngine.h"

#include <gtest/gtest.h>

#include <RmlUi/Core/StringUtilities.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace Vixen::Text;

void SetEnv(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

// Each test gets its own store root under the working directory (the build tree), never the
// real user cache.
class GlyphCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::current_path() / ("glyph-cache-test-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        std::error_code ec;
        fs::remove_all(root_, ec);
        SetEnv("VIXEN_SHARED_CACHE_DIR", root_.string());
        SetEnv("VIXEN_GLYPH_CACHE", "on");
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    struct Run {
        std::vector<GlyphInstance> instances;
        std::vector<std::uint8_t> pixels;
        std::size_t generated = 0;
        double milliseconds = 0.0;
    };

    // One engine lifetime: load the face, lay out the text at each size, report the atlas.
    static Run Render(const std::vector<int>& sizes, const std::string& text) {
        Run run;
        RmlFontEngine engine;
        engine.Initialize();
        EXPECT_TRUE(engine.LoadFontFace(TEXT_LATO_FONT_PATH, 0, false, Rml::Style::FontWeight::Normal));
        const auto start = std::chrono::steady_clock::now();
        for (int size : sizes) {
            const Rml::FontFaceHandle handle =
                engine.GetFontFaceHandle("LatoLatin", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, size);
            engine.BuildGlyphInstances(handle, Rml::StringView(text), {4.0f, 20.0f}, 0, 0.0f, run.instances);
            GlyphAtlas* atlas = engine.GetGlyphAtlas(handle);
            run.generated += atlas->GeneratedGlyphCount();
            const auto pixels = atlas->Pixels();
            run.pixels.insert(run.pixels.end(), pixels.begin(), pixels.end());
        }
        run.milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        engine.Shutdown(); // flushes the stores
        return run;
    }

    static bool SameInstances(const Run& a, const Run& b) {
        return a.instances.size() == b.instances.size() &&
               std::memcmp(a.instances.data(), b.instances.data(), a.instances.size() * sizeof(GlyphInstance)) == 0;
    }

    static std::vector<fs::path> StoreFiles(const fs::path& root) {
        std::vector<fs::path> files;
        if (!fs::exists(root / "glyph-cache"))
            return files;
        for (const auto& entry : fs::directory_iterator(root / "glyph-cache"))
            files.push_back(entry.path());
        return files;
    }

    fs::path root_;
};

const std::string kText = "The quick brown fox jumps over the lazy dog 0123456789 & Ünïcödé ÀÉÎÕÜ ççñ ?!";

} // namespace

TEST_F(GlyphCacheTest, WarmStoreReproducesGeneratedAtlasAndInstancesByteForByte) {
    const Run cold = Render({12, 16}, kText);
    EXPECT_GT(cold.generated, 0u);
    ASSERT_FALSE(StoreFiles(root_).empty());

    const Run warm = Render({12, 16}, kText);
    EXPECT_EQ(warm.generated, 0u) << "every glyph should come from the store";
    EXPECT_EQ(cold.pixels, warm.pixels);
    EXPECT_TRUE(SameInstances(cold, warm));
}

TEST_F(GlyphCacheTest, DisabledCacheWritesNothingAndMatches) {
    const Run cold = Render({14}, kText);
    SetEnv("VIXEN_GLYPH_CACHE", "off");
    std::error_code ec;
    fs::remove_all(root_, ec);
    const Run off = Render({14}, kText);
    EXPECT_TRUE(StoreFiles(root_).empty());
    EXPECT_EQ(cold.pixels, off.pixels);
    EXPECT_TRUE(SameInstances(cold, off));
}

TEST_F(GlyphCacheTest, CorruptedStoreIsRejectedAndRegenerated) {
    const Run cold = Render({16}, kText);
    const auto files = StoreFiles(root_);
    ASSERT_EQ(files.size(), 2u) << "one cell file and one page file";

    auto readAll = [](const fs::path& path) {
        std::vector<char> bytes(static_cast<std::size_t>(fs::file_size(path)));
        std::ifstream in(path, std::ios::binary);
        in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return bytes;
    };
    auto writeAll = [](const fs::path& path, const std::vector<char>& bytes) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };
    std::vector<std::vector<char>> originals;
    for (const auto& file : files) {
        originals.push_back(readAll(file));
        ASSERT_GT(originals.back().size(), 200u);
    }
    auto restore = [&] {
        for (std::size_t i = 0; i < files.size(); ++i)
            writeAll(files[i], originals[i]);
    };

    using Edit = std::function<void(std::vector<char>&)>;
    const std::vector<std::pair<const char*, Edit>> corruptions = {
        {"flipped payload byte", [&](std::vector<char>& b) { b[b.size() - 10] ^= 0x5A; }},
        {"flipped header byte", [&](std::vector<char>& b) { b[3] ^= 0x01; }},
        {"truncated", [&](std::vector<char>& b) { b.resize(b.size() / 2); }},
        {"empty", [&](std::vector<char>& b) { b.clear(); }},
        {"garbage", [&](std::vector<char>& b) { for (auto& c : b) c = static_cast<char>(0xA5); }},
    };
    // Both files corrupted: nothing may be trusted and the glyphs are regenerated.
    for (const auto& [name, edit] : corruptions) {
        restore();
        for (std::size_t i = 0; i < files.size(); ++i) {
            std::vector<char> bytes = originals[i];
            edit(bytes);
            writeAll(files[i], bytes);
        }
        const Run again = Render({16}, kText);
        EXPECT_GT(again.generated, 0u) << name << ": corrupt store must not be trusted";
        EXPECT_EQ(cold.pixels, again.pixels) << name;
        EXPECT_TRUE(SameInstances(cold, again)) << name;
        // The regeneration republishes a valid store.
        const Run healed = Render({16}, kText);
        EXPECT_EQ(healed.generated, 0u) << name << ": store should be healed";
        EXPECT_EQ(cold.pixels, healed.pixels) << name;
    }
    // Only the page corrupted: it is rejected and the cell file still serves every glyph.
    // Only the cell file corrupted: the valid page still holds every glyph.
    for (std::size_t victim = 0; victim < files.size(); ++victim) {
        for (const auto& [name, edit] : corruptions) {
            restore();
            std::vector<char> bytes = originals[victim];
            edit(bytes);
            writeAll(files[victim], bytes);
            const Run again = Render({16}, kText);
            EXPECT_EQ(again.generated, 0u) << name << ": the intact file serves every glyph";
            EXPECT_EQ(cold.pixels, again.pixels) << name;
            EXPECT_TRUE(SameInstances(cold, again)) << name;
        }
    }
}

TEST_F(GlyphCacheTest, DifferentGenerationParametersNeverShareCells) {
    Render({16}, kText);
    GlyphCacheKey key{0x1234, 0, 16, 1024, 4.0f};
    GlyphCacheKey other = key;
    other.distanceRange = 6.0f;
    EXPECT_NE(key.Identity(), other.Identity());
    EXPECT_NE(GlyphCellStore::PathFor(key), GlyphCellStore::PathFor(other));
    other = key;
    other.pageDimension = 512;
    EXPECT_NE(key.Identity(), other.Identity());

    // A file renamed onto another key's path carries the wrong identity and is rejected.
    const auto files = StoreFiles(root_);
    ASSERT_EQ(files.size(), 2u);
    const fs::path cellFile = files[0].string().ends_with(".page.bin") ? files[1] : files[0];
    const fs::path foreign = cellFile.parent_path() / "foreign.bin";
    fs::copy_file(cellFile, foreign);
    GlyphCacheKey wrong{0x1234, 0, 16, 1024, 4.0f};
    std::string reason;
    const auto snapshot = GlyphCellStore::Load(foreign, wrong, &reason);
    EXPECT_EQ(snapshot->Size(), 0u);
    EXPECT_FALSE(reason.empty());
}

// Measurement: prints generation and layout time for a scenario. Cold/warm/off are separate
// process runs selected by VIXEN_GLYPH_BENCH_SCENARIO=ui|heavy; the store root is whatever the
// environment already names, so the same run can hit the real user cache.
TEST(GlyphCacheBench, ReportsLayoutTimeForScenario) {
    const char* scenario = std::getenv("VIXEN_GLYPH_BENCH_SCENARIO");
    if (!scenario)
        GTEST_SKIP() << "set VIXEN_GLYPH_BENCH_SCENARIO=ui|heavy";
    const bool heavy = std::string(scenario) == "heavy";
    std::string text = "File Edit View Window Help  Resolution 1920x1080  FPS 144  Frame 6.9 ms";
    std::vector<int> sizes = {12, 14, 16};
    if (heavy) {
        text = "The quick brown fox jumps over the lazy dog. Pack my box with five dozen liquor jugs. "
               "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789 !@#$%^&*()_+-=[]{};':\",./<>? "
               "ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖØÙÚÛÜÝÞß àáâãäåæçèéêëìíîïðñòóôõöøùúûüýþÿ ŒœŠšŸŽž €£¥©®™";
        sizes = {10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32, 48};
    }
    const auto begin = std::chrono::steady_clock::now();
    RmlFontEngine engine;
    engine.Initialize();
    ASSERT_TRUE(engine.LoadFontFace(TEXT_LATO_FONT_PATH, 0, false, Rml::Style::FontWeight::Normal));
    const auto loaded = std::chrono::steady_clock::now();
    std::vector<GlyphInstance> instances;
    double firstMs = 0.0;
    std::size_t generated = 0;
    const auto layoutStart = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const Rml::FontFaceHandle handle = engine.GetFontFaceHandle(
            "LatoLatin", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, sizes[i]);
        engine.BuildGlyphInstances(handle, Rml::StringView(text), {4.0f, 20.0f}, 0, 0.0f, instances);
        generated += engine.GetGlyphAtlas(handle)->GeneratedGlyphCount();
        if (i == 0)
            firstMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - layoutStart).count();
    }
    const auto laidOut = std::chrono::steady_clock::now();
    engine.Shutdown();
    const auto done = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::printf("GLYPHBENCH scenario=%s sizes=%zu glyph_instances=%zu generated=%zu load_face_ms=%.3f "
                "first_size_text_ms=%.3f layout_all_ms=%.3f shutdown_flush_ms=%.3f\n",
                scenario, sizes.size(), instances.size(), generated, ms(begin, loaded), firstMs,
                ms(layoutStart, laidOut), ms(laidOut, done));
}
