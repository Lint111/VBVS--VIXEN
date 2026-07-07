// Pipeline-cache blob header/UUID validation (audit V-M8): PipelineCacheBlobMatchesDevice is a
// pure function over VkPipelineCacheHeaderVersionOne — no VkDevice needed, just hand-built
// 32-byte headers.

#include <gtest/gtest.h>
#include <PipelineCacher.h>

#include <cstring>

namespace {

VkPhysicalDeviceProperties MakeProps(uint32_t vendorID, uint32_t deviceID, uint8_t uuidByte) {
    VkPhysicalDeviceProperties props{};
    props.vendorID = vendorID;
    props.deviceID = deviceID;
    std::memset(props.pipelineCacheUUID, uuidByte, VK_UUID_SIZE);
    return props;
}

std::vector<uint8_t> MakeHeader(uint32_t headerVersion, uint32_t vendorID, uint32_t deviceID, uint8_t uuidByte, size_t extraBytes = 0) {
    std::vector<uint8_t> blob(32 + extraBytes, 0);
    uint32_t headerSize = 32;
    std::memcpy(blob.data() + 0, &headerSize, sizeof(headerSize));
    std::memcpy(blob.data() + 4, &headerVersion, sizeof(headerVersion));
    std::memcpy(blob.data() + 8, &vendorID, sizeof(vendorID));
    std::memcpy(blob.data() + 12, &deviceID, sizeof(deviceID));
    std::memset(blob.data() + 16, uuidByte, VK_UUID_SIZE);
    return blob;
}

}  // namespace

TEST(PipelineCacheHeader, MatchingHeaderAndUUIDPasses) {
    const auto props = MakeProps(0x10DE, 0x2684, 0x42);
    const auto blob = MakeHeader(VK_PIPELINE_CACHE_HEADER_VERSION_ONE, 0x10DE, 0x2684, 0x42);
    EXPECT_TRUE(CashSystem::PipelineCacheBlobMatchesDevice(blob, props));
}

TEST(PipelineCacheHeader, WrongUUIDFails) {
    const auto props = MakeProps(0x10DE, 0x2684, 0x42);
    const auto blob = MakeHeader(VK_PIPELINE_CACHE_HEADER_VERSION_ONE, 0x10DE, 0x2684, 0x99);
    EXPECT_FALSE(CashSystem::PipelineCacheBlobMatchesDevice(blob, props));
}

TEST(PipelineCacheHeader, WrongVendorIdFails) {
    const auto props = MakeProps(0x10DE, 0x2684, 0x42);
    const auto blob = MakeHeader(VK_PIPELINE_CACHE_HEADER_VERSION_ONE, 0x1002, 0x2684, 0x42);
    EXPECT_FALSE(CashSystem::PipelineCacheBlobMatchesDevice(blob, props));
}

TEST(PipelineCacheHeader, WrongDeviceIdFails) {
    const auto props = MakeProps(0x10DE, 0x2684, 0x42);
    const auto blob = MakeHeader(VK_PIPELINE_CACHE_HEADER_VERSION_ONE, 0x10DE, 0x1234, 0x42);
    EXPECT_FALSE(CashSystem::PipelineCacheBlobMatchesDevice(blob, props));
}

TEST(PipelineCacheHeader, WrongHeaderVersionFails) {
    const auto props = MakeProps(0x10DE, 0x2684, 0x42);
    const auto blob = MakeHeader(999, 0x10DE, 0x2684, 0x42);
    EXPECT_FALSE(CashSystem::PipelineCacheBlobMatchesDevice(blob, props));
}

TEST(PipelineCacheHeader, TooShortBlobFails) {
    const auto props = MakeProps(0x10DE, 0x2684, 0x42);
    std::vector<uint8_t> blob(16, 0);  // shorter than the 32-byte header
    EXPECT_FALSE(CashSystem::PipelineCacheBlobMatchesDevice(blob, props));
}

TEST(PipelineCacheHeader, EmptyBlobFails) {
    const auto props = MakeProps(0x10DE, 0x2684, 0x42);
    EXPECT_FALSE(CashSystem::PipelineCacheBlobMatchesDevice({}, props));
}

TEST(PipelineCacheHeader, MatchingHeaderWithTrailingDataPasses) {
    const auto props = MakeProps(0x10DE, 0x2684, 0x42);
    const auto blob = MakeHeader(VK_PIPELINE_CACHE_HEADER_VERSION_ONE, 0x10DE, 0x2684, 0x42, /*extraBytes=*/128);
    EXPECT_TRUE(CashSystem::PipelineCacheBlobMatchesDevice(blob, props));
}
