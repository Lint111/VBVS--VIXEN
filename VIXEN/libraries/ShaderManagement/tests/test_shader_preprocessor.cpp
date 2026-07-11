#include <gtest/gtest.h>
#include "ShaderPreprocessor.h"
#include <filesystem>
#include <fstream>

using namespace ShaderManagement;

// Regression coverage for the includeGuard scoping bug found during Sampled
// Lighting Inc3 M1: a guarded file included twice, non-nested (e.g. once
// transitively via another include, once again directly later in the same
// file), was misreported as a circular include because includeGuard entries
// were never erased once a nested include finished processing.
class ShaderPreprocessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir = std::filesystem::temp_directory_path() / "shader_preprocessor_test";
        std::filesystem::create_directories(testDir);
    }

    void TearDown() override {
        if (std::filesystem::exists(testDir)) {
            std::filesystem::remove_all(testDir);
        }
    }

    void WriteFile(const std::filesystem::path& path, const std::string& contents) {
        std::ofstream out(path);
        out << contents;
    }

    std::filesystem::path testDir;
};

TEST_F(ShaderPreprocessorTest, GuardedFileIncludedTwiceNonNestedSucceeds) {
    // B.glsl: a normal #ifndef-guarded header, like Generated/LightingConfig.glsl.
    WriteFile(testDir / "B.glsl",
        "#ifndef B_GLSL\n"
        "#define B_GLSL\n"
        "struct Thing { float x; };\n"
        "#endif\n");

    // A.glsl includes B once transitively (via Mid.glsl) and once directly,
    // non-nested -- the exact DirectLighting.comp / SceneBindings.glsl shape.
    WriteFile(testDir / "Mid.glsl", "#include \"B.glsl\"\n");
    WriteFile(testDir / "A.glsl",
        "#include \"Mid.glsl\"\n"
        "#include \"B.glsl\"\n"
        "void main() {}\n");

    PreprocessorConfig cfg;
    cfg.includePaths = { testDir };
    ShaderPreprocessor preprocessor(cfg);

    auto result = preprocessor.PreprocessFile(testDir / "A.glsl");

    EXPECT_TRUE(result.success) << "Error: " << result.errorMessage;
}

TEST_F(ShaderPreprocessorTest, TrueCircularIncludeStillDetected) {
    // X includes Y includes X while X is still on the active include stack --
    // a genuine cycle, must still be rejected.
    WriteFile(testDir / "X.glsl", "#include \"Y.glsl\"\n");
    WriteFile(testDir / "Y.glsl", "#include \"X.glsl\"\n");

    PreprocessorConfig cfg;
    cfg.includePaths = { testDir };
    ShaderPreprocessor preprocessor(cfg);

    auto result = preprocessor.PreprocessFile(testDir / "X.glsl");

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.errorMessage.find("Circular include"), std::string::npos)
        << "Error was: " << result.errorMessage;
}
