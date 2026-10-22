// Pipeline-cache blob header/UUID validation (audit V-M8): PipelineCacheBlobMatchesDevice is a
// pure function over VkPipelineCacheHeaderVersionOne — no VkDevice needed, just hand-built
// 32-byte headers.

#include <gtest/gtest.h>
#include <PipelineCacher.h>
#include <SharedPipelineCacheStore.h>
#include <RuntimeCachePaths.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

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

// ---- Shared (device/driver-keyed) pipeline cache store ----------------------------------------

namespace {

struct SharedStoreSandbox {
    std::filesystem::path root;
    SharedStoreSandbox() {
        root = std::filesystem::temp_directory_path() /
               ("vixen_shared_store_test_" + std::to_string(Vixen::CurrentProcessId()));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    ~SharedStoreSandbox() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void SetEnvironment(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

}  // namespace

TEST(SharedPipelineCacheStore, RoundTripsABlobForTheSameDeviceAndDriver) {
    SharedStoreSandbox sandbox;
    auto props = MakeProps(0x1002, 0x744C, 0x42);
    props.driverVersion = 7;
    const auto blob = MakeHeader(VK_PIPELINE_CACHE_HEADER_VERSION_ONE, 0x1002, 0x744C, 0x42, 64);
    const auto path = sandbox.root / "compute.bin";

    ASSERT_TRUE(CashSystem::SaveSharedPipelineCacheBlob(path, props, "compute", blob));

    std::vector<uint8_t> loaded;
    EXPECT_EQ(CashSystem::LoadSharedPipelineCacheBlob(path, props, "compute", loaded), CashSystem::CacheFileStatus::Loaded);
    EXPECT_EQ(loaded, blob);
}

TEST(SharedPipelineCacheStore, MissingFileIsMissingNotAnError) {
    SharedStoreSandbox sandbox;
    const auto props = MakeProps(0x1002, 0x744C, 0x42);
    std::vector<uint8_t> loaded;
    EXPECT_EQ(CashSystem::LoadSharedPipelineCacheBlob(sandbox.root / "none.bin", props, "compute", loaded),
              CashSystem::CacheFileStatus::Missing);
    EXPECT_TRUE(loaded.empty());
}

TEST(SharedPipelineCacheStore, DriverVersionChangeIsRejected) {
    SharedStoreSandbox sandbox;
    auto props = MakeProps(0x1002, 0x744C, 0x42);
    props.driverVersion = 7;
    const auto blob = MakeHeader(VK_PIPELINE_CACHE_HEADER_VERSION_ONE, 0x1002, 0x744C, 0x42, 64);
    const auto path = sandbox.root / "compute.bin";
    ASSERT_TRUE(CashSystem::SaveSharedPipelineCacheBlob(path, props, "compute", blob));

    props.driverVersion = 8;
    std::vector<uint8_t> loaded;
    EXPECT_EQ(CashSystem::LoadSharedPipelineCacheBlob(path, props, "compute", loaded), CashSystem::CacheFileStatus::Rejected);
    EXPECT_TRUE(loaded.empty());
}

TEST(SharedPipelineCacheStore, MismatchedVulkanHeaderIsRejectedOnSaveAndOnLoad) {
    SharedStoreSandbox sandbox;
    const auto props = MakeProps(0x1002, 0x744C, 0x42);
    const auto otherUuid = MakeHeader(VK_PIPELINE_CACHE_HEADER_VERSION_ONE, 0x1002, 0x744C, 0x99, 64);
    const auto path = sandbox.root / "compute.bin";
    EXPECT_FALSE(CashSystem::SaveSharedPipelineCacheBlob(path, props, "compute", otherUuid));
    EXPECT_FALSE(std::filesystem::exists(path));

    // A valid envelope (right identity and checksum) around a blob with another device UUID.
    const std::vector<char> payload(otherUuid.begin(), otherUuid.end());
    ASSERT_TRUE(CashSystem::WriteCacheEnvelopeBytesAtomically(
        path, CashSystem::SharedPipelineCacheIdentity(props), "shared-vk-pipeline-cache:compute", payload));
    std::vector<uint8_t> loaded;
    EXPECT_EQ(CashSystem::LoadSharedPipelineCacheBlob(path, props, "compute", loaded), CashSystem::CacheFileStatus::Rejected);
    EXPECT_TRUE(loaded.empty());
}

TEST(SharedPipelineCacheStore, CorruptedPayloadIsRejected) {
    SharedStoreSandbox sandbox;
    const auto props = MakeProps(0x1002, 0x744C, 0x42);
    const auto blob = MakeHeader(VK_PIPELINE_CACHE_HEADER_VERSION_ONE, 0x1002, 0x744C, 0x42, 64);
    const auto path = sandbox.root / "compute.bin";
    ASSERT_TRUE(CashSystem::SaveSharedPipelineCacheBlob(path, props, "compute", blob));
    {
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(-1, std::ios::end);
        file.put('\x7f');
    }
    std::vector<uint8_t> loaded;
    EXPECT_EQ(CashSystem::LoadSharedPipelineCacheBlob(path, props, "compute", loaded), CashSystem::CacheFileStatus::Rejected);
    EXPECT_TRUE(loaded.empty());
}

TEST(SharedPipelineCacheStore, KindsAreIsolated) {
    SharedStoreSandbox sandbox;
    const auto props = MakeProps(0x1002, 0x744C, 0x42);
    const auto blob = MakeHeader(VK_PIPELINE_CACHE_HEADER_VERSION_ONE, 0x1002, 0x744C, 0x42, 64);
    const auto path = sandbox.root / "compute.bin";
    ASSERT_TRUE(CashSystem::SaveSharedPipelineCacheBlob(path, props, "compute", blob));
    std::vector<uint8_t> loaded;
    EXPECT_EQ(CashSystem::LoadSharedPipelineCacheBlob(path, props, "graphics", loaded), CashSystem::CacheFileStatus::Rejected);
}

TEST(SharedPipelineCacheStore, PathFollowsTheSharedCacheEnvironment) {
    SharedStoreSandbox sandbox;
    SetEnvironment("VIXEN_SHARED_CACHE_DIR", sandbox.root.string().c_str());
    const auto props = MakeProps(0x1002, 0x744C, 0x42);
    const auto path = CashSystem::SharedPipelineCachePath(props, "graphics");
    EXPECT_EQ(path.parent_path().parent_path().parent_path(), sandbox.root);
    EXPECT_EQ(path.filename().string(), "graphics.bin");
    SetEnvironment("VIXEN_SHARED_CACHE_DIR", "");
}
