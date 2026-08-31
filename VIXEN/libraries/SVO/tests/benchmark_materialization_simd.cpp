#include "BulkMaterialization.h"
#include "MipBake.h"
#include "SdfBake.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace Vixen::SVO;

namespace {

constexpr std::size_t kRounds = 5;
std::atomic<std::uint64_t> g_sink{0};

Recipe::SdfInstruction opcode(Recipe::SdfOpCode value) {
    Recipe::SdfInstruction instruction{};
    instruction.opCode = static_cast<std::uint8_t>(value);
    return instruction;
}

BulkMaterializationRequest makeRequest() {
    auto sphere = opcode(Recipe::SdfOpCode::Sphere);
    sphere.data[3] = 19.0f;
    auto box = opcode(Recipe::SdfOpCode::Box);
    box.data[0] = 16.0f;
    box.data[1] = 20.0f;
    box.data[2] = 14.0f;
    auto smoothUnion = opcode(Recipe::SdfOpCode::SmoothUnion);
    smoothUnion.data[2] = 0.75f;
    return BulkMaterializationRequest{
        .key = {.region = 1, .generation = 1},
        .recipe = {sphere, box, smoothUnion},
        .parameters = {},
        .center = glm::vec3(32.0f),
        .resolution = 64,
        .bandVoxels = 2.0f,
        .brickDepth = 3,
    };
}

SerializedOctree scalarMaterialize(const BulkMaterializationRequest& request) {
    auto baked = BakeRecipeInstructionsToSdfWorld(
        request.recipe.data(), static_cast<std::uint32_t>(request.recipe.size()),
        request.center, static_cast<int>(request.resolution), request.bandVoxels,
        static_cast<int>(request.brickDepth), request.parameters);
    auto body = BuildSdfBodyOctree(baked, static_cast<int>(request.brickDepth));
    return SerializeSdfWithMips(body);
}

template <typename Fn>
std::pair<double, std::uint64_t> medianMilliseconds(Fn&& fn) {
    std::array<double, kRounds> samples{};
    std::uint64_t lastHash = 0;
    for (double& sample : samples) {
        const auto start = std::chrono::steady_clock::now();
        const SerializedOctree page = fn();
        const auto end = std::chrono::steady_clock::now();
        lastHash = canonicalHash(page);
        g_sink.fetch_add(lastHash, std::memory_order_relaxed);
        sample = std::chrono::duration<double, std::milli>(end - start).count();
    }
    std::ranges::sort(samples);
    return {samples[kRounds / 2], lastHash};
}

} // namespace

int main() {
    const BulkMaterializationRequest request = makeRequest();
    CpuRecipeMaterializer simdBackend;

    const auto [scalarMs, scalarHash] = medianMilliseconds([&] {
        return scalarMaterialize(request);
    });
    const auto [simdMs, simdHash] = medianMilliseconds([&] {
        return simdBackend.materialize(request, {});
    });

    std::cout << "method,lane_width,resolution,rounds,median_ms,canonical_hash\n";
    std::cout << "scalar,1," << request.resolution << ',' << kRounds << ','
              << scalarMs << ',' << scalarHash << '\n';
    std::cout << "compiler_simd,4," << request.resolution << ',' << kRounds << ','
              << simdMs << ',' << simdHash << '\n';
    if (scalarHash != simdHash) {
        std::cerr << "scalar/SIMD canonical hash mismatch\n";
        return 2;
    }
    return g_sink.load(std::memory_order_relaxed) == 0 ? 1 : 0;
}
