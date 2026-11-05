#include "ScopedEnvironment.h"
#include <gtest/gtest.h>

#include "VulkanGlobalNames.h"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

#if defined(__linux__)
namespace {

class ScopedEnvironment {
public:
    explicit ScopedEnvironment(const char* name) : name_(name) {
        if (const char* value = std::getenv(name_); value != nullptr) {
            hadValue_ = true;
            value_ = value;
        }
    }

    ~ScopedEnvironment() {
        if (hadValue_) {
            Vixen::Core::Env::Set(name_, value_);
        } else {
            Vixen::Core::Env::Unset(name_);
        }
    }

    void Unset() { Vixen::Core::Env::Unset(name_); }
    void Set(const char* value) { Vixen::Core::Env::Set(name_, value); }

private:
    const char* name_;
    bool hadValue_ = false;
    std::string value_;
};

TEST(VulkanGlobalNamesPolicy, MissingDozenManifestThrowsByDefault) {
    ASSERT_TRUE(std::filesystem::exists("/dev/dxg")) << "WSL GPU device is required for this witness";
    ASSERT_FALSE(std::filesystem::exists(VIXEN_WSL_DZN_ICD));

    ScopedEnvironment icd("VK_ICD_FILENAMES");
    ScopedEnvironment allowSoftware("VIXEN_ALLOW_SOFTWARE_VULKAN");
    icd.Unset();
    allowSoftware.Unset();

    std::string error;
    try {
        VixenSelectWslGpuIcd();
    } catch (const std::runtime_error& exception) {
        error = exception.what();
    }

    ASSERT_FALSE(error.empty());
    EXPECT_NE(error.find("VK_ICD_FILENAMES"), std::string::npos);
    EXPECT_NE(error.find(VIXEN_WSL_DZN_ICD), std::string::npos);
    EXPECT_NE(error.find("VIXEN_ALLOW_SOFTWARE_VULKAN=1"), std::string::npos);
}

TEST(VulkanGlobalNamesPolicy, SoftwareOptInAllowsMissingDozenManifest) {
    ASSERT_TRUE(std::filesystem::exists("/dev/dxg")) << "WSL GPU device is required for this witness";
    ASSERT_FALSE(std::filesystem::exists(VIXEN_WSL_DZN_ICD));

    ScopedEnvironment icd("VK_ICD_FILENAMES");
    ScopedEnvironment allowSoftware("VIXEN_ALLOW_SOFTWARE_VULKAN");
    icd.Unset();
    allowSoftware.Set("1");

    EXPECT_NO_THROW(VixenSelectWslGpuIcd());
    EXPECT_EQ(std::getenv("VK_ICD_FILENAMES"), nullptr);
}

TEST(VulkanGlobalNamesPolicy, ExplicitIcdIsHonouredWithoutOptIn) {
    ASSERT_TRUE(std::filesystem::exists("/dev/dxg")) << "WSL GPU device is required for this witness";

    ScopedEnvironment icd("VK_ICD_FILENAMES");
    ScopedEnvironment allowSoftware("VIXEN_ALLOW_SOFTWARE_VULKAN");
    icd.Set("/tmp/explicit-vulkan-icd.json");
    allowSoftware.Unset();

    EXPECT_NO_THROW(VixenSelectWslGpuIcd());
    ASSERT_NE(std::getenv("VK_ICD_FILENAMES"), nullptr);
    EXPECT_STREQ(std::getenv("VK_ICD_FILENAMES"), "/tmp/explicit-vulkan-icd.json");
}

}  // namespace
#else
TEST(VulkanGlobalNamesPolicy, WslPolicyIsLinuxOnly) {
    GTEST_SKIP() << "VixenSelectWslGpuIcd is a no-op off Linux/WSL";
}
#endif
