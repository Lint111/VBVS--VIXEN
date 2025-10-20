#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <chrono>

namespace EventBus {

/**
 * @brief Unique identifier for message sender
 * 
 * Can represent nodes, systems, threads, etc.
 * Value 0 reserved for "system" (no specific sender).
 */
using SenderID = uint64_t;

/**
 * @brief Message type identifier for filtering
 * 
 * Users define their own message type enums and cast to MessageType.
 * Base types (0-99 reserved):
 * - 0: Generic message
 * - 1: Worker thread result
 */
using MessageType = uint32_t;

/**
 * @brief Base message class
 * 
 * All messages inherit from this. Provides:
 * - Sender identification
 * - Timestamp for ordering
 * - Type ID for filtering
 * 
 * Usage:
 * ```cpp
 * struct MyMessage : public Message {
 *     MyMessage(SenderID sender) : Message(sender, MY_MESSAGE_TYPE) {}
 *     std::string data;
 * };
 * ```
 */
struct Message {
    SenderID sender;
    MessageType type;
    std::chrono::steady_clock::time_point timestamp;

    Message(SenderID senderID, MessageType msgType)
        : sender(senderID)
        , type(msgType)
        , timestamp(std::chrono::steady_clock::now()) {}

    virtual ~Message() = default;

    // Convenience: Get time since message creation
    double GetAgeSeconds() const {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - timestamp);
        return duration.count() / 1000000.0;
    }
};

// ============================================================================
// Common Message Types
// ============================================================================

/**
 * @brief Generic text message (debugging, logging)
 */
struct TextMessage : public Message {
    static constexpr MessageType TYPE = 0;

    std::string content;

    TextMessage(SenderID sender, std::string text)
        : Message(sender, TYPE)
        , content(std::move(text)) {}
};

/**
 * @brief Worker thread result message
 * 
 * Automatically emitted by WorkerThreadBridge when async work completes.
 */
struct WorkerResultMessage : public Message {
    static constexpr MessageType TYPE = 1;

    uint64_t workID;      // Correlate with original request
    bool success;
    std::string error;    // Empty if success

    WorkerResultMessage(SenderID sender, uint64_t id, bool succeeded, std::string err = "")
        : Message(sender, TYPE)
        , workID(id)
        , success(succeeded)
        , error(std::move(err)) {}
};

} // namespace EventBus
