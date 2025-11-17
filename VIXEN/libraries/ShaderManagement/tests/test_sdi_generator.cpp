#include <gtest/gtest.h>
#include "ShaderManagement/SdiRegistryManager.h"

using namespace ShaderManagement;

// SDI registry tests
TEST(SdiRegistryTest, RegisterShader) {
    SdiRegistryManager::Config config;
    config.sdiDirectory = "./test_sdi";
    config.namespacePrefix = "TestSDI";

    SdiRegistryManager registry(config);

    SdiRegistryEntry entry;
    entry.uuid = "test-uuid-123";
    entry.programName = "TestShader";
    entry.aliasName = "MyShader";
    entry.sdiHeaderPath = "./test.h";
    entry.sdiNamespace = "TestSDI::MyShader";

    bool registered = registry.RegisterShader(entry);
    EXPECT_TRUE(registered);
}

TEST(SdiRegistryTest, GetRegisteredShader) {
    SdiRegistryManager::Config config;
    config.sdiDirectory = "./test_sdi";

    SdiRegistryManager registry(config);

    SdiRegistryEntry entry;
    entry.uuid = "test-uuid-456";
    entry.programName = "TestShader2";

    registry.RegisterShader(entry);

    auto retrieved = registry.GetEntry("test-uuid-456");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->uuid, "test-uuid-456");
    EXPECT_EQ(retrieved->programName, "TestShader2");
}

TEST(SdiRegistryTest, NonExistentShader) {
    SdiRegistryManager::Config config;
    config.sdiDirectory = "./test_sdi";

    SdiRegistryManager registry(config);

    auto result = registry.GetEntry("non-existent-uuid");
    EXPECT_FALSE(result.has_value());
}
