// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace Vixen::RenderGraph {

// Shader source directory for runtime shader compilation. `compiledDir` is the build's
// VIXEN_SHADER_SOURCE_DIR. When VIXEN_SHADER_SOURCE_DIR_OVERRIDE is set to an existing
// directory it wins, so a before/after (A/B) capture can point one process at a pristine copy
// of the shaders without swapping tracked files under concurrent GPU runs in the same tree.
// A set-but-invalid override throws instead of silently falling back to the compiled path.
inline std::string ResolveShaderSourceDir(const char* compiledDir) {
    const char* override_ = std::getenv("VIXEN_SHADER_SOURCE_DIR_OVERRIDE");
    if (!override_ || !*override_) return compiledDir;
    std::error_code ec;
    if (!std::filesystem::is_directory(override_, ec)) {
        throw std::runtime_error(
            std::string("VIXEN_SHADER_SOURCE_DIR_OVERRIDE='") + override_ +
            "' is not an existing directory, so shader sources cannot be loaded from it. "
            "Fix: point it at a directory holding the shaders (e.g. a git worktree's "
            "VIXEN/shaders copy) or unset it to use the compiled-in " + compiledDir);
    }
    static const bool logged = [&] {
        std::fprintf(stderr, "[shaders] VIXEN_SHADER_SOURCE_DIR_OVERRIDE active: %s (compiled-in: %s)\n",
                     override_, compiledDir);
        return true;
    }();
    (void)logged;
    return override_;
}

}  // namespace Vixen::RenderGraph
