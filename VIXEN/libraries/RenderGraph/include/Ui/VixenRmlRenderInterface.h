#pragma once
// VixenRmlRenderInterface — RmlUi's Rml::RenderInterface implemented over raw Vulkan (no VMA, so it
// stays decoupled from VIXEN's allocator). It owns a small 2D UI pipeline (embedded SPIR-V), a 1x1
// white default texture (so untextured geometry reuses the textured path), and per-CompiledGeometry
// host-visible vertex/index buffers. RmlUi compiles geometry on layout change (not per frame), so the
// buffers are cheap. The owning UIRenderNode calls Init() once (Compile) and BeginFrame()+context
// Render() each frame; draws are recorded into the BeginFrame() command buffer.
#include <vulkan/vulkan.h>
#include <RmlUi/Core/RenderInterface.h>

#include <cstdint>
#include <vector>

namespace Vixen::Ui {

class VixenRmlRenderInterface final : public Rml::RenderInterface {
public:
    VixenRmlRenderInterface() = default;
    ~VixenRmlRenderInterface() override = default;

    // One-time setup once the device, command pool, and (UI-owned, color-only) render pass exist.
    void Init(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue queue, uint32_t queueFamilyIndex,
              const VkPhysicalDeviceMemoryProperties& memProps, VkCommandPool commandPool, VkRenderPass renderPass);
    // Destroy all owned Vulkan objects. Caller must vkDeviceWaitIdle first.
    void Shutdown();

    // Per-frame: bind the active command buffer + context dimensions before context->Render().
    void BeginFrame(VkCommandBuffer cmd, VkExtent2D extent);

    // --- Rml::RenderInterface (the 8 required functions) ---
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

private:
    struct Geometry {
        VkBuffer vbuf = VK_NULL_HANDLE; VkDeviceMemory vmem = VK_NULL_HANDLE;
        VkBuffer ibuf = VK_NULL_HANDLE; VkDeviceMemory imem = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };
    struct Texture {
        VkImage image = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE; VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                      VkBuffer& outBuf, VkDeviceMemory& outMem);
    Texture* CreateTextureRGBA(const uint8_t* rgba, uint32_t width, uint32_t height);
    void CreatePipeline();

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex_ = 0;
    VkPhysicalDeviceMemoryProperties memProps_{};
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    Texture* defaultTexture_ = nullptr;  // 1x1 white, handle for untextured geometry

    // Per-frame state set by BeginFrame().
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    bool scissorEnabled_ = false;
    VkRect2D scissor_{};

    std::vector<Geometry*> geometries_;  // owned; destroyed in Shutdown
    std::vector<Texture*> textures_;     // owned; destroyed in Shutdown
};

} // namespace Vixen::Ui
