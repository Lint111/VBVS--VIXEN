#include <gtest/gtest.h>
#include <fstream>
#include <iterator>
#include <string>

#include "AppFlowBlobFile.h"
#include "AppFlowRuntime.h"

using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

namespace {

std::string ReadGeneratedBlob() {
    std::ifstream file(APPFLOW_GENERATED_DATA_PATH, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string ReplaceOnce(std::string text, const std::string& from, const std::string& to) {
    const std::size_t offset = text.find(from);
    if (offset != std::string::npos)
        text.replace(offset, from.size(), to);
    return text;
}

void ExpectViewMatchesCompiled(const AppFlowContainerView& actual) {
    const AppFlowContainerView expected{};

    ASSERT_EQ(actual.actionTable().size(), expected.actions().size());
    for (std::size_t i = 0; i < actual.actionTable().size(); ++i) {
        const auto& got = actual.actionTable()[i];
        const auto& want = expected.actions()[i];
        EXPECT_EQ(got.id, want.id);
        EXPECT_EQ(got.footprintBytes, want.footprintBytes);
        EXPECT_EQ(got.hasInvert, want.hasInvert);
        ASSERT_EQ(got.paramCount, want.paramCount);
        for (uint32_t p = 0; p < got.paramCount; ++p) {
            EXPECT_STREQ(got.params[p].name, want.params[p].name);
            EXPECT_EQ(got.params[p].type, want.params[p].type);
        }
    }

    ASSERT_EQ(actual.transitionTable().size(), expected.transitions().size());
    for (std::size_t i = 0; i < actual.transitionTable().size(); ++i) {
        const auto& got = actual.transitionTable()[i];
        const auto& want = expected.transitions()[i];
        EXPECT_EQ(got.from, want.from);
        EXPECT_EQ(got.to, want.to);
        EXPECT_EQ(got.guard, want.guard);
        EXPECT_STREQ(got.effect, want.effect);
    }

    ASSERT_EQ(actual.elementTriggerTable().size(), expected.elementTriggers().size());
    for (std::size_t i = 0; i < actual.elementTriggerTable().size(); ++i) {
        const auto& got = actual.elementTriggerTable()[i];
        const auto& want = expected.elementTriggers()[i];
        EXPECT_STREQ(got.elementPattern, want.elementPattern);
        EXPECT_EQ(got.action, want.action);
        EXPECT_STREQ(got.paramName, want.paramName);
        EXPECT_STREQ(got.on, want.on);
    }

    ASSERT_EQ(actual.keyDefaultTable().size(), expected.keyDefaults().size());
    for (std::size_t i = 0; i < actual.keyDefaultTable().size(); ++i) {
        const auto& got = actual.keyDefaultTable()[i];
        const auto& want = expected.keyDefaults()[i];
        EXPECT_EQ(got.action, want.action);
        EXPECT_EQ(got.chord.key, want.chord.key);
        EXPECT_EQ(got.chord.mods, want.chord.mods);
        EXPECT_EQ(got.scope, want.scope);
        EXPECT_EQ(got.state, want.state);
    }

    ASSERT_EQ(actual.returnEdgeTable().size(), expected.returnEdges().size());
    for (std::size_t i = 0; i < actual.returnEdgeTable().size(); ++i) {
        const auto& got = actual.returnEdgeTable()[i];
        const auto& want = expected.returnEdges()[i];
        EXPECT_EQ(got.from, want.from);
        EXPECT_EQ(got.trigger.key, want.trigger.key);
        EXPECT_EQ(got.trigger.mods, want.trigger.mods);
    }

    ASSERT_EQ(actual.dataTargetTable().size(), expected.dataTargets().size());
    for (std::size_t i = 0; i < actual.dataTargetTable().size(); ++i) {
        EXPECT_EQ(actual.dataTargetTable()[i].action, expected.dataTargets()[i].action);
        EXPECT_EQ(actual.dataTargetTable()[i].viewNoun, expected.dataTargets()[i].viewNoun);
    }
}

}  // namespace

TEST(AppFlowBlob, BlobMatchesCompiledContainerAndRuntimeBehavior) {
    const std::string data = ReadGeneratedBlob();
    ASSERT_FALSE(data.empty());

    auto blob = AppFlowBlobFile::Load(APPFLOW_GENERATED_DATA_PATH);
    ASSERT_TRUE(blob.has_value());
    EXPECT_EQ(blob->ShapeHash(), kAppFlowShapeHash);
    ExpectViewMatchesCompiled(blob->View());

    AppFlowRuntime compiled(nullptr, 1);
    AppFlowRuntime loaded(nullptr, 1);
    ASSERT_EQ(compiled.Load(), LoadResult::Ok);
    ASSERT_EQ(loaded.Load(&blob->View()), LoadResult::Ok);

    compiled.SetGuardResult(FlowGuardId::DocumentValid, true);
    loaded.SetGuardResult(FlowGuardId::DocumentValid, true);
    compiled.SetCurrent(FlowStateId::Editing);
    loaded.SetCurrent(FlowStateId::Editing);
    EXPECT_EQ(compiled.NavTo(FlowStateId::Simulating), DispatchResult::Ok);
    EXPECT_EQ(loaded.NavTo(FlowStateId::Simulating), DispatchResult::Ok);
    EXPECT_EQ(loaded.Current(), compiled.Current());
}

TEST(AppFlowBlob, InteriorEditPreservesShapeHash) {
    const std::string interior = ReplaceOnce(
        ReadGeneratedBlob(), "layer-{index}-toggle", "layer-{index}-other");
    auto blob = AppFlowBlobFile::Parse(interior);

    ASSERT_TRUE(blob.has_value());
    EXPECT_EQ(blob->ShapeHash(), kAppFlowShapeHash);
    ASSERT_EQ(blob->View().elementTriggerTable().size(), 2u);
    EXPECT_STREQ(blob->View().elementTriggerTable()[0].elementPattern, "layer-{index}-other");
}

TEST(AppFlowBlob, ShapeHashMismatchRejectsAndRuntimeFallsBack) {
    const std::string shapeEdit = ReplaceOnce(
        ReadGeneratedBlob(), "shape 0x113F6528", "shape 0x00000000");
    auto blob = AppFlowBlobFile::Parse(shapeEdit);
    EXPECT_FALSE(blob.has_value());

    AppFlowRuntime runtime(nullptr, 1);
    EXPECT_EQ(runtime.Load(blob ? &blob->View() : nullptr), LoadResult::Ok);
    EXPECT_EQ(runtime.Current(), FlowStateId::Editing);
}
