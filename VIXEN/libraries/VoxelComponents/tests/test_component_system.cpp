#include <gtest/gtest.h>
#include "VoxelComponents.h"
#include <glm/glm.hpp>
#include <algorithm>

using namespace Vixen::GaiaVoxel;

// ===========================================================================
// Macro Component Registry Tests
// ===========================================================================

TEST(ComponentSystemTest, MacroComponentRegistry_AllComponentsAccessible) {
    // Verify all 12 components registered via FOR_EACH_COMPONENT macro
    size_t componentCount = 0;
    std::vector<std::string> names;

    ComponentRegistry::visitAll([&](auto component) {
        using Component = std::decay_t<decltype(component)>;
        names.push_back(Component::Name);
        componentCount++;
    });

    // FOR_EACH_COMPONENT: 7 Value types + 5 Ref types = 12 total
    EXPECT_EQ(componentCount, 12);

    // Verify Value-type component names
    EXPECT_TRUE(std::find(names.begin(), names.end(), "density") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "material") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "emission_intensity") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "color") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "normal") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "emission") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "position") != names.end()); // MortonKey

    // Verify Ref-type component names
    EXPECT_TRUE(std::find(names.begin(), names.end(), "transform") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "aabb") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "volume") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "volume_grid") != names.end());
}

TEST(ComponentSystemTest, MacroComponentRegistry_ValueComponents) {
    // Verify Value-type components are accessible via visitValueComponents
    size_t componentCount = 0;
    std::vector<std::string> names;

    ComponentRegistry::visitValueComponents([&](auto component) {
        using Component = std::decay_t<decltype(component)>;
        names.push_back(Component::Name);
        componentCount++;
    });

    // FOR_EACH_VALUE_COMPONENT: 7 Value types
    EXPECT_EQ(componentCount, 7);

    EXPECT_TRUE(std::find(names.begin(), names.end(), "density") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "position") != names.end()); // MortonKey
}

TEST(ComponentSystemTest, ComponentRegistry_VisitByName) {
    // Test string-based component lookup
    bool foundDensity = ComponentRegistry::visitByName("density", [](auto component) {
        using Component = std::decay_t<decltype(component)>;
        EXPECT_STREQ(Component::Name, "density");
    });
    EXPECT_TRUE(foundDensity);

    bool foundColor = ComponentRegistry::visitByName("color", [](auto component) {
        using Component = std::decay_t<decltype(component)>;
        EXPECT_STREQ(Component::Name, "color");
    });
    EXPECT_TRUE(foundColor);

    bool foundInvalid = ComponentRegistry::visitByName("invalid_name", [](auto) {
        FAIL() << "Should not call visitor for invalid component";
    });
    EXPECT_FALSE(foundInvalid);
}

// ===========================================================================
// ComponentVariant Tests
// ===========================================================================

TEST(ComponentSystemTest, ComponentVariant_TypeSafety) {
    // Verify ComponentVariant can hold any component type
    ComponentVariant v1 = Density{0.5f};
    ComponentVariant v2 = Color{glm::vec3(1, 0, 0)};
    ComponentVariant v3 = Material{100};
    ComponentVariant v4 = Normal{glm::vec3(0, 0, 1)};

    // Type checking
    EXPECT_TRUE(std::holds_alternative<Density>(v1));
    EXPECT_TRUE(std::holds_alternative<Color>(v2));
    EXPECT_TRUE(std::holds_alternative<Material>(v3));
    EXPECT_TRUE(std::holds_alternative<Normal>(v4));

    // Value extraction
    EXPECT_FLOAT_EQ(std::get<Density>(v1).value, 0.5f);
    EXPECT_EQ(std::get<Color>(v2).toVec3(), glm::vec3(1, 0, 0));
    EXPECT_EQ(std::get<Material>(v3).value, 100u);
    EXPECT_EQ(std::get<Normal>(v4).toVec3(), glm::vec3(0, 0, 1));
}

// ===========================================================================
// MortonKey Tests
// ===========================================================================

TEST(ComponentSystemTest, MortonKey_EncodeDecodeRoundtrip) {
    // Morton encoding floors to integer grid
    glm::vec3 originalPos(10.5f, 20.3f, 30.7f);

    uint64_t code = MortonKeyUtils::encode(originalPos);
    EXPECT_NE(code, 0);

    glm::vec3 decodedPos = MortonKeyUtils::toWorldPos(code);

    // Verify floored to integer grid
    EXPECT_EQ(decodedPos, glm::vec3(10.0f, 20.0f, 30.0f));

    // Exact round-trip with integer positions
    glm::vec3 intPos(10.0f, 20.0f, 30.0f);
    uint64_t code2 = MortonKeyUtils::encode(intPos);
    glm::vec3 decoded2 = MortonKeyUtils::toWorldPos(code2);
    EXPECT_EQ(decoded2, intPos);
}

TEST(ComponentSystemTest, MortonKey_GridPositionRoundtrip) {
    // Integer grid position exact round-trip
    glm::ivec3 gridPos(100, 200, 300);

    uint64_t code = MortonKeyUtils::encode(gridPos);
    glm::ivec3 decoded = MortonKeyUtils::decode(code);

    EXPECT_EQ(decoded, gridPos);
}

// ===========================================================================
// Vec3 Component Tests
// ===========================================================================

TEST(ComponentSystemTest, Vec3Components_GlmConversion) {
    // Color conversion
    Color color(glm::vec3(0.8f, 0.2f, 0.5f));
    EXPECT_EQ(color.toVec3(), glm::vec3(0.8f, 0.2f, 0.5f));
    EXPECT_FLOAT_EQ(color.r, 0.8f);
    EXPECT_FLOAT_EQ(color.g, 0.2f);
    EXPECT_FLOAT_EQ(color.b, 0.5f);

    // Normal conversion
    Normal normal(glm::vec3(0, 1, 0));
    EXPECT_EQ(normal.toVec3(), glm::vec3(0, 1, 0));
    EXPECT_FLOAT_EQ(normal.x, 0.0f);
    EXPECT_FLOAT_EQ(normal.y, 1.0f);
    EXPECT_FLOAT_EQ(normal.z, 0.0f);

    // Implicit conversion operator
    glm::vec3 colorVec = color;
    glm::vec3 normalVec = normal;
    EXPECT_EQ(colorVec, glm::vec3(0.8f, 0.2f, 0.5f));
    EXPECT_EQ(normalVec, glm::vec3(0, 1, 0));
}

// ===========================================================================
// Scalar Component Tests
// ===========================================================================

TEST(ComponentSystemTest, ScalarComponents_DefaultValues) {
    // Test default values match spec
    Density density;
    EXPECT_FLOAT_EQ(density.value, 1.0f);

    Material material;
    EXPECT_EQ(material.value, 0u);

    EmissionIntensity emissionIntensity;
    EXPECT_FLOAT_EQ(emissionIntensity.value, 0.0f);
}

TEST(ComponentSystemTest, ScalarComponents_CustomValues) {
    // Test custom initialization
    Density density{0.5f};
    EXPECT_FLOAT_EQ(density.value, 0.5f);

    Material material{42};
    EXPECT_EQ(material.value, 42u);

    EmissionIntensity emissionIntensity{1.5f};
    EXPECT_FLOAT_EQ(emissionIntensity.value, 1.5f);
}
