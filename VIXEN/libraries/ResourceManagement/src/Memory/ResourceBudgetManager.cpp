#include "Memory/ResourceBudgetManager.h"
#include <algorithm>
#include <limits>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/sysinfo.h>
#endif

namespace ResourceManagement {

// Budget configuration
void ResourceBudgetManager::Slot::Set(const ResourceBudget& budget) noexcept {
    maxBytes.store(budget.maxBytes, std::memory_order_relaxed);
    warningThreshold.store(budget.warningThreshold, std::memory_order_relaxed);
    strict.store(budget.strict, std::memory_order_relaxed);
    configured.store(true, std::memory_order_release);
}

std::optional<ResourceBudget> ResourceBudgetManager::Slot::Get() const noexcept {
    if (!configured.load(std::memory_order_acquire)) return std::nullopt;
    return ResourceBudget(maxBytes.load(std::memory_order_relaxed),
                          warningThreshold.load(std::memory_order_relaxed),
                          strict.load(std::memory_order_relaxed));
}

void ResourceBudgetManager::Slot::ResetAll() noexcept {
    configured.store(false, std::memory_order_release);
    maxBytes.store(0, std::memory_order_relaxed);
    warningThreshold.store(0, std::memory_order_relaxed);
    strict.store(false, std::memory_order_relaxed);
    usage.Reset();
}

ResourceBudgetManager::~ResourceBudgetManager() {
    CustomSlot* node = customHead_.load(std::memory_order_acquire);
    while (node) {
        CustomSlot* next = node->next;
        delete node;
        node = next;
    }
}

ResourceBudgetManager::Slot* ResourceBudgetManager::FindSlot(BudgetResourceType type) noexcept {
    const auto index = static_cast<size_t>(type);
    return index < kStandardTypeCount ? &slots_[index] : nullptr;
}

const ResourceBudgetManager::Slot* ResourceBudgetManager::FindSlot(BudgetResourceType type) const noexcept {
    const auto index = static_cast<size_t>(type);
    return index < kStandardTypeCount ? &slots_[index] : nullptr;
}

ResourceBudgetManager::CustomSlot* ResourceBudgetManager::FindCustom(const std::string& name) const noexcept {
    for (CustomSlot* node = customHead_.load(std::memory_order_acquire); node; node = node->next) {
        if (node->name == name) return node;
    }
    return nullptr;
}

ResourceBudgetManager::CustomSlot* ResourceBudgetManager::GetOrCreateCustom(const std::string& name) {
    if (CustomSlot* existing = FindCustom(name)) return existing;

    // First use of a name (cold): publish one node by CAS; a loser re-checks for a racing insert.
    auto* node = new CustomSlot(name);
    CustomSlot* head = customHead_.load(std::memory_order_acquire);
    for (;;) {
        node->next = head;
        if (customHead_.compare_exchange_weak(head, node,
                std::memory_order_release, std::memory_order_acquire)) {
            return node;
        }
        for (CustomSlot* seen = head; seen; seen = seen->next) {
            if (seen->name == name) {
                delete node;
                return seen;
            }
        }
    }
}

void ResourceBudgetManager::SetBudget(BudgetResourceType type, const ResourceBudget& budget) {
    if (Slot* slot = FindSlot(type)) slot->Set(budget);
}

void ResourceBudgetManager::SetBudget(const std::string& customType, const ResourceBudget& budget) {
    GetOrCreateCustom(customType)->Set(budget);
}

std::optional<ResourceBudget> ResourceBudgetManager::GetBudget(BudgetResourceType type) const {
    const Slot* slot = FindSlot(type);
    return slot ? slot->Get() : std::nullopt;
}

std::optional<ResourceBudget> ResourceBudgetManager::GetBudget(const std::string& customType) const {
    const CustomSlot* slot = FindCustom(customType);
    return slot ? slot->Get() : std::nullopt;
}

// Allocation attempts (check if allocation would succeed)
bool ResourceBudgetManager::TryAllocate(BudgetResourceType type, uint64_t bytes) {
    const Slot* slot = FindSlot(type);
    return slot ? TryAllocateImpl(*slot, bytes) : true;
}

bool ResourceBudgetManager::TryAllocate(const std::string& customType, uint64_t bytes) {
    const CustomSlot* slot = FindCustom(customType);
    return slot ? TryAllocateImpl(*slot, bytes) : true;  // no budget configured: allow
}

// Record actual allocations (atomic updates on the slot's counters)
void ResourceBudgetManager::RecordAllocation(BudgetResourceType type, uint64_t bytes) {
    if (Slot* slot = FindSlot(type)) RecordAllocationImpl(&slot->usage, bytes);
}

void ResourceBudgetManager::RecordAllocation(const std::string& customType, uint64_t bytes) {
    RecordAllocationImpl(&GetOrCreateCustom(customType)->usage, bytes);
}

// Record deallocations
void ResourceBudgetManager::RecordDeallocation(BudgetResourceType type, uint64_t bytes) {
    if (Slot* slot = FindSlot(type)) RecordDeallocationImpl(&slot->usage, bytes);
}

void ResourceBudgetManager::RecordDeallocation(const std::string& customType, uint64_t bytes) {
    if (CustomSlot* slot = FindCustom(customType)) RecordDeallocationImpl(&slot->usage, bytes);
}

// Query current state (atomic reads)
BudgetResourceUsage ResourceBudgetManager::GetUsage(BudgetResourceType type) const {
    const Slot* slot = FindSlot(type);
    return slot ? slot->usage.ToUsage() : BudgetResourceUsage{};
}

BudgetResourceUsage ResourceBudgetManager::GetUsage(const std::string& customType) const {
    const CustomSlot* slot = FindCustom(customType);
    return slot ? slot->usage.ToUsage() : BudgetResourceUsage{};
}

uint64_t ResourceBudgetManager::AvailableBytesImpl(const Slot* slot) {
    if (!slot || !slot->configured.load(std::memory_order_acquire)) {
        return (std::numeric_limits<uint64_t>::max)(); // Unlimited
    }
    const uint64_t max = slot->maxBytes.load(std::memory_order_relaxed);
    if (max == 0) return (std::numeric_limits<uint64_t>::max)(); // Unlimited

    const uint64_t current = slot->usage.currentBytes.load(std::memory_order_acquire);
    return (current < max) ? (max - current) : 0;
}

bool ResourceBudgetManager::NearWarningImpl(const Slot* slot) {
    if (!slot || !slot->configured.load(std::memory_order_acquire)) return false;
    const uint64_t warning = slot->warningThreshold.load(std::memory_order_relaxed);
    if (warning == 0) return false; // No warning threshold set
    return slot->usage.currentBytes.load(std::memory_order_acquire) >= warning;
}

uint64_t ResourceBudgetManager::GetAvailableBytes(BudgetResourceType type) const {
    return AvailableBytesImpl(FindSlot(type));
}

uint64_t ResourceBudgetManager::GetAvailableBytes(const std::string& customType) const {
    return AvailableBytesImpl(FindCustom(customType));
}

bool ResourceBudgetManager::IsOverBudget(BudgetResourceType type) const {
    return GetAvailableBytes(type) == 0;
}

bool ResourceBudgetManager::IsOverBudget(const std::string& customType) const {
    return GetAvailableBytes(customType) == 0;
}

bool ResourceBudgetManager::IsNearWarningThreshold(BudgetResourceType type) const {
    return NearWarningImpl(FindSlot(type));
}

bool ResourceBudgetManager::IsNearWarningThreshold(const std::string& customType) const {
    return NearWarningImpl(FindCustom(customType));
}

// System memory detection
uint64_t ResourceBudgetManager::DetectHostMemoryBytes() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return static_cast<uint64_t>(memInfo.ullTotalPhys);
    }
    return 0;
#else
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        return static_cast<uint64_t>(si.totalram) * si.mem_unit;
    }
    return 0;
#endif
}

uint64_t ResourceBudgetManager::DetectDeviceMemoryBytes(VkPhysicalDevice physicalDevice) {
    if (physicalDevice == VK_NULL_HANDLE) {
        return 0;
    }

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    uint64_t totalDeviceMemory = 0;
    for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            totalDeviceMemory += memProps.memoryHeaps[i].size;
        }
    }

    return totalDeviceMemory;
}

// Utilities
void ResourceBudgetManager::Reset() {
    for (auto& slot : slots_) slot.ResetAll();
    // Custom nodes stay allocated (lock-free readers may hold them); they just go back to unconfigured.
    for (CustomSlot* node = customHead_.load(std::memory_order_acquire); node; node = node->next) {
        node->ResetAll();
    }
}

void ResourceBudgetManager::ResetUsage(BudgetResourceType type) {
    if (Slot* slot = FindSlot(type)) slot->usage.Reset();
}

void ResourceBudgetManager::ResetUsage(const std::string& customType) {
    if (CustomSlot* slot = FindCustom(customType)) slot->usage.Reset();
}

// Internal helpers
bool ResourceBudgetManager::TryAllocateImpl(const Slot& slot, uint64_t bytes) {
    const auto budget = slot.Get();
    if (!budget || budget->maxBytes == 0) {
        return true; // No budget limit - allow
    }

    const uint64_t current = slot.usage.currentBytes.load(std::memory_order_acquire);
    const uint64_t newTotal = current + bytes;

    if (budget->strict && newTotal > budget->maxBytes) {
        return false; // Strict mode - reject allocation over budget
    }

    return true; // Allow allocation (may exceed budget if not strict)
}

void ResourceBudgetManager::RecordAllocationImpl(AtomicResourceUsage* usage, uint64_t bytes) {
    // Atomic add for current bytes
    uint64_t oldValue = usage->currentBytes.fetch_add(bytes, std::memory_order_acq_rel);
    uint64_t newValue = oldValue + bytes;

    // Update peak using CAS loop
    uint64_t peak = usage->peakBytes.load(std::memory_order_relaxed);
    while (newValue > peak &&
           !usage->peakBytes.compare_exchange_weak(peak, newValue,
               std::memory_order_release, std::memory_order_relaxed)) {
        // peak updated by CAS, retry if still greater
    }

    usage->allocationCount.fetch_add(1, std::memory_order_relaxed);
}

void ResourceBudgetManager::RecordDeallocationImpl(AtomicResourceUsage* usage, uint64_t bytes) {
    // Atomic subtract with underflow protection
    uint64_t current = usage->currentBytes.load(std::memory_order_acquire);
    uint64_t newValue;

    do {
        newValue = (current >= bytes) ? (current - bytes) : 0;
    } while (!usage->currentBytes.compare_exchange_weak(current, newValue,
                std::memory_order_release, std::memory_order_acquire));

    // Decrement allocation count (saturating at 0)
    uint32_t count = usage->allocationCount.load(std::memory_order_relaxed);
    while (count > 0 &&
           !usage->allocationCount.compare_exchange_weak(count, count - 1,
               std::memory_order_release, std::memory_order_relaxed)) {
        // count updated, retry
    }
}

} // namespace ResourceManagement
