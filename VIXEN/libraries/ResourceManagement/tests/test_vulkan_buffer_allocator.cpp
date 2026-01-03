/**
 * @file test_vulkan_buffer_allocator.cpp
 * @brief Sprint 5 Phase 5.2: Unit tests for VulkanBufferAllocator
 *
 * Tests the IMemoryAllocator interface and related data structures.
 * Uses an enhanced MockAllocator to simulate device addresses and OOM conditions.
 *
 * Covers:
 * - BufferAllocationRequest/BufferAllocation structures
 * - Device address retrieval
 * - Error handling for OOM
 * - HostVisible vs DeviceLocal memory locations
 * - AllocationError enum and string conversion
 * - Aliased allocation behavior
 */

#include <gtest/gtest.h>
#include "Memory/IMemoryAllocator.h"
#include "Memory/DeviceBudgetManager.h"
#include <memory>
#include <atomic>
#include <thread>

using namespace ResourceManagement;

// ============================================================================
// Enhanced Mock Allocator with Device Address and OOM Simulation
// ============================================================================

/**
 * @brief Enhanced mock allocator supporting device addresses and OOM simulation
 */
class EnhancedMockAllocator : public IMemoryAllocator {
public:
    EnhancedMockAllocator() = default;
    ~EnhancedMockAllocator() override = default;

    // Configuration methods for testing
    void SetSimulateOOM(bool enable) { simulateOOM_ = enable; }
    void SetDeviceAddressSupport(bool enable) { deviceAddressSupport_ = enable; }
    void SetMappableMemory(bool enable) { mappableMemory_ = enable; }
    void SetAliasingSupport(bool enable) { aliasingSupport_ = enable; }

    [[nodiscard]] std::expected<BufferAllocation, AllocationError>
    AllocateBuffer(const BufferAllocationRequest& request) override {
        std::lock_guard<std::mutex> lock(mutex_);

        // Simulate OOM condition
        if (simulateOOM_) {
            if (request.location == MemoryLocation::DeviceLocal) {
                return std::unexpected(AllocationError::OutOfDeviceMemory);
            } else {
                return std::unexpected(AllocationError::OutOfHostMemory);
            }
        }

        // Validate request parameters
        if (request.size == 0) {
            return std::unexpected(AllocationError::InvalidParameters);
        }

        // Simulate allocation
        uint64_t handle = nextHandle_++;
        totalAllocated_ += request.size;
        allocationCount_++;

        BufferAllocation result{
            .buffer = reinterpret_cast<VkBuffer>(handle),
            .allocation = reinterpret_cast<AllocationHandle>(handle),
            .size = request.size,
            .offset = 0,
            .mappedData = nullptr,
            .deviceAddress = 0,
            .canAlias = request.allowAliasing,
            .isAliased = false
        };

        // Set device address if supported and requested
        if (deviceAddressSupport_ &&
            (request.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)) {
            result.deviceAddress = 0x100000 + handle * 0x1000;  // Fake device address
        }

        // Set mapped pointer for host-visible memory
        if (mappableMemory_ && (request.location == MemoryLocation::HostVisible ||
                                request.location == MemoryLocation::HostCached)) {
            result.mappedData = reinterpret_cast<void*>(0xCAFE0000 + handle);
        }

        allocations_[handle] = {request.size, request.allowAliasing};

        return result;
    }

    void FreeBuffer(BufferAllocation& allocation) override {
        std::lock_guard<std::mutex> lock(mutex_);

        uint64_t handle = reinterpret_cast<uint64_t>(allocation.allocation);
        auto it = allocations_.find(handle);
        if (it != allocations_.end()) {
            // Don't count freed memory from aliased allocations (they don't own memory)
            if (!allocation.isAliased) {
                totalAllocated_ -= it->second.size;
            }
            allocationCount_--;
            allocations_.erase(it);
        }
        allocation = {};
    }

    [[nodiscard]] std::expected<ImageAllocation, AllocationError>
    AllocateImage(const ImageAllocationRequest& request) override {
        if (simulateOOM_) {
            return std::unexpected(AllocationError::OutOfDeviceMemory);
        }
        return std::unexpected(AllocationError::Unknown);
    }

    void FreeImage(ImageAllocation& allocation) override {}

    [[nodiscard]] std::expected<BufferAllocation, AllocationError>
    CreateAliasedBuffer(const AliasedBufferRequest& request) override {
        if (!aliasingSupport_) {
            return std::unexpected(AllocationError::InvalidParameters);
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Verify source allocation exists and supports aliasing
        uint64_t sourceHandle = reinterpret_cast<uint64_t>(request.sourceAllocation);
        auto it = allocations_.find(sourceHandle);
        if (it == allocations_.end() || !it->second.canAlias) {
            return std::unexpected(AllocationError::InvalidParameters);
        }

        uint64_t handle = nextHandle_++;
        // Aliased allocation doesn't add to total memory
        aliasedCount_++;

        BufferAllocation result{
            .buffer = reinterpret_cast<VkBuffer>(handle),
            .allocation = request.sourceAllocation,  // Share source allocation
            .size = request.size,
            .offset = request.offsetInAllocation,
            .mappedData = nullptr,
            .deviceAddress = 0,
            .canAlias = false,
            .isAliased = true
        };

        return result;
    }

    [[nodiscard]] std::expected<ImageAllocation, AllocationError>
    CreateAliasedImage(const AliasedImageRequest& request) override {
        return std::unexpected(AllocationError::Unknown);
    }

    [[nodiscard]] bool SupportsAliasing(AllocationHandle allocation) const override {
        if (!aliasingSupport_) return false;

        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t handle = reinterpret_cast<uint64_t>(allocation);
        auto it = allocations_.find(handle);
        return it != allocations_.end() && it->second.canAlias;
    }

    [[nodiscard]] void* MapBuffer(const BufferAllocation& allocation) override {
        if (!mappableMemory_) return nullptr;
        if (simulateOOM_) return nullptr;  // Simulate mapping failure

        return reinterpret_cast<void*>(0xDEADBEEF);
    }

    void UnmapBuffer(const BufferAllocation& allocation) override {}

    void FlushMappedRange(const BufferAllocation& allocation, VkDeviceSize offset, VkDeviceSize size) override {}

    void InvalidateMappedRange(const BufferAllocation& allocation, VkDeviceSize offset, VkDeviceSize size) override {}

    void SetBudgetManager(ResourceBudgetManager* budgetManager) override {
        budgetManager_ = budgetManager;
    }

    [[nodiscard]] ResourceBudgetManager* GetBudgetManager() const override {
        return budgetManager_;
    }

    [[nodiscard]] AllocationStats GetStats() const override {
        return AllocationStats{
            .totalAllocatedBytes = totalAllocated_.load(),
            .totalUsedBytes = totalAllocated_.load(),
            .allocationCount = static_cast<uint32_t>(allocationCount_.load()),
            .blockCount = static_cast<uint32_t>(allocationCount_.load()),
            .fragmentationRatio = 0.0f
        };
    }

    [[nodiscard]] std::string_view GetName() const override {
        return "EnhancedMockAllocator";
    }

    // Test accessors
    uint64_t GetTotalAllocated() const { return totalAllocated_.load(); }
    int GetAllocationCount() const { return allocationCount_.load(); }
    int GetAliasedCount() const { return aliasedCount_.load(); }

private:
    struct AllocationInfo {
        VkDeviceSize size;
        bool canAlias;
    };

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, AllocationInfo> allocations_;
    uint64_t nextHandle_ = 1;
    std::atomic<uint64_t> totalAllocated_{0};
    std::atomic<int> allocationCount_{0};
    std::atomic<int> aliasedCount_{0};
    ResourceBudgetManager* budgetManager_ = nullptr;

    // Simulation flags
    bool simulateOOM_ = false;
    bool deviceAddressSupport_ = true;
    bool mappableMemory_ = true;
    bool aliasingSupport_ = true;
};

// ============================================================================
// AllocationError Enum Tests
// ============================================================================

class AllocationErrorTest : public ::testing::Test {};

TEST_F(AllocationErrorTest, ErrorToStringConversion) {
    EXPECT_EQ(AllocationErrorToString(AllocationError::Success), "Success");
    EXPECT_EQ(AllocationErrorToString(AllocationError::OutOfDeviceMemory), "Out of device memory");
    EXPECT_EQ(AllocationErrorToString(AllocationError::OutOfHostMemory), "Out of host memory");
    EXPECT_EQ(AllocationErrorToString(AllocationError::OverBudget), "Over budget");
    EXPECT_EQ(AllocationErrorToString(AllocationError::InvalidParameters), "Invalid parameters");
    EXPECT_EQ(AllocationErrorToString(AllocationError::MappingFailed), "Mapping failed");
    EXPECT_EQ(AllocationErrorToString(AllocationError::Unknown), "Unknown error");
}

TEST_F(AllocationErrorTest, AllErrorsHaveStrings) {
    // Ensure all error codes produce non-empty strings
    for (uint8_t i = 0; i <= static_cast<uint8_t>(AllocationError::Unknown); ++i) {
        auto error = static_cast<AllocationError>(i);
        auto str = AllocationErrorToString(error);
        EXPECT_FALSE(str.empty()) << "Error " << static_cast<int>(i) << " has empty string";
    }
}

// ============================================================================
// BufferAllocationRequest Tests
// ============================================================================

class BufferAllocationRequestTest : public ::testing::Test {};

TEST_F(BufferAllocationRequestTest, DefaultValues) {
    BufferAllocationRequest request;
    EXPECT_EQ(request.size, 0);
    EXPECT_EQ(request.usage, 0);
    EXPECT_EQ(request.location, MemoryLocation::DeviceLocal);
    EXPECT_TRUE(request.debugName.empty());
    EXPECT_FALSE(request.dedicated);
    EXPECT_FALSE(request.allowAliasing);
}

TEST_F(BufferAllocationRequestTest, DesignatedInitializers) {
    BufferAllocationRequest request{
        .size = 4096,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .location = MemoryLocation::HostVisible,
        .debugName = "TestBuffer",
        .dedicated = true,
        .allowAliasing = true
    };

    EXPECT_EQ(request.size, 4096);
    EXPECT_TRUE(request.usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    EXPECT_EQ(request.location, MemoryLocation::HostVisible);
    EXPECT_EQ(request.debugName, "TestBuffer");
    EXPECT_TRUE(request.dedicated);
    EXPECT_TRUE(request.allowAliasing);
}

// ============================================================================
// BufferAllocation Tests
// ============================================================================

class BufferAllocationTest : public ::testing::Test {};

TEST_F(BufferAllocationTest, DefaultValues) {
    BufferAllocation alloc;
    EXPECT_EQ(alloc.buffer, VK_NULL_HANDLE);
    EXPECT_EQ(alloc.allocation, nullptr);
    EXPECT_EQ(alloc.size, 0);
    EXPECT_EQ(alloc.offset, 0);
    EXPECT_EQ(alloc.mappedData, nullptr);
    EXPECT_EQ(alloc.deviceAddress, 0);
    EXPECT_FALSE(alloc.canAlias);
    EXPECT_FALSE(alloc.isAliased);
}

TEST_F(BufferAllocationTest, BoolOperator) {
    BufferAllocation invalid;
    EXPECT_FALSE(invalid);

    BufferAllocation valid{
        .buffer = reinterpret_cast<VkBuffer>(1),
        .size = 1024
    };
    EXPECT_TRUE(valid);
}

// ============================================================================
// MemoryLocation Tests
// ============================================================================

class MemoryLocationTest : public ::testing::Test {};

TEST_F(MemoryLocationTest, AllLocationsDistinct) {
    EXPECT_NE(MemoryLocation::DeviceLocal, MemoryLocation::HostVisible);
    EXPECT_NE(MemoryLocation::HostVisible, MemoryLocation::HostCached);
    EXPECT_NE(MemoryLocation::HostCached, MemoryLocation::Auto);
    EXPECT_NE(MemoryLocation::Auto, MemoryLocation::DeviceLocal);
}

// ============================================================================
// Device Address Tests
// ============================================================================

class DeviceAddressTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator_ = std::make_shared<EnhancedMockAllocator>();
    }

    std::shared_ptr<EnhancedMockAllocator> allocator_;
};

TEST_F(DeviceAddressTest, DeviceAddressReturned) {
    allocator_->SetDeviceAddressSupport(true);

    BufferAllocationRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "DeviceAddressBuffer"
    };

    auto result = allocator_->AllocateBuffer(request);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->deviceAddress, 0);

    allocator_->FreeBuffer(*result);
}

TEST_F(DeviceAddressTest, NoDeviceAddressWithoutFlag) {
    allocator_->SetDeviceAddressSupport(true);

    // Request without SHADER_DEVICE_ADDRESS_BIT
    BufferAllocationRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "RegularBuffer"
    };

    auto result = allocator_->AllocateBuffer(request);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->deviceAddress, 0);

    allocator_->FreeBuffer(*result);
}

TEST_F(DeviceAddressTest, UniqueDeviceAddresses) {
    allocator_->SetDeviceAddressSupport(true);

    BufferAllocationRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "Buffer"
    };

    auto result1 = allocator_->AllocateBuffer(request);
    auto result2 = allocator_->AllocateBuffer(request);
    auto result3 = allocator_->AllocateBuffer(request);

    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());
    ASSERT_TRUE(result3.has_value());

    // Each buffer should have a unique device address
    EXPECT_NE(result1->deviceAddress, result2->deviceAddress);
    EXPECT_NE(result2->deviceAddress, result3->deviceAddress);
    EXPECT_NE(result1->deviceAddress, result3->deviceAddress);

    allocator_->FreeBuffer(*result1);
    allocator_->FreeBuffer(*result2);
    allocator_->FreeBuffer(*result3);
}

// ============================================================================
// OOM Error Handling Tests
// ============================================================================

class OOMHandlingTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator_ = std::make_shared<EnhancedMockAllocator>();
    }

    std::shared_ptr<EnhancedMockAllocator> allocator_;
};

TEST_F(OOMHandlingTest, DeviceLocalOOM) {
    allocator_->SetSimulateOOM(true);

    BufferAllocationRequest request{
        .size = 1024 * 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "OOMBuffer"
    };

    auto result = allocator_->AllocateBuffer(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AllocationError::OutOfDeviceMemory);
}

TEST_F(OOMHandlingTest, HostVisibleOOM) {
    allocator_->SetSimulateOOM(true);

    BufferAllocationRequest request{
        .size = 1024 * 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .location = MemoryLocation::HostVisible,
        .debugName = "OOMBuffer"
    };

    auto result = allocator_->AllocateBuffer(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AllocationError::OutOfHostMemory);
}

TEST_F(OOMHandlingTest, InvalidParameters) {
    BufferAllocationRequest request{
        .size = 0,  // Invalid: zero size
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "InvalidBuffer"
    };

    auto result = allocator_->AllocateBuffer(request);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), AllocationError::InvalidParameters);
}

TEST_F(OOMHandlingTest, RecoverAfterOOM) {
    // First simulate OOM
    allocator_->SetSimulateOOM(true);

    BufferAllocationRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "Buffer"
    };

    auto result1 = allocator_->AllocateBuffer(request);
    EXPECT_FALSE(result1.has_value());

    // Disable OOM simulation
    allocator_->SetSimulateOOM(false);

    // Now allocation should succeed
    auto result2 = allocator_->AllocateBuffer(request);
    ASSERT_TRUE(result2.has_value());
    EXPECT_NE(result2->buffer, VK_NULL_HANDLE);

    allocator_->FreeBuffer(*result2);
}

// ============================================================================
// Memory Location Behavior Tests
// ============================================================================

class MemoryLocationBehaviorTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator_ = std::make_shared<EnhancedMockAllocator>();
    }

    std::shared_ptr<EnhancedMockAllocator> allocator_;
};

TEST_F(MemoryLocationBehaviorTest, DeviceLocalNotMapped) {
    allocator_->SetMappableMemory(true);

    BufferAllocationRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "DeviceBuffer"
    };

    auto result = allocator_->AllocateBuffer(request);
    ASSERT_TRUE(result.has_value());

    // DeviceLocal memory typically shouldn't be pre-mapped
    // (though our mock doesn't set mappedData for DeviceLocal)
    EXPECT_EQ(result->mappedData, nullptr);

    allocator_->FreeBuffer(*result);
}

TEST_F(MemoryLocationBehaviorTest, HostVisibleMapped) {
    allocator_->SetMappableMemory(true);

    BufferAllocationRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .location = MemoryLocation::HostVisible,
        .debugName = "HostBuffer"
    };

    auto result = allocator_->AllocateBuffer(request);
    ASSERT_TRUE(result.has_value());

    // HostVisible memory should be mappable
    EXPECT_NE(result->mappedData, nullptr);

    allocator_->FreeBuffer(*result);
}

TEST_F(MemoryLocationBehaviorTest, HostCachedMapped) {
    allocator_->SetMappableMemory(true);

    BufferAllocationRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .location = MemoryLocation::HostCached,
        .debugName = "CachedBuffer"
    };

    auto result = allocator_->AllocateBuffer(request);
    ASSERT_TRUE(result.has_value());

    // HostCached memory should be mappable
    EXPECT_NE(result->mappedData, nullptr);

    allocator_->FreeBuffer(*result);
}

// ============================================================================
// Aliasing Tests
// ============================================================================

class AliasingTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator_ = std::make_shared<EnhancedMockAllocator>();
    }

    std::shared_ptr<EnhancedMockAllocator> allocator_;
};

TEST_F(AliasingTest, SupportsAliasingQuery) {
    allocator_->SetAliasingSupport(true);

    // Create allocation with aliasing enabled
    BufferAllocationRequest request{
        .size = 4096,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "AliasableBuffer",
        .allowAliasing = true
    };

    auto result = allocator_->AllocateBuffer(request);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->canAlias);

    // Query should return true
    EXPECT_TRUE(allocator_->SupportsAliasing(result->allocation));

    allocator_->FreeBuffer(*result);
}

TEST_F(AliasingTest, NonAliasableAllocation) {
    allocator_->SetAliasingSupport(true);

    // Create allocation without aliasing
    BufferAllocationRequest request{
        .size = 4096,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "NonAliasableBuffer",
        .allowAliasing = false
    };

    auto result = allocator_->AllocateBuffer(request);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->canAlias);

    // Query should return false
    EXPECT_FALSE(allocator_->SupportsAliasing(result->allocation));

    allocator_->FreeBuffer(*result);
}

TEST_F(AliasingTest, CreateAliasedBuffer) {
    allocator_->SetAliasingSupport(true);

    // Create source allocation with aliasing enabled
    BufferAllocationRequest sourceRequest{
        .size = 4096,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "SourceBuffer",
        .allowAliasing = true
    };

    auto source = allocator_->AllocateBuffer(sourceRequest);
    ASSERT_TRUE(source.has_value());

    // Create aliased buffer
    AliasedBufferRequest aliasRequest{
        .size = 2048,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sourceAllocation = source->allocation,
        .offsetInAllocation = 0,
        .debugName = "AliasedBuffer"
    };

    auto aliased = allocator_->CreateAliasedBuffer(aliasRequest);
    ASSERT_TRUE(aliased.has_value());
    EXPECT_TRUE(aliased->isAliased);
    EXPECT_EQ(aliased->allocation, source->allocation);  // Shares allocation

    // Total allocated should only count source (aliased doesn't add memory)
    EXPECT_EQ(allocator_->GetTotalAllocated(), 4096);  // Only source allocation
    EXPECT_EQ(allocator_->GetAliasedCount(), 1);

    allocator_->FreeBuffer(*aliased);
    allocator_->FreeBuffer(*source);
}

TEST_F(AliasingTest, CannotAliasNonAliasable) {
    allocator_->SetAliasingSupport(true);

    // Create source WITHOUT aliasing
    BufferAllocationRequest sourceRequest{
        .size = 4096,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "NonAliasableSource",
        .allowAliasing = false
    };

    auto source = allocator_->AllocateBuffer(sourceRequest);
    ASSERT_TRUE(source.has_value());

    // Try to create aliased buffer (should fail)
    AliasedBufferRequest aliasRequest{
        .size = 2048,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sourceAllocation = source->allocation,
        .offsetInAllocation = 0,
        .debugName = "AliasedBuffer"
    };

    auto aliased = allocator_->CreateAliasedBuffer(aliasRequest);
    EXPECT_FALSE(aliased.has_value());
    EXPECT_EQ(aliased.error(), AllocationError::InvalidParameters);

    allocator_->FreeBuffer(*source);
}

// ============================================================================
// Allocator Stats Tests
// ============================================================================

class AllocatorStatsTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator_ = std::make_shared<EnhancedMockAllocator>();
    }

    std::shared_ptr<EnhancedMockAllocator> allocator_;
};

TEST_F(AllocatorStatsTest, InitialStats) {
    auto stats = allocator_->GetStats();
    EXPECT_EQ(stats.totalAllocatedBytes, 0);
    EXPECT_EQ(stats.totalUsedBytes, 0);
    EXPECT_EQ(stats.allocationCount, 0);
    EXPECT_EQ(stats.blockCount, 0);
    EXPECT_EQ(stats.fragmentationRatio, 0.0f);
}

TEST_F(AllocatorStatsTest, StatsAfterAllocations) {
    BufferAllocationRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .location = MemoryLocation::DeviceLocal,
        .debugName = "StatsBuffer"
    };

    auto alloc1 = allocator_->AllocateBuffer(request);
    auto alloc2 = allocator_->AllocateBuffer(request);
    auto alloc3 = allocator_->AllocateBuffer(request);

    ASSERT_TRUE(alloc1.has_value());
    ASSERT_TRUE(alloc2.has_value());
    ASSERT_TRUE(alloc3.has_value());

    auto stats = allocator_->GetStats();
    EXPECT_EQ(stats.totalAllocatedBytes, 3 * 1024);
    EXPECT_EQ(stats.allocationCount, 3);

    allocator_->FreeBuffer(*alloc1);
    allocator_->FreeBuffer(*alloc2);
    allocator_->FreeBuffer(*alloc3);

    stats = allocator_->GetStats();
    EXPECT_EQ(stats.totalAllocatedBytes, 0);
    EXPECT_EQ(stats.allocationCount, 0);
}

TEST_F(AllocatorStatsTest, GetAllocatorName) {
    EXPECT_EQ(allocator_->GetName(), "EnhancedMockAllocator");
}

// ============================================================================
// Map/Unmap Tests
// ============================================================================

class MapUnmapTest : public ::testing::Test {
protected:
    void SetUp() override {
        allocator_ = std::make_shared<EnhancedMockAllocator>();
    }

    std::shared_ptr<EnhancedMockAllocator> allocator_;
};

TEST_F(MapUnmapTest, MapBufferSuccess) {
    allocator_->SetMappableMemory(true);

    BufferAllocationRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .location = MemoryLocation::HostVisible,
        .debugName = "MappableBuffer"
    };

    auto result = allocator_->AllocateBuffer(request);
    ASSERT_TRUE(result.has_value());

    void* mapped = allocator_->MapBuffer(*result);
    EXPECT_NE(mapped, nullptr);

    allocator_->UnmapBuffer(*result);
    allocator_->FreeBuffer(*result);
}

TEST_F(MapUnmapTest, MapBufferFailure) {
    allocator_->SetMappableMemory(false);

    BufferAllocationRequest request{
        .size = 1024,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .location = MemoryLocation::HostVisible,
        .debugName = "UnmappableBuffer"
    };

    auto result = allocator_->AllocateBuffer(request);
    ASSERT_TRUE(result.has_value());

    void* mapped = allocator_->MapBuffer(*result);
    EXPECT_EQ(mapped, nullptr);

    allocator_->FreeBuffer(*result);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
