#include "VulkanGraphApplication.h"
#include "Nodes/BodyOctreeSceneNode.h"
#include "Nodes/CameraNode.h"
#include "Nodes/MiningBeamBufferNode.h"
#include "graph/CornellBoxSceneDefinition.h"
#include <stb_image.h>

#include <glm/gtc/matrix_transform.hpp>

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::vector<char> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct RgbImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};

RgbImage DecodeRgb(const std::vector<char>& png) {
    RgbImage image;
    if (png.empty()) return image;
    int channels = 0;
    auto* decoded = stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>(png.data()), static_cast<int>(png.size()),
        &image.width, &image.height, &channels, 3);
    if (!decoded) return image;
    image.pixels.assign(decoded, decoded + (size_t(image.width) * image.height * 3));
    stbi_image_free(decoded);
    return image;
}

char CornellColorClass(const RgbImage& image, int x, int y) {
    const size_t offset = (size_t(y) * image.width + x) * 3;
    const unsigned char r = image.pixels[offset + 0];
    const unsigned char g = image.pixels[offset + 1];
    const unsigned char b = image.pixels[offset + 2];
    if (r > 2 * g && r > 2 * b) return 'R';
    if (g > 2 * r && g > 2 * b) return 'G';
    if (std::abs(int(r) - int(g)) <= 20 && std::abs(int(g) - int(b)) <= 20) return 'N';
    return 'X';
}

struct SeamLineCheck {
    int colorChanges = 0;
    int unexpectedPixels = 0;
};

SeamLineCheck CheckVerticalSeam(const RgbImage& image, int x, int firstY, int lastY,
                                char expectedColor) {
    SeamLineCheck check;
    char previous = CornellColorClass(image, x, firstY);
    check.unexpectedPixels += previous != expectedColor;
    for (int y = firstY + 1; y <= lastY; ++y) {
        const char current = CornellColorClass(image, x, y);
        check.colorChanges += current != previous;
        check.unexpectedPixels += current != expectedColor;
        previous = current;
    }
    return check;
}

enum class BeamScenario {
    Default,
    DisabledWithList,
    EnabledEmpty,
    Active,
    RotatedActive,
};

struct BeamScreenSegment {
    glm::vec2 start{0.0f};
    glm::vec2 end{0.0f};
    glm::vec2 staleStart{0.0f};
    glm::vec2 staleEnd{0.0f};
    bool valid = false;
};

class HeadlessCornellApplication final : public VulkanGraphApplication {
public:
    HeadlessCornellApplication(BeamScenario scenario, std::string capturePrefix)
        : scenario_(scenario), capturePrefix_(std::move(capturePrefix)) {
        SetPresentationTarget(PresentationTarget::Offscreen);
    }

    void BuildRenderGraph() override { VulkanGraphApplication::BuildRenderGraph(); }

    bool Render() override {
        const bool rotatedScenario = scenario_ == BeamScenario::RotatedActive;
        if (!configuredBeamScenario_ || rotatedScenario) {
            auto* graph = GetRenderGraph();
            auto* node = graph
                ? static_cast<Vixen::RenderGraph::MiningBeamBufferNode*>(
                      graph->GetInstanceByName("mining_beam_buffer"))
                : nullptr;
            if (!node) {
                captureError_ = "production graph did not create mining_beam_buffer";
                return false;
            }

            Vixen::RenderGraph::MiningBeamInput beam;
            beam.sourceInstanceIndex = 6; // Legacy fixture slots for the unrotated scenarios.
            beam.targetInstanceIndex = 7;
            // The virtual Cornell bodies are world-authored procedural recipes.
            // These points sit on their facing surfaces, leaving a visible gap.
            beam.sourceLocalOffset = glm::vec3(13.85f, 9.4f, 14.93f);
            beam.targetLocalOffset = glm::vec3(18.8f, 9.1f, 17.9f);
            beam.radius = 0.09f;
            beam.luminosity = 0.75f;
            beam.purposeScale = 0.12f;

            if (rotatedScenario) {
                auto* bodyScene = graph
                    ? static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
                          graph->GetInstanceByName("body_octree_scene"))
                    : nullptr;
                if (!bodyScene) {
                    captureError_ = "production graph did not create body_octree_scene";
                    return false;
                }
                std::vector<Vixen::SVO::BodyInstanceGpu> instances = bodyScene->GetInstances();
                // UpdateBodySceneResidency sorts this list before the first Render call, so
                // authored body identity must come from recipeId rather than a stable slot.
                constexpr uint32_t kSphereRecipeId = 8u;
                constexpr uint32_t kBoxRecipeId = 9u;
                const auto sourceIt = std::find_if(instances.begin(), instances.end(), [](const auto& instance) {
                    return instance.material.recipeId == kSphereRecipeId;
                });
                const auto targetIt = std::find_if(instances.begin(), instances.end(), [](const auto& instance) {
                    return instance.material.recipeId == kBoxRecipeId;
                });
                if (sourceIt == instances.end() || targetIt == instances.end()) {
                    captureError_ = "Cornell scene did not seed the sphere and box recipes";
                    return false;
                }
                beam.sourceInstanceIndex = static_cast<uint32_t>(sourceIt - instances.begin());
                beam.targetInstanceIndex = static_cast<uint32_t>(targetIt - instances.begin());

                const glm::mat4 rotation = glm::rotate(
                    glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
                const glm::vec3 sourceCenter = Vixen::App::CornellBox::kSphereObjectCenter;
                if (!rotatedOriginalsCaptured_) {
                    sourceOriginalTransform_ = instances[beam.sourceInstanceIndex].transform.localToWorld;
                    targetOriginalTransform_ = instances[beam.targetInstanceIndex].transform.localToWorld;
                    rotatedOriginalsCaptured_ = true;
                }
                const glm::mat4 sourcePivot =
                    glm::translate(glm::mat4(1.0f), sourceCenter) * rotation *
                    glm::translate(glm::mat4(1.0f), -sourceCenter);
                const glm::mat4 rotatedLocalToWorld =
                    sourcePivot * Vixen::SVO::ToMat4(sourceOriginalTransform_);
                if (!rotatedInstanceStaged_) {
                    Vixen::SVO::SetInstanceTransform(
                        instances[beam.sourceInstanceIndex], rotatedLocalToWorld);
                    bodyScene->SetInstances(std::move(instances));
                    rotatedInstanceStaged_ = true;
                }
                expectedBeamStartWorld_ = Vixen::SVO::TransformPoint(
                    Vixen::SVO::ToAffine3x4(rotatedLocalToWorld), beam.sourceLocalOffset);
                expectedBeamEndWorld_ = Vixen::SVO::TransformPoint(
                    targetOriginalTransform_, beam.targetLocalOffset);
                staleBeamStartWorld_ = Vixen::SVO::TransformPoint(sourceOriginalTransform_, beam.sourceLocalOffset);
                staleBeamEndWorld_ = Vixen::SVO::TransformPoint(targetOriginalTransform_, beam.targetLocalOffset);
            }

            switch (scenario_) {
                case BeamScenario::Default:
                    break;
                case BeamScenario::DisabledWithList:
                    node->SetBeams({beam});
                    node->SetEnabled(false);
                    break;
                case BeamScenario::EnabledEmpty:
                    node->SetBeams({});
                    node->SetEnabled(true);
                    break;
                case BeamScenario::Active:
                    node->SetBeams({beam});
                    node->SetEnabled(true);
                    break;
                case BeamScenario::RotatedActive:
                    node->SetBeams({beam});
                    node->SetEnabled(true);
                    break;
            }
            configuredBeamScenario_ = true;
        }

        if (!VulkanGraphApplication::Render()) return false;
        if (scenario_ == BeamScenario::RotatedActive) {
            auto* graph = GetRenderGraph();
            auto* camera = graph
                ? static_cast<Vixen::RenderGraph::CameraNode*>(graph->GetInstanceByName("raymarch_camera"))
                : nullptr;
            if (!camera) {
                captureError_ = "production graph did not expose its camera node";
                return false;
            }
            const auto& data = camera->GetCurrentCameraData();
            const float tanHalfFov = std::tan(glm::radians(data.fov * 0.5f));
            auto project = [&](const glm::vec3& worldPoint) {
                const glm::vec3 relative = worldPoint - data.cameraPos;
                const float depth = glm::dot(relative, data.cameraDir);
                const float ndcX = glm::dot(relative, data.cameraRight) /
                                   (depth * tanHalfFov * data.aspect);
                const float ndcY = glm::dot(relative, data.cameraUp) /
                                   (depth * tanHalfFov);
                return glm::vec2((ndcX + 1.0f) * 250.0f, (1.0f - ndcY) * 250.0f);
            };
            expectedBeamScreen_ = {
                project(expectedBeamStartWorld_), project(expectedBeamEndWorld_),
                project(staleBeamStartWorld_), project(staleBeamEndWorld_), true};
        }
        if (++completedFrames_ < 5) return true;

        auto* graph = GetRenderGraph();
        if (!graph || graph->GetInstanceByName("main_window") ||
            graph->GetInstanceByName("main_swapchain") || graph->GetInstanceByName("present") ||
            !graph->GetInstanceByName("main_offscreen_target")) {
            captureError_ = "production graph did not select its offscreen terminal target";
            return false;
        }

        auto* target = graph->GetInstanceByName("main_offscreen_target")->GetOutput(0)->GetHandle<Vixen::Vulkan::Resources::IRenderTarget*>();
        EXPECT_EQ(target->GetCurrentLayout(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        EXPECT_TRUE(target->GetImageUsageFlags() & VK_IMAGE_USAGE_STORAGE_BIT);
        EXPECT_TRUE(target->GetImageUsageFlags() & VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        EXPECT_FALSE(target->UsesWsiSynchronization());
        const std::string first = capturePrefix_ + "-a.png";
        const std::string second = capturePrefix_ + "-b.png";
        if (!CaptureOffscreenFrameToPng(first, captureError_) ||
            !CaptureOffscreenFrameToPng(second, captureError_)) return false;
        captured_ = true;
        return true;
    }

    bool Captured() const { return captured_; }
    const std::string& CaptureError() const { return captureError_; }
    const BeamScreenSegment& ExpectedBeamScreen() const { return expectedBeamScreen_; }

private:
    BeamScenario scenario_;
    std::string capturePrefix_;
    bool captured_ = false;
    bool configuredBeamScenario_ = false;
    uint32_t completedFrames_ = 0;
    std::string captureError_;
    glm::vec3 expectedBeamStartWorld_{0.0f};
    glm::vec3 expectedBeamEndWorld_{0.0f};
    glm::vec3 staleBeamStartWorld_{0.0f};
    glm::vec3 staleBeamEndWorld_{0.0f};
    Vixen::SVO::Affine3x4Gpu sourceOriginalTransform_{};
    Vixen::SVO::Affine3x4Gpu targetOriginalTransform_{};
    bool rotatedOriginalsCaptured_ = false;
    bool rotatedInstanceStaged_ = false;
    BeamScreenSegment expectedBeamScreen_{};
};

TEST(HeadlessCornellGraph, ProductionGraphRendersDeterministicCornellWithStableSharedWallSeams) {
    const char* prefix = std::getenv("VIXEN_HEADLESS_CORNELL_CAPTURE_PREFIX");
    ASSERT_NE(prefix, nullptr) << "CMake must set VIXEN_HEADLESS_CORNELL_CAPTURE_PREFIX";
    const std::filesystem::path first = std::string(prefix) + "-a.png";
    const std::filesystem::path second = std::string(prefix) + "-b.png";
    std::error_code ec;
    std::filesystem::remove(first, ec);
    std::filesystem::remove(second, ec);

    auto captureScenario = [](BeamScenario scenario, const std::string& capturePrefix) {
        HeadlessCornellApplication app(scenario, capturePrefix);
        const int result = app.Run(RunOptions{.exitAfterFrames = 5});
        EXPECT_EQ(result, 0) << app.CaptureError();
        EXPECT_TRUE(app.Captured()) << app.CaptureError();

        const auto firstBytes = ReadBytes(capturePrefix + "-a.png");
        const auto secondBytes = ReadBytes(capturePrefix + "-b.png");
        EXPECT_FALSE(firstBytes.empty()) << "first engine PNG readback was empty";
        EXPECT_FALSE(secondBytes.empty()) << "second engine PNG readback was empty";
        EXPECT_EQ(firstBytes, secondBytes)
            << "capturing the same completed frame twice must produce identical PNG bytes";
        return firstBytes;
    };

    const auto defaultBytes = captureScenario(BeamScenario::Default, prefix);
    const auto disabledBytes = captureScenario(BeamScenario::DisabledWithList,
                                               std::string(prefix) + "-beam-disabled");
    const auto emptyBytes = captureScenario(BeamScenario::EnabledEmpty,
                                            std::string(prefix) + "-beam-empty");
    const auto activeBytes = captureScenario(BeamScenario::Active,
                                             std::string(prefix) + "-beam-active");
    ASSERT_FALSE(defaultBytes.empty());
    ASSERT_FALSE(disabledBytes.empty());
    ASSERT_FALSE(emptyBytes.empty());
    ASSERT_FALSE(activeBytes.empty());
    EXPECT_EQ(defaultBytes, disabledBytes)
        << "a populated but disabled beam list must preserve current capture bytes";
    EXPECT_EQ(defaultBytes, emptyBytes)
        << "an enabled empty list must preserve current capture bytes";
    EXPECT_NE(defaultBytes, activeBytes)
        << "the active beam capture must differ from the no-beam production frame";

    struct RotatedCapture {
        std::vector<char> bytes;
        BeamScreenSegment beam;
    };
    auto captureRotatedScenario = [&](const std::string& capturePrefix) {
        HeadlessCornellApplication app(BeamScenario::RotatedActive, capturePrefix);
        const int result = app.Run(RunOptions{.exitAfterFrames = 5});
        EXPECT_EQ(result, 0) << app.CaptureError();
        EXPECT_TRUE(app.Captured()) << app.CaptureError();
        RotatedCapture captured{ReadBytes(capturePrefix + "-a.png"), app.ExpectedBeamScreen()};
        EXPECT_FALSE(captured.bytes.empty()) << "rotated Cornell capture was empty";
        EXPECT_TRUE(captured.beam.valid) << "rotated beam endpoint projection was unavailable";
        return captured;
    };
    const RotatedCapture rotatedActive = captureRotatedScenario(
        std::string(prefix) + "-rotated-beam-active");
    const RgbImage rotatedActiveImage = DecodeRgb(rotatedActive.bytes);
    ASSERT_EQ(rotatedActiveImage.width, 500);
    ASSERT_EQ(rotatedActiveImage.height, 500);
    const BeamScreenSegment& expectedBeam = rotatedActive.beam;
    const float sourceEndpointShift = glm::length(expectedBeam.start - expectedBeam.staleStart);
    const float targetEndpointShift = glm::length(expectedBeam.end - expectedBeam.staleEnd);
    EXPECT_GT(std::max(sourceEndpointShift, targetEndpointShift), 20.0f)
        << "the rotated endpoint segment must be visibly distinct from stale world endpoints";

    const auto beamCorePixelsNear = [&](const glm::vec2& center) {
        size_t count = 0;
        for (int y = std::max(0, static_cast<int>(center.y) - 4);
             y <= std::min(499, static_cast<int>(center.y) + 4); ++y) {
            for (int x = std::max(0, static_cast<int>(center.x) - 4);
                 x <= std::min(499, static_cast<int>(center.x) + 4); ++x) {
                const glm::vec2 pixel(static_cast<float>(x) + 0.5f,
                                      static_cast<float>(y) + 0.5f);
                if (glm::length(pixel - center) > 4.0f) continue;
                const size_t index = (static_cast<size_t>(y) * 500u +
                                      static_cast<size_t>(x)) * 3u;
                const int red = rotatedActiveImage.pixels[index + 0u];
                const int green = rotatedActiveImage.pixels[index + 1u];
                const int blue = rotatedActiveImage.pixels[index + 2u];
                if (green + blue - 2 * red > 60) ++count;
            }
        }
        return count;
    };
    const size_t transformedStartCorePixels = beamCorePixelsNear(expectedBeam.start);
    const size_t staleStartCorePixels = beamCorePixelsNear(expectedBeam.staleStart);
    std::printf("[R424-BEAM] transformed-start-core=%zu stale-start-core=%zu endpoint-shift=%.2f px transformed=(%.1f,%.1f)->(%.1f,%.1f) stale=(%.1f,%.1f)->(%.1f,%.1f)\n",
                transformedStartCorePixels, staleStartCorePixels,
                std::max(sourceEndpointShift, targetEndpointShift),
                expectedBeam.start.x, expectedBeam.start.y,
                expectedBeam.end.x, expectedBeam.end.y,
                expectedBeam.staleStart.x, expectedBeam.staleStart.y,
                expectedBeam.staleEnd.x, expectedBeam.staleEnd.y);
    EXPECT_GT(transformedStartCorePixels, 3u)
        << "the rendered beam core is missing at the rotated instance's transformed endpoint";
    EXPECT_EQ(staleStartCorePixels, 0u)
        << "the rendered beam still reaches the original unrotated endpoint";

    const RgbImage image = DecodeRgb(defaultBytes);
    ASSERT_EQ(image.width, 500) << "Cornell terminal capture must be a nonempty 500x500 RGB PNG";
    ASSERT_EQ(image.height, 500) << "Cornell terminal capture must be a nonempty 500x500 RGB PNG";
    ASSERT_EQ(image.pixels.size(), size_t(image.width) * image.height * 3);

    // On the 500x500 Cornell camera, columns 151 and 348 are the shared left/back and
    // right/back wall seams. Rows 170..220 avoid the HUD panels. P584's exact-distance/lower-
    // body-slot tie-break must give one stable owner down each seam, with no alternating pixels.
    constexpr int kFirstSeamRow = 170;
    constexpr int kLastSeamRow = 220;
    const SeamLineCheck leftSeam = CheckVerticalSeam(image, 151, kFirstSeamRow, kLastSeamRow, 'R');
    const SeamLineCheck rightSeam = CheckVerticalSeam(image, 348, kFirstSeamRow, kLastSeamRow, 'G');
    EXPECT_EQ(leftSeam.unexpectedPixels, 0)
        << "left/back shared-wall seam must consistently select the red left wall";
    EXPECT_EQ(leftSeam.colorChanges, 0)
        << "left/back shared-wall seam has row-to-row pixel ownership alternation";
    EXPECT_EQ(rightSeam.unexpectedPixels, 0)
        << "right/back shared-wall seam must consistently select the green right wall";
    EXPECT_EQ(rightSeam.colorChanges, 0)
        << "right/back shared-wall seam has row-to-row pixel ownership alternation";
}

} // namespace
