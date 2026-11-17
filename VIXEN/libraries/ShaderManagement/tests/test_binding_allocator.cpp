// R896: generated descriptor bindings. Pure data-level tests of the allocator that
// sdi_tool merge-variants runs before compiling a program's variants.
#include "BindingAllocator.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace ShaderManagement;

namespace {

class BindingAllocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = fs::temp_directory_path() /
                ("vixen-binding-allocator-" + std::to_string(stamp) + "-" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::create_directories(root_ / "Generated");
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    fs::path Write(const std::string& rel, const std::string& text) {
        const fs::path p = root_ / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << text;
        return p;
    }
    BindingAllocationResult Allocate(const std::string& program, const fs::path& source) {
        return AllocateProgramBindings(program, source, {root_});
    }
    fs::path root_;
};

SpirvDescriptorBinding Reflected(const std::string& name, uint32_t binding) {
    SpirvDescriptorBinding b{};
    b.set = 0;
    b.binding = binding;
    b.name = name;
    b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    return b;
}

} // namespace

TEST_F(BindingAllocatorTest, NamesAcrossIncludesAllocateDenselyInOrdinalOrder) {
    Write("Shared.glsl",
          "// a comment naming VXB_Ignored must not allocate\n"
          "layout(std430, binding = VXB_Zeta) readonly buffer Zeta { uint z[]; };\n"
          "#define COUNTERS_BINDING VXB_Counters\n"
          "/* VXB_AlsoIgnored */\n");
    const fs::path src = Write("Prog.comp",
          "#version 460\n#include \"Generated/Prog.bindings.g.glsl\"\n"
          "#include \"Shared.glsl\"\n"
          "#ifdef SOME_FEATURE\n"
          "layout(binding = VXB_alphaImage) uniform image2D alphaImage;\n"
          "#endif\n"
          "layout(std430, binding = VXB_Beta) buffer Beta { uint b[]; };\n");
    const auto result = Allocate("Prog", src);
    ASSERT_TRUE(result.success) << result.errorMessage;
    const auto& a = result.allocation;
    EXPECT_TRUE(a.migrated);
    // Ordinal (byte) order: upper case sorts before lower case.
    ASSERT_EQ(a.names, (std::vector<std::string>{"Beta", "Counters", "Zeta", "alphaImage"}));
    EXPECT_EQ(a.bindingOf.at("Beta"), 0u);
    EXPECT_EQ(a.bindingOf.at("alphaImage"), 3u);

    const std::string glsl = EmitProgramBindingsGlsl(a);
    EXPECT_NE(glsl.find("#define VXB_Counters 1\n"), std::string::npos);
    const std::string cpp = EmitProgramBindingsCpp(a);
    EXPECT_NE(cpp.find("namespace Prog {"), std::string::npos);
    EXPECT_NE(cpp.find("inline constexpr uint32_t Zeta = 2u;"), std::string::npos);
    EXPECT_NE(cpp.find("inline constexpr uint32_t COUNT = 4u;"), std::string::npos);
}

TEST_F(BindingAllocatorTest, AllocationIsIndependentOfDeclarationOrder) {
    Write("A.glsl", "layout(binding = VXB_Apple) buffer Apple { uint a[]; };\n");
    Write("B.glsl", "layout(binding = VXB_Banana) buffer Banana { uint b[]; };\n");
    const fs::path ab = Write("AB.comp", "#include \"Generated/AB.bindings.g.glsl\"\n#include \"A.glsl\"\n#include \"B.glsl\"\n");
    const fs::path ba = Write("BA.comp", "#include \"Generated/BA.bindings.g.glsl\"\n#include \"B.glsl\"\n#include \"A.glsl\"\n");
    const auto r1 = Allocate("AB", ab);
    const auto r2 = Allocate("BA", ba);
    ASSERT_TRUE(r1.success && r2.success);
    EXPECT_EQ(r1.allocation.names, r2.allocation.names);
    EXPECT_EQ(r1.allocation.bindingOf, r2.allocation.bindingOf);
}

TEST_F(BindingAllocatorTest, HandPickedLiteralInMigratedProgramIsLocatedError) {
    Write("Inc.glsl", "layout(std430, binding = 53) readonly buffer Tile { uint t[]; };\n");
    const fs::path src = Write("Prog.comp",
          "#include \"Generated/Prog.bindings.g.glsl\"\n#include \"Inc.glsl\"\n"
          "layout(binding = VXB_Out) buffer Out { uint o[]; };\n");
    const auto result = Allocate("Prog", src);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.errorMessage.find("Inc.glsl:1: R896: hand-picked binding number"), std::string::npos)
        << result.errorMessage;
}

TEST_F(BindingAllocatorTest, UnmigratedProgramKeepsLiteralsButCannotUseGeneratedNames) {
    const fs::path legacy = Write("Legacy.comp", "layout(binding = 0) buffer Out { uint o[]; };\n");
    const auto ok = Allocate("Legacy", legacy);
    ASSERT_TRUE(ok.success) << ok.errorMessage;
    EXPECT_FALSE(ok.allocation.migrated);
    EXPECT_EQ(ok.allocation.literals.size(), 1u);  // counted by the ratchet

    const fs::path half = Write("Half.comp", "layout(binding = VXB_Out) buffer Out { uint o[]; };\n");
    const auto bad = Allocate("Half", half);
    EXPECT_FALSE(bad.success);
    EXPECT_NE(bad.errorMessage.find("Generated/Half.bindings.g.glsl"), std::string::npos) << bad.errorMessage;
}

TEST_F(BindingAllocatorTest, AnotherProgramsBindingsFileIsRefused) {
    Write("Generated/Other.bindings.g.glsl", "#define VXB_Out 0\n");
    const fs::path src = Write("Prog.comp",
          "#include \"Generated/Prog.bindings.g.glsl\"\n#include \"Generated/Other.bindings.g.glsl\"\n");
    const auto result = Allocate("Prog", src);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.errorMessage.find("another program's bindings file"), std::string::npos);
}

TEST_F(BindingAllocatorTest, ReflectionMustMatchTheAllocation) {
    const fs::path src = Write("Prog.comp",
          "#include \"Generated/Prog.bindings.g.glsl\"\n"
          "layout(binding = VXB_Tape) buffer Tape { uint t[]; };\n"
          "layout(binding = VXB_Tile) buffer Tile { uint t[]; };\n");
    const auto result = Allocate("Prog", src);
    ASSERT_TRUE(result.success) << result.errorMessage;

    SdiMergedInterface merged;
    merged.bindings.push_back({Reflected("Tape", 0), {}});
    merged.bindings.push_back({Reflected("Tile", 1), {}});
    EXPECT_EQ(CheckReflectionAgainstAllocation(result.allocation, merged), "");

    // The 53/54 class: a resource reaching a number through another resource's name.
    SdiMergedInterface aliased;
    aliased.bindings.push_back({Reflected("Tape", 1), {}});
    aliased.bindings.push_back({Reflected("Tile", 1), {}});
    EXPECT_NE(CheckReflectionAgainstAllocation(result.allocation, aliased).find("'Tape' reflects at binding 1"),
              std::string::npos);

    // A resource the shader declares with a literal or an injected line.
    SdiMergedInterface undeclared;
    undeclared.bindings.push_back({Reflected("Injected", 53), {}});
    EXPECT_NE(CheckReflectionAgainstAllocation(result.allocation, undeclared).find("not declared through VXB_Injected"),
              std::string::npos);
}
