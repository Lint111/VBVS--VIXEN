/**
 * @file test_node_parameter_manager.cpp
 * @brief Tests for NodeParameterManager::GetParameterValue integral tolerance
 *
 * Covers the int32_t<->uint32_t silent-mismatch failure mode described in
 * Widescreen-Perf-Sweep-Findings-2026-07 (D1): callers store int literals,
 * readers request uint32_t, and a strict std::get_if silently returns the
 * default instead of the stored value.
 */

#include <gtest/gtest.h>
#include "Data/NodeParameterManager.h"

using namespace Vixen::RenderGraph;

TEST(NodeParameterManager, Int32StoredUInt32ReadConverts) {
    NodeParameterManager mgr;
    mgr.SetParameter("width", 1280);
    EXPECT_EQ(mgr.GetParameterValue<uint32_t>("width", 800u), 1280u);
}

TEST(NodeParameterManager, UInt32StoredInt32ReadConverts) {
    NodeParameterManager mgr;
    mgr.SetParameter("count", 42u);
    EXPECT_EQ(mgr.GetParameterValue<int32_t>("count", -1), 42);
}

TEST(NodeParameterManager, NegativeInt32DoesNotConvertToUInt32) {
    NodeParameterManager mgr;
    mgr.SetParameter("width", -5);
    EXPECT_EQ(mgr.GetParameterValue<uint32_t>("width", 800u), 800u);
}

TEST(NodeParameterManager, GenuineMismatchReturnsDefault) {
    NodeParameterManager mgr;
    mgr.SetParameter("width", std::string("wide"));
    EXPECT_EQ(mgr.GetParameterValue<uint32_t>("width", 800u), 800u);
}

TEST(NodeParameterManager, ExactMatchStillWorks) {
    NodeParameterManager mgr;
    mgr.SetParameter("width", 1280u);
    EXPECT_EQ(mgr.GetParameterValue<uint32_t>("width", 800u), 1280u);
}

TEST(NodeParameterManager, MissingParameterReturnsDefault) {
    NodeParameterManager mgr;
    EXPECT_EQ(mgr.GetParameterValue<uint32_t>("missing", 800u), 800u);
}
