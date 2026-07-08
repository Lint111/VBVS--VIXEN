// SPIR-V loader hardening (audit V-M7): ShaderModuleCacher::CompileShader used to resize() its
// destination to floor(fileSize/4) words but memcpy() fileSize bytes into it — a file whose size
// isn't a multiple of 4 overflowed the vector by up to 3 bytes. It also never checked file.read()
// or the SPIR-V magic number. These tests exercise CompileShader directly (no VkDevice needed —
// it's pure file I/O) via a probe that exposes the private method.

#include <gtest/gtest.h>
#include <ShaderModuleCacher.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

struct CompileProbe : CashSystem::ShaderModuleCacher {
    using CashSystem::ShaderModuleCacher::CompileShader;
};

std::filesystem::path TempPath(const char* name) {
    return std::filesystem::temp_directory_path() / (std::string("spirv_loader_test_") + name);
}

void WriteFile(const std::filesystem::path& path, const std::vector<char>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!bytes.empty()) {
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
}

CashSystem::ShaderModuleCreateParams ParamsFor(const std::filesystem::path& spvPath) {
    CashSystem::ShaderModuleCreateParams p;
    p.sourcePath = spvPath.string();  // already ends in .spv, used as-is
    p.entryPoint = "main";
    p.shaderName = "test_shader";
    return p;
}

}  // namespace

TEST(SpirvLoader, SevenByteFileFailsCleanly) {
    const auto path = TempPath("seven_bytes.spv");
    WriteFile(path, std::vector<char>(7, '\x01'));  // not a multiple of 4

    CompileProbe probe;
    CashSystem::ShaderModuleWrapper wrapper;
    EXPECT_THROW(probe.CompileShader(ParamsFor(path), wrapper), std::exception);
    // No overflow, no partial module created.
    EXPECT_TRUE(wrapper.spirvCode.empty());
    EXPECT_EQ(wrapper.shaderModule, VK_NULL_HANDLE);

    std::filesystem::remove(path);
}

TEST(SpirvLoader, WrongMagicFailsCleanly) {
    const auto path = TempPath("wrong_magic.spv");
    // 4-byte aligned, but not the SPIR-V magic number.
    std::vector<uint32_t> words = {0xDEADBEEFu, 0, 0, 0};
    std::vector<char> bytes(words.size() * sizeof(uint32_t));
    std::memcpy(bytes.data(), words.data(), bytes.size());
    WriteFile(path, bytes);

    CompileProbe probe;
    CashSystem::ShaderModuleWrapper wrapper;
    EXPECT_THROW(probe.CompileShader(ParamsFor(path), wrapper), std::exception);

    std::filesystem::remove(path);
}

TEST(SpirvLoader, EmptyFileFailsCleanly) {
    const auto path = TempPath("empty.spv");
    WriteFile(path, {});

    CompileProbe probe;
    CashSystem::ShaderModuleWrapper wrapper;
    EXPECT_THROW(probe.CompileShader(ParamsFor(path), wrapper), std::exception);
    EXPECT_TRUE(wrapper.spirvCode.empty());

    std::filesystem::remove(path);
}

TEST(SpirvLoader, ValidMagicWordAlignedFileLoadsSuccessfully) {
    const auto path = TempPath("valid.spv");
    std::vector<uint32_t> words = {0x07230203u, 0x00010000u, 0, 4, 0};  // plausible minimal header
    std::vector<char> bytes(words.size() * sizeof(uint32_t));
    std::memcpy(bytes.data(), words.data(), bytes.size());
    WriteFile(path, bytes);

    CompileProbe probe;
    CashSystem::ShaderModuleWrapper wrapper;
    EXPECT_NO_THROW(probe.CompileShader(ParamsFor(path), wrapper));
    ASSERT_EQ(wrapper.spirvCode.size(), words.size());
    EXPECT_EQ(wrapper.spirvCode[0], 0x07230203u);

    std::filesystem::remove(path);
}

TEST(SpirvLoader, MissingFileFailsCleanly) {
    const auto path = TempPath("does_not_exist.spv");
    std::filesystem::remove(path);  // ensure absence

    CompileProbe probe;
    CashSystem::ShaderModuleWrapper wrapper;
    EXPECT_THROW(probe.CompileShader(ParamsFor(path), wrapper), std::exception);
}
