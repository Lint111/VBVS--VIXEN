#pragma once

#include <Headers.h>
#include "VulkanApplicationBase.h"
#include "error/VulkanError.h"
#include "Logger.h"

class VulkanRenderer;

using namespace Vixen::Vulkan::Resources;

/**
 * @brief Traditional Vulkan application with renderer-based rendering
 * 
 * Uses VulkanRenderer for traditional forward rendering pipeline.
 * Maintains singleton pattern for backward compatibility.
 */
class VulkanApplication : public VulkanApplicationBase {
private:
	VulkanApplication();
	
public:
	~VulkanApplication() override;

private:
    // Variable for singleton pattern
    static std::unique_ptr<VulkanApplication> instance;
    static std::once_flag onlyOnce;

public:
    static VulkanApplication* GetInstance();
    
    // Override base class methods
    void Initialize() override;
    void DeInitialize() override;
    void Prepare() override;
    void Update() override;
    bool Render() override;

    // Legacy method name support
    inline bool render() { return Render(); }

    // Renderer access
    inline VulkanRenderer* GetRenderer() const { return renderObj.get(); }

    // Public for compatibility
    std::unique_ptr<VulkanRenderer> renderObj; // Vulkan renderer object variable
};
