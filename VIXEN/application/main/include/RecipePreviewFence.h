#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

// A small host-side completion ledger for editor recipe previews. The render thread publishes
// the highest serial whose frame fence has retired; channel requests can wait without touching
// Vulkan or blocking the frame thread.
class RecipePreviewFence {
public:
    uint64_t PublishedSerial() const noexcept {
        return publishedSerial_.load(std::memory_order_acquire);
    }

    void Publish(uint64_t serial, bool succeeded) noexcept {
        if (serial == 0) return;
        if (!succeeded) {
            auto current = failedSerials_.load(std::memory_order_acquire);
            for (;;) {
                auto next = std::make_shared<std::vector<uint64_t>>(*current);
                next->push_back(serial);
                if (failedSerials_.compare_exchange_weak(
                        current, std::move(next),
                        std::memory_order_release, std::memory_order_acquire)) break;
            }
        }
        auto published = publishedSerial_.load(std::memory_order_relaxed);
        while (published < serial && !publishedSerial_.compare_exchange_weak(
                   published, serial, std::memory_order_release, std::memory_order_relaxed)) {}
    }

    bool Wait(uint64_t serial, std::chrono::milliseconds timeout) const {
        if (serial == 0) return true;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (publishedSerial_.load(std::memory_order_acquire) < serial) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return false;
            std::this_thread::sleep_for(std::min(std::chrono::milliseconds(1),
                                                 std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)));
        }
        return true;
    }

    bool Succeeded(uint64_t serial) const noexcept {
        if (serial == 0) return true;
        const auto failedSerials = failedSerials_.load(std::memory_order_acquire);
        for (const uint64_t failed : *failedSerials) {
            if (failed == serial) return false;
        }
        return true;
    }

private:
    std::atomic<uint64_t> publishedSerial_{0};
    std::atomic<std::shared_ptr<std::vector<uint64_t>>> failedSerials_{
        std::make_shared<std::vector<uint64_t>>()};
};
