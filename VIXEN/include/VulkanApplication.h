#pragma once

#include <Headers.h>
#include "VulkanInstance.h"
#include "VulkanDevice.h"

class VulkanRenderer;

class VulkanApplication {
private:
	VulkanApplication();
public:
	~VulkanApplication();

private:
    // variable for singleton pattern
    static std::unique_ptr<VulkanApplication> instance;
    static std::once_flag onlyOnce;

public:
    static VulkanApplication* GetInstance();
    void Initialize();
    void DeInitialize();
    void Prepare();
    void Update();
    bool render();

    inline bool IsPrepared() const { return isPrepared; }


private:
    VkResult CreateVulkanInstance(std::vector<const char*>& layers,
                                  std::vector<const char*>& extensions,
                                  const char* applicationName);

    VkResult HandShakeWithDevice(VkPhysicalDevice *gpu, std::vector<const char *> &layers, std::vector<const char *> &extensions);
    VkResult EnumeratePhysicalDevices(std::vector<VkPhysicalDevice>& gpuList);


public:
    VulkanInstance instanceObj; // Vulkan instance object variable
    std::unique_ptr<VulkanDevice> deviceObj; // Vulkan device object variable
    std::unique_ptr<VulkanRenderer> renderObj; // Vulkan renderer object variable

private:
    bool debugFlag; // enable or disable debug callback
    bool isPrepared = false;

    // Store gpuList as member to prevent it from going out of scope
    std::vector<VkPhysicalDevice> gpuList;


};
