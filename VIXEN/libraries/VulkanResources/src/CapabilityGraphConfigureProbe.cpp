#include "CapabilityGraph.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

VkPhysicalDevice SelectDefaultDevice(const std::vector<VkPhysicalDevice>& devices) {
    if (devices.empty()) return VK_NULL_HANDLE;
    for (const VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) return device;
    }
    return devices.front();
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 8) {
        std::cerr << "usage: <header> <mode> <system> <architecture> <compiler-id> "
                     "<compiler-version> <product-variant>\n";
        return 2;
    }

    const std::string outputPath = argv[1];
    const std::string mode = argv[2];
    const bool personalized = mode == "PERSONALIZED";

    Vixen::CapabilityGraph graph;
    graph.BuildStandardCapabilities();
    graph.RegisterBuildFact("BuildFact:Platform",
        Vixen::PlatformBuildFact{argv[3], argv[4], argv[5], argv[6]});
    graph.RegisterBuildFact("BuildFact:ProductVariant",
        Vixen::ProductVariantBuildFact{argv[7]});

    VkInstance instance = VK_NULL_HANDLE;
    if (personalized) {
        VkApplicationInfo applicationInfo{};
        applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        applicationInfo.pApplicationName = "VIXEN CapabilityGraph configure probe";
        applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        applicationInfo.pEngineName = "VIXEN";
        applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        applicationInfo.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &applicationInfo;
        const VkResult createResult = vkCreateInstance(&createInfo, nullptr, &instance);
        if (createResult != VK_SUCCESS) {
            std::cerr << "vkCreateInstance failed during the personalized capability probe: "
                      << createResult << '\n';
            return 3;
        }

        uint32_t deviceCount = 0;
        VkResult result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (result != VK_SUCCESS || deviceCount == 0) {
            std::cerr << "The personalized configure probe found no Vulkan physical device ("
                      << result << ").\n";
            vkDestroyInstance(instance, nullptr);
            return 4;
        }
        std::vector<VkPhysicalDevice> devices(deviceCount);
        result = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
        if (result != VK_SUCCESS) {
            std::cerr << "vkEnumeratePhysicalDevices failed during configure: " << result << '\n';
            vkDestroyInstance(instance, nullptr);
            return 5;
        }
        const VkPhysicalDevice selected = SelectDefaultDevice(devices);
        if (selected == VK_NULL_HANDLE) {
            std::cerr << "The personalized configure probe could not select a Vulkan device.\n";
            vkDestroyInstance(instance, nullptr);
            return 6;
        }
        graph.ObservePhysicalDevice(selected);
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(selected, &properties);
        std::cout << "VIXEN CapabilityGraph personalized device: " << properties.deviceName << '\n';
    }

    graph.SetAllBindingTimes(personalized ? Vixen::CapabilityBindingTime::BuildTime
                                          : Vixen::CapabilityBindingTime::Runtime);
    try {
        graph.GenerateBuildConfigHeader(outputPath, personalized);
    } catch (const std::exception& exception) {
        if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
        std::cerr << exception.what() << '\n';
        return 7;
    }

    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
    return 0;
}
