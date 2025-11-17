#include "Headers.h"
#include "Core/SlotTask.h"
#include "Core/NodeInstance.h"
#include "Core/ResourceBudgetManager.h"

namespace Vixen::RenderGraph {

// Generate tasks from array slot
std::vector<SlotTaskContext> SlotTaskManager::GenerateTasks(
    NodeInstance* node,
    uint32_t slotIndex,
    SlotScope resourceScope)
{
    std::vector<SlotTaskContext> tasks;

    if (!node) {
        return tasks;
    }

    // Get array size for this slot
    size_t arraySize = node->GetInputCount(slotIndex);

    if (arraySize == 0) {
        return tasks; // No data to process
    }

    // Determine task granularity based on SlotScope
    switch (resourceScope) {
        case SlotScope::NodeLevel:
            // Single task for entire node (all array elements together)
            {
                SlotTaskContext task;
                task.node = node;
                task.taskIndex = 0;
                task.totalTasks = 1;
                task.arrayStartIndex = 0;
                task.arrayCount = static_cast<uint32_t>(arraySize);
                task.resourceScope = resourceScope;
                tasks.push_back(task);
            }
            break;

        case SlotScope::TaskLevel:
            // One task per array element (default for independent processing)
            tasks.reserve(arraySize);
            for (size_t i = 0; i < arraySize; ++i) {
                SlotTaskContext task;
                task.node = node;
                task.taskIndex = static_cast<uint32_t>(i);
                task.totalTasks = static_cast<uint32_t>(arraySize);
                task.arrayStartIndex = static_cast<uint32_t>(i);
                task.arrayCount = 1;
                task.resourceScope = resourceScope;
                tasks.push_back(task);
            }
            break;

        case SlotScope::InstanceLevel:
            // Parameterized: each array element drives instance creation
            // (Same as TaskLevel for now, can be extended for batching)
            tasks.reserve(arraySize);
            for (size_t i = 0; i < arraySize; ++i) {
                SlotTaskContext task;
                task.node = node;
                task.taskIndex = static_cast<uint32_t>(i);
                task.totalTasks = static_cast<uint32_t>(arraySize);
                task.arrayStartIndex = static_cast<uint32_t>(i);
                task.arrayCount = 1;
                task.resourceScope = resourceScope;
                tasks.push_back(task);
            }
            break;
    }

    return tasks;
}

// Execute tasks sequentially
uint32_t SlotTaskManager::ExecuteSequential(
    std::vector<SlotTaskContext>& tasks,
    const SlotTaskFunction& taskFunction)
{
    if (!taskFunction) {
        return 0;
    }

    uint32_t successCount = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    lastStats_ = ExecutionStats{};
    lastStats_.totalTasks = static_cast<uint32_t>(tasks.size());

    for (auto& task : tasks) {
        task.status = TaskStatus::Running;

        bool success = taskFunction(task);

        if (success) {
            task.status = TaskStatus::Completed;
            successCount++;
            lastStats_.completedTasks++;
        } else {
            task.status = TaskStatus::Failed;
            lastStats_.failedTasks++;
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    lastStats_.totalExecutionTimeMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    return successCount;
}

// Execute tasks in parallel (budget-aware)
uint32_t SlotTaskManager::ExecuteParallel(
    std::vector<SlotTaskContext>& tasks,
    const SlotTaskFunction& taskFunction,
    ResourceBudgetManager* budgetManager,
    uint32_t maxParallelism)
{
    if (!taskFunction || tasks.empty()) {
        return 0;
    }

    // Determine optimal parallelism level
    uint32_t parallelism = maxParallelism;
    if (parallelism == 0) {
        if (budgetManager) {
            parallelism = CalculateOptimalParallelism(tasks, budgetManager);
        } else {
            // Default: use hardware concurrency
            parallelism = std::max(1u, std::thread::hardware_concurrency());
        }
    }

    // Clamp to task count
    parallelism = std::min(parallelism, static_cast<uint32_t>(tasks.size()));

    auto startTime = std::chrono::high_resolution_clock::now();
    lastStats_ = ExecutionStats{};
    lastStats_.totalTasks = static_cast<uint32_t>(tasks.size());

    // Simple parallel execution using futures
    std::vector<std::future<bool>> futures;
    futures.reserve(parallelism);

    uint32_t successCount = 0;
    size_t taskIdx = 0;

    while (taskIdx < tasks.size()) {
        // Launch up to 'parallelism' tasks concurrently
        futures.clear();

        for (uint32_t p = 0; p < parallelism && taskIdx < tasks.size(); ++p, ++taskIdx) {
            auto& task = tasks[taskIdx];
            task.status = TaskStatus::Running;

            // Launch async task
            futures.push_back(std::async(std::launch::async,
                [&task, &taskFunction]() {
                    return taskFunction(task);
                }
            ));
        }

        // Wait for all launched tasks to complete
        size_t completedBatch = futures.size();
        size_t batchStartIdx = taskIdx - completedBatch;

        for (size_t i = 0; i < futures.size(); ++i) {
            bool success = futures[i].get();
            auto& task = tasks[batchStartIdx + i];

            if (success) {
                task.status = TaskStatus::Completed;
                successCount++;
                lastStats_.completedTasks++;
            } else {
                task.status = TaskStatus::Failed;
                lastStats_.failedTasks++;
            }
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    lastStats_.totalExecutionTimeMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    return successCount;
}

// Calculate optimal parallelism based on budget
uint32_t SlotTaskManager::CalculateOptimalParallelism(
    const std::vector<SlotTaskContext>& tasks,
    ResourceBudgetManager* budgetManager) const
{
    if (!budgetManager || tasks.empty()) {
        return std::max(1u, std::thread::hardware_concurrency());
    }

    // Calculate average memory per task
    uint64_t totalEstimatedMemory = 0;
    uint32_t tasksWithEstimates = 0;

    for (const auto& task : tasks) {
        if (task.estimatedMemoryBytes > 0) {
            totalEstimatedMemory += task.estimatedMemoryBytes;
            tasksWithEstimates++;
        }
    }

    if (tasksWithEstimates == 0) {
        // No memory estimates - use hardware concurrency
        return std::max(1u, std::thread::hardware_concurrency());
    }

    uint64_t avgMemoryPerTask = totalEstimatedMemory / tasksWithEstimates;

    // Get available host memory
    uint64_t availableMemory = budgetManager->GetAvailableBytes(BudgetResourceType::HostMemory);

    if (availableMemory == std::numeric_limits<uint64_t>::max()) {
        // Unlimited budget - use hardware concurrency
        return std::max(1u, std::thread::hardware_concurrency());
    }

    // Calculate how many tasks can fit in available memory
    uint32_t memoryBasedParallelism = (availableMemory > 0 && avgMemoryPerTask > 0)
        ? static_cast<uint32_t>(availableMemory / avgMemoryPerTask)
        : 1;

    // Clamp to hardware concurrency
    uint32_t hwConcurrency = std::max(1u, std::thread::hardware_concurrency());
    uint32_t optimalParallelism = std::min(memoryBasedParallelism, hwConcurrency);

    return std::max(1u, optimalParallelism);
}

} // namespace Vixen::RenderGraph
