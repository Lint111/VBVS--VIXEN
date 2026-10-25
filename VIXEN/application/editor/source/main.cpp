#include "EditorApplication.h"
#include "VulkanGlobalNames.h"
#include <Logger.h>
#include <cstdint>
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

    // R765: VIXEN_EDITOR_PACK=<pack root holding manifest.utdl> with VIXEN_EDITOR_PACK_SCHEMA=<the
    // generated pack schema, schemas.json> makes the editor browse that pack's render-recipe documents.
    // It opens the first one by id; the OpenRecipe action opens any other, and Save writes the open
    // document back into the pack. Without a pack the editor edits a loose document as before.
    const char* packRoot = std::getenv("VIXEN_EDITOR_PACK");
    const bool packMode = packRoot != nullptr && *packRoot != '\0';
    const std::string documentPath = (argc > 1) ? std::string(argv[1])
                                     : packMode ? std::string()
                                                : std::string(VIXEN_EDITOR_DEFAULT_DOCUMENT);

    auto app = std::make_unique<EditorApplication>(documentPath);

    // Editor-specific: must run before Initialize()/Prepare() (Run() only owns the lifecycle
    // from Initialize() onward).
    if (packMode) {
        const char* schemaPath = std::getenv("VIXEN_EDITOR_PACK_SCHEMA");
        if (schemaPath == nullptr || *schemaPath == '\0') {
            mainLogger->Error("VIXEN_EDITOR_PACK requires VIXEN_EDITOR_PACK_SCHEMA (the pack's generated schemas.json)");
            return -1;
        }
        if (!app->ConfigurePack(packRoot, schemaPath)) {
            mainLogger->Error("Failed to open pack: " + app->LastEditorError());
            return -1;
        }
    }
    if (!documentPath.empty()) {
        mainLogger->Info("vixen_editor: document = " + documentPath);
        if (!app->LoadDocument(documentPath)) {
            mainLogger->Error("Failed to load document: " + app->LastEditorError());
            return -1;
        }
    } else {
        if (app->Pack().Recipes().empty()) {
            mainLogger->Error("Pack " + app->Pack().PackRoot() + " lists no render-recipe documents");
            return -1;
        }
        const std::string& first = app->Pack().Recipes().front().id;
        mainLogger->Info("vixen_editor: pack recipe = " + first);
        if (!app->OpenPackRecipe(first)) {
            mainLogger->Error("Failed to open pack recipe " + first + ": " + app->LastEditorError());
            return -1;
        }
    }

    // Inc-2b M3: VIXEN_EXIT_AFTER_FRAMES=<n> -- close cleanly after n frames, mirroring
    // application/main/source/main.cpp's identical knob (same env name, same semantics: a
    // frame-counted clean exit through the normal DeInitialize() path, not a window-close
    // event). Needed so an unattended scripted run (VIXEN/temp/run_editor_script.bat) can
    // self-terminate -- without this the editor's own main.cpp had no exit knob at all and a
    // scripted windowed gate would hang forever waiting for a window the harness never closes.
    uint64_t exitAfterFrames = 0;
    if (const char* env = std::getenv("VIXEN_EXIT_AFTER_FRAMES")) {
        exitAfterFrames = static_cast<uint64_t>(std::strtoull(env, nullptr, 10));
    }

    // Engine-owned loop: Initialize -> Prepare -> loop -> DeInitialize. No frame-timer for the
    // editor. All lifecycle + the try/catch boundary now live in Run().
    return app->Run({ .exitAfterFrames = exitAfterFrames });
}
