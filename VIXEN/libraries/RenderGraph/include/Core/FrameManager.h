// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file FrameManager.h
 * @brief Single source of truth for frame lifecycle events
 *
 * Sprint 6.3: Event-Driven Architecture (Option A)
 *
 * FrameManager is responsible for:
 * - Owning the global frame counter (single source of truth)
 * - Publishing FrameStartEvent/FrameEndEvent to MessageBus
 * - Allowing subsystems to self-manage via event subscriptions
 *
 * This decouples frame lifecycle from RenderGraph, enabling:
 * - Open/Closed principle: add new frame-aware systems without modifying RenderGraph
 * - Single responsibility: RenderGraph only orchestrates node execution
 * - Testability: systems testable in isolation with mock events
 */

#include "MessageBus.h"
#include "Message.h"
#include <cstdint>
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Central frame lifecycle manager
 *
 * Usage:
 * ```cpp
 * // Setup
 * FrameManager frameManager(messageBus);
 *
 * // Per frame
 * frameManager.BeginFrame();
 * // ... execute nodes ...
 * frameManager.EndFrame();
 * ```
 *
 * Subsystems subscribe to MessageBus for FrameStartEvent/FrameEndEvent:
 * ```cpp
 * messageBus->Subscribe(FrameStartEvent::TYPE, [this](const BaseEventMessage& e) {
 *     auto& event = static_cast<const FrameStartEvent&>(e);
 *     OnFrameStart(event.frameNumber);
 *     return true;
 * });
 * ```
 */
class FrameManager {
public:
    /**
     * @brief Construct FrameManager with MessageBus
     *
     * @param messageBus MessageBus for publishing events (non-owning)
     */
    explicit FrameManager(EventBus::MessageBus* messageBus)
        : messageBus_(messageBus) {}

    /**
     * @brief Begin a new frame
     *
     * Increments frame counter and publishes FrameStartEvent.
     * Call this BEFORE node execution.
     *
     * Subscribers receive FrameStartEvent with the NEW frame number.
     */
    void BeginFrame() {
        frameIndex_++;

        if (messageBus_) {
            auto event = std::make_unique<EventBus::FrameStartEvent>(
                SENDER_ID, frameIndex_);
            messageBus_->Publish(std::move(event));
            messageBus_->ProcessMessages();  // Synchronous dispatch
        }
    }

    /**
     * @brief End the current frame
     *
     * Publishes FrameEndEvent with current frame number.
     * Call this AFTER node execution.
     *
     * Subscribers receive FrameEndEvent and can compute per-frame metrics.
     */
    void EndFrame() {
        if (messageBus_) {
            auto event = std::make_unique<EventBus::FrameEndEvent>(
                SENDER_ID, frameIndex_);
            messageBus_->Publish(std::move(event));
            messageBus_->ProcessMessages();  // Synchronous dispatch
        }
    }

    /**
     * @brief Get current frame index
     */
    [[nodiscard]] uint64_t GetFrameIndex() const { return frameIndex_; }

    /**
     * @brief Reset frame counter (for testing or restart)
     */
    void Reset() { frameIndex_ = 0; }

    /**
     * @brief Get MessageBus (for systems that need to subscribe)
     */
    [[nodiscard]] EventBus::MessageBus* GetMessageBus() const { return messageBus_; }

private:
    static constexpr EventBus::SenderID SENDER_ID = 0;  // System sender

    EventBus::MessageBus* messageBus_ = nullptr;
    uint64_t frameIndex_ = 0;
};

} // namespace Vixen::RenderGraph
