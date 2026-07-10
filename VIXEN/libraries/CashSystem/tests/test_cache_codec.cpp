// Bounds-checked disk-cache codec (audit V-M5..M8, N6, N7).
//
// CacheReader/CacheWriter are the shared mechanics behind VoxelSceneCacher, ShaderModuleCacher,
// and PipelineCacher's SerializeToFile/DeserializeFromFile. These tests are pure — no device,
// no Vulkan — because the codec never touches a VkDevice; it only ever sees std::fstream.

#include <gtest/gtest.h>
#include <CacheCodec.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

using CashSystem::CacheReader;
using CashSystem::CacheWriter;

namespace {

std::filesystem::path TempPath(const char* name) {
    return std::filesystem::temp_directory_path() / (std::string("cache_codec_test_") + name);
}

}  // namespace

TEST(CacheCodec, RoundTripPodStringVector) {
    const auto path = TempPath("roundtrip.bin");
    {
        std::ofstream out(path, std::ios::binary);
        CacheWriter w(out);
        w.WritePod<uint32_t>(0xDEADBEEF);
        w.WriteString("hello world");
        w.WriteVector(std::vector<int>{1, 2, 3, 4, 5});
    }

    std::ifstream in(path, std::ios::binary);
    CacheReader r(in);

    uint32_t magic = 0;
    ASSERT_TRUE(r.ReadPod(magic));
    EXPECT_EQ(magic, 0xDEADBEEFu);

    std::string s;
    ASSERT_TRUE(r.ReadString(s, 1024));
    EXPECT_EQ(s, "hello world");

    std::vector<int> v;
    ASSERT_TRUE(r.ReadVector(v, 1024));
    EXPECT_EQ(v, (std::vector<int>{1, 2, 3, 4, 5}));
    EXPECT_TRUE(r.Ok());

    std::filesystem::remove(path);
}

TEST(CacheCodec, ReadVectorHugeLengthFailsWithoutAllocating) {
    const auto path = TempPath("huge_length.bin");
    {
        std::ofstream out(path, std::ios::binary);
        CacheWriter w(out);
        // Hand-write a length that dwarfs the file — no vector payload follows it.
        w.WritePod<uint64_t>(0xFFFFFFFFFFFFFFFFULL);
    }

    std::ifstream in(path, std::ios::binary);
    CacheReader r(in);

    std::vector<uint32_t> v;
    EXPECT_FALSE(r.ReadVector(v, 1'000'000));
    EXPECT_FALSE(r.Ok());
    // No allocation happened — resize() was never reached because the length exceeded both the
    // cap and the remaining file size.
    EXPECT_TRUE(v.empty());

    std::filesystem::remove(path);
}

TEST(CacheCodec, ReadVectorLengthWithinCapButBeyondFileFails) {
    const auto path = TempPath("beyond_file.bin");
    {
        std::ofstream out(path, std::ios::binary);
        CacheWriter w(out);
        // Claims 1000 elements (well under any reasonable cap) but writes none.
        w.WritePod<uint64_t>(1000);
    }

    std::ifstream in(path, std::ios::binary);
    CacheReader r(in);

    std::vector<uint32_t> v;
    EXPECT_FALSE(r.ReadVector(v, 1'000'000));
    EXPECT_FALSE(r.Ok());

    std::filesystem::remove(path);
}

TEST(CacheCodec, TruncatedStreamFailsAtTheRightRead) {
    const auto path = TempPath("truncated.bin");
    {
        std::ofstream out(path, std::ios::binary);
        CacheWriter w(out);
        w.WritePod<uint32_t>(42);
        w.WriteVector(std::vector<int>{1, 2, 3});
    }

    // Truncate the file to cut off partway through the vector payload.
    {
        std::ifstream probe(path, std::ios::binary | std::ios::ate);
        auto fullSize = probe.tellg();
        probe.close();
        std::filesystem::resize_file(path, static_cast<uintmax_t>(fullSize) - 4);
    }

    std::ifstream in(path, std::ios::binary);
    CacheReader r(in);

    uint32_t magic = 0;
    ASSERT_TRUE(r.ReadPod(magic));
    EXPECT_EQ(magic, 42u);

    std::vector<int> v;
    EXPECT_FALSE(r.ReadVector(v, 1024));
    EXPECT_FALSE(r.Ok());

    std::filesystem::remove(path);
}

TEST(CacheCodec, ReadStringLengthBeyondCapFails) {
    const auto path = TempPath("string_cap.bin");
    {
        std::ofstream out(path, std::ios::binary);
        CacheWriter w(out);
        w.WriteString(std::string(2000, 'x'));
    }

    std::ifstream in(path, std::ios::binary);
    CacheReader r(in);

    std::string s;
    EXPECT_FALSE(r.ReadString(s, 100));  // cap smaller than the actual string
    EXPECT_FALSE(r.Ok());

    std::filesystem::remove(path);
}

TEST(CacheCodec, SizeTimesSizeofOverflowRejected) {
    const auto path = TempPath("overflow.bin");
    {
        std::ofstream out(path, std::ios::binary);
        CacheWriter w(out);
        // A count that overflows size_t*sizeof(T) when widened — must be rejected before any
        // multiplication wraps back into a small, allocatable number.
        w.WritePod<uint64_t>(std::numeric_limits<uint64_t>::max() / 2);
    }

    std::ifstream in(path, std::ios::binary);
    CacheReader r(in);

    struct Big { uint8_t data[64]; };
    std::vector<Big> v;
    EXPECT_FALSE(r.ReadVector(v, std::numeric_limits<size_t>::max()));
    EXPECT_FALSE(r.Ok());

    std::filesystem::remove(path);
}

TEST(CacheCodec, RoundTrip32BitLengthVariants) {
    const auto path = TempPath("roundtrip32.bin");
    {
        std::ofstream out(path, std::ios::binary);
        CacheWriter w(out);
        w.WriteString32("shader source path");
        w.WriteVector32(std::vector<uint32_t>{0x07230203u, 1, 2, 3});
    }

    std::ifstream in(path, std::ios::binary);
    CacheReader r(in);

    std::string s;
    ASSERT_TRUE(r.ReadString32(s, 1024));
    EXPECT_EQ(s, "shader source path");

    std::vector<uint32_t> v;
    ASSERT_TRUE(r.ReadVector32(v, 1024));
    EXPECT_EQ(v, (std::vector<uint32_t>{0x07230203u, 1, 2, 3}));
    EXPECT_TRUE(r.Ok());

    std::filesystem::remove(path);
}

TEST(CacheCodec, ReadVector32HugeLengthFailsWithoutAllocating) {
    const auto path = TempPath("huge32.bin");
    {
        std::ofstream out(path, std::ios::binary);
        CacheWriter w(out);
        w.WritePod<uint32_t>(0xFFFFFFFFu);
    }

    std::ifstream in(path, std::ios::binary);
    CacheReader r(in);

    std::vector<uint32_t> v;
    EXPECT_FALSE(r.ReadVector32(v, 1'000'000));
    EXPECT_FALSE(r.Ok());
    EXPECT_TRUE(v.empty());

    std::filesystem::remove(path);
}

TEST(CacheCodec, EmptyVectorRoundTrips) {
    const auto path = TempPath("empty_vec.bin");
    {
        std::ofstream out(path, std::ios::binary);
        CacheWriter w(out);
        w.WriteVector(std::vector<int>{});
    }

    std::ifstream in(path, std::ios::binary);
    CacheReader r(in);

    std::vector<int> v{1, 2, 3};  // pre-populated to prove it gets cleared to empty
    ASSERT_TRUE(r.ReadVector(v, 1024));
    EXPECT_TRUE(v.empty());
    EXPECT_TRUE(r.Ok());

    std::filesystem::remove(path);
}
