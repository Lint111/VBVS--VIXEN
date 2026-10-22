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
#include <functional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vixen::Text { class GlyphAtlas; }
namespace Vixen::Text { struct GlyphInstance; struct SpanStyle; struct SpanAnim; }

namespace Vixen::Ui {

class VixenRmlRenderInterface final : public Rml::RenderInterface {
public:
    VixenRmlRenderInterface() = default;
    ~VixenRmlRenderInterface() override = default;

    /// Hands a recorded one-shot command buffer (allocated from the pool) to the graph's submission
    /// point; `release` runs once the GPU has finished it. Returns false when the submit failed.
    using OneShotSubmit = std::function<bool(VkCommandPool, VkCommandBuffer, std::function<void()> release)>;

    // One-time setup once the device, command pool, and (UI-owned, color-only) render pass exist.
    // R715: the texture-upload one-shot rides the owning node's submission stream through
    // `submitOneShot` (RenderGraph::SubmitOneShot), so this interface never takes VK2 nor waits.
    // pipelineCache (not owned) is the device's shared persistent graphics cache.
    void Init(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue queue, uint32_t queueFamilyIndex,
              const VkPhysicalDeviceMemoryProperties& memProps, VkCommandPool commandPool, VkRenderPass renderPass,
              OneShotSubmit submitOneShot, VkPipelineCache pipelineCache);
    // Destroy all owned Vulkan objects. Caller must vkDeviceWaitIdle first.
    void Shutdown();

    // Per-frame: bind the active command buffer + context dimensions before context->Render().
    void BeginFrame(VkCommandBuffer cmd, VkExtent2D extent);

    // Record one generated MSDF batch. Text uses its own pipeline and descriptor layout; the
    // existing RmlUi geometry path remains unchanged.
    void DrawTextInstances(const Vixen::Text::GlyphAtlas& atlas,
                  std::span<const Vixen::Text::GlyphInstance> glyphs,
                  std::span<const Vixen::Text::SpanStyle> styles,
                  std::span<const Vixen::Text::SpanAnim> animations,
                  float timeSeconds);

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
        bool isMsdf = false;
    };
    struct TextDrawResources {
        VkBuffer glyphBuffer = VK_NULL_HANDLE; VkDeviceMemory glyphMemory = VK_NULL_HANDLE;
        VkBuffer styleBuffer = VK_NULL_HANDLE; VkDeviceMemory styleMemory = VK_NULL_HANDLE;
        VkBuffer animationBuffer = VK_NULL_HANDLE; VkDeviceMemory animationMemory = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };
    struct AtlasTexture {
        uint32_t version = 0;
        Texture* texture = nullptr;
    };

    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                      VkBuffer& outBuf, VkDeviceMemory& outMem);
    Texture* CreateTextureRGBA(const uint8_t* rgba, uint32_t width, uint32_t height, bool isMsdf = false);
    void CreatePipeline();
    void CreateTextPipeline();
    void DestroyTextDrawResources(TextDrawResources* resources);

    // Per-struct Vulkan teardown — shared by the deferred-free path (BeginFrame) and Shutdown so the
    // two destroy a resource identically. Both null-check, destroy the owned objects, and `delete` the
    // struct; the caller owns removing the pointer from any list.
    void DestroyGeometry(Geometry* g);
    void DestroyTexture(Texture* t);

    // Frames-in-flight deferred delete for RmlUi geometry/textures. RmlUi recompiles geometry (and may
    // regenerate textures) on every relayout — which happens whenever a bound HUD value like {{tick}}
    // changes — so a no-op Release would leak a vertex+index buffer (+ 2 VkDeviceMemory) per relayout,
    // growing GPU allocations without bound until the driver/D3D12 heap faults. Instead Release defers:
    // it records the release frame and the resource is freed only once it can no longer be referenced by
    // any in-flight frame. Freeing a GPU resource a frame too EARLY is a use-after-free, so we bias late.
    static constexpr uint64_t kDeferFrames = 8;  // must be >= max frames-in-flight (FrameSync
                                                 // MAX_FRAMES_IN_FLIGHT = 4); 8 is a safe 2x margin and
                                                 // the HUD geometry is tiny, so the margin is free.
    uint64_t frameCounter_ = 0;  // ++ per BeginFrame; the age clock for deferred frees.
    std::vector<std::pair<uint64_t, Geometry*>> pendingGeometryDeletes_;  // {releaseFrame, ptr}
    std::vector<std::pair<uint64_t, Texture*>> pendingTextureDeletes_;    // {releaseFrame, ptr}
    std::vector<std::pair<uint64_t, TextDrawResources*>> pendingTextDrawDeletes_;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;  // shared, owned by PipelineCacher
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    OneShotSubmit submitOneShot_;  // R715: the owning node's graph submission point
    uint32_t queueFamilyIndex_ = 0;
    VkPhysicalDeviceMemoryProperties memProps_{};
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout textDescriptorLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout textPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline textPipeline_ = VK_NULL_HANDLE;

    Texture* defaultTexture_ = nullptr;  // 1x1 white, handle for untextured geometry

    // Per-frame state set by BeginFrame().
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    bool scissorEnabled_ = false;
    VkRect2D scissor_{};

    std::vector<Geometry*> geometries_;  // owned; destroyed in Shutdown
    std::vector<Texture*> textures_;     // owned; destroyed in Shutdown
    std::unordered_map<const Vixen::Text::GlyphAtlas*, AtlasTexture> atlasTextures_;
};

} // namespace Vixen::Ui
