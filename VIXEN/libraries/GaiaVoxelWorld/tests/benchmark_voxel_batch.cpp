#include "GaiaVoxelWorld.h"
#include "MortonEncoding.h"
#include "VoxelComponents.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

using namespace Vixen::GaiaVoxel;

namespace {

constexpr size_t kRequestCount = 100'000;
constexpr size_t kRounds = 5;
std::atomic<uint64_t> g_sink{0};

struct RequestFixture {
    std::vector<std::array<ComponentQueryRequest, 2>> components;
    std::vector<VoxelCreationRequest> requests;

    RequestFixture() {
        components.reserve(kRequestCount);
        requests.reserve(kRequestCount);
        for (size_t i = 0; i < kRequestCount; ++i) {
            components.push_back({
                Density{static_cast<float>(i % 97) / 96.0f},
                Material{static_cast<uint32_t>((i % 31) + 1)},
            });
            requests.emplace_back(
                glm::vec3(static_cast<float>(i % 256),
                          static_cast<float>((i / 256) % 256),
                          static_cast<float>(i / (256 * 256))),
                components.back());
        }
    }
};

struct PagePlacementMetadata {
    uint64_t region;
    uint64_t generation;
    uint32_t nodeOffset;
    uint32_t brickOffset;
};

template <typename Fn>
double medianMilliseconds(Fn&& fn) {
    std::array<double, kRounds> samples{};
    for (double& sample : samples) {
        const auto start = std::chrono::steady_clock::now();
        g_sink.fetch_add(fn(), std::memory_order_relaxed);
        const auto end = std::chrono::steady_clock::now();
        sample = std::chrono::duration<double, std::milli>(end - start).count();
    }
    std::ranges::sort(samples);
    return samples[kRounds / 2];
}

void printResult(std::string_view name, double milliseconds) {
    const double operationsPerSecond =
        static_cast<double>(kRequestCount) * 1000.0 / milliseconds;
    std::cout << name << ',' << kRequestCount << ',' << kRounds << ','
              << milliseconds << ',' << operationsPerSecond << '\n';
}

} // namespace

int main() {
    const RequestFixture fixture;

    const double serial = medianMilliseconds([&] {
        GaiaVoxelWorld world;
        const auto ids = world.createVoxelsBatch(
            fixture.requests, GaiaVoxelWorld::BatchCreateStrategy::Serial);
        return static_cast<uint64_t>(ids.size());
    });

    const double grouped = medianMilliseconds([&] {
        GaiaVoxelWorld world;
        const auto ids = world.createVoxelsBatch(
            fixture.requests, GaiaVoxelWorld::BatchCreateStrategy::GroupedCopyN);
        return static_cast<uint64_t>(ids.size());
    });

    const double gaiaCopyN = medianMilliseconds([&] {
        GaiaVoxelWorld holder;
        auto& world = holder.getWorld();
        const auto seed = world.add();
        world.add<MortonKey>(seed, MortonKeyUtils::fromPosition(glm::vec3(0.0f)));
        world.add<Density>(seed, Density{1.0f});
        world.add<Material>(seed, Material{1u});
        uint64_t count = 1;
        world.copy_n(seed, static_cast<uint32_t>(kRequestCount - 1), [&](gaia::ecs::Entity) {
            ++count;
        });
        return count;
    });

    const double directPageMetadata = medianMilliseconds([&] {
        std::vector<PagePlacementMetadata> placements;
        placements.reserve(kRequestCount);
        for (size_t i = 0; i < kRequestCount; ++i) {
            placements.push_back(PagePlacementMetadata{
                .region = i,
                .generation = 7,
                .nodeOffset = static_cast<uint32_t>(i * 8),
                .brickOffset = static_cast<uint32_t>(i * 2048),
            });
        }
        return placements.back().region + 1;
    });

    std::cout << "method,items,rounds,median_ms,items_per_second\n";
    printResult("serial_wrapper", serial);
    printResult("grouped_copy_n", grouped);
    printResult("pinned_gaia_copy_n", gaiaCopyN);
    printResult("direct_page_metadata", directPageMetadata);
    return g_sink.load(std::memory_order_relaxed) == 0 ? 1 : 0;
}
