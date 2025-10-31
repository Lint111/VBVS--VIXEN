# Vulkan Rendering Pipeline: Node-Based vs. Legacy Implementations

## Overview
This document details the inputs, outputs, and parameters for each step in the Vulkan rendering pipeline, comparing the new node-based architecture (`CommandPoolNode`, etc.) with the legacy monolithic approach (`VulkanRenderer`, `VulkanDrawable`).

---

## Node-Based Implementation (Modern)

### 1. CommandPoolNode
- **Inputs:**
  - `DeviceObj` (VkDevice): Logical Vulkan device to create the command pool on
- **Outputs:**
  - `COMMAND_POOL` (VkCommandPool): Created command pool
- **Parameters:**
  - `queue_family_index` (uint32_t): Queue family index for the pool

#### Node Lifecycle
- **Setup:** Prepares node, validates parameters
- **Compile:** Creates the command pool using provided device and queue family index
- **Execute:** (Typically not needed for pool creation)
- **Cleanup:** Destroys the command pool

---

### Node-Based Principles
- **No rendererObj reference:**
  - All required Vulkan handles (device, physical device, etc.) must be explicit node inputs
- **Inputs/Outputs:**
  - Each node exposes its dependencies and products via typed input/output ports
- **Parameters:**
  - Node parameters are explicit, validated at compile-time and runtime

---

## Legacy Implementation (Monolithic)

### VulkanRenderer
- **Inputs:**
  - `VulkanApplicationBase* appObj`: Application context
  - `VulkanDevice* deviceObj`: Logical device
  - (Implicit) Physical device, swapchain, etc. via member variables
- **Outputs:**
  - `VkCommandPool cmdPool`: Command pool
  - `VkRenderPass renderPass`: Render pass
  - `std::vector<VkFramebuffer> frameBuffers`: Framebuffers
  - `std::vector<std::unique_ptr<VulkanDrawable>> vecDrawables`: Drawable objects
  - `std::unique_ptr<VulkanPipeline> pipelineState`: Pipeline state
  - `std::unique_ptr<VulkanShader> shaderObj`: Shader
  - `TextureData texture`: Texture
- **Parameters:**
  - `width`, `height`: Window size
  - Various flags (e.g., `isInitialized`, `frameBufferResized`)
  - Swapchain, depth image, etc. managed internally

#### Legacy Steps
- **CreateCommandPool:** Uses `deviceObj` and internal state
- **BuildSwapChainAndDepthImage:** Uses `deviceObj`, window size
- **CreateVertexBuffer:** Uses geometry data, device
- **CreateRenderPass:** Uses device, depth flag
- **CreateFrameBuffer:** Uses device, render pass
- **CreateShaders:** Uses device, shader sources
- **CreatePipelineStateManagement:** Uses device, pipeline config
- **CreateDescriptors:** Uses device, descriptor config

---

### VulkanDrawable
- **Inputs:**
  - `VulkanRenderer* rendererObj`: Renderer context
  - Geometry data, texture data
- **Outputs:**
  - `VkBuffer VertexBuffer.buf`: Vertex buffer
  - `VkDeviceMemory VertexBuffer.mem`: Vertex buffer memory
  - `VkDescriptorBufferInfo VertexBuffer.bufferInfo`: Buffer info
  - `VkBuffer IndexBuffer.buf`: Index buffer
  - `VkDeviceMemory IndexBuffer.mem`: Index buffer memory
  - `VkDescriptorBufferInfo IndexBuffer.bufferInfo`: Buffer info
  - `UniformData`: Uniform buffer, memory, descriptor info
  - `VkPipeline pipelineHandle`: Graphics pipeline
- **Parameters:**
  - Vertex data, index data, stride, texture usage flag
  - Transformation matrices (Projection, View, Model, MVP)

#### Legacy Steps
- **CreateVertexBuffer:** Uses vertex data, stride, texture flag
- **CreateVertexIndex:** Uses index data, stride
- **CreatePipelineLayout:** Uses device, descriptor set layout
- **CreateDescriptorSetLayout:** Uses device, texture flag
- **CreateDescriptorPool:** Uses device, texture flag
- **CreateDescriptorSet:** Uses device, texture flag
- **CreateDescriptorResources:** Uses device
- **CreateUniformBuffer:** Uses device

---

## Node-Based Migration Notes
- **Device/PhysicalDevice:**
  - Must be explicit node inputs (no global rendererObj)
- **Resource Ownership:**
  - Each node owns its Vulkan resources (RAII)
- **Parameterization:**
  - All configuration is explicit, not inferred from renderer state
- **Event-Driven:**
  - Resource invalidation and updates handled via EventBus

---

## Step-by-Step Inputs/Outputs/Parameters Table

| Step                        | Inputs                              | Outputs                        | Parameters                |
|-----------------------------|-------------------------------------|-------------------------------|---------------------------|
| CommandPoolNode             | VkDevice, queue_family_index         | VkCommandPool                  | queue_family_index        |
| VertexBufferNode (modern)   | VkDevice, geometry data, stride      | VkBuffer, VkDeviceMemory       | vertex_count, stride      |
| DescriptorSetNode (modern)  | VkDevice, layout info                | VkDescriptorSet, VkDescriptorPool, VkDescriptorSetLayout | include_texture, bindings |
| GraphicsPipelineNode        | VkDevice, shader stages, layout, render pass | VkPipeline, VkPipelineLayout | depth_test, cull_mode, topology |
| FramebufferNode             | VkDevice, images, render pass        | VkFramebuffer                  | width, height             |
| SwapChainNode               | VkDevice, surface, window            | VkSwapchainKHR, images         | width, height, format     |

---

## Summary
- **Node-based system:** All dependencies and resources are explicit, modular, and validated.
- **Legacy system:** Dependencies are implicit, tightly coupled to rendererObj and global state.
- **Migration:** Ensure every Vulkan resource needed by a node is provided as an input, not assumed from a global context.
