#pragma once

#include "Headers.h"
#include "VulkanResources/VulkanDevice.h"

using namespace Vixen::Vulkan::Resources;

class VulkanShader;
class VulkanDrawable;
class VulkanApplication;

#define NUMBER_OF_VIEWPORTS 1
#define NUMBER_OF_SCISSORS NUMBER_OF_VIEWPORTS

class VulkanPipeline {
public:
    // Configuration structure for creating pipeline
    struct Config {
        bool enableDepthTest = false;
        bool enableDepthWrite = false;
        bool enableVertexInput = false;
        VkViewport viewPort = {};
        VkRect2D scissor = {};
    };

public:
    VulkanPipeline();
    ~VulkanPipeline();

    // Creates the pipeline cache object and stores pipeline object
    void CreatePipelineCache();

    // Returns the created pipeline object, it takes the drawable object which contains
    // the vertex input rate and data interpretation information, shader files,
    // Boolean flag to check depth is supported or not, and a flag to check if the
    // vertex input are available.
    bool CreatePipeline(
        VulkanDrawable* drawableObj,
        VulkanShader* shaderObj,
        Config config,
        VkPipeline& pipeline
    );

    // Destruct the pipeline cache object.
    void DestroyPipelineCache();

    public:
    // The pipeline cache object
    VkPipelineCache pipelineCache;
    VkPipelineLayout pipelineLayout;

    // References to other user defined classes
    VulkanApplication* appObj;
    VulkanDevice* deviceObj;
};