#include "VulkanRenderer.h"
#include "VulkanApplication.h"
#include "VulkanResources/VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "wrapper.h"
#include "VulkanDrawable.h"
#include "VulkanPipeline.h"
#include "MeshData.h"

// STB Image - define implementation in this single translation unit
#include "TextureHandling/Loading/STBTextureLoader.h"
// GLI Texture Loader
#include "TextureHandling/Loading/GLITextureLoader.h"

VulkanRenderer::VulkanRenderer(VulkanApplicationBase* app, VulkanDevice* deviceObject, bool createSwapChain) {
    std::cout << "[VulkanRenderer] Constructor START (createSwapChain=" << createSwapChain << ")" << std::endl;

    // Note: app can be nullptr when used only for window creation (e.g., VulkanGraphApplication)
    if (!deviceObject) {
        std::cerr << "Fatal error: VulkanRenderer requires non-null device object" << std::endl;
        exit(1);
    }

    std::cout << "[VulkanRenderer] Device OK, initializing members" << std::endl;

    memset(&Depth, 0, sizeof(Depth));
    memset(&texture, 0, sizeof(texture));
    connection = GetModuleHandle(nullptr);
    wcscpy_s(name, APP_NAME_STR_LEN, L"Vulkan Window");

    appObj = app;
    deviceObj = deviceObject;

    // Initialize window dimensions (will be set properly in CreatePresentationWindow)
    width = 0;
    height = 0;
    window = nullptr;

    // Only create swapchain if requested (graph-based apps manage their own)
    if (createSwapChain) {
        std::cout << "[VulkanRenderer] Creating SwapChain..." << std::endl;
        swapChainObj = std::make_unique<VulkanSwapChain>(this);
    } else {
        std::cout << "[VulkanRenderer] Skipping SwapChain creation (managed externally)" << std::endl;
    }

    std::cout << "[VulkanRenderer] Constructor COMPLETE" << std::endl;

    // Initialize Vulkan handles
    renderPass = VK_NULL_HANDLE;
    cmdPool = VK_NULL_HANDLE;
    cmdDepthImage = VK_NULL_HANDLE;
    cmdVertexBuffer = VK_NULL_HANDLE;
    isInitialized = false;
    frameBufferResized = false;
    isResizing = false;

    // Create FPS logger only if this renderer will actually manage a swapchain
    // (i.e., it's being used for on-screen rendering). Graph-based applications
    // may create VulkanRenderer instances only for helper tasks and don't need
    // an FPS logger attached to the main logger.
    if (createSwapChain && appObj && appObj->mainLogger) {
        fpsLogger = std::make_shared<FrameRateLogger>("Renderer", true);
        // To attach the fps logger to the main logger use:
        // appObj->mainLogger->AddChild(fpsLogger.get());
        // The call is intentionally commented out - registration should be
        // explicit and intentional elsewhere in application code.
    }

    // Create a drawable object for rendering
    auto drawable = std::make_unique<VulkanDrawable>(this);
    if (auto result = drawable->Initialize(); !result) {
        std::cerr << "Failed to initialize drawable: " << result.error().toString() << std::endl;
        exit(1);
    }
    vecDrawables.push_back(std::move(drawable));

}

VulkanRenderer::~VulkanRenderer() {
    DeInitialize();
}

void VulkanRenderer::Initialize()
{
    // Only create window if it doesn't exist (initial setup)
    constexpr int DEFAULT_WINDOW_WIDTH = 500;
    constexpr int DEFAULT_WINDOW_HEIGHT = 500;
    if (window == nullptr) {
        CreatePresentationWindow(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);
    }

    // Swap chain object is created in constructor, but Initialize() needs to be called
    // During first run: creates surface, loads extension function pointers
    // During resize: surface already exists, function pointers already loaded
    if (!swapChainObj) {
        swapChainObj = std::make_unique<VulkanSwapChain>(this);
    }

    // Always call Initialize - it handles both first-time and resize scenarios
    swapChainObj->Initialize();

    CreateCommandPool();

    // Set loader for texture handling BEFORE BuildSwapChainAndDepthImage (needed for CreateDepthImage)
    textureLoader = std::make_unique<STBTextureLoader>(deviceObj, cmdPool);


    BuildSwapChainAndDepthImage();

    // Drawables are created in constructor, no need to recreate
    if (vecDrawables.empty()) {
        auto drawable = std::make_unique<VulkanDrawable>(this);
        if (auto result = drawable->Initialize(); !result) {
            std::cerr << "Failed to initialize drawable: " << result.error().toString() << std::endl;
            exit(1);
        }
        vecDrawables.push_back(std::move(drawable));
    }

    CreateVertexBuffer();

    const bool includeDepth = true;
    CreateRenderPass(includeDepth);
    CreateFrameBuffer(includeDepth);

    CreateShaders();

    const char* fileName = "C:\\Users\\liory\\Downloads\\earthmap.jpg";

    if (!textureLoader) {
        std::cerr << "ERROR: textureLoader is NULL!" << std::endl;
        exit(1);
    }

    TextureLoadConfig config;
    config.uploadMode = TextureLoadConfig::UploadMode::Optimal;
    texture = textureLoader->Load(fileName, config);
    std::cout << "Texture loaded successfully!" << std::endl;


    for (auto& drawable : vecDrawables) {
        drawable->SetTexture(&texture);
    }

    CreateDescriptors();

    CreatePipelineStateManagement();
    
    isInitialized = true;
}


void VulkanRenderer::DeInitialize()
{
    isInitialized = false;

    DestroyDrawableVertexBuffer();
    DestroyFrameBuffer();
    DestroyDepthBuffer();
    DestroyRenderPass();
    DestroyPipeline();
    DestroyShaders();
    DestroyTexture();

    vecDrawables.clear();

    if (swapChainObj) {
        swapChainObj->DestroySwapChain();
        swapChainObj.reset();
    }

    DestroyCommandPool();
    DestroyPresentationWindow();
}

void VulkanRenderer::HandleResize()
{
    if (!isInitialized || !frameBufferResized) {
        return; // No need to resize if not initialized or no resize event
    }
    isInitialized = false;

    // Get current window dimensions
    RECT rect;
    if (GetClientRect(window, &rect)) {
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;

        if (swapChainObj) {
            swapChainObj->SetSwapChainExtent(width, height);
        }
    }

    vkDeviceWaitIdle(deviceObj->device);

    // Free command buffers before recreating resources
    if (cmdDepthImage != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(deviceObj->device, cmdPool, 1, &cmdDepthImage);
        cmdDepthImage = VK_NULL_HANDLE;
    }

    DestroyFrameBuffer();
    DestroyDepthBuffer();
    swapChainObj->DestroySwapChain();

    BuildSwapChainAndDepthImage();
    CreateFrameBuffer(true);

    // Re-record drawable command buffers with new framebuffers
    for (auto& drawable : vecDrawables) {
        drawable->Prepare();
    }

    isInitialized = true;
    frameBufferResized = false;
}

void VulkanRenderer::Prepare()
{
    for (auto& drawable : vecDrawables) {
        drawable->Prepare();
    }
}

bool VulkanRenderer::Render()
{
    // Start frame timing
    if (fpsLogger) {
        fpsLogger->FrameStart();
    }

    // Process all pending window messages (non-blocking)
    MSG msg;
    while(PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if(msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if(frameBufferResized) {
        HandleResize();
    }

    // Skip rendering during resize
    if (isResizing) {
        return true;
    }

    // Render directly here (not in WM_PAINT) for maximum framerate
    if (isInitialized) {
        for (auto& drawableObj : vecDrawables) {
            VkResult result = drawableObj->Render();
            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
                frameBufferResized = true;
                return true;
            }
            if (result != VK_SUCCESS) {
                std::cerr << "Render Error: " << result << std::endl;
                return false;
            }
        }
    }

    // End frame timing
    if (fpsLogger) {
        fpsLogger->FrameEnd();
    }

    return true;
}

void VulkanRenderer::Update()
{
    // Update time system
    time.Update();

    // Update all drawables with delta time
    for (auto& drawable : vecDrawables) {
        if (auto result = drawable->Update(time.GetDeltaTime()); !result) {
            std::cerr << "Failed to update drawable: " << result.error().toString() << std::endl;
            exit(1);
        }
    }
}

void VulkanRenderer::CreatePresentationWindow(const int &windowWidth, const int &windowHeight)
{
    #ifdef _WIN32
    CreatePresentationWindowWin32(windowWidth, windowHeight);
    #else // _WIN32
    CreatePresentationWindowX(windowWidth, windowHeight);
    #endif // _WIN32
}

#ifdef _WIN32
void  VulkanRenderer::CreatePresentationWindowWin32(const int &windowWidth, const int &windowHeight)
{
    if (windowWidth <= 0 || windowHeight <= 0) {
        std::cerr << "Invalid window dimensions: " << windowWidth << "x" << windowHeight << std::endl;
        exit(1);
    }

    width = windowWidth;
    height = windowHeight;

    WNDCLASSEXW winInfo;
    memset(&winInfo, 0, sizeof(WNDCLASSEXW));
    winInfo.cbSize = sizeof(WNDCLASSEXW);
    winInfo.style = CS_HREDRAW | CS_VREDRAW;
    winInfo.lpfnWndProc = WndProc;
    winInfo.cbClsExtra = 0;
    winInfo.cbWndExtra = 0;
    winInfo.hInstance = connection;
    winInfo.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    winInfo.hCursor = LoadCursor(nullptr, IDC_ARROW);
    winInfo.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    winInfo.lpszMenuName = nullptr;
    winInfo.lpszClassName = name;
    winInfo.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    // Try to register the window class
    // If it's already registered (during resize), that's okay
    if(!RegisterClassExW(&winInfo)) {
        DWORD error = GetLastError();
        // ERROR_CLASS_ALREADY_EXISTS (1410) is okay
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            printf("Unexpected error registering window class: %lu\n", error);
            fflush(stdout);
            exit(1);
        }
    }

    RECT wr = {0, 0, width, height};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    window = CreateWindowExW(
        0,
        name,
        name,
        WS_OVERLAPPEDWINDOW | 
        WS_VISIBLE | 
        WS_SYSMENU,
        100, 100, 
        wr.right - wr.left, 
        wr.bottom - wr.top,
        nullptr,
        nullptr,
        connection,
        nullptr
    );

    if(!window) {
        printf("Cannot create a window in which to draw!\n");
        fflush(stdout);
        exit(1);
    }

    SetWindowLongPtr(window, GWLP_USERDATA, (LONG_PTR)this);

    // Ensure the window is shown and updated
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
}
#endif // _WIN32

#ifdef _WIN32
LRESULT VulkanRenderer::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    VulkanApplication* appObj = VulkanApplication::GetInstance();
    VulkanRenderer* renderObj = appObj ? appObj->renderObj.get() : nullptr;

    if(!appObj || !renderObj) {
        std::cerr << "Fatal Error: Application or Renderer object is null in WndProc!" << std::endl;
        exit(1);
    }


    VkResult result;

    switch (msg) {
        case WM_CLOSE:
            PostQuitMessage(0);
            break;

        case WM_ENTERSIZEMOVE:
            // User started resizing
            if (renderObj->isInitialized) {
                renderObj->isResizing = true;
            }
            break;

        case WM_EXITSIZEMOVE:
            // User finished resizing - rebuild swapchain
            if (renderObj->isResizing) {
                renderObj->isResizing = false;
                renderObj->frameBufferResized = true;
            }
            break;

        case WM_PAINT: {
            // During resize, skip rendering - swapchain images are old size
            // Windows will show frozen content at old dimensions (standard Vulkan behavior)
            // Once resize completes, HandleResize() rebuilds swapchain at new size
            if (renderObj->isResizing) {
                ValidateRect(hWnd, nullptr);
                return 0;
            }

            // Normal rendering (only if renderer is fully initialized)
            if (appObj && appObj->renderObj && appObj->renderObj->isInitialized) {
                for (auto& drawableObj : appObj->renderObj->vecDrawables) {
                    result = drawableObj->Render();
                    switch (result)
                    {
                    case VK_SUCCESS:
                        return true;

                    case VK_ERROR_OUT_OF_DATE_KHR:
                    case VK_SUBOPTIMAL_KHR:
                        // Swapchain is out of date, handle resize
                        renderObj->frameBufferResized = true;
                        return false;
                    default:
                        std::cerr << "Render Error: " << result << std::endl;
                        return false;
                    }


                }
            }
            break;
        }

        case WM_SIZE: {
            if(wParam != SIZE_MINIMIZED && appObj && appObj->IsPrepared() &&
               appObj->renderObj && appObj->renderObj->isInitialized) {

                // If not actively resizing, trigger immediate swapchain rebuild
                if (!renderObj->isResizing && !renderObj->frameBufferResized) {
                    renderObj->frameBufferResized = true;
                }
            }
            break;
        }
        default:
            break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
#endif // _WIN32

#ifdef _linux
void VulkanRenderer::CreatePresentationWindowX(const int &windowWidth, const int &windowHeight) {
    if (windowWidth <= 0 || windowHeight <= 0) {
        std::cerr << "Invalid window dimensions: " << windowWidth << "x" << windowHeight << std::endl;
        exit(1);
    }

    const xcb_setup_t *setup;
    xcb_screen_iterator_t iter;
    int scr;

    connection = xcb_connect(nullptr, &scr);
    if(xcb_connection_has_error(connection)) {
        std::cerr << "Cannot find a compatible Vulkan ICD.\n" << std::endl;
        exit(1);
    }
    setup = xcb_get_setup(connection);
    iter = xcb_setup_roots_iterator(setup);
    while(scr-- > 0) {
        xcb_screen_next(&iter);
    }
    screen = iter.data;

    width = windowWidth;
    height = windowHeight;

    uint32_t value_mask, value_list[32];

    window = xcb_generate_id(connection);

    value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    value_list[0] = screen->black_pixel;
    value_list[1] = XCB_EVENT_MASK_KEY_RELEASE |
                    XCB_EVENT_MASK_EXPOSURE;

    xcb_create_window(
        connection,
        XCB_COPY_FROM_PARENT,
        window,
        screen->root,
        0,
        0,
        width,
        height,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen->root_visual,
        value_mask,
        value_list
    );

    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(
        connection,
        1,
        12,
        "WM_PROTOCOLS"
    );
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
        connection,
        cookie,
        0
    );

    xcb_intern_atom_cookie_t cookie2 = xcb_intern_atom(
        connection,
        0,
        16,
        "WM_DELETE_WINDOW"
    );
    replay = xcb_intern_atom_reply(
        connection,
        cookie2,
        0
    );

    xcb_map_window(connection, window);

    const uint32_t coords[] = {100, 100};
    xcb_configure_window(
        connection,
        window,
        XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
        coords
    );
    xcb_flush(connection);

    xcb_generic_event_t *e;
    while((e = xcb_wait_for_event(connection))) {
        if((e->response_type & ~0x80) == XCB_EXPOSE) {
            break;
        }
    }
}
#endif // _linux

#ifdef _linux
void VulkanRenderer::DestroyWindow()
{
    xcb_destroy_window(connection, window);
    xcb_disconnect(connection);
}
#endif // _linux

void VulkanRenderer::CreateCommandPool()
{
    VulkanDevice* deviceObj = appObj->deviceObj.get();
    VkResult result;

    VkCommandPoolCreateInfo cmdPoolInfo = {};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.pNext = nullptr;
    cmdPoolInfo.queueFamilyIndex = deviceObj->graphicsQueueIndex;
    cmdPoolInfo.flags = 0;

    result = vkCreateCommandPool(deviceObj->device, &cmdPoolInfo, nullptr, &cmdPool);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create command pool: " << VulkanError{result, ""}.toString() << std::endl;
        exit(1);
    }
}

void VulkanRenderer::CreateDepthImage()
{
    VkResult result;
    VkImageCreateInfo imageInfo = {};

    // If the depth format is undefined
    // use fall back as 16 byte value
    if(Depth.format == VK_FORMAT_UNDEFINED) {
        Depth.format = VK_FORMAT_D16_UNORM;
    }

    const VkFormat depthFormat = Depth.format;

    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(*deviceObj->gpu, depthFormat, &props);

    if(props.optimalTilingFeatures &
       VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    } else if(props.linearTilingFeatures & 
              VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
    } else {
        std::cout << "Unsupported depth format, try other Depth formats.\n" << std::endl;
        exit(-1);
    }
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext = nullptr;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = depthFormat;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = NUM_SAMPLES;
    imageInfo.queueFamilyIndexCount = 0;
    imageInfo.pQueueFamilyIndices = nullptr;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.flags = 0;
    
    result = vkCreateImage(deviceObj->device, &imageInfo, nullptr, &Depth.image);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create depth image: " << VulkanError{result, ""}.toString() << std::endl;
        exit(1);
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(
        deviceObj->device, 
        Depth.image, 
        &memReqs
    );

    VkMemoryAllocateInfo memAlloc = {};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAlloc.pNext = nullptr;
    memAlloc.allocationSize = 0;
    memAlloc.memoryTypeIndex = 0;
    memAlloc.allocationSize = memReqs.size;

    auto memTypeResult = deviceObj->MemoryTypeFromProperties(memReqs.memoryTypeBits, 0);
    if (!memTypeResult) {
        std::cerr << "Failed to find suitable memory type for depth buffer: " << memTypeResult.error().toString() << std::endl;
        exit(1);
    }
    memAlloc.memoryTypeIndex = memTypeResult.value();

    result = vkAllocateMemory(deviceObj->device, &memAlloc, nullptr, &Depth.mem);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to allocate depth image memory: " << VulkanError{result, ""}.toString() << std::endl;
        exit(1);
    }

    result = vkBindImageMemory(deviceObj->device, Depth.image, Depth.mem, 0);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to bind depth image memory: " << VulkanError{result, ""}.toString() << std::endl;
        exit(1);
    }

    VkImageViewCreateInfo imgViewInfo = {};
    imgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imgViewInfo.pNext = nullptr;
    imgViewInfo.format = depthFormat;
    imgViewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY};
    imgViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    imgViewInfo.subresourceRange.baseMipLevel = 0;
    imgViewInfo.subresourceRange.levelCount = 1;
    imgViewInfo.subresourceRange.baseArrayLayer = 0;
    imgViewInfo.subresourceRange.layerCount = 1;
    imgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imgViewInfo.flags = 0;

    if(depthFormat == VK_FORMAT_D16_UNORM_S8_UINT ||
       depthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
       depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        imgViewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    CommandBufferMgr::AllocateCommandBuffer(
        &deviceObj->device, 
        cmdPool, 
        &cmdDepthImage
    ); 

    CommandBufferMgr::BeginCommandBuffer(cmdDepthImage);

    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.layerCount = 1;



    textureLoader->SetImageLayout(
        Depth.image,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        subresourceRange,
        cmdDepthImage
    );


    CommandBufferMgr::EndCommandBuffer(cmdDepthImage);
    CommandBufferMgr::SubmitCommandBuffer(
        deviceObj->queue,
        &cmdDepthImage
    );

    imgViewInfo.image = Depth.image;
    result = vkCreateImageView(deviceObj->device, &imgViewInfo, nullptr, &Depth.view);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create depth image view: " << VulkanError{result, ""}.toString() << std::endl;
        exit(1);
    }
}

void VulkanRenderer::CreateRenderPass(bool isDepthSupported, bool clear)
{
    // Dependency on VulkanSwapChain::CreateSwapChain() to
    // get the color image and VulkanRenderer::CreateDepthImage()
    // to get the depth image.

    VkResult result;
    // Attach the color buffer and depth buffer as an
    // attachment to the render pass instance.
    VkAttachmentDescription attachments[2];
    attachments[0].format = swapChainObj->scPublicVars.Format;
    attachments[0].samples = NUM_SAMPLES;
    attachments[0].loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : 
                                    VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachments[0].flags = 0;

    if(isDepthSupported) {
        attachments[1].format = Depth.format;
        attachments[1].samples = NUM_SAMPLES;
        attachments[1].loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : 
                                        VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[1].flags = 0;
    }

    // Defome the color buffer attachment binding point
    // and layout information
    VkAttachmentReference colorReference = {};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Define the depth buffer attachment binding point
    // and layout information
    VkAttachmentReference depthReference = {};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Specify the attachments - color, depth, resolve, preserve etc.
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.flags = 0;
    subpass.inputAttachmentCount = 0;
    subpass.pInputAttachments = nullptr;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pResolveAttachments = nullptr;
    subpass.pDepthStencilAttachment = isDepthSupported ? &depthReference : nullptr;
    subpass.preserveAttachmentCount = 0;
    subpass.pPreserveAttachments = nullptr;

    // Add subpass dependency to handle layout transition from UNDEFINED to COLOR_ATTACHMENT_OPTIMAL
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = 0;

    // Specify the attachment and subpass associate with render pass.
    VkRenderPassCreateInfo rpInfo = {};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.pNext = nullptr;
    rpInfo.attachmentCount = isDepthSupported ? 2 : 1;
    rpInfo.pAttachments = attachments;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;

    result = vkCreateRenderPass(deviceObj->device, &rpInfo, nullptr, &renderPass);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create render pass: " << VulkanError{result, ""}.toString() << std::endl;
        exit(1);
    }
}

void VulkanRenderer::CreateFrameBuffer(bool includeDepth, bool clear)
{
    // Dependecy on CreateDepthBuffer(), CreateRenderPass()
    // and VulkanSwapChain::CreateSwapChain()
    if (Depth.view == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE ||
        !swapChainObj || swapChainObj->scPublicVars.swapChainImageCount == 0) {
        std::cerr << "CreateFrameBuffer: Prerequisites not met (depth view, render pass, or swapchain)" << std::endl;
        exit(1);
    }

    VkResult result;
    VkImageView attachments[2];
    attachments[1] = Depth.view;

    VkFramebufferCreateInfo fbInfo = {};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.pNext = nullptr;
    fbInfo.renderPass = renderPass;
    fbInfo.attachmentCount = includeDepth ? 2 : 1;
    fbInfo.pAttachments = attachments;
    fbInfo.width = width;
    fbInfo.height = height;
    fbInfo.layers = 1;

    uint32_t i;
    frameBuffers.clear();
    frameBuffers.resize(swapChainObj->scPublicVars.swapChainImageCount);

    for (i = 0; i < swapChainObj->scPublicVars.swapChainImageCount; i++) {
        attachments[0] = swapChainObj->scPublicVars.colorBuffers[i].view;
        result = vkCreateFramebuffer(deviceObj->device, &fbInfo, nullptr, &frameBuffers.at(i));
        if (result != VK_SUCCESS) {
            std::cerr << "Failed to create framebuffer: " << VulkanError{result, ""}.toString() << std::endl;
            exit(1);
        }
    }
}

void VulkanRenderer::BuildSwapChainAndDepthImage()
{
    // Get the appropriate queue from the device
    deviceObj->GetDeviceQueue();

    // Create the swap chain and get the color image
    swapChainObj->CreateSwapChain(cmdDepthImage);

    // Create the depth image
    CreateDepthImage();
}

void VulkanRenderer::CreateVertexBuffer()
{
    CommandBufferMgr::AllocateCommandBuffer(
        &deviceObj->device, 
        cmdPool, 
        &cmdVertexBuffer
    );
    CommandBufferMgr::BeginCommandBuffer(cmdVertexBuffer);
    for (size_t i = 0; i < vecDrawables.size(); i++)
    {
        if (auto result = vecDrawables[i]->CreateVertexBuffer(geometryData, sizeof(geometryData), sizeof(geometryData[0]), false); !result) {
            std::cerr << "Failed to create vertex buffer: " << result.error().toString() << std::endl;
            exit(1);
        }
        // if (auto result = vecDrawables[i]->CreateVertexIndex(squareIndices, sizeof(squareIndices), 0); !result) {
        //     std::cerr << "Failed to create vertex index buffer: " << result.error().toString() << std::endl;
        //     exit(1);
        // }
    }
    CommandBufferMgr::EndCommandBuffer(cmdVertexBuffer);
    CommandBufferMgr::SubmitCommandBuffer(
        deviceObj->queue,
        &cmdVertexBuffer
    );
}

void VulkanRenderer::CreateShaders()
{
    if(!shaderObj) {
        shaderObj = std::make_unique<VulkanShader>();
    }

    if(shaderObj->initialized) {
        // Shaders already created, no need to recreate
        return;
    }

    void* vertShaderCode, *fragShaderCode;
    size_t vertShaderSize, fragShaderSize;

    #ifdef AUTO_COMPILE_GLSL_TO_SPV
    vertShaderCode = ReadFile("../Shaders/Draw.vert", &vertShaderSize);
    fragShaderCode = ReadFile("../Shaders/Draw.frag", &fragShaderSize);

    std::cout << "Loaded vertex shader: " << vertShaderSize << " bytes" << std::endl;
    std::cout << "Loaded fragment shader: " << fragShaderSize << " bytes" << std::endl;

    if (vertShaderCode && fragShaderCode) {
        shaderObj->BuildShader(
            (const char*)vertShaderCode,
            (const char*)fragShaderCode
        );
        std::cout << "Shaders compiled successfully!" << std::endl;
    } else {
        std::cerr << "Failed to load shader files!" << std::endl;
    }

    #else
    vertShaderCode = ReadFile("./../shaders/vertShader.spv", &vertShaderSize);
    fragShaderCode = ReadFile("./../shaders/fragShader.spv", &fragShaderSize);
    shaderObj->BuildShaderModuleWithSPV(
        (uint32_t*)vertShaderCode,
        vertShaderSize,
        (uint32_t*)fragShaderCode,
        fragShaderSize
    );
    #endif // AUTO_COMPILE_GLSL_TO_SPV

    free(vertShaderCode);
    free(fragShaderCode);
    shaderObj->initialized = true;
}

void VulkanRenderer::CreatePipelineStateManagement()
{
    for (auto& drawable : vecDrawables) {
        if (auto result = drawable->CreatePipelineLayout(); !result) {
            std::cerr << "Failed to create pipeline layout: " << result.error().toString() << std::endl;
            exit(1);
        }
    }


    pipelineState = std::make_unique<VulkanPipeline>();
    pipelineState->CreatePipelineCache();

    VulkanPipeline::Config config = {};
    config.enableDepthTest = true;
    config.enableDepthWrite = true;
    config.enableVertexInput = true;
    config.viewPort = {0, 0, (float)width, (float)height};
    config.scissor = {{0, 0}, {(unsigned int)width, (unsigned int)height}};

    for (auto& drawable : vecDrawables) {
        VkPipeline pipelineHandle = VK_NULL_HANDLE; // Direct handle, not a pointer
        
        if(pipelineState->CreatePipeline(
            drawable.get(),
            shaderObj.get(),
            config,
            &pipelineHandle // Pass address to receive the handle
        )) {
            drawable->SetPipeline(pipelineHandle); // Pass the actual handle
            pipelineHandles.push_back(pipelineHandle);
        }
        else {
            std::cerr << "Failed to create pipeline for drawable!" << std::endl;
            // VkPipeline is a handle, not allocated memory - don't free it
            // pipelineHandle is already VK_NULL_HANDLE from initialization
        }
    }
}

void VulkanRenderer::DestroyRenderPass()
{
    if(renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(deviceObj->device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::DestroyCommandBuffer()
{
    VkCommandBuffer cmdBufs[] = {cmdDepthImage};
    vkFreeCommandBuffers(
        deviceObj->device, 
        cmdPool, 
        sizeof(cmdBufs) / sizeof(VkCommandBuffer), 
        cmdBufs
    );
}

void VulkanRenderer::DestroyCommandPool() {
    VulkanDevice* deviceObj = appObj->deviceObj.get();
    vkDestroyCommandPool(deviceObj->device, cmdPool, nullptr);
    cmdPool = VK_NULL_HANDLE;
    cmdDepthImage = VK_NULL_HANDLE;
}

void VulkanRenderer::DestroyDepthBuffer()
{
    if(Depth.view != VK_NULL_HANDLE)
        vkDestroyImageView(deviceObj->device, Depth.view, nullptr);
    if(Depth.image != VK_NULL_HANDLE)
        vkDestroyImage(deviceObj->device, Depth.image, nullptr);
    if(Depth.mem != VK_NULL_HANDLE)
        vkFreeMemory(deviceObj->device, Depth.mem, nullptr);
        
    Depth.view = VK_NULL_HANDLE;
    Depth.image = VK_NULL_HANDLE;
    Depth.mem = VK_NULL_HANDLE;
}

void VulkanRenderer::DestroyDrawableVertexBuffer()
{
    for (size_t i = 0; i < vecDrawables.size(); i++)
    {
        vecDrawables[i]->DestroyVertexBuffer();
		vecDrawables[i]->DestroyIndexBuffer();
    }    
}

void VulkanRenderer::DestroyPresentationWindow()
{
    DestroyWindow(window);
}

void VulkanRenderer::DestroyFrameBuffer()
{
    for (auto& framebuffer : frameBuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(deviceObj->device, framebuffer, nullptr);
        }
    }
    frameBuffers.clear();
}

void VulkanRenderer::DestroyPipeline()
{
    if(!pipelineState) {
        return; // Nothing to destroy
    }
    for (auto pipeline : pipelineHandles) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(deviceObj->device, pipeline, nullptr);
        }
    }
    pipelineHandles.clear();

    pipelineState->DestroyPipelineCache();
    pipelineState.reset();
}

void VulkanRenderer::DestroyShaders()
{
    if (shaderObj) {
        shaderObj->DestroyShader();
    }
}

void VulkanRenderer::CreateDescriptors()
{
    for (auto& drawAble : vecDrawables) {
        if (auto result = drawAble->CreateDescriptorSetLayout(true); !result) {
            std::cerr << "Failed to create descriptor set layout: " << result.error().toString() << std::endl;
            exit(1);
        }
        if (auto result = drawAble->CreateDescriptor(true); !result) {
            std::cerr << "Failed to create descriptor: " << result.error().toString() << std::endl;
            exit(1);
        }
    }
}

void VulkanRenderer::DestroyTexture()
{
    if (texture.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(deviceObj->device, texture.sampler, nullptr);
        texture.sampler = VK_NULL_HANDLE;
    }
    if (texture.view != VK_NULL_HANDLE) {
        vkDestroyImageView(deviceObj->device, texture.view, nullptr);
        texture.view = VK_NULL_HANDLE;
    }
    if (texture.image != VK_NULL_HANDLE) {
        vkDestroyImage(deviceObj->device, texture.image, nullptr);
        texture.image = VK_NULL_HANDLE;
    }
    if (texture.mem != VK_NULL_HANDLE) {
        vkFreeMemory(deviceObj->device, texture.mem, nullptr);
        texture.mem = VK_NULL_HANDLE;
    }
    if (texture.cmdTexture != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(deviceObj->device, cmdPool, 1, &texture.cmdTexture);
        texture.cmdTexture = VK_NULL_HANDLE;
    }
}
