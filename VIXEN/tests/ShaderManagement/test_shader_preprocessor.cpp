/**
 * @file test_shader_preprocessor.cpp
 * @brief Unit tests for ShaderPreprocessor class
 */

#include <gtest/gtest.h>
#include <ShaderManagement/ShaderPreprocessor.h>
#include <filesystem>
#include <fstream>

using namespace ShaderManagement;

// ============================================================================
// Test Fixture
// ============================================================================

class ShaderPreprocessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temp directory for include files
        testIncludeDir = std::filesystem::temp_directory_path() / "shader_include_test";
        std::filesystem::create_directories(testIncludeDir);

        // Setup preprocessor with test include path
        config.includePaths = {testIncludeDir};
        config.enableLineDirectives = false;
    }

    void TearDown() override {
        std::filesystem::remove_all(testIncludeDir);
    }

    // Helper to create a temporary include file
    void CreateIncludeFile(const std::string& filename, const std::string& content) {
        std::filesystem::path path = testIncludeDir / filename;
        std::ofstream file(path);
        file << content;
        file.close();
    }

    std::filesystem::path testIncludeDir;
    PreprocessorConfig config;
};

// ============================================================================
// Construction Tests
// ============================================================================

TEST_F(ShaderPreprocessorTest, DefaultConstruction) {
    ShaderPreprocessor preprocessor;
    EXPECT_TRUE(preprocessor.GetIncludePaths().empty());
    EXPECT_TRUE(preprocessor.GetGlobalDefines().empty());
}

TEST_F(ShaderPreprocessorTest, ConstructionWithConfig) {
    ShaderPreprocessor preprocessor(config);
    EXPECT_EQ(preprocessor.GetIncludePaths().size(), 1);
}

// ============================================================================
// Define Injection Tests
// ============================================================================

TEST_F(ShaderPreprocessorTest, InjectSimpleDefine) {
    ShaderPreprocessor preprocessor;

    std::string source = R"(
#version 450
void main() {
    #ifdef USE_FEATURE
    // feature code
    #endif
}
)";

    std::unordered_map<std::string, std::string> defines;
    defines["USE_FEATURE"] = "";

    auto result = preprocessor.Preprocess(source, defines);
    ASSERT_TRUE(result.success);
    EXPECT_NE(result.processedSource.find("#define USE_FEATURE"), std::string::npos);
}

TEST_F(ShaderPreprocessorTest, InjectDefineWithValue) {
    ShaderPreprocessor preprocessor;

    std::string source = R"(
#version 450
void main() {}
)";

    std::unordered_map<std::string, std::string> defines;
    defines["MAX_LIGHTS"] = "16";
    defines["PI"] = "3.14159";

    auto result = preprocessor.Preprocess(source, defines);
    ASSERT_TRUE(result.success);
    EXPECT_NE(result.processedSource.find("#define MAX_LIGHTS 16"), std::string::npos);
    EXPECT_NE(result.processedSource.find("#define PI 3.14159"), std::string::npos);
}

TEST_F(ShaderPreprocessorTest, DefineInjectionAfterVersion) {
    ShaderPreprocessor preprocessor;

    std::string source = "#version 450\nvoid main() {}";

    std::unordered_map<std::string, std::string> defines;
    defines["TEST"] = "1";

    auto result = preprocessor.Preprocess(source, defines);
    ASSERT_TRUE(result.success);

    // Find positions
    size_t versionPos = result.processedSource.find("#version 450");
    size_t definePos = result.processedSource.find("#define TEST 1");

    ASSERT_NE(versionPos, std::string::npos);
    ASSERT_NE(definePos, std::string::npos);
    EXPECT_LT(versionPos, definePos);  // Define should come after version
}

TEST_F(ShaderPreprocessorTest, DefineWithoutVersion) {
    ShaderPreprocessor preprocessor;

    std::string source = "void main() {}";  // No #version directive

    std::unordered_map<std::string, std::string> defines;
    defines["TEST"] = "1";

    auto result = preprocessor.Preprocess(source, defines);
    ASSERT_TRUE(result.success);
    // Define should be at the beginning
    EXPECT_EQ(result.processedSource.find("#define TEST 1"), 0);
}

// ============================================================================
// Global Defines Tests
// ============================================================================

TEST_F(ShaderPreprocessorTest, GlobalDefines) {
    ShaderPreprocessor preprocessor;

    preprocessor.AddGlobalDefine("GLOBAL1", "value1");
    preprocessor.AddGlobalDefine("GLOBAL2");

    auto globalDefines = preprocessor.GetGlobalDefines();
    EXPECT_EQ(globalDefines.size(), 2);
    EXPECT_EQ(globalDefines["GLOBAL1"], "value1");
    EXPECT_EQ(globalDefines["GLOBAL2"], "");
}

TEST_F(ShaderPreprocessorTest, GlobalAndLocalDefinesMerge) {
    ShaderPreprocessor preprocessor;
    preprocessor.AddGlobalDefine("GLOBAL_DEFINE", "1");

    std::string source = "#version 450\nvoid main() {}";
    std::unordered_map<std::string, std::string> localDefines;
    localDefines["LOCAL_DEFINE"] = "2";

    auto result = preprocessor.Preprocess(source, localDefines);
    ASSERT_TRUE(result.success);

    EXPECT_NE(result.processedSource.find("#define GLOBAL_DEFINE 1"), std::string::npos);
    EXPECT_NE(result.processedSource.find("#define LOCAL_DEFINE 2"), std::string::npos);
}

TEST_F(ShaderPreprocessorTest, RemoveGlobalDefine) {
    ShaderPreprocessor preprocessor;

    preprocessor.AddGlobalDefine("TEST", "1");
    EXPECT_EQ(preprocessor.GetGlobalDefines().size(), 1);

    preprocessor.RemoveGlobalDefine("TEST");
    EXPECT_EQ(preprocessor.GetGlobalDefines().size(), 0);
}

TEST_F(ShaderPreprocessorTest, ClearGlobalDefines) {
    ShaderPreprocessor preprocessor;

    preprocessor.AddGlobalDefine("DEF1", "1");
    preprocessor.AddGlobalDefine("DEF2", "2");
    preprocessor.AddGlobalDefine("DEF3", "3");

    EXPECT_EQ(preprocessor.GetGlobalDefines().size(), 3);

    preprocessor.ClearGlobalDefines();
    EXPECT_EQ(preprocessor.GetGlobalDefines().size(), 0);
}

// ============================================================================
// Include Resolution Tests
// ============================================================================

TEST_F(ShaderPreprocessorTest, SimpleInclude) {
    CreateIncludeFile("common.glsl", "// Common functions\nfloat square(float x) { return x * x; }");

    ShaderPreprocessor preprocessor(config);

    std::string source = R"(
#version 450
#include "common.glsl"
void main() {}
)";

    auto result = preprocessor.Preprocess(source);
    ASSERT_TRUE(result.success);
    EXPECT_NE(result.processedSource.find("float square(float x)"), std::string::npos);
    EXPECT_EQ(result.includedFiles.size(), 1);
}

TEST_F(ShaderPreprocessorTest, MultipleIncludes) {
    CreateIncludeFile("math.glsl", "const float PI = 3.14159;");
    CreateIncludeFile("utils.glsl", "vec3 normalize(vec3 v);");

    ShaderPreprocessor preprocessor(config);

    std::string source = R"(
#version 450
#include "math.glsl"
#include "utils.glsl"
void main() {}
)";

    auto result = preprocessor.Preprocess(source);
    ASSERT_TRUE(result.success);
    EXPECT_NE(result.processedSource.find("const float PI"), std::string::npos);
    EXPECT_NE(result.processedSource.find("vec3 normalize"), std::string::npos);
    EXPECT_EQ(result.includedFiles.size(), 2);
}

TEST_F(ShaderPreprocessorTest, NestedIncludes) {
    CreateIncludeFile("base.glsl", "// Base file");
    CreateIncludeFile("mid.glsl", "#include \"base.glsl\"\n// Mid file");

    ShaderPreprocessor preprocessor(config);

    std::string source = R"(
#version 450
#include "mid.glsl"
void main() {}
)";

    auto result = preprocessor.Preprocess(source);
    ASSERT_TRUE(result.success);
    EXPECT_NE(result.processedSource.find("// Base file"), std::string::npos);
    EXPECT_NE(result.processedSource.find("// Mid file"), std::string::npos);
}

TEST_F(ShaderPreprocessorTest, CircularIncludePrevention) {
    CreateIncludeFile("a.glsl", "#include \"b.glsl\"\n// File A");
    CreateIncludeFile("b.glsl", "#include \"a.glsl\"\n// File B");

    ShaderPreprocessor preprocessor(config);

    std::string source = R"(
#version 450
#include "a.glsl"
void main() {}
)";

    auto result = preprocessor.Preprocess(source);
    ASSERT_TRUE(result.success);
    // Should not hang or crash - circular include guard should prevent infinite loop
}

TEST_F(ShaderPreprocessorTest, IncludeNotFound) {
    ShaderPreprocessor preprocessor(config);

    std::string source = R"(
#version 450
#include "nonexistent.glsl"
void main() {}
)";

    auto result = preprocessor.Preprocess(source);
    // Should still succeed but with a warning (file not found)
    // The preprocessor should continue processing
    EXPECT_TRUE(result.success || !result.errorMessage.empty());
}

// ============================================================================
// Include Path Tests
// ============================================================================

TEST_F(ShaderPreprocessorTest, MultipleIncludePaths) {
    auto secondDir = testIncludeDir / "subdir";
    std::filesystem::create_directories(secondDir);

    CreateIncludeFile("common.glsl", "// From main dir");

    std::ofstream subFile(secondDir / "utils.glsl");
    subFile << "// From subdir";
    subFile.close();

    PreprocessorConfig customConfig;
    customConfig.includePaths = {testIncludeDir, secondDir};

    ShaderPreprocessor preprocessor(customConfig);

    std::string source = R"(
#include "common.glsl"
#include "utils.glsl"
)";

    auto result = preprocessor.Preprocess(source);
    ASSERT_TRUE(result.success);
    EXPECT_NE(result.processedSource.find("// From main dir"), std::string::npos);
    EXPECT_NE(result.processedSource.find("// From subdir"), std::string::npos);
}

TEST_F(ShaderPreprocessorTest, AddIncludePath) {
    ShaderPreprocessor preprocessor;

    EXPECT_EQ(preprocessor.GetIncludePaths().size(), 0);

    preprocessor.AddIncludePath("/path/1");
    preprocessor.AddIncludePath("/path/2");

    EXPECT_EQ(preprocessor.GetIncludePaths().size(), 2);
}

TEST_F(ShaderPreprocessorTest, SetIncludePaths) {
    ShaderPreprocessor preprocessor;

    std::vector<std::filesystem::path> paths = {"/a", "/b", "/c"};
    preprocessor.SetIncludePaths(paths);

    EXPECT_EQ(preprocessor.GetIncludePaths().size(), 3);
}

// ============================================================================
// Preprocess From File Tests
// ============================================================================

TEST_F(ShaderPreprocessorTest, PreprocessFromFile) {
    auto shaderFile = testIncludeDir / "test_shader.glsl";
    std::ofstream file(shaderFile);
    file << "#version 450\nvoid main() {}";
    file.close();

    ShaderPreprocessor preprocessor(config);

    auto result = preprocessor.PreprocessFile(shaderFile);
    ASSERT_TRUE(result.success);
    EXPECT_NE(result.processedSource.find("#version 450"), std::string::npos);
}

TEST_F(ShaderPreprocessorTest, PreprocessFromNonexistentFile) {
    ShaderPreprocessor preprocessor;

    auto result = preprocessor.PreprocessFile("/nonexistent/file.glsl");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
}

// ============================================================================
// Line Directives Tests
// ============================================================================

TEST_F(ShaderPreprocessorTest, LineDirectivesDisabled) {
    ShaderPreprocessor preprocessor(config);  // Line directives disabled by default

    std::string source = "#version 450\nvoid main() {}";
    auto result = preprocessor.Preprocess(source);

    ASSERT_TRUE(result.success);
    // Should not contain #line directives
    EXPECT_EQ(result.processedSource.find("#line"), std::string::npos);
}

TEST_F(ShaderPreprocessorTest, LineDirectivesEnabled) {
    config.enableLineDirectives = true;
    ShaderPreprocessor preprocessor(config);

    std::string source = "#version 450\nvoid main() {}";
    auto result = preprocessor.Preprocess(source);

    ASSERT_TRUE(result.success);
    // Should contain #line directives for debugging
    EXPECT_NE(result.processedSource.find("#line"), std::string::npos);
}

// ============================================================================
// Utility Function Tests
// ============================================================================

TEST(ParseDefinesStringTest, EmptyString) {
    auto defines = ParseDefinesString("");
    EXPECT_TRUE(defines.empty());
}

TEST(ParseDefinesStringTest, SingleDefine) {
    auto defines = ParseDefinesString("USE_PBR");
    EXPECT_EQ(defines.size(), 1);
    EXPECT_EQ(defines["USE_PBR"], "");
}

TEST(ParseDefinesStringTest, DefineWithValue) {
    auto defines = ParseDefinesString("MAX_LIGHTS=16");
    EXPECT_EQ(defines.size(), 1);
    EXPECT_EQ(defines["MAX_LIGHTS"], "16");
}

TEST(ParseDefinesStringTest, MultipleDefines) {
    auto defines = ParseDefinesString("USE_PBR,MAX_LIGHTS=16,ENABLE_SHADOWS");
    EXPECT_EQ(defines.size(), 3);
    EXPECT_EQ(defines["USE_PBR"], "");
    EXPECT_EQ(defines["MAX_LIGHTS"], "16");
    EXPECT_EQ(defines["ENABLE_SHADOWS"], "");
}

TEST(DefinesToStringTest, EmptyMap) {
    std::unordered_map<std::string, std::string> defines;
    auto str = DefinesToString(defines);
    EXPECT_TRUE(str.empty());
}

TEST(DefinesToStringTest, SingleDefine) {
    std::unordered_map<std::string, std::string> defines;
    defines["TEST"] = "";
    auto str = DefinesToString(defines);
    EXPECT_FALSE(str.empty());
}

TEST(DefinesToStringTest, MultipleDefines) {
    std::unordered_map<std::string, std::string> defines;
    defines["A"] = "1";
    defines["B"] = "2";
    auto str = DefinesToString(defines);

    // Parse back and verify
    auto parsed = ParseDefinesString(str);
    EXPECT_EQ(parsed["A"], "1");
    EXPECT_EQ(parsed["B"], "2");
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(ShaderPreprocessorTest, EmptySource) {
    ShaderPreprocessor preprocessor;

    auto result = preprocessor.Preprocess("");
    EXPECT_TRUE(result.success);
}

TEST_F(ShaderPreprocessorTest, OnlyComments) {
    ShaderPreprocessor preprocessor;

    std::string source = "// Just a comment\n/* Block comment */";
    auto result = preprocessor.Preprocess(source);

    ASSERT_TRUE(result.success);
}

TEST_F(ShaderPreprocessorTest, MaxIncludeDepth) {
    // Create a chain of includes exceeding max depth
    for (int i = 0; i < 35; ++i) {
        std::string content = (i < 34) ? "#include \"file" + std::to_string(i + 1) + ".glsl\"\n" : "// End";
        CreateIncludeFile("file" + std::to_string(i) + ".glsl", content);
    }

    ShaderPreprocessor preprocessor(config);

    std::string source = "#include \"file0.glsl\"";
    auto result = preprocessor.Preprocess(source);

    // Should either succeed or fail gracefully with an error
    EXPECT_TRUE(result.success || !result.errorMessage.empty());
}
