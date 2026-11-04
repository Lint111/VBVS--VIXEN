// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include <gtest/gtest.h>

#include "Core/ShaderSourceDir.h"

#include <cstdlib>
#include <filesystem>

using Vixen::RenderGraph::ResolveShaderSourceDir;

namespace {
constexpr const char* kVar = "VIXEN_SHADER_SOURCE_DIR_OVERRIDE";

struct EnvGuard {
    ~EnvGuard() { unsetenv(kVar); }
};
}  // namespace

TEST(ShaderSourceDir, UsesCompiledDirWhenNoOverride) {
    EnvGuard guard;
    unsetenv(kVar);
    EXPECT_EQ(ResolveShaderSourceDir("/compiled/shaders"), "/compiled/shaders");
}

TEST(ShaderSourceDir, EmptyOverrideIsIgnored) {
    EnvGuard guard;
    setenv(kVar, "", 1);
    EXPECT_EQ(ResolveShaderSourceDir("/compiled/shaders"), "/compiled/shaders");
}

TEST(ShaderSourceDir, ExistingOverrideWins) {
    EnvGuard guard;
    const auto dir = std::filesystem::temp_directory_path();
    setenv(kVar, dir.c_str(), 1);
    EXPECT_EQ(ResolveShaderSourceDir("/compiled/shaders"), dir.string());
}

TEST(ShaderSourceDir, MissingOverrideDirectoryThrowsWithFix) {
    EnvGuard guard;
    setenv(kVar, "/nonexistent/vixen/shaders", 1);
    try {
        ResolveShaderSourceDir("/compiled/shaders");
        FAIL() << "expected a throw";
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("/nonexistent/vixen/shaders"), std::string::npos);
        EXPECT_NE(msg.find("Fix:"), std::string::npos);
    }
}
