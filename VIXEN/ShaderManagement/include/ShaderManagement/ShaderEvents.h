#pragma once

#include <EventBus/Message.h>
#include "ShaderDataBundle.h"
#include <string>
#include <vector>

namespace ShaderManagement {

/**
 * @brief Message type IDs for shader compilation events
 *
 * Range: 200-299 reserved for ShaderManagement
 */
enum class ShaderMessageType : uint32_t {
    CompilationStarted  = 200,
    CompilationProgress = 201,
    CompilationCompleted = 202,
    CompilationFailed   = 203,
    SdiGenerated        = 204,
    RegistryUpdated     = 205,
    HotReloadReady      = 206
};

/**
 * @brief Shader compilation started event
 */
struct ShaderCompilationStartedMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE = static_cast<uint32_t>(ShaderMessageType::CompilationStarted);

    std::string programName;
    std::string uuid;
    uint32_t stageCount;

    ShaderCompilationStartedMessage(EventBus::SenderID sender, std::string name, std::string id, uint32_t stages)
        : Message(sender, TYPE)
        , programName(std::move(name))
        , uuid(std::move(id))
        , stageCount(stages) {}
};

/**
 * @brief Compilation progress update
 */
struct ShaderCompilationProgressMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE = static_cast<uint32_t>(ShaderMessageType::CompilationProgress);

    std::string uuid;
    std::string currentStage;  // "Preprocessing", "Compiling", "Reflecting", "Generating SDI"
    uint32_t completedStages;
    uint32_t totalStages;
    float progressPercent;     // 0.0 - 1.0

    ShaderCompilationProgressMessage(
        EventBus::SenderID sender,
        std::string id,
        std::string stage,
        uint32_t completed,
        uint32_t total
    )
        : Message(sender, TYPE)
        , uuid(std::move(id))
        , currentStage(std::move(stage))
        , completedStages(completed)
        , totalStages(total)
        , progressPercent(total > 0 ? static_cast<float>(completed) / total : 0.0f) {}
};

/**
 * @brief Shader compilation completed successfully
 *
 * Contains the complete ShaderDataBundle ready to use.
 */
struct ShaderCompilationCompletedMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE = static_cast<uint32_t>(ShaderMessageType::CompilationCompleted);

    ShaderDataBundle bundle;
    bool usedCache;

    // Build statistics
    std::chrono::milliseconds preprocessTime;
    std::chrono::milliseconds compileTime;
    std::chrono::milliseconds reflectTime;
    std::chrono::milliseconds sdiGenTime;
    std::chrono::milliseconds totalTime;

    std::vector<std::string> warnings;

    ShaderCompilationCompletedMessage(EventBus::SenderID sender, ShaderDataBundle b)
        : Message(sender, TYPE)
        , bundle(std::move(b))
        , usedCache(false)
        , preprocessTime(0)
        , compileTime(0)
        , reflectTime(0)
        , sdiGenTime(0)
        , totalTime(0) {}
};

/**
 * @brief Shader compilation failed
 */
struct ShaderCompilationFailedMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE = static_cast<uint32_t>(ShaderMessageType::CompilationFailed);

    std::string programName;
    std::string uuid;
    std::string errorMessage;
    std::string failedStage;   // Which stage failed
    std::vector<std::string> warnings;

    ShaderCompilationFailedMessage(
        EventBus::SenderID sender,
        std::string name,
        std::string id,
        std::string error,
        std::string stage = ""
    )
        : Message(sender, TYPE)
        , programName(std::move(name))
        , uuid(std::move(id))
        , errorMessage(std::move(error))
        , failedStage(std::move(stage)) {}
};

/**
 * @brief SDI header file generated
 */
struct SdiGeneratedMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE = static_cast<uint32_t>(ShaderMessageType::SdiGenerated);

    std::string uuid;
    std::string sdiHeaderPath;
    std::string sdiNamespace;
    std::string aliasName;

    SdiGeneratedMessage(
        EventBus::SenderID sender,
        std::string id,
        std::string path,
        std::string ns,
        std::string alias = ""
    )
        : Message(sender, TYPE)
        , uuid(std::move(id))
        , sdiHeaderPath(std::move(path))
        , sdiNamespace(std::move(ns))
        , aliasName(std::move(alias)) {}
};

/**
 * @brief Central SDI registry updated
 */
struct SdiRegistryUpdatedMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE = static_cast<uint32_t>(ShaderMessageType::RegistryUpdated);

    std::string registryHeaderPath;
    uint32_t activeShaderCount;
    std::vector<std::string> addedUuids;
    std::vector<std::string> removedUuids;

    SdiRegistryUpdatedMessage(
        EventBus::SenderID sender,
        std::string path,
        uint32_t count
    )
        : Message(sender, TYPE)
        , registryHeaderPath(std::move(path))
        , activeShaderCount(count) {}
};

/**
 * @brief Shader hot-reload ready
 *
 * Emitted when a shader recompilation is complete and interface is compatible
 * with the previous version (safe to hot-swap).
 */
struct ShaderHotReloadReadyMessage : public EventBus::Message {
    static constexpr EventBus::MessageType TYPE = static_cast<uint32_t>(ShaderMessageType::HotReloadReady);

    std::string uuid;
    ShaderDataBundle newBundle;
    bool interfaceChanged;     // If true, C++ code needs recompilation
    std::string oldInterfaceHash;
    std::string newInterfaceHash;

    ShaderHotReloadReadyMessage(
        EventBus::SenderID sender,
        std::string id,
        ShaderDataBundle bundle,
        bool changed,
        std::string oldHash,
        std::string newHash
    )
        : Message(sender, TYPE)
        , uuid(std::move(id))
        , newBundle(std::move(bundle))
        , interfaceChanged(changed)
        , oldInterfaceHash(std::move(oldHash))
        , newInterfaceHash(std::move(newHash)) {}
};

} // namespace ShaderManagement
