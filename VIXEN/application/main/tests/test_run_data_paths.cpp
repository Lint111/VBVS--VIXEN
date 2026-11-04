/**
 * @file test_run_data_paths.cpp
 * @brief T-4950: the app log and calibration store live under the run-data dir (VIXEN_RUN_DATA_DIR,
 * else the executable's directory), never a cwd-relative path that leaks into the source root.
 */
#include <gtest/gtest.h>

#include "RunDataPaths.h"

using namespace Vixen::App;
namespace fs = std::filesystem;

TEST(RunDataPaths, EnvOverridesExecutableDirectory) {
    EXPECT_EQ(ResolveRunDataDir("/run/data", "/build/bin"), fs::path("/run/data"));
}

TEST(RunDataPaths, FallsBackToExecutableDirectoryWhenEnvUnsetOrEmpty) {
    EXPECT_EQ(ResolveRunDataDir(nullptr, "/build/bin"), fs::path("/build/bin"));
    EXPECT_EQ(ResolveRunDataDir("", "/build/bin"), fs::path("/build/bin"));
}

TEST(RunDataPaths, LogAndCalibrationAreRootedUnderRunDataDir) {
    const fs::path root = "/run/data";
    EXPECT_EQ(AppLogPath(root), root / "vulkan_app_log.txt");
    EXPECT_EQ(CalibrationDir(root), root / "calibration");
    EXPECT_TRUE(AppLogPath(root).is_absolute());
}

TEST(RunDataPaths, ExecutableDirectoryIsAbsoluteAndExists) {
    const fs::path dir = ExecutableDirectory();
    EXPECT_TRUE(dir.is_absolute());
    EXPECT_TRUE(fs::is_directory(dir));
}
