#pragma once

#include "Headers.h"
#include "VulkanPipeline.h"
#include "VulkanShader.h"

class VulkanApplication;
class VulkanDevice;
class VulkanSwapChain;   
class VulkanRenderer;
class VulkanDrawable;

#define NUM_SAMPLES VK_SAMPLE_COUNT_1_BIT

class VulkanRenderer {

    public:
    VulkanRenderer(VulkanApplication* app, VulkanDevice* deviceObject);
    ~VulkanRenderer();

    public:
    void Initialize();
    void DeInitialize();
    void HandleResize();
    bool Render();
    void Prepare();

    // create an empty window for presentation
    void CreatePresentationWindow(const int& width = 500, const int& height = 500);
    void SetImageLayout(VkImage image, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout, VkAccessFlagBits srcAccessMask, const VkCommandBuffer &cmd);

    void DestroyPresentationWindow();

   

    void CreateCommandPool();
    void BuildSwapChainAndDepthImage();
    void CreateDepthImage();
    void CreateVertexBuffer();
    void CreateRenderPass(bool includeDepth, bool clear = true);
    void CreateFrameBuffer(bool includeDepth, bool clear = true);
    void CreateShaders();
    void CreatePipelineStateManagement();


    void DestroyCommandBuffer();
    void DestroyCommandPool();
    void DestroyDepthBuffer();
    void DestroyDrawableVertexBuffer();
    void DestroyRenderPass();
    void DestroyFrameBuffer();
    void DestroyPipeline();
	void DestroyShaders();

    inline VulkanApplication* GetApp() const { return appObj; }
    inline VulkanDevice* GetDevice() const { return deviceObj; }
    inline VulkanSwapChain* GetSwapChain() const { return swapChainObj.get(); }
    inline const std::vector<std::unique_ptr<VulkanDrawable>>& GetDrawingItems() const { return vecDrawables; }
    inline VkCommandPool GetCommandPool() const { return cmdPool; }
    inline VulkanShader* GetShader() const { return shaderObj.get(); }
    inline VulkanPipeline* GetPipeline() const { return pipelineState.get(); }


    #ifdef _WIN32

    HINSTANCE connection; // hInstance - Windows Instance
    WCHAR name[APP_NAME_STR_LEN]; // The name of the window class
    HWND window; // hWnd - The window handle

    private:
    void CreatePresentationWindowWin32(const int &windowWidth, const int &windowHeight);

    // window procedure method for handling events
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


    #else // _WIN32
    public:
    xcb_connection_t *connection;
    xcb_screen_t *screen;
    xcb_window_t window;
    xcb_intern_atom_reply_t *reply;

    private:
    void CreatePresentationWindowX(const int &windowWidth, const int &windowHeight);
    void DestroyWindow();

    #endif // _WIN32

    public:
    struct DepthImage {
        VkFormat format;
        VkImage image;
        VkDeviceMemory mem;
        VkImageView view;
    } Depth;

    VkCommandBuffer cmdDepthImage; // command buffer for setting the depth image layout
    VkCommandPool cmdPool;
    VkCommandBuffer cmdVertexBuffer; // command buffer for setting the vertex buffer image layout

    VkRenderPass renderPass;
    std::vector<VkFramebuffer> frameBuffers;
    std::vector<VkPipeline> pipelineHandles;

    int width, height;

    private:
    // --- Core ---
    VulkanApplication* appObj;
    VulkanDevice* deviceObj;
    VkQueue graphicsQueue = VK_NULL_HANDLE;

    // --- Persistent ---
    std::unique_ptr<VulkanShader> shaderObj;


    std::unique_ptr<VulkanSwapChain> swapChainObj;

    std::vector<std::unique_ptr<VulkanDrawable>> vecDrawables;
    std::unique_ptr<VulkanPipeline> pipelineState;

    bool isInitialized;
    bool frameBufferResized;
    bool isResizing;

};

