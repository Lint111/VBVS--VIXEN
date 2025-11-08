#pragma once

/**
 * @file NodeLogging.h
 * @brief Unconditional logging macros for render graph nodes
 *
 * Loggers are always available but disabled by default (enabled=false).
 * Enable logging per-node in VulkanGraphApplication for debugging.
 * Use SetTerminalOutput(true) to also print logs to console in real-time.
 */

/**
 * @brief Log debug message (verbose)
 * Usage: NODE_LOG_DEBUG("Enumerating devices...");
 */
#define NODE_LOG_DEBUG(msg) \
    do { if (nodeLogger) nodeLogger->Debug(msg); } while(0)

/**
 * @brief Log info message (important events)
 * Usage: NODE_LOG_INFO("Device created successfully");
 */
#define NODE_LOG_INFO(msg) \
    do { if (nodeLogger) nodeLogger->Info(msg); } while(0)

/**
 * @brief Log warning message (recoverable issues)
 * Usage: NODE_LOG_WARNING("GPU index out of range, using default");
 */
#define NODE_LOG_WARNING(msg) \
    do { if (nodeLogger) nodeLogger->Warning(msg); } while(0)

/**
 * @brief Log error message (failures)
 * Usage: NODE_LOG_ERROR("Failed to create device");
 */
#define NODE_LOG_ERROR(msg) \
    do { if (nodeLogger) nodeLogger->Error(msg); } while(0)

/**
 * @brief Log critical message (fatal errors)
 * Usage: NODE_LOG_CRITICAL("No Vulkan-capable GPUs found");
 */
#define NODE_LOG_CRITICAL(msg) \
    do { if (nodeLogger) nodeLogger->Critical(msg); } while(0)

// Object-aware variants for static contexts; usage: NODE_LOG_INFO_OBJ(objPtr, "message")
#define NODE_LOG_DEBUG_OBJ(obj, msg) \
    do { if ((obj) && (obj)->nodeLogger) (obj)->nodeLogger->Debug(msg); } while(0)
#define NODE_LOG_INFO_OBJ(obj, msg) \
    do { if ((obj) && (obj)->nodeLogger) (obj)->nodeLogger->Info(msg); } while(0)
#define NODE_LOG_WARNING_OBJ(obj, msg) \
    do { if ((obj) && (obj)->nodeLogger) (obj)->nodeLogger->Warning(msg); } while(0)
#define NODE_LOG_ERROR_OBJ(obj, msg) \
    do { if ((obj) && (obj)->nodeLogger) (obj)->nodeLogger->Error(msg); } while(0)
#define NODE_LOG_CRITICAL_OBJ(obj, msg) \
    do { if ((obj) && (obj)->nodeLogger) (obj)->nodeLogger->Critical(msg); } while(0)

/**
 * @brief Helper macro to format strings for logging
 * Usage: NODE_LOG_INFO(NODE_FORMAT("Selected GPU: {}", gpuName));
 */
#define NODE_FORMAT(fmt, ...) \
    (std::ostringstream() << fmt).str()

// Example usage in node code:
//
// void DeviceNode::Setup() {
//     NODE_LOG_INFO("Setup: Preparing device creation");
//
//     NODE_LOG_DEBUG("Reading gpu_index parameter");
//     uint32_t index = GetParameterValue<uint32_t>("gpu_index", 0);
//
//     if (index >= availableGPUs.size()) {
//         NODE_LOG_WARNING("GPU index out of range, using default");
//     }
//
//     NODE_LOG_INFO("Setup complete");
// }