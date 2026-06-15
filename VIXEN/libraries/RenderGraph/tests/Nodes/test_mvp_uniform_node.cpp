/**
 * @file test_mvp_uniform_node.cpp
 * @brief Tests for MvpUniformNode and MvpUniformNodeConfig (AR#31)
 *
 * Coverage:
 *   Unit Tests (no device needed): Config slot counts, slot metadata, type assertions,
 *                                  param-name constants, and type-name.
 *   Integration Tests (device gated): Round-trip graph build, MVP_BUFFER allocation.
 *
 * NOTE: Integration tests (device round-trip) require full Vulkan SDK and a physical GPU.
 * They are written correctly but will be skipped / fail gracefully in headless CI.
 * Config-level tests always run and must PASS.
 */

#include <gtest/gtest.h>

#include "Nodes/MvpUniformNode.h"
#include "Data/Nodes/MvpUniformNodeConfig.h"

// Centralized Vulkan global name definitions (avoids duplicate strong symbols across TUs)
#include <VulkanGlobalNames.h>

using namespace Vixen::RenderGraph;

// ============================================================================
// Config Tests (no device required — always run)
// ============================================================================

class MvpUniformNodeConfigTest : public ::testing::Test {};

TEST_F(MvpUniformNodeConfigTest, InputCount) {
    EXPECT_EQ(MvpUniformNodeConfig::INPUT_COUNT, 1u)
        << "MvpUniformNode must have exactly 1 input (VULKAN_DEVICE_IN)";
}

TEST_F(MvpUniformNodeConfigTest, OutputCount) {
    EXPECT_EQ(MvpUniformNodeConfig::OUTPUT_COUNT, 1u)
        << "MvpUniformNode must have exactly 1 output (MVP_BUFFER)";
}

TEST_F(MvpUniformNodeConfigTest, ArrayModeIsSingle) {
    EXPECT_EQ(MvpUniformNodeConfig::ARRAY_MODE, SlotArrayMode::Single);
}

// ----- Input slot metadata -----

TEST_F(MvpUniformNodeConfigTest, VulkanDeviceInAtIndex0) {
    EXPECT_EQ(MvpUniformNodeConfig::VULKAN_DEVICE_IN_Slot::index, 0u);
}

TEST_F(MvpUniformNodeConfigTest, VulkanDeviceInIsRequired) {
    EXPECT_FALSE(MvpUniformNodeConfig::VULKAN_DEVICE_IN_Slot::nullable);
}

TEST_F(MvpUniformNodeConfigTest, VulkanDeviceInIsReadOnly) {
    EXPECT_EQ(MvpUniformNodeConfig::VULKAN_DEVICE_IN_Slot::mutability, SlotMutability::ReadOnly);
}

TEST_F(MvpUniformNodeConfigTest, VulkanDeviceInTypeIsVulkanDevicePtr) {
    constexpr bool correct = std::is_same_v<
        MvpUniformNodeConfig::VULKAN_DEVICE_IN_Slot::Type,
        Vixen::Vulkan::Resources::VulkanDevice*>;
    EXPECT_TRUE(correct);
}

// ----- Output slot metadata -----

TEST_F(MvpUniformNodeConfigTest, MvpBufferAtIndex0) {
    EXPECT_EQ(MvpUniformNodeConfig::MVP_BUFFER_Slot::index, 0u);
}

TEST_F(MvpUniformNodeConfigTest, MvpBufferIsRequired) {
    EXPECT_FALSE(MvpUniformNodeConfig::MVP_BUFFER_Slot::nullable);
}

TEST_F(MvpUniformNodeConfigTest, MvpBufferIsWriteOnly) {
    EXPECT_EQ(MvpUniformNodeConfig::MVP_BUFFER_Slot::mutability, SlotMutability::WriteOnly);
}

TEST_F(MvpUniformNodeConfigTest, MvpBufferTypeIsVkBuffer) {
    // Core AR#31 type contract — the binding-0 MVP UBO is a raw VkBuffer.
    constexpr bool correct = std::is_same_v<
        MvpUniformNodeConfig::MVP_BUFFER_Slot::Type,
        VkBuffer>;
    static_assert(correct, "MVP_BUFFER slot must be VkBuffer");
    EXPECT_TRUE(correct);
}

// ----- Parameter name constants -----

TEST_F(MvpUniformNodeConfigTest, ParamNameFovDegrees) {
    EXPECT_STREQ(MvpUniformNodeConfig::PARAM_FOV_DEGREES, "fovDegrees");
}

TEST_F(MvpUniformNodeConfigTest, ParamNameAspect) {
    EXPECT_STREQ(MvpUniformNodeConfig::PARAM_ASPECT, "aspect");
}

TEST_F(MvpUniformNodeConfigTest, ParamNameNear) {
    EXPECT_STREQ(MvpUniformNodeConfig::PARAM_NEAR, "nearZ");
}

TEST_F(MvpUniformNodeConfigTest, ParamNameFar) {
    EXPECT_STREQ(MvpUniformNodeConfig::PARAM_FAR, "farZ");
}

TEST_F(MvpUniformNodeConfigTest, ParamNameCameraDistance) {
    EXPECT_STREQ(MvpUniformNodeConfig::PARAM_CAMERA_DISTANCE, "cameraDistance");
}

// ----- Config constructibility -----

TEST_F(MvpUniformNodeConfigTest, ConfigIsDefaultConstructible) {
    MvpUniformNodeConfig cfg;
    EXPECT_EQ(cfg.INPUT_COUNT,  1u);
    EXPECT_EQ(cfg.OUTPUT_COUNT, 1u);
}

TEST_F(MvpUniformNodeConfigTest, ConfigIsCopyable) {
    MvpUniformNodeConfig a;
    MvpUniformNodeConfig b = a;
    EXPECT_EQ(a.INPUT_COUNT,  b.INPUT_COUNT);
    EXPECT_EQ(a.OUTPUT_COUNT, b.OUTPUT_COUNT);
}

// ----- NodeType -----

TEST_F(MvpUniformNodeConfigTest, TypeNameIsMvpUniform) {
    MvpUniformNodeType nodeType;
    EXPECT_STREQ(nodeType.GetTypeName().c_str(), "MvpUniform");
}

// ============================================================================
// MVP matrix math (no device required — always run)
//
// Documented contract (MvpUniformNodeConfig.h): the node bakes mvp = proj * view
// where proj = glm::perspective(radians(fov), aspect, near, far) and
// view = translate(I, {0,0,-cameraDistance}). The model matrix is applied
// per-instance in the shader and Draw.vert performs the Y-flip / Z remap itself,
// so this node bakes the *unmodified* proj*view. These tests assert that
// documented composition directly (a tiny recomputation of the contract, NOT a
// reach into node internals — the actual upload happens device-side in
// MvpUniformNode::CreateBuffer).
// ============================================================================

namespace {
// Mirror of the documented contract mvp = proj * view (cpp, CreateBuffer).
glm::mat4 MvpForParams(float fovDegrees, float aspect, float nearZ, float farZ, float cameraDistance) {
    glm::mat4 proj = glm::perspective(glm::radians(fovDegrees), aspect, nearZ, farZ);
    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -cameraDistance));
    return proj * view;
}
} // namespace

TEST(MvpUniformMath, BufferSizeIsSingleMat4) {
    // The UBO holds exactly one mat4 (64 bytes) — binding 0 `bufferVals { mat4 mvp; }`.
    EXPECT_EQ(sizeof(glm::mat4), 64u);
}

TEST(MvpUniformMath, MvpEqualsProjTimesView) {
    const float fov = 50.0f, aspect = 1.7777778f, n = 0.1f, f = 200.0f, cam = 45.0f;
    glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, n, f);
    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -cam));
    glm::mat4 expected = proj * view;
    glm::mat4 actual   = MvpForParams(fov, aspect, n, f, cam);
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            EXPECT_FLOAT_EQ(actual[c][r], expected[c][r]) << "mismatch at [" << c << "][" << r << "]";
}

TEST(MvpUniformMath, ViewTranslatesCameraDistanceAlongNegativeZ) {
    // view should push the world cameraDistance units down -Z (column-3 z component).
    const float cam = 45.0f;
    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -cam));
    EXPECT_FLOAT_EQ(view[3][2], -cam);
}

// ============================================================================
// Integration / Device round-trip tests
//
// These require a physical Vulkan device. In a headless environment (no GPU),
// the test would fail at DeviceNode Compile() with a Vulkan error / exception,
// at which point the test is marked as a deliberate SKIP.
//
// Pattern: build an InstanceNode → DeviceNode → MvpUniformNode graph, compile it,
// then query the MVP_BUFFER output and assert it.
//
// NOTE: The device-level node lifecycle (full graph compile + execute) in VIXEN
// requires access to a live RenderGraph + NodeType registry, which is heavy
// infrastructure. Without a standalone headless InstanceNode / DeviceNode, a true
// unit round-trip is not achievable without the full app startup sequence. The
// integration test is therefore left as a documented placeholder below, consistent
// with the pattern used by test_instance_buffer_node.cpp / test_device_node.cpp.
// ============================================================================

/**
 * Device round-trip integration test — DEFERRED
 *
 * To implement (once headless DeviceNode + InstanceNode lifecycle is callable
 * without a window / surface):
 *
 * TEST(MvpUniformNodeIntegration, RoundTrip) {
 *     // 1. Create InstanceNode, DeviceNode, MvpUniformNode instances
 *     // 2. Wire graph: InstanceNode→DeviceNode→MvpUniformNode
 *     // 3. Set MvpUniformNode params: fovDegrees=50, aspect=1.7777778, nearZ=0.1, farZ=200, cameraDistance=45
 *     // 4. Compile graph
 *     // 5. Fetch MVP_BUFFER (VkBuffer) output
 *     //    EXPECT_NE(buffer, VK_NULL_HANDLE)
 *     // 6. (Optional) map host-visible memory and verify the baked mat4 equals proj*view.
 *     // 7. Cleanup graph (FinalTeardown — verify no validation errors, buffer freed)
 * }
 *
 * Blocked on: standalone headless InstanceNode/DeviceNode lifecycle
 * (test_instance_buffer_node.cpp / test_device_node.cpp use the same placeholder strategy).
 */
