#pragma once

#include <Headers.h>
#include "error/VulkanError.h"
#include "VulkanInstance.h"
#include "VulkanDevice.h"
#include "Logger.h"

using namespace Vixen::Vulkan::Resources;

/**
 * @brief Base class for Vulkan applications
 * 
 * Provides core Vulkan initialization, device management, and lifecycle methods.
 * Derived classes implement specific rendering strategies (e.g., traditional renderer or graph-based).
 */
class VulkanApplicationBase {
protected:
    VulkanApplicationBase();
    
public:
    virtual ~VulkanApplicationBase();

    // Prevent copying
    VulkanApplicationBase(const VulkanApplicationBase&) = delete;
    VulkanApplicationBase& operator=(const VulkanApplicationBase&) = delete;

    // ====== Core Lifecycle Methods ======

    /**
     * @brief Initialize the Vulkan application
     * 
     * Sets up Vulkan instance, devices, and prepares the rendering subsystem.
     */
    virtual void Initialize();

    /**
     * @brief Prepare the application for rendering
     * 
     * Called after initialization to set up rendering resources.
     */
    virtual void Prepare() = 0;

    /**
     * @brief Update application state
     * 
     * Called each frame to update application logic.
     */
    virtual void Update() = 0;

    /**
     * @brief Render a frame
     * @return true if rendering succeeded, false otherwise
     */
    virtual bool Render() = 0;

    /**
     * @brief Clean up and destroy all Vulkan resources
     */
    virtual void DeInitialize();

    // ====== Getters ======

    inline bool IsPrepared() const { return isPrepared; }
    inline VulkanInstance* GetInstance() { return &instanceObj; }
    inline std::shared_ptr<Logger> GetLogger() const { return mainLogger; }

    // Public access for compatibility with existing code
    VulkanInstance instanceObj;                    // Vulkan instance
    std::shared_ptr<Logger> mainLogger;            // Application logger

protected:
    // ====== Protected Helper Methods ======

    /**
     * @brief Create Vulkan instance
     */
    VulkanStatus CreateVulkanInstance(std::vector<const char*>& layers,
                                       std::vector<const char*>& extensions,
                                       const char* applicationName);

    

    /**
     * @brief Enumerate available physical devices
     */
    VulkanStatus EnumeratePhysicalDevices(std::vector<VkPhysicalDevice>& gpuList);


    /**
     * @brief Initialize core Vulkan (instance and device)
     */
    void InitializeVulkanCore();

protected:
    // ====== State ======
    bool debugFlag;                                 // Debug mode enabled
    bool isPrepared;                                // Ready to render
};
