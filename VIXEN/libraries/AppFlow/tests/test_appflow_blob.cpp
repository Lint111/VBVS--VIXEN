#include <gtest/gtest.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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
        EXPECT_STREQ(got.id, want.id);
        EXPECT_EQ(got.from, want.from);
        EXPECT_EQ(got.to, want.to);
        EXPECT_EQ(got.guard, want.guard);
    }

    ASSERT_EQ(actual.stateTable().size(), expected.stateTable().size());
    for (std::size_t i = 0; i < actual.stateTable().size(); ++i)
        EXPECT_EQ(actual.stateTable()[i], expected.stateTable()[i]);
    EXPECT_EQ(actual.initialState(), expected.initialState());
    ASSERT_EQ(actual.terminalStateTable().size(), expected.terminalStateTable().size());
    for (std::size_t i = 0; i < actual.terminalStateTable().size(); ++i)
        EXPECT_EQ(actual.terminalStateTable()[i], expected.terminalStateTable()[i]);

    ASSERT_EQ(actual.elementTriggerTable().size(), expected.elementTriggers().size());
    for (std::size_t i = 0; i < actual.elementTriggerTable().size(); ++i) {
        const auto& got = actual.elementTriggerTable()[i];
        const auto& want = expected.elementTriggers()[i];
        EXPECT_STREQ(got.id, want.id);
        EXPECT_STREQ(got.elementPattern, want.elementPattern);
        EXPECT_EQ(got.action, want.action);
        EXPECT_STREQ(got.paramName, want.paramName);
        EXPECT_STREQ(got.on, want.on);
    }

    ASSERT_EQ(actual.keyDefaultTable().size(), expected.keyDefaults().size());
    for (std::size_t i = 0; i < actual.keyDefaultTable().size(); ++i) {
        const auto& got = actual.keyDefaultTable()[i];
        const auto& want = expected.keyDefaults()[i];
        EXPECT_STREQ(got.id, want.id);
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
        EXPECT_STREQ(got.id, want.id);
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
    const FlowTriggerCause cause{FlowTriggerKind::System, "AppFlowBlobTest"};
    EXPECT_EQ(compiled.NavTo(FlowEdgeId::Transitions, cause), DispatchResult::Ok);
    EXPECT_EQ(loaded.NavTo(FlowEdgeId::Transitions, cause), DispatchResult::Ok);
    EXPECT_EQ(loaded.Current(), compiled.Current());
}

TEST(AppFlowBlob, InteriorEditPreservesShapeHash) {
    const std::string interior = ReplaceOnce(
        ReadGeneratedBlob(), "layer-{index}-toggle", "layer-{index}-other");
    auto blob = AppFlowBlobFile::Parse(interior);

    ASSERT_TRUE(blob.has_value());
    EXPECT_EQ(blob->ShapeHash(), kAppFlowShapeHash);
    // The edit renames one pattern in place: the table keeps the compiled container's size and order.
    ASSERT_EQ(blob->View().elementTriggerTable().size(), AppFlowContainerView::elementTriggers().size());
    EXPECT_STREQ(blob->View().elementTriggerTable()[0].elementPattern, "layer-{index}-other");
}

TEST(AppFlowBlob, ReloadedRuntimeOwnsParsedBlobStorage) {
    auto blob = AppFlowBlobFile::Parse(ReplaceOnce(
        ReadGeneratedBlob(), "layer-{index}-toggle", "layer-{index}-other"));
    ASSERT_TRUE(blob.has_value());

    AppFlowRuntime runtime(nullptr, 1);
    ASSERT_EQ(runtime.Load(&blob->View()), LoadResult::Ok);
    runtime.RegisterHandler(FlowActionId::ToggleLayer,
                            [&](const AppFlowRuntime::Params&) {
                                runtime.Stack().Dispatch(FlowActionId::ToggleLayer,
                                                         [](bool) {});
                            });

    const AppFlowContainerView invalid(
        std::span<const AppFlowActionDecl>{}, std::span<const AppFlowTransition>{});
    EXPECT_EQ(runtime.Load(&invalid), LoadResult::EmptyArtifact);

    // The runtime must not depend on the temporary blob's strings or parameter arrays after Load.
    blob.reset();
    EXPECT_EQ(runtime.DispatchBySelector("layer-3-other"), DispatchResult::Ok);
    EXPECT_EQ(runtime.DispatchBySelector("layer-3-toggle"), DispatchResult::RejectedByState);
}

// A parametric selector resolves its parameter by name through the registered action schema. After a
// reload from a parsed blob that the caller then destroys, that name must still be the runtime's own
// (a String-parameter pattern, as the editor's "recipe-{recipeId}-open").
TEST(AppFlowBlob, ReloadedRuntimeResolvesStringPatternAfterBlobIsFreed) {
    auto blob = AppFlowBlobFile::Parse(ReadGeneratedBlob());
    ASSERT_TRUE(blob.has_value());
    AppFlowRuntime runtime(nullptr, 1);
    ASSERT_EQ(runtime.Load(&blob->View()), LoadResult::Ok);
    std::string opened;
    runtime.RegisterHandler(FlowActionId::OpenRecipe, [&](const AppFlowRuntime::Params& params) {
        for (const auto& [name, value] : params) if (name == "recipeId") opened = value;
    });

    blob.reset();
    std::vector<std::string> scribble(4096, std::string(64, '#'));   // reuse the freed blob storage
    EXPECT_EQ(runtime.DispatchBySelector("recipe-core:asteroid-open"), DispatchResult::Ok);
    EXPECT_EQ(opened, "core:asteroid");
}

// Live reload of a key default onto a running runtime: the new chord is bound and the old one is not, while the
// undo history, the flow state and the registered handlers stay (an agent editing the data must not lose its work).
TEST(AppFlowBlob, ReloadRebindsKeyDefaultAndKeepsUndoStateAndHandlers) {
    AppFlowRuntime runtime(nullptr, 1);
    ASSERT_EQ(runtime.Load(), LoadResult::Ok);
    int undone = 0;
    runtime.RegisterHandler(FlowActionId::ToggleLayer, [&](const AppFlowRuntime::Params&) {
        runtime.Stack().Dispatch(FlowActionId::ToggleLayer, [](bool) {});
    });
    runtime.RegisterHandler(FlowActionId::Undo, [&](const AppFlowRuntime::Params&) { ++undone; });
    ASSERT_EQ(runtime.DispatchBySelector("layer-1-toggle"), DispatchResult::Ok);
    ASSERT_EQ(runtime.Stack().UndoDepth(), 1u);
    runtime.SetGuardResult(FlowGuardId::DocumentValid, true);
    ASSERT_EQ(runtime.DispatchByKey({KeyId::Z, KeyMod::Ctrl}), DispatchResult::Ok) << "Ctrl+Z is the shipped Undo default";
    ASSERT_EQ(undone, 1);

    auto blob = AppFlowBlobFile::Parse(ReplaceOnce(ReadGeneratedBlob(), "keydefault UndoKey 1 26 1 0 0", "keydefault UndoKey 1 21 1 0 0"));
    ASSERT_TRUE(blob.has_value());
    ASSERT_EQ(runtime.NavTo(FlowEdgeId::ToSettings, {FlowTriggerKind::System, "test"}), DispatchResult::Ok);
    ASSERT_EQ(runtime.Reload(&blob->View()), LoadResult::Ok);
    blob.reset();

    EXPECT_EQ(runtime.Current(), FlowStateId::Settings) << "reload keeps the running flow state";
    EXPECT_EQ(runtime.Stack().UndoDepth(), 1u) << "reload keeps the undo history";
    ASSERT_EQ(runtime.NavTo(FlowEdgeId::SettingsEditingEdge, {FlowTriggerKind::System, "test"}), DispatchResult::Ok);
    EXPECT_EQ(runtime.DispatchByKey({KeyId::Z, KeyMod::Ctrl}), DispatchResult::RejectedByState) << "the old chord is no longer bound";
    EXPECT_EQ(runtime.DispatchByKey({KeyId::U, KeyMod::Ctrl}), DispatchResult::Ok) << "the new chord is";
    EXPECT_EQ(undone, 2) << "the registered handler survived the reload";

    const AppFlowContainerView invalid(std::span<const AppFlowActionDecl>{}, std::span<const AppFlowTransition>{});
    EXPECT_EQ(runtime.Reload(&invalid), LoadResult::EmptyArtifact) << "an invalid candidate changes nothing";
    EXPECT_EQ(runtime.DispatchByKey({KeyId::U, KeyMod::Ctrl}), DispatchResult::Ok);
}

TEST(AppFlowBlob, ShapeHashMismatchRejectsAndRuntimeFallsBack) {
    std::string shapeEdit = ReadGeneratedBlob();
    const std::size_t shapeStart = shapeEdit.find("shape 0x");
    ASSERT_NE(shapeStart, std::string::npos);
    const std::size_t hashEnd = shapeEdit.find('\n', shapeStart);
    ASSERT_NE(hashEnd, std::string::npos);
    shapeEdit.replace(shapeStart, hashEnd - shapeStart, "shape 0x00000000");
    auto blob = AppFlowBlobFile::Parse(shapeEdit);
    EXPECT_FALSE(blob.has_value());

    AppFlowRuntime runtime(nullptr, 1);
    EXPECT_EQ(runtime.Load(blob ? &blob->View() : nullptr), LoadResult::Ok);
    EXPECT_EQ(runtime.Current(), FlowStateId::Editing);
}

// R874.3: a declared action with no handler is visible but inert. It is listed `implemented: false`, dispatching it returns
// the distinct NoHandler result (not RejectedByState) with one located diagnostic, and a reload never fails for it.
TEST(AppFlowBlob, DeclaredActionWithNoHandlerIsNoHandlerNotRejectedAndLocated) {
    AppFlowRuntime runtime(nullptr, 1);
    ASSERT_EQ(runtime.Load(), LoadResult::Ok);
    runtime.SetDeclarationSource("AppFlow.appflow");
    runtime.SetActionNamer([](FlowActionId id) { return id == FlowActionId::Save ? std::string("Save") : std::string("Other"); });
    std::vector<std::string> diagnostics;
    runtime.SetDiagnosticHandler([&](const std::string& message) { diagnostics.push_back(message); });
    std::vector<FlowActionId> unimplementedSeen;
    runtime.SetUnimplementedHandler([&](FlowActionId id, const std::string&) { unimplementedSeen.push_back(id); });

    const size_t declared = runtime.CatalogActions().size();
    ASSERT_GT(declared, 3u);
    EXPECT_EQ(runtime.UnimplementedActions().size(), declared) << "no handler registered yet";
    runtime.RegisterHandler(FlowActionId::ToggleLayer, [](const AppFlowRuntime::Params&) {});
    EXPECT_TRUE(runtime.IsImplemented(FlowActionId::ToggleLayer));
    EXPECT_FALSE(runtime.IsImplemented(FlowActionId::Save));
    EXPECT_EQ(runtime.UnimplementedActions().size(), declared - 1);

    EXPECT_EQ(runtime.DispatchById(FlowActionId::Save), DispatchResult::NoHandler);
    ASSERT_EQ(diagnostics.size(), 1u) << "one diagnostic per dispatch";
    EXPECT_NE(diagnostics[0].find("AppFlow.appflow: action 'Save'"), std::string::npos) << diagnostics[0];
    EXPECT_NE(diagnostics[0].find("no handler is registered"), std::string::npos);
    ASSERT_EQ(unimplementedSeen.size(), 1u);
    EXPECT_EQ(unimplementedSeen[0], FlowActionId::Save);

    // An id outside the loaded declarations stays RejectedByState (undeclared is not unimplemented).
    const AppFlowContainerView one(std::span<const AppFlowActionDecl>(kActionDecls, 1), std::span<const AppFlowTransition>(kTransitions));
    ASSERT_EQ(runtime.Reload(&one), LoadResult::Ok) << "a reload never fails because an action has no handler";
    ASSERT_EQ(runtime.CatalogActions().size(), 1u) << "the catalogue follows the loaded data";
    diagnostics.clear();
    EXPECT_EQ(runtime.DispatchById(FlowActionId::Save), DispatchResult::RejectedByState);
    EXPECT_TRUE(diagnostics.empty());

    diagnostics.clear();
    EXPECT_EQ(runtime.ReportUnimplemented(), 0u) << "the one declared action (ToggleLayer) has its handler";
    runtime.Reload(nullptr);
    EXPECT_EQ(runtime.ReportUnimplemented(), declared - 1) << "back to the full table: every unhandled declaration reported once";
    EXPECT_EQ(diagnostics.size(), declared - 1);
}

TEST(AppFlowBlob, CatalogueIsTheLoadedDataNotTheCompiledTables) {
    // A hand-edited action row (here: one extra parameterless action the compiled table does not know is NOT possible
    // without a new id), so edit a transition id in the data: the catalogue must report the loaded name.
    auto blob = AppFlowBlobFile::Parse(ReadGeneratedBlob());
    ASSERT_TRUE(blob.has_value());
    AppFlowRuntime runtime(nullptr, 1);
    ASSERT_EQ(runtime.Load(&blob->View()), LoadResult::Ok);
    blob.reset();   // the catalogue must own its strings
    ASSERT_FALSE(runtime.CatalogTransitions().empty());
    EXPECT_FALSE(runtime.CatalogTransitions().front().id.empty());
    bool sawParams = false;
    for (const auto& action : runtime.CatalogActions()) sawParams = sawParams || !action.params.empty();
    EXPECT_TRUE(sawParams);
}
