#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace Vixen::App {

// Where the app writes its runtime artifacts (app log, calibration store). Relative paths resolve
// against the process cwd, which for an embedding launcher is the source root, so artifacts leaked
// into the checkout (T-4950). Instead: VIXEN_RUN_DATA_DIR when set, else the running executable's
// directory (the build output dir).
inline std::filesystem::path ExecutableDirectory() {
    std::error_code ec;
#if defined(_WIN32)
    wchar_t buf[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    if (n > 0 && n < sizeof(buf) / sizeof(buf[0])) return std::filesystem::path(buf).parent_path();
#else
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) return exe.parent_path();
#endif
    return std::filesystem::current_path(ec);
}

inline std::filesystem::path ResolveRunDataDir(const char* envValue, const std::filesystem::path& exeDir) {
    if (envValue != nullptr && *envValue != '\0') return std::filesystem::path(envValue);
    return exeDir;
}

inline std::filesystem::path RunDataDir() {
    return ResolveRunDataDir(std::getenv("VIXEN_RUN_DATA_DIR"), ExecutableDirectory());
}

inline std::filesystem::path AppLogPath(const std::filesystem::path& runDataDir) {
    return runDataDir / "vulkan_app_log.txt";
}

inline std::filesystem::path CalibrationDir(const std::filesystem::path& runDataDir) {
    return runDataDir / "calibration";
}

}  // namespace Vixen::App
