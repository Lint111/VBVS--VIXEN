#include "Memory/MemoryPressure.h"

namespace ResourceManagement {

LiveDeviceBudget QueryLiveDeviceBudget(VkPhysicalDevice physicalDevice, bool extensionEnabled) {
    LiveDeviceBudget out{};
    if (physicalDevice == VK_NULL_HANDLE) return out;

    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
    budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

    VkPhysicalDeviceMemoryProperties2 props{};
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    if (extensionEnabled) props.pNext = &budget;
    vkGetPhysicalDeviceMemoryProperties2(physicalDevice, &props);

    const auto& mem = props.memoryProperties;
    for (uint32_t i = 0; i < mem.memoryHeapCount; ++i) {
        if (!(mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)) continue;
        if (extensionEnabled) {
            out.budgetBytes += budget.heapBudget[i];
            out.usageBytes += budget.heapUsage[i];
        } else {
            out.budgetBytes += mem.memoryHeaps[i].size;
        }
    }
    out.fromExtension = extensionEnabled;
    return out;
}

}  // namespace ResourceManagement
