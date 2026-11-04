// UiTestAssets.h — UI test targets stage assets/ui next to their executable (CMake POST_BUILD) and
// RmlUi resolves "assets/ui/..." against the process working directory. ctest runs from the
// executable directory, but invoking the binary from the repository root made document loads fail.
// Call EnterExecutableDirectory() in SetUp so the binary works from any working directory.
#pragma once

#include <filesystem>
#include <system_error>

namespace Vixen::TestSupport {

// Returns false (and leaves the working directory alone) if the executable path cannot be resolved.
inline bool EnterExecutableDirectory() {
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return false;  // non-Linux: ctest already starts in the executable directory
    std::filesystem::current_path(exe.parent_path(), ec);
    return !ec;
}

}  // namespace Vixen::TestSupport
