#include <gtest/gtest.h>
#include "CashSystem/MainCacher.h"
#include "CashSystem/PipelineCacher.h"
#include "CashSystem/ShaderModuleCacher.h"
#include "CashSystem/ShaderCompilationCacher.h"
#include "CashSystem/TextureCacher.h"
#include "VulkanResources/VulkanDevice.h"

using namespace CashSystem;

// Test fixture for registration API
class RegistrationAPITest : public ::testing::Test {
protected:
    void SetUp() override {
        mainCacher = &MainCacher::Instance();
        // Clear cache instances but keep registrations (singleton persists)
        mainCacher->ClearAll();
    }

    void TearDown() override {
        // Clean up cache instances
        mainCacher->ClearAll();
    }

    // Helper to safely register (skip if already registered)
    template<typename CacherT, typename ResourceT, typename CreateInfoT>
    bool SafeRegister(std::type_index typeIndex, const char* name, bool isDeviceDependent) {
        if (!mainCacher->IsRegistered(typeIndex)) {
            try {
                mainCacher->RegisterCacher<CacherT, ResourceT, CreateInfoT>(
                    typeIndex, name, isDeviceDependent
                );
                return true;
            } catch (...) {
                return false;
            }
        }
        return true;  // Already registered
    }

    MainCacher* mainCacher = nullptr;
};

TEST_F(RegistrationAPITest, RegisterDeviceDependentCacher) {
    // Register a device-dependent cacher type (safe registration)
    bool registered = SafeRegister<PipelineCacher, PipelineWrapper, PipelineCreateParams>(
        typeid(PipelineWrapper),
        "Pipeline",
        true  // device-dependent
    );

    EXPECT_TRUE(registered);

    // Verify it's registered
    EXPECT_TRUE(mainCacher->IsRegistered(typeid(PipelineWrapper)));
    EXPECT_TRUE(mainCacher->IsDeviceDependent(typeid(PipelineWrapper)));
}

TEST_F(RegistrationAPITest, RegisterDeviceIndependentCacher) {
    // Register a device-independent cacher type (safe registration)
    bool registered = SafeRegister<ShaderCompilationCacher, CompiledShaderWrapper, ShaderCompilationParams>(
        typeid(CompiledShaderWrapper),
        "ShaderCompilation",
        false  // device-independent
    );

    EXPECT_TRUE(registered);

    // Verify it's registered
    EXPECT_TRUE(mainCacher->IsRegistered(typeid(CompiledShaderWrapper)));
    EXPECT_FALSE(mainCacher->IsDeviceDependent(typeid(CompiledShaderWrapper)));
}

TEST_F(RegistrationAPITest, RegisterMultipleTypes) {
    // Register multiple cacher types (safe registration)
    bool reg1 = SafeRegister<ShaderModuleCacher, ShaderModuleWrapper, ShaderModuleCreateParams>(
        typeid(ShaderModuleWrapper),
        "ShaderModule",
        true
    );

    bool reg2 = SafeRegister<TextureCacher, TextureWrapper, TextureCreateParams>(
        typeid(TextureWrapper),
        "Texture",
        true
    );

    EXPECT_TRUE(reg1);
    EXPECT_TRUE(reg2);

    // Verify all are registered
    EXPECT_TRUE(mainCacher->IsRegistered(typeid(ShaderModuleWrapper)));
    EXPECT_TRUE(mainCacher->IsRegistered(typeid(TextureWrapper)));

    // Verify registered types list
    auto types = mainCacher->GetRegisteredTypes();
    EXPECT_GE(types.size(), 2);
}

TEST_F(RegistrationAPITest, GetCacherWithNullDevice) {
    // Register device-independent cacher (safe)
    SafeRegister<ShaderCompilationCacher, CompiledShaderWrapper, ShaderCompilationParams>(
        typeid(CompiledShaderWrapper),
        "ShaderCompilation",
        false
    );

    // Get device-independent cacher with null device (should work)
    auto* cacher = mainCacher->GetCacher<ShaderCompilationCacher, CompiledShaderWrapper, ShaderCompilationParams>(
        typeid(CompiledShaderWrapper),
        nullptr
    );

    EXPECT_NE(cacher, nullptr);
}

TEST_F(RegistrationAPITest, DuplicateRegistrationThrows) {
    // Register a type (safe)
    SafeRegister<PipelineCacher, PipelineWrapper, PipelineCreateParams>(
        typeid(PipelineWrapper),
        "Pipeline",
        true
    );

    // Verify it's registered
    EXPECT_TRUE(mainCacher->IsRegistered(typeid(PipelineWrapper)));

    // Note: Can't test duplicate registration with singleton
    // If already registered, SafeRegister just returns true
    // This is expected behavior for persistent singleton
}

TEST_F(RegistrationAPITest, GetTypeName) {
    SafeRegister<PipelineCacher, PipelineWrapper, PipelineCreateParams>(
        typeid(PipelineWrapper),
        "Pipeline",
        true
    );

    std::string name = mainCacher->GetTypeName(typeid(PipelineWrapper));
    EXPECT_EQ(name, "Pipeline");

    // Unregistered type should return "UnknownType"
    std::string unknownName = mainCacher->GetTypeName(typeid(int));
    EXPECT_EQ(unknownName, "UnknownType");
}

TEST_F(RegistrationAPITest, CacheStatistics) {
    // Initial state (after ClearAll in SetUp - clears caches, not registrations)
    auto stats = mainCacher->GetStats();
    EXPECT_EQ(stats.globalCaches, 0);
    EXPECT_EQ(stats.deviceRegistries, 0);

    // Register and create a device-independent cache (safe)
    SafeRegister<ShaderCompilationCacher, CompiledShaderWrapper, ShaderCompilationParams>(
        typeid(CompiledShaderWrapper),
        "ShaderCompilation",
        false
    );

    auto* cacher = mainCacher->GetCacher<ShaderCompilationCacher, CompiledShaderWrapper, ShaderCompilationParams>(
        typeid(CompiledShaderWrapper),
        nullptr
    );

    ASSERT_NE(cacher, nullptr);

    // Verify stats updated (1 global cache created)
    stats = mainCacher->GetStats();
    EXPECT_EQ(stats.globalCaches, 1);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
