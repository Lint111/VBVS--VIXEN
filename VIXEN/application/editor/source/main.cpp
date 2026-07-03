#include "EditorApplication.h"
#include "VulkanApplicationBase.h"
#include "VulkanGlobalNames.h"
#include <Logger.h>
#include <cstdlib>
#include <memory>
#include <string>

#ifndef VIXEN_VULKAN_VALIDATION
#define VIXEN_VULKAN_VALIDATION 0
#endif

#ifndef VIXEN_EDITOR_DEFAULT_DOCUMENT
#error "VIXEN_EDITOR_DEFAULT_DOCUMENT must be defined by CMake"
#endif

// Same global Vulkan extension/layer init as application/main/source/main.cpp — both targets
// link the same VixenApp driver, which reads these globals during Initialize().
static bool initGlobalNames = []() {
    deviceExtensionNames = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
        VK_KHR_MAINTENANCE_6_EXTENSION_NAME,
    };
    instanceExtensionNames = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,
        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME
#if VIXEN_VULKAN_VALIDATION
        , VK_EXT_DEBUG_REPORT_EXTENSION_NAME
#endif
    };
    layerNames = {
#if VIXEN_VULKAN_VALIDATION
        "VK_LAYER_KHRONOS_validation"
#endif
    };
    return true;
}();

int main(int argc, char** argv) {
    {
        Vixen::Log::LogLevel level = Vixen::Log::LogLevel::LOG_INFO;
        if (const char* env = std::getenv("VIXEN_LOG_LEVEL")) {
            const std::string lv(env);
            if      (lv == "DEBUG")                   level = Vixen::Log::LogLevel::LOG_DEBUG;
            else if (lv == "INFO")                    level = Vixen::Log::LogLevel::LOG_INFO;
            else if (lv == "WARNING" || lv == "WARN") level = Vixen::Log::LogLevel::LOG_WARNING;
            else if (lv == "ERROR")                   level = Vixen::Log::LogLevel::LOG_ERROR;
            else if (lv == "CRITICAL")                level = Vixen::Log::LogLevel::LOG_CRITICAL;
        }
        Vixen::Log::Logger::SetGlobalMinLevel(level);
    }

    auto mainLogger = std::make_shared<Vixen::Log::Logger>("editor_main", true);
    mainLogger->SetTerminalOutput(true);

    const std::string documentPath = (argc > 1) ? std::string(argv[1])
                                                 : std::string(VIXEN_EDITOR_DEFAULT_DOCUMENT);
    mainLogger->Info("vixen_editor: document = " + documentPath);

    try {
        auto app = std::make_unique<EditorApplication>(documentPath);
        VulkanApplicationBase* appObj = app.get();

        if (!app->LoadDocument(documentPath)) {
            mainLogger->Error("Failed to load document: " + app->LastEditorError());
            return -1;
        }

        mainLogger->Info("Calling Initialize...");
        appObj->Initialize();

        mainLogger->Info("Calling Prepare...");
        appObj->Prepare();
        if (!appObj->IsPrepared()) {
            mainLogger->Error("Prepare failed: " + appObj->GetLastError() + " - aborting before render loop");
            appObj->DeInitialize();
            return -1;
        }

        mainLogger->Info("Entering render loop...");
        bool isWindowOpen = true;
        while (isWindowOpen) {
            appObj->Update();
            isWindowOpen = appObj->Render();
        }

        mainLogger->Info("Cleaning up...");
        appObj->DeInitialize();
        mainLogger->Info("DeInitialize complete");
    }
    catch (const std::exception& e) {
        mainLogger->Error(std::string("Exception caught: ") + e.what());
        return -1;
    }
    catch (...) {
        mainLogger->Error("Unknown exception caught!");
        return -1;
    }

    mainLogger->Info("Exiting normally");
    return 0;
}
