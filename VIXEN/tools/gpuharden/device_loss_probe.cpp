// Standalone provocation of a genuine GPU device loss (TDR / D3D12 device removal) and of the
// recreate-device recovery step, independent of the engine. A compute dispatch spins for
// `iterations` shader loop turns; the host waits on a fence bounded by --bound-ms.
//
//   device_loss_probe <spv> <iterations> [--bound-ms N] [--recover same|fresh|none]
//
// Build: g++ -O1 -std=c++20 -pthread -I$SDK/include device_loss_probe.cpp -o device_loss_probe -L$SDK/lib/VulkanLoader/lib -lvulkan -Wl,-rpath,$SDK/lib/VulkanLoader/lib
// Output is one `[PROBE] key=value ...` line per stage so callers can grep it.
// Exit code: 0 = completed or recovered, 3 = lost and recovery not attempted, 4 = recovery failed.
#include <vulkan/vulkan.h>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static void OnCrash(int sig) {
    void* frames[48];
    const int n = backtrace(frames, 48);
    std::fprintf(stderr, "[PROBE] stage=crash signal=%d\n", sig);
    backtrace_symbols_fd(frames, n, 2);
    _exit(128 + sig);
}

static double Ms(std::chrono::steady_clock::time_point a) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
}

struct Ctx {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
};

static bool MakeInstance(Ctx& c) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, nullptr, &c.instance) != VK_SUCCESS) return false;
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(c.instance, &n, nullptr);
    if (!n) return false;
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(c.instance, &n, devs.data());
    c.phys = devs[0];
    return true;
}

static VkResult MakeDevice(Ctx& c) {
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(c.phys, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(c.phys, &qn, qf.data());
    for (uint32_t i = 0; i < qn; ++i)
        if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { c.family = i; break; }
    float prio = 1.0f;
    VkDeviceQueueCreateInfo dq{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    dq.queueFamilyIndex = c.family;
    dq.queueCount = 1;
    dq.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &dq;
    const VkResult r = vkCreateDevice(c.phys, &dci, nullptr, &c.device);
    if (r == VK_SUCCESS) vkGetDeviceQueue(c.device, c.family, 0, &c.queue);
    return r;
}

static uint32_t FindHostMemory(VkPhysicalDevice phys, uint32_t bits) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) return i;
    return 0;
}

// Returns the fence-wait result. Times the wait.
static VkResult SpinDispatch(Ctx& c, const std::vector<char>& spv, uint32_t iterations,
                             uint64_t boundNs, double& waitMs, VkResult& submitResult) {
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = spv.size();
    smi.pCode = reinterpret_cast<const uint32_t*>(spv.data());
    VkShaderModule sm;
    vkCreateShaderModule(c.device, &smi, nullptr, &sm);
    VkDescriptorSetLayoutBinding b{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 1;
    dli.pBindings = &b;
    VkDescriptorSetLayout dl;
    vkCreateDescriptorSetLayout(c.device, &dli, nullptr, &dl);
    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &dl;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    VkPipelineLayout pl;
    vkCreatePipelineLayout(c.device, &pli, nullptr, &pl);
    VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, sm, "main", nullptr};
    cpi.layout = pl;
    VkPipeline pipe;
    vkCreateComputePipelines(c.device, VK_NULL_HANDLE, 1, &cpi, nullptr, &pipe);

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = 1 << 20;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkBuffer buf;
    vkCreateBuffer(c.device, &bi, nullptr, &buf);
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(c.device, buf, &mr);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = FindHostMemory(c.phys, mr.memoryTypeBits);
    VkDeviceMemory mem;
    vkAllocateMemory(c.device, &mai, nullptr, &mem);
    vkBindBufferMemory(c.device, buf, mem, 0);

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    VkDescriptorPool dp;
    vkCreateDescriptorPool(c.device, &dpi, nullptr, &dp);
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = dp;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &dl;
    VkDescriptorSet ds;
    vkAllocateDescriptorSets(c.device, &dai, &ds);
    VkDescriptorBufferInfo dbi{buf, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet wds{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wds.dstSet = ds;
    wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wds.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(c.device, 1, &wds, 0, nullptr);

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.queueFamilyIndex = c.family;
    VkCommandPool cp;
    vkCreateCommandPool(c.device, &cpci, nullptr, &cp);
    VkCommandBufferAllocateInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbi.commandPool = cp;
    cbi.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(c.device, &cbi, &cb);
    VkCommandBufferBeginInfo bgi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cb, &bgi);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, nullptr);
    vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &iterations);
    vkCmdDispatch(cb, 1024, 1, 1);
    vkEndCommandBuffer(cb);

    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    vkCreateFence(c.device, &fci, nullptr, &fence);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    submitResult = vkQueueSubmit(c.queue, 1, &si, fence);
    const auto t0 = std::chrono::steady_clock::now();
    VkResult w = submitResult == VK_SUCCESS ? vkWaitForFences(c.device, 1, &fence, VK_TRUE, boundNs) : submitResult;
    waitMs = Ms(t0);
    return w;
}

int main(int argc, char** argv) {
    signal(SIGSEGV, OnCrash);
    signal(SIGABRT, OnCrash);
    if (argc < 3) { std::fprintf(stderr, "usage: %s <spv> <iterations> [--bound-ms N] [--recover same|fresh|none]\n", argv[0]); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<char> spv((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const uint32_t iterations = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
    uint64_t boundMs = 15000;
    std::string recover = "none";
    for (int i = 3; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--bound-ms")) boundMs = std::strtoull(argv[i + 1], nullptr, 10);
        else if (!std::strcmp(argv[i], "--recover")) recover = argv[i + 1];
    }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::atomic<bool> done{false};
    std::thread heartbeat([&done] {  // keeps queue stall detectors from reading a long bounded wait as a hang
        for (int ticks = 0; !done.load(); ++ticks) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (ticks % 60 == 59) std::fprintf(stderr, "[PROBE] heartbeat\n");
        }
    });
    struct Join { std::atomic<bool>& d; std::thread& t; ~Join() { d = true; t.join(); } } join{done, heartbeat};
    Ctx c;
    if (!MakeInstance(c) || MakeDevice(c) != VK_SUCCESS) { std::fprintf(stderr, "[PROBE] stage=init result=fail\n"); return 2; }
    double waitMs = 0;
    VkResult submitResult = VK_SUCCESS;
    const VkResult w = SpinDispatch(c, spv, iterations, boundMs * 1000000ull, waitMs, submitResult);
    std::printf("[PROBE] stage=dispatch iterations=%u submit=%d wait=%d wait_ms=%.1f\n", iterations, submitResult, w, waitMs);
    std::fflush(stdout);
    if (w == VK_SUCCESS) return 0;
    if (recover == "none") return 3;

    const auto t0 = std::chrono::steady_clock::now();
    vkDeviceWaitIdle(c.device);
    std::printf("[PROBE] stage=wait-idle ms=%.1f\n", Ms(t0));
    std::fflush(stdout);
    vkDestroyDevice(c.device, nullptr);
    c.device = VK_NULL_HANDLE;
    if (recover == "fresh") {
        vkDestroyInstance(c.instance, nullptr);
        c.instance = VK_NULL_HANDLE;
        // The D3D12 adapter stays unusable for a while after removal; retry the instance with backoff.
        int attempt = 0;
        const auto tr = std::chrono::steady_clock::now();
        while (!MakeInstance(c)) {
            if (c.instance != VK_NULL_HANDLE) { vkDestroyInstance(c.instance, nullptr); c.instance = VK_NULL_HANDLE; }
            if (++attempt >= 20) { std::printf("[PROBE] stage=recover mode=fresh result=no-instance attempts=%d\n", attempt); return 4; }
            std::this_thread::sleep_for(std::chrono::milliseconds(500 * attempt));
        }
        std::printf("[PROBE] stage=recover-instance attempts=%d ms=%.1f\n", attempt + 1, Ms(tr));
    }
    std::printf("[PROBE] stage=recover-begin mode=%s\n", recover.c_str());
    std::fflush(stdout);
    const VkResult r = MakeDevice(c);
    std::printf("[PROBE] stage=recover mode=%s create_device=%d\n", recover.c_str(), r);
    if (r != VK_SUCCESS) return 4;
    double ms2 = 0;
    VkResult s2 = VK_SUCCESS;
    const VkResult w2 = SpinDispatch(c, spv, 1000, 15000ull * 1000000ull, ms2, s2);
    std::printf("[PROBE] stage=post-recover-dispatch submit=%d wait=%d wait_ms=%.1f\n", s2, w2, ms2);
    return w2 == VK_SUCCESS ? 0 : 4;
}
