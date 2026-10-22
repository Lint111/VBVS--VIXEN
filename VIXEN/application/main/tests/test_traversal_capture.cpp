// Default-scene traversal capture (R716 witness): renders the production graph's default
// procedural bodies offscreen from an optional fixed pose, writes the frame as PNG and the
// HitRecord buffer as raw bytes, so hit/miss, normals and the HITRECORD_FLAG_*_EXHAUSTED
// counts can be read per pixel.
//
// Environment:
//   VIXEN_TRAVCAP_PREFIX   output stem (required): <stem>.png, <stem>.hits.bin
//   VIXEN_TRAVCAP_EYE      "x,y,z" camera position (optional; default graph pose)
//   VIXEN_TRAVCAP_TARGET   "x,y,z" look target (with EYE)
//   VIXEN_TRAVCAP_FRAME    completed frame to capture (default 8)
//   VIXEN_TRAVCAP_FRAMES   frames to run in total (default FRAME; raise for timing)
#include "VulkanGraphApplication.h"
#include "Core/RenderGraph.h"
#include "Core/NodeInstance.h"
#include "Nodes/CameraNode.h"
#include "Nodes/ComputeStageNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/StorageBufferNode.h"
#include "Data/Nodes/StorageBufferNodeConfig.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

std::optional<glm::vec3> EnvVec3(const char* name) {
    const char* text = std::getenv(name);
    if (!text) return std::nullopt;
    glm::vec3 v(0.0f);
    char* end = nullptr;
    v.x = std::strtof(text, &end);
    if (!end || *end != ',') return std::nullopt;
    v.y = std::strtof(end + 1, &end);
    if (!end || *end != ',') return std::nullopt;
    v.z = std::strtof(end + 1, &end);
    return v;
}

uint32_t EnvUint(const char* name, uint32_t fallback) {
    const char* text = std::getenv(name);
    return text ? static_cast<uint32_t>(std::strtoul(text, nullptr, 10)) : fallback;
}

class TraversalCaptureApplication final : public VulkanGraphApplication {
public:
    TraversalCaptureApplication() { SetPresentationTarget(PresentationTarget::Offscreen); }

    ~TraversalCaptureApplication() override { ReleaseSnapshot(); }

    void BuildRenderGraph() override {
        VulkanGraphApplication::BuildRenderGraph();
        auto* graph = GetRenderGraph();
        camera_ = dynamic_cast<Vixen::RenderGraph::CameraNode*>(
            graph->GetInstanceByName("raymarch_camera"));
        auto* records = dynamic_cast<Vixen::RenderGraph::StorageBufferNode*>(
            graph->GetInstanceByName("hit_record_buffer"));
        auto* wave = dynamic_cast<Vixen::RenderGraph::ComputeStageNode*>(
            graph->GetInstanceByName("shadow_visibility_wave"));
        if (!camera_ || !records || !wave)
            throw std::runtime_error("default graph lacks raymarch_camera/hit_record_buffer/shadow_visibility_wave");
        records->SetParameter(Vixen::RenderGraph::StorageBufferNodeConfig::PARAM_EXTRA_USAGE_FLAGS,
                              static_cast<uint32_t>(VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
        wave->SetPostDispatchRecorderForTest([this, records](VkCommandBuffer cmd) {
            RecordSnapshotCopy(cmd, *records);
        });
    }

    bool Render() override {
        if (camera_) {
            // Graph compile re-reads the default pose, so the fixed pose is applied per frame.
            if (const auto eye = EnvVec3("VIXEN_TRAVCAP_EYE")) {
                camera_->SetPositionForTest(*eye);
                camera_->SetLookTargetNoOrbitForTest(
                    EnvVec3("VIXEN_TRAVCAP_TARGET").value_or(*eye + glm::vec3(0, 0, -1)));
            }
        }
        if (!VulkanGraphApplication::Render()) return false;
        ++completedFrames_;
        if (completedFrames_ == EnvUint("VIXEN_TRAVCAP_FRAME", 8u)) return Capture();
        return true;
    }

    bool Captured() const { return captured_; }
    const std::string& Error() const { return error_; }

private:
    VkDevice Device() {
        auto* deviceNode = dynamic_cast<Vixen::RenderGraph::DeviceNode*>(
            GetRenderGraph()->GetInstanceByName("main_device"));
        auto* device = deviceNode ? deviceNode->GetVulkanDevice() : nullptr;
        if (!device) throw std::runtime_error("missing main_device");
        device_ = device;
        return device->device;
    }

    void RecordSnapshotCopy(VkCommandBuffer cmd, Vixen::RenderGraph::StorageBufferNode& records) {
        VkDevice device = Device();
        if (!snapshot_) {
            snapshotSize_ = records.GetSizeBytes();
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = snapshotSize_;
            info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(device, &info, nullptr, &snapshot_) != VK_SUCCESS)
                throw std::runtime_error("snapshot buffer allocation failed");
            snapshotDevice_ = device;
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device, snapshot_, &requirements);
            VkPhysicalDeviceMemoryProperties properties{};
            vkGetPhysicalDeviceMemoryProperties(*device_->gpu, &properties);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = properties.memoryTypeCount;
            constexpr VkMemoryPropertyFlags kHost =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
                if ((requirements.memoryTypeBits & (1u << i)) &&
                    (properties.memoryTypes[i].propertyFlags & kHost) == kHost) {
                    allocation.memoryTypeIndex = i;
                    break;
                }
            if (allocation.memoryTypeIndex == properties.memoryTypeCount ||
                vkAllocateMemory(device, &allocation, nullptr, &snapshotMemory_) != VK_SUCCESS ||
                vkBindBufferMemory(device, snapshot_, snapshotMemory_, 0) != VK_SUCCESS)
                throw std::runtime_error("snapshot host memory allocation failed");
        }
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        const VkBufferCopy copy{0, 0, snapshotSize_};
        vkCmdCopyBuffer(cmd, records.GetBufferHandle(), snapshot_, 1, &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    bool Capture() {
        const char* prefix = std::getenv("VIXEN_TRAVCAP_PREFIX");
        if (!prefix) {
            error_ = "VIXEN_TRAVCAP_PREFIX is not set";
            return false;
        }
        const std::string stem(prefix);
        if (!CaptureOffscreenFrameToPng(stem + ".png", error_)) return false;
        if (!snapshotMemory_ || vkDeviceWaitIdle(snapshotDevice_) != VK_SUCCESS) {
            error_ = "HitRecord snapshot was never recorded";
            return false;
        }
        void* mapped = nullptr;
        if (vkMapMemory(snapshotDevice_, snapshotMemory_, 0, snapshotSize_, 0, &mapped) != VK_SUCCESS) {
            error_ = "could not map HitRecord snapshot";
            return false;
        }
        std::ofstream hits(stem + ".hits.bin", std::ios::binary);
        hits.write(static_cast<const char*>(mapped), static_cast<std::streamsize>(snapshotSize_));
        vkUnmapMemory(snapshotDevice_, snapshotMemory_);
        captured_ = static_cast<bool>(hits);
        if (!captured_) error_ = "could not write HitRecord snapshot";
        return captured_;
    }

    void ReleaseSnapshot() {
        if (!snapshotDevice_) return;
        vkDeviceWaitIdle(snapshotDevice_);
        vkDestroyBuffer(snapshotDevice_, snapshot_, nullptr);
        vkFreeMemory(snapshotDevice_, snapshotMemory_, nullptr);
        snapshotDevice_ = VK_NULL_HANDLE;
    }

    Vixen::RenderGraph::CameraNode* camera_ = nullptr;
    Vixen::Vulkan::Resources::VulkanDevice* device_ = nullptr;
    VkDevice snapshotDevice_ = VK_NULL_HANDLE;
    VkBuffer snapshot_ = VK_NULL_HANDLE;
    VkDeviceMemory snapshotMemory_ = VK_NULL_HANDLE;
    VkDeviceSize snapshotSize_ = 0;
    uint32_t completedFrames_ = 0;
    bool captured_ = false;
    std::string error_;
};

TEST(TraversalCapture, DefaultSceneCapturesFrameAndHitRecords) {
    ASSERT_NE(std::getenv("VIXEN_TRAVCAP_PREFIX"), nullptr) << "set VIXEN_TRAVCAP_PREFIX";
    const uint32_t frame = EnvUint("VIXEN_TRAVCAP_FRAME", 8u);
    TraversalCaptureApplication app;
    ASSERT_EQ(app.Run(RunOptions{.exitAfterFrames = std::max(frame, EnvUint("VIXEN_TRAVCAP_FRAMES", frame))}), 0)
        << app.Error();
    EXPECT_TRUE(app.Captured()) << app.Error();
}

} // namespace
