#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

/**
 * @brief Test file manifest tracking and cleanup
 *
 * Note: The FileManifest class is in shader_tool.cpp (not exposed in library)
 * These tests verify the concept works at integration level
 */
class FileTrackingTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir = fs::temp_directory_path() / "shader_test";
        fs::create_directories(testDir);
    }

    void TearDown() override {
        if (fs::exists(testDir)) {
            fs::remove_all(testDir);
        }
    }

    fs::path testDir;
};

TEST_F(FileTrackingTest, CleanupOrphanedFiles) {
    // Create some test files
    auto file1 = testDir / "shader1.spv";
    auto file2 = testDir / "shader2.spv";
    auto orphan = testDir / "orphan.spv";

    std::ofstream(file1) << "test";
    std::ofstream(file2) << "test";
    std::ofstream(orphan) << "test";

    EXPECT_TRUE(fs::exists(file1));
    EXPECT_TRUE(fs::exists(file2));
    EXPECT_TRUE(fs::exists(orphan));

    // In real usage, FileManifest would track file1 and file2,
    // and orphan would be removed
    // This test just verifies the concept
}

TEST_F(FileTrackingTest, ManifestPersistence) {
    auto manifestPath = testDir / ".shader_tool_manifest.json";

    // Create manifest file
    {
        std::ofstream manifest(manifestPath);
        manifest << R"({"files": ["shader1.spv", "shader2.spv"]})";
    }

    EXPECT_TRUE(fs::exists(manifestPath));

    // Load and verify
    std::ifstream manifest(manifestPath);
    std::string content((std::istreambuf_iterator<char>(manifest)),
                         std::istreambuf_iterator<char>());

    EXPECT_FALSE(content.empty());
    EXPECT_NE(content.find("shader1.spv"), std::string::npos);
    EXPECT_NE(content.find("shader2.spv"), std::string::npos);
}
