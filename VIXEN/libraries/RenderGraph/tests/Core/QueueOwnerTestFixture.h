#pragma once

#include "Core/SubmitChannel.h"
#include "MessageBus.h"
#include "QueueSubmitRequest.h"
#include "VulkanDevice.h"

#include <vector>

namespace Vixen::RenderGraph::Test {

/// Small standalone owner seam for CPU tests: producers publish through a real MessageBus and the
/// fixture drains them into the same SubmitRecord shape used by RenderGraph. GPU tests can replace
/// Drain with their device-specific issue function without rewriting publication plumbing.
class QueueOwnerTestFixture {
public:
    QueueOwnerTestFixture() {
        bus_.Subscribe(
            Vixen::EventBus::QueueSubmitRequestMessage::TYPE,
            [this](const Vixen::EventBus::BaseEventMessage& base) {
                const auto& message = static_cast<const Vixen::EventBus::QueueSubmitRequestMessage&>(base);
                records_.push_back(SubmitRecord::FromQueueSubmitRequest(message.request));
                return true;
            });
    }

    void Attach(Vixen::Vulkan::Resources::VulkanDevice& device) {
        device.SetMessageBus(&bus_);
    }

    void Drain() { bus_.ProcessMessages(); }

    [[nodiscard]] const std::vector<SubmitRecord>& Records() const { return records_; }

private:
    Vixen::EventBus::MessageBus bus_;
    std::vector<SubmitRecord> records_;
};

} // namespace Vixen::RenderGraph::Test
