// test_ui_startup_cache.cpp — times every UI startup phase up to the first rendered UI frame on a
// real Vulkan device and fingerprints the result, so cached and uncached runs can be compared.
//
// Output lines (parsed by the evidence scripts):
//   UISTARTUP <phase> <ms>            one per phase
//   UISTARTUP-PIXELS <hash> <bytes>   FNV of the rendered RGBA frame
//   UISTARTUP-HITS <hash> <count>     FNV of a fixed grid of hit-mask results
// VIXEN_UI_STARTUP_OUT=<dir> also writes the raw frame (frame.rgba) for a byte compare.
//
// Needs a Vulkan device (queued GPU job). Timing under Dozen is correctness-only.

#include <gtest/gtest.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include "RuntimeCachePaths.h"
#include "Text/RmlFontEngine.h"
#include "Ui/UiHitMask.h"
#include "Ui/ViewBlobFile.h"
#include "Ui/VixenRmlRenderInterface.h"
#include "Ui/VixenRmlSystemInterface.h"
#include "VulkanGlobalNames.h"
#include "graph/BuildingInspectorView.h"
#include "graph/HudView.h"

#include <vulkan/vulkan.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

class PhaseTimer {
public:
    void Mark(const char* phase) {
        const auto now = Clock::now();
        std::printf("UISTARTUP %s %.3f\n", phase, std::chrono::duration<double, std::milli>(now - last_).count());
        std::fflush(stdout);
        last_ = now;
    }
    void Reset() { last_ = Clock::now(); }

private:
    Clock::time_point last_ = Clock::now();
};

#define VK_OK(expr) ASSERT_EQ((expr), VK_SUCCESS) << #expr

} // namespace

TEST(UiStartupCache, FirstFrame) {
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    PhaseTimer timer;

    // --- Vulkan device (not UI startup work; timed for context only) ---
    VixenSelectWslGpuIcd();
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "test_ui_startup_cache";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    VK_OK(vkCreateInstance(&ici, nullptr, &instance));
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
    ASSERT_GT(gpuCount, 0u);
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(instance, &gpuCount, gpus.data());
    VkPhysicalDevice gpu = gpus[0];
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, families.data());
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < familyCount; ++i)
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { family = i; break; }
    ASSERT_NE(family, UINT32_MAX);
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    VkDevice device = VK_NULL_HANDLE;
    VK_OK(vkCreateDevice(gpu, &dci, nullptr, &device));
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &memProps);
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = family;
    VkCommandPool pool = VK_NULL_HANDLE;
    VK_OK(vkCreateCommandPool(device, &cpci, nullptr, &pool));

    // Colour target, render pass, framebuffer, readback buffer.
    VkImageCreateInfo imci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imci.imageType = VK_IMAGE_TYPE_2D;
    imci.format = VK_FORMAT_R8G8B8A8_UNORM;
    imci.extent = {kWidth, kHeight, 1};
    imci.mipLevels = 1;
    imci.arrayLayers = 1;
    imci.samples = VK_SAMPLE_COUNT_1_BIT;
    imci.tiling = VK_IMAGE_TILING_OPTIMAL;
    imci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage target = VK_NULL_HANDLE;
    VK_OK(vkCreateImage(device, &imci, nullptr, &target));
    auto findMemory = [&](uint32_t bits, VkMemoryPropertyFlags want) {
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & want) == want) return i;
        return UINT32_MAX;
    };
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device, target, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ASSERT_NE(mai.memoryTypeIndex, UINT32_MAX);
    VkDeviceMemory targetMemory = VK_NULL_HANDLE;
    VK_OK(vkAllocateMemory(device, &mai, nullptr, &targetMemory));
    VK_OK(vkBindImageMemory(device, target, targetMemory, 0));
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = target;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView targetView = VK_NULL_HANDLE;
    VK_OK(vkCreateImageView(device, &vci, nullptr, &targetView));

    VkAttachmentDescription color{};
    color.format = VK_FORMAT_R8G8B8A8_UNORM;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &color;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VK_OK(vkCreateRenderPass(device, &rpci, nullptr, &renderPass));
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = renderPass;
    fci.attachmentCount = 1;
    fci.pAttachments = &targetView;
    fci.width = kWidth;
    fci.height = kHeight;
    fci.layers = 1;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VK_OK(vkCreateFramebuffer(device, &fci, nullptr, &framebuffer));

    const VkDeviceSize frameBytes = VkDeviceSize{kWidth} * kHeight * 4;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = frameBytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer readback = VK_NULL_HANDLE;
    VK_OK(vkCreateBuffer(device, &bci, nullptr, &readback));
    vkGetBufferMemoryRequirements(device, readback, &req);
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemory(req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    ASSERT_NE(mai.memoryTypeIndex, UINT32_MAX);
    VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
    VK_OK(vkAllocateMemory(device, &mai, nullptr, &readbackMemory));
    VK_OK(vkBindBufferMemory(device, readback, readbackMemory, 0));
    timer.Mark("vulkan_device_setup (context only)");

    // --- UI startup: the work the first UI frame waits on ---
    Vixen::Ui::VixenRmlSystemInterface systemInterface;
    Vixen::Ui::VixenRmlRenderInterface renderInterface;
    Vixen::Text::RmlFontEngine fontEngine;

    timer.Reset();
    renderInterface.Init(device, gpu, queue, family, memProps, pool, renderPass, nullptr);
    timer.Mark("render_interface_init (shader modules + 2 pipelines)");

    Rml::SetSystemInterface(&systemInterface);
    Rml::SetRenderInterface(&renderInterface);
    Rml::SetFontEngineInterface(&fontEngine);
    ASSERT_TRUE(Rml::Initialise());
    timer.Mark("rml_initialise");

    ASSERT_TRUE(Rml::LoadFontFace("assets/ui/LatoLatin-Regular.ttf"));
    timer.Mark("load_font_face");

    Rml::Context* context = Rml::CreateContext("ui_startup", Rml::Vector2i(kWidth, kHeight));
    ASSERT_NE(context, nullptr);
    timer.Mark("create_context");

    Vixen::App::HudView hud;
    {
        Rml::DataModelConstructor c = context->CreateDataModel(hud.ModelName());
        ASSERT_TRUE(static_cast<bool>(c));
        hud.Register(c);
    }
    const Vixen::App::HudFactionIn factions[] = {
        {"Empire", 0.4f, true, true, true, 0}, {"Guild", 0.1f, false, false, true, 3},
        {"Order", 0.7f, false, true, false, 9}, {"Clans", 0.2f, false, false, false, 9}};
    const Vixen::App::HudEventIn events[] = {{"war", 81}, {"famine", 80}, {"treaty", 77}, {"raid", 70}};
    hud.SetHudView(42, 7, 1, 3, factions, events);
    timer.Mark("hud_data_model_setup");

    Rml::ElementDocument* hudDoc = context->LoadDocument("assets/ui/hud.rml");
    ASSERT_NE(hudDoc, nullptr);
    hudDoc->Show();
    timer.Mark("load_document_hud (parse rml+rcss)");

    Vixen::App::BuildingInspectorView inspector;
    {
        Rml::DataModelConstructor c = context->CreateDataModel(inspector.ModelName());
        ASSERT_TRUE(static_cast<bool>(c));
        inspector.Register(c);
    }
    Rml::ElementDocument* inspectorDoc = context->LoadDocument("assets/ui/building_inspector.rml");
    ASSERT_NE(inspectorDoc, nullptr);
    inspectorDoc->Show();
    timer.Mark("load_document_building_inspector");

    context->Update();
    timer.Mark("first_update (layout + text shaping + glyph atlas)");

    // Record, submit and wait for the first frame (geometry compile, atlas texture upload, draw).
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_OK(vkAllocateCommandBuffers(device, &cbai, &cmd));
    VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_OK(vkBeginCommandBuffer(cmd, &cbbi));
    VkClearValue clear{};
    clear.color = {{0.05f, 0.05f, 0.08f, 1.0f}};
    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass = renderPass;
    rpbi.framebuffer = framebuffer;
    rpbi.renderArea = {{0, 0}, {kWidth, kHeight}};
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    renderInterface.BeginFrame(cmd, {kWidth, kHeight});
    context->Render();
    vkCmdEndRenderPass(cmd);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {kWidth, kHeight, 1};
    vkCmdCopyImageToBuffer(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
    VK_OK(vkEndCommandBuffer(cmd));
    timer.Mark("first_render_record (layout + glyph gen + geometry + texture + record)");
    const auto& compile = renderInterface.Stats();
    std::printf("UISTARTUP-DETAIL geometry_compile_ms %.3f (%u) texture_upload_ms %.3f (%u)\n", compile.compileGeometryMs,
                compile.geometries, compile.generateTextureMs, compile.textures);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VK_OK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    VK_OK(vkQueueWaitIdle(queue));
    timer.Mark("first_frame_gpu_execute");

    // Fingerprint the frame.
    void* mapped = nullptr;
    VK_OK(vkMapMemory(device, readbackMemory, 0, frameBytes, 0, &mapped));
    const std::uint64_t pixelHash = Vixen::Checksum64(mapped, static_cast<std::size_t>(frameBytes));
    std::printf("UISTARTUP-PIXELS %016llx %llu\n", static_cast<unsigned long long>(pixelHash),
                static_cast<unsigned long long>(frameBytes));
    if (const char* out = std::getenv("VIXEN_UI_STARTUP_OUT")) {
        std::filesystem::create_directories(out);
        std::ofstream frame(std::filesystem::path(out) / "frame.rgba", std::ios::binary);
        frame.write(static_cast<const char*>(mapped), static_cast<std::streamsize>(frameBytes));
    }
    vkUnmapMemory(device, readbackMemory);

    // Hit results over a fixed grid, for every shape the authored UI uses.
    {
        using namespace Vixen::RenderGraph;
        const HitMaskSpec specs[] = {ParseHitMask("none"), ParseHitMask("ellipse"), ParseHitMask("rounded-rect 12")};
        std::uint64_t acc = 1469598103934665603ULL;
        std::uint32_t count = 0;
        for (const HitMaskSpec& spec : specs)
            for (int y = 0; y <= 40; ++y)
                for (int x = 0; x <= 60; ++x) {
                    const unsigned char bit = HitMaskContains(spec, static_cast<float>(x), static_cast<float>(y), 60.0f, 40.0f);
                    acc = (acc ^ bit) * 1099511628211ULL;
                    ++count;
                }
        std::printf("UISTARTUP-HITS %016llx %u\n", static_cast<unsigned long long>(acc), count);
    }

    // View blob load (the data-file front end).
    timer.Reset();
    auto blob = Vixen::RenderGraph::ViewBlobFile::Load("assets/ui/hud.viewblob");
    timer.Mark("view_blob_load");
    EXPECT_TRUE(blob.has_value());

    // Teardown (not startup).
    vkDeviceWaitIdle(device);
    Rml::RemoveContext("ui_startup");
    Rml::Shutdown();
    renderInterface.Shutdown();
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    vkDestroyImageView(device, targetView, nullptr);
    vkDestroyImage(device, target, nullptr);
    vkFreeMemory(device, targetMemory, nullptr);
    vkDestroyBuffer(device, readback, nullptr);
    vkFreeMemory(device, readbackMemory, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}
