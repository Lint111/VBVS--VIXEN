// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include <gtest/gtest.h>

#include "Core/ShaderSourceDir.h"
#include "ScopedEnvironment.h"

#include <filesystem>

using Vixen::RenderGraph::ResolveShaderSourceDir;

namespace {
constexpr const char* kVar = "VIXEN_SHADER_SOURCE_DIR_OVERRIDE";

}  // namespace

TEST(ShaderSourceDir, UsesCompiledDirWhenNoOverride) {
    Vixen::Core::Env::ScopedEnvironment guard(kVar, nullptr);
    EXPECT_EQ(ResolveShaderSourceDir("/compiled/shaders"), "/compiled/shaders");
}

TEST(ShaderSourceDir, EmptyOverrideIsIgnored) {
    Vixen::Core::Env::ScopedEnvironment guard(kVar, "");
    EXPECT_EQ(ResolveShaderSourceDir("/compiled/shaders"), "/compiled/shaders");
}

TEST(ShaderSourceDir, ExistingOverrideWins) {
    const auto dir = std::filesystem::temp_directory_path();
    Vixen::Core::Env::ScopedEnvironment guard(kVar, dir.c_str());
    EXPECT_EQ(ResolveShaderSourceDir("/compiled/shaders"), dir.string());
}

TEST(ShaderSourceDir, MissingOverrideDirectoryThrowsWithFix) {
    Vixen::Core::Env::ScopedEnvironment guard(kVar, "/nonexistent/vixen/shaders");
    try {
        ResolveShaderSourceDir("/compiled/shaders");
        FAIL() << "expected a throw";
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("/nonexistent/vixen/shaders"), std::string::npos);
        EXPECT_NE(msg.find("Fix:"), std::string::npos);
    }
}
