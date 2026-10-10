// A second Vulkan client for R477: one tiny compute frame per declared period.
// Build with the same SDK as VIXEN; pass probe SPIR-V, CSV path, seconds and period ns.
#include <vulkan/vulkan.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

static void Check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        std::cerr << operation << ": VkResult=" << result << '\n';
        std::exit(1);
    }
}

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "Usage: gpu-cadence-probe <spv> <csv> <seconds> <period-ns>\n";
        return 2;
    }
    const double seconds = std::strtod(argv[3], nullptr);
    const auto period = std::chrono::nanoseconds(std::strtoll(argv[4], nullptr, 10));
    if (seconds <= 0 || period.count() <= 0) return 2;
    std::ifstream shader(argv[1], std::ios::binary | std::ios::ate);
    if (!shader || shader.tellg() <= 0 || shader.tellg() % 4 != 0) return 2;
    std::vector<uint32_t> spirv(static_cast<size_t>(shader.tellg()) / 4);
    shader.seekg(0);
    shader.read(reinterpret_cast<char*>(spirv.data()), spirv.size() * 4);
    std::ofstream csv(argv[2]);
    if (!shader || !csv) return 2;

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "VIXEN R477 cadence probe";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance instance{};
    Check(vkCreateInstance(&ici, nullptr, &instance), "create instance");
    uint32_t count = 0;
    Check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "count devices");
    if (count == 0) return 1;
    std::vector<VkPhysicalDevice> devices(count);
    Check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate devices");
    const auto gpu = devices.front(); // The witness supplies the same single-device ICD as VIXEN.
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(gpu, &properties);
    std::cerr << "device=" << properties.deviceName << " period_ns=" << period.count() << '\n';
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, families.data());
    uint32_t family = 0;
    while (family < familyCount && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
    if (family == familyCount) return 1;
    const float priority = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    VkDevice device{};
    Check(vkCreateDevice(gpu, &dci, nullptr, &device), "create device");
    VkQueue queue{};
    vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = family;
    VkCommandPool pool{};
    Check(vkCreateCommandPool(device, &pci, nullptr, &pool), "create command pool");
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer command{};
    Check(vkAllocateCommandBuffers(device, &cai, &command), "allocate command buffer");

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = 4;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkBuffer buffer{};
    Check(vkCreateBuffer(device, &bci, nullptr, &buffer), "create buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &memoryProperties);
    uint32_t memoryType = 0;
    constexpr auto hostFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while (memoryType < memoryProperties.memoryTypeCount &&
           (!(requirements.memoryTypeBits & (1u << memoryType)) ||
            (memoryProperties.memoryTypes[memoryType].propertyFlags & hostFlags) != hostFlags)) ++memoryType;
    if (memoryType == memoryProperties.memoryTypeCount) return 1;
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = requirements.size;
    mai.memoryTypeIndex = memoryType;
    VkDeviceMemory memory{};
    Check(vkAllocateMemory(device, &mai, nullptr, &memory), "allocate memory");
    Check(vkBindBufferMemory(device, buffer, memory, 0), "bind memory");
    void* mapped = nullptr;
    Check(vkMapMemory(device, memory, 0, 4, 0, &mapped), "map output");
    *static_cast<uint32_t*>(mapped) = 0;

    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = 1;
    lci.pBindings = &binding;
    VkDescriptorSetLayout setLayout{};
    Check(vkCreateDescriptorSetLayout(device, &lci, nullptr, &setLayout), "create descriptor layout");
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &setLayout;
    VkPipelineLayout layout{};
    Check(vkCreatePipelineLayout(device, &plci, nullptr, &layout), "create pipeline layout");
    VkShaderModuleCreateInfo sci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    sci.codeSize = spirv.size() * 4;
    sci.pCode = spirv.data();
    VkShaderModule module{};
    Check(vkCreateShaderModule(device, &sci, nullptr, &module), "create shader module");
    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci.layout = layout;
    cpci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                  VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr};
    VkPipeline pipeline{};
    Check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline), "create compute pipeline");
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &poolSize;
    VkDescriptorPool descriptorPool{};
    Check(vkCreateDescriptorPool(device, &dpci, nullptr, &descriptorPool), "create descriptor pool");
    VkDescriptorSetAllocateInfo sai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    sai.descriptorPool = descriptorPool;
    sai.descriptorSetCount = 1;
    sai.pSetLayouts = &setLayout;
    VkDescriptorSet set{};
    Check(vkAllocateDescriptorSets(device, &sai, &set), "allocate descriptor set");
    VkDescriptorBufferInfo bufferInfo{buffer, 0, 4};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    Check(vkBeginCommandBuffer(command, &begin), "begin command buffer");
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
    vkCmdDispatch(command, 1, 1, 1);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
    Check(vkEndCommandBuffer(command), "end command buffer");
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence{};
    Check(vkCreateFence(device, &fci, nullptr, &fence), "create fence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    const auto end = started + std::chrono::duration<double>(seconds);
    auto deadline = started;
    auto previousCompletion = started;
    csv << "frame,scheduled_ms,submit_ms,complete_ms,service_ms,cadence_ms,deadline_lateness_ms,complete_monotonic_ns\n";
    uint64_t frame = 0;
    while (Clock::now() < end) {
        std::this_thread::sleep_until(deadline);
        const auto submitted = Clock::now();
        Check(vkQueueSubmit(queue, 1, &submit, fence), "submit frame");
        Check(vkWaitForFences(device, 1, &fence, VK_TRUE, 5'000'000'000ull), "wait for frame");
        const auto complete = Clock::now();
        if (*static_cast<uint32_t*>(mapped) != 0x477u) return 1;
        const auto ms = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
        csv << ++frame << ',' << ms(deadline - started) << ',' << ms(submitted - started) << ','
            << ms(complete - started) << ',' << ms(complete - submitted) << ','
            << ms(complete - previousCompletion) << ',' << ms(complete - deadline) << ','
            << std::chrono::duration_cast<std::chrono::nanoseconds>(complete.time_since_epoch()).count() << '\n';
        previousCompletion = complete;
        Check(vkResetFences(device, 1, &fence), "reset fence");
        // Missed slots do not trigger a burst of catch-up submissions.
        deadline += period;
        if (deadline < complete) deadline = complete + period;
    }
    Check(vkDeviceWaitIdle(device), "drain device");
    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, module, nullptr);
    vkDestroyPipelineLayout(device, layout, nullptr);
    vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    vkUnmapMemory(device, memory);
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}
