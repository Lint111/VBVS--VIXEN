/**
 * @file test_cleanup_stack_dependents.cpp
 * @brief CleanupStack::GetAllDependents must visit each node once (sn-gpuquery1).
 *
 * The recompile wave asks for the transitive dependents of every dirty node. The walk used to
 * recurse once per PATH, so a layered graph where each layer feeds both nodes of the next took
 * 2^layers steps: seconds of frame-thread time per shader recompile on the real graph.
 */

#include <gtest/gtest.h>

#include "CleanupStack.h"

#include <chrono>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;

TEST(CleanupStackDependents, LayeredDiamondsAreWalkedOncePerNode) {
    constexpr uint32_t kLayers = 40;  // 2^40 root-to-leaf paths
    CleanupStack stack;
    std::vector<NodeHandle> previous;
    uint32_t next = 0;
    NodeHandle root{next++};
    stack.Register(root, "root", [] {});
    previous.push_back(root);
    for (uint32_t layer = 0; layer < kLayers; ++layer) {
        std::vector<NodeHandle> current;
        for (int k = 0; k < 2; ++k) {
            NodeHandle h{next++};
            stack.Register(h, "n" + std::to_string(h.index), [] {}, previous);
            current.push_back(h);
        }
        previous = current;
    }

    const auto start = std::chrono::steady_clock::now();
    const auto dependents = stack.GetAllDependents(root);
    const auto elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    EXPECT_EQ(dependents.size(), static_cast<size_t>(kLayers) * 2u);
    EXPECT_LT(elapsedMs, 1000.0) << "dependents walk is exponential again";
}
