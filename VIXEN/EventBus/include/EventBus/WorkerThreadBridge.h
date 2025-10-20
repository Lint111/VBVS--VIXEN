#pragma once

#include "Message.h"
#include "MessageBus.h"
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <memory>

namespace EventBus {

/**
 * @brief Work item submitted to worker thread
 */
template<typename ResultType>
struct WorkItem {
    uint64_t workID;
    SenderID sender;
    std::function<ResultType()> workFunction;
};

/**
 * @brief Result returned from worker thread
 */
template<typename ResultType>
struct WorkResult {
    uint64_t workID;
    SenderID sender;
    ResultType result;
    bool success;
    std::string error;
};

/**
 * @brief Bridge between worker threads and main thread via MessageBus
 * 
 * Features:
 * - Submit work to background thread (non-blocking)
 * - Automatic result publishing to MessageBus
 * - Type-safe work/result pairs
 * - Exception handling on worker thread
 * - RAII thread lifecycle management
 * 
 * Architecture:
 * ```
 * Main Thread                Worker Thread
 * ───────────                ─────────────
 * SubmitWork()
 *    ↓
 * [Work Queue] ────────────→ Execute work function
 *                                  ↓
 *                            Result captured
 *                                  ↓
 * MessageBus::Publish()  ←──── Emit result message
 *    ↓
 * ProcessMessages()
 *    ↓
 * Subscriber receives result
 * ```
 * 
 * Usage:
 * ```cpp
 * // Define result message
 * struct CompilationResult : public Message {
 *     static constexpr MessageType TYPE = 100;
 *     std::vector<uint32_t> spirv;
 *     std::string errors;
 * };
 * 
 * // Create bridge
 * WorkerThreadBridge<CompilationResult> bridge(messageBus);
 * 
 * // Subscribe to results
 * messageBus->Subscribe(CompilationResult::TYPE, [](const Message& msg) {
 *     auto& result = static_cast<const CompilationResult&>(msg);
 *     if (result.success) {
 *         UpdateShaderLibrary(result.spirv);
 *     }
 *     return true;
 * });
 * 
 * // Submit work (non-blocking)
 * uint64_t id = bridge.SubmitWork(senderID, []() {
 *     CompilationResult result;
 *     result.spirv = CompileGLSL(source);
 *     result.success = true;
 *     return result;
 * });
 * ```
 */
template<typename ResultType>
class WorkerThreadBridge {
public:
    /**
     * @brief Constructor
     * 
     * @param messageBus MessageBus for publishing results
     */
    explicit WorkerThreadBridge(MessageBus* messageBus)
        : bus(messageBus)
        , running(true)
        , nextWorkID(1) {
        workerThread = std::thread(&WorkerThreadBridge::WorkerLoop, this);
    }

    ~WorkerThreadBridge() {
        // Signal shutdown
        running = false;
        workCV.notify_one();

        // Wait for worker to finish
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }

    // Non-copyable
    WorkerThreadBridge(const WorkerThreadBridge&) = delete;
    WorkerThreadBridge& operator=(const WorkerThreadBridge&) = delete;

    /**
     * @brief Submit work to worker thread (non-blocking)
     * 
     * @param sender Sender ID for result message
     * @param workFunc Function to execute on worker thread
     * @return Work ID for tracking
     */
    uint64_t SubmitWork(SenderID sender, std::function<ResultType()> workFunc) {
        uint64_t id = nextWorkID++;

        {
            std::lock_guard<std::mutex> lock(workMutex);
            workQueue.push({id, sender, std::move(workFunc)});
        }

        workCV.notify_one();
        return id;
    }

    /**
     * @brief Get number of queued work items
     */
    size_t GetQueuedCount() const {
        std::lock_guard<std::mutex> lock(workMutex);
        return workQueue.size();
    }

private:
    void WorkerLoop() {
        while (running) {
            WorkItem<ResultType> item;

            // Wait for work
            {
                std::unique_lock<std::mutex> lock(workMutex);
                workCV.wait(lock, [this] { return !workQueue.empty() || !running; });

                if (!running && workQueue.empty()) {
                    break;
                }

                if (workQueue.empty()) {
                    continue;
                }

                item = std::move(workQueue.front());
                workQueue.pop();
            }

            // Execute work
            WorkResult<ResultType> result;
            result.workID = item.workID;
            result.sender = item.sender;

            try {
                result.result = item.workFunction();
                result.success = true;
            } catch (const std::exception& e) {
                result.success = false;
                result.error = std::string("Worker thread exception: ") + e.what();
            } catch (...) {
                result.success = false;
                result.error = "Unknown worker thread exception";
            }

            // Publish result to MessageBus
            auto message = std::make_unique<ResultType>(std::move(result.result));
            message->sender = result.sender;
            
            if (!result.success) {
                // Attach error information if result type supports it
                if constexpr (requires { message->error; }) {
                    message->error = result.error;
                    message->success = false;
                }
            }

            bus->Publish(std::move(message));
        }
    }

    MessageBus* bus;
    std::thread workerThread;
    std::atomic<bool> running;
    std::atomic<uint64_t> nextWorkID;

    std::queue<WorkItem<ResultType>> workQueue;
    mutable std::mutex workMutex;
    std::condition_variable workCV;
};

} // namespace EventBus
