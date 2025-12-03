#include <gtest/gtest.h>
#include "SVOTypes.h"

using namespace Vixen::SVO;

// ===========================================================================
// ChildDescriptor Tests
// ===========================================================================

TEST(ChildDescriptorTest, DefaultInitialization) {
    ChildDescriptor desc{};

    EXPECT_EQ(desc.childPointer, 0);
    EXPECT_EQ(desc.farBit, 0);
    EXPECT_EQ(desc.validMask, 0);
    EXPECT_EQ(desc.leafMask, 0);
    EXPECT_EQ(desc.contourPointer, 0);
    EXPECT_EQ(desc.contourMask, 0);
}

TEST(ChildDescriptorTest, HasChild) {
    ChildDescriptor desc{};
    desc.validMask = 0b10101010;  // Children 1, 3, 5, 7

    EXPECT_FALSE(desc.hasChild(0));
    EXPECT_TRUE(desc.hasChild(1));
    EXPECT_FALSE(desc.hasChild(2));
    EXPECT_TRUE(desc.hasChild(3));
    EXPECT_TRUE(desc.hasChild(7));
}

TEST(ChildDescriptorTest, IsLeaf) {
    ChildDescriptor desc{};
    desc.leafMask = 0b00001111;  // First 4 are leaves

    EXPECT_TRUE(desc.isLeaf(0));
    EXPECT_TRUE(desc.isLeaf(3));
    EXPECT_FALSE(desc.isLeaf(4));
    EXPECT_FALSE(desc.isLeaf(7));
}

TEST(ChildDescriptorTest, GetChildCount) {
    ChildDescriptor desc{};
    desc.validMask = 0b10101010;  // 4 children

    EXPECT_EQ(desc.getChildCount(), 4);

    desc.validMask = 0b11111111;  // 8 children
    EXPECT_EQ(desc.getChildCount(), 8);

    desc.validMask = 0;
    EXPECT_EQ(desc.getChildCount(), 0);
}

TEST(ChildDescriptorTest, BitfieldSizes) {
    // Verify bitfield packing is correct (64 bits total)
    EXPECT_EQ(sizeof(ChildDescriptor), 8);
}

// ===========================================================================
// Contour Tests
// ===========================================================================

TEST(ContourTest, EncodeDecode) {
    glm::vec3 normal(0.0f, 1.0f, 0.0f);  // Y-up
    float thickness = 0.5f;
    float position = 0.25f;

    Contour contour = makeContour(normal, position, thickness);  // Correct order: normal, centerPos, thickness

    glm::vec3 decodedNormal = decodeContourNormal(contour);
    float decodedThickness = decodeContourThickness(contour);
    float decodedPosition = decodeContourPosition(contour);

    // Allow small precision loss from 7-bit encoding
    EXPECT_NEAR(decodedNormal.x, normal.x, 0.05f);
    EXPECT_NEAR(decodedNormal.y, normal.y, 0.05f);
    EXPECT_NEAR(decodedNormal.z, normal.z, 0.05f);
    EXPECT_NEAR(decodedThickness, thickness, 0.01f);
    EXPECT_NEAR(decodedPosition, position, 0.01f);
}

TEST(ContourTest, NormalVectors) {
    // Test various normal directions
    std::vector<glm::vec3> normals = {
        glm::vec3(1, 0, 0),
        glm::vec3(0, 1, 0),
        glm::vec3(0, 0, 1),
        glm::normalize(glm::vec3(1, 1, 1)),
        glm::normalize(glm::vec3(-1, 0.5f, 0.2f))
    };

    for (const auto& normal : normals) {
        Contour contour = makeContour(normal, 0.5f, 0.0f);
        glm::vec3 decoded = decodeContourNormal(contour);

        // Should preserve direction (allow some precision loss)
        float dot = glm::dot(glm::normalize(decoded), glm::normalize(normal));
        EXPECT_GT(dot, 0.95f) << "Normal direction lost: "
                              << normal.x << "," << normal.y << "," << normal.z;
    }
}

// ===========================================================================
// Attribute Encoding Tests
// ===========================================================================

TEST(AttributeTest, UncompressedSize) {
    EXPECT_EQ(sizeof(UncompressedAttributes), 8);
}

TEST(AttributeTest, MakeAttributes) {
    glm::vec3 color(1.0f, 0.5f, 0.25f);
    glm::vec3 normal(0.0f, 1.0f, 0.0f);

    UncompressedAttributes attr = makeAttributes(color, normal);

    // Check color fields directly
    EXPECT_NEAR(attr.red / 255.0f, color.r, 0.01f);
    EXPECT_NEAR(attr.green / 255.0f, color.g, 0.01f);
    EXPECT_NEAR(attr.blue / 255.0f, color.b, 0.01f);
    EXPECT_EQ(attr.alpha, 255);

    // Check that getColor() method works
    glm::vec3 decodedColor = attr.getColor();
    EXPECT_NEAR(decodedColor.r, color.r, 0.01f);
    EXPECT_NEAR(decodedColor.g, color.g, 0.01f);
    EXPECT_NEAR(decodedColor.b, color.b, 0.01f);
}

// ===========================================================================
// BuildParams Tests
// ===========================================================================

TEST(BuildParamsTest, DefaultValues) {
    BuildParams params;

    EXPECT_EQ(params.maxLevels, 16);
    EXPECT_TRUE(params.enableContours);
    EXPECT_TRUE(params.enableCompression);
    EXPECT_GT(params.geometryErrorThreshold, 0.0f);
    EXPECT_GT(params.colorErrorThreshold, 0.0f);
}
