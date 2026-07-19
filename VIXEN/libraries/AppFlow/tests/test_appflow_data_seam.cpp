// Seam M2c: the agnostic handshake, the pieces test_appflow_loader.cpp's
// DataActionDrivesProviderMask / DataLegInertWithoutProvider don't already cover.
//   1. the loader populates the DataTargetTable from the generated dataTargets() (the raw mapping);
//   2. the seam is NOUN-AGNOSTIC: the same DispatchData/ReadData path services an undertow HUD
//      noun (UndertowHud_bodyCount) through a test-double provider fed HUD-shaped data -- proving
//      the mechanics don't model the editor's LayerMask specifically (the live host provider for
//      that noun is a follow-on; the seam already accepts it);
//   3. a provider that REJECTS the wired noun is fallible (read false, write a dropped no-op),
//      never a crash.
// The editor-noun end-to-end route (LayerControllerViewDataProvider) + the no-provider inert case
// live in test_appflow_loader.cpp (DataActionDrivesProviderMask / DataLegInertWithoutProvider).
#include <gtest/gtest.h>
#include "AppFlowRuntime.h"
#include "AppFlowLoader.h"
#include "LayerControllerViewDataProvider.h"
using namespace Vixen::AppFlow;
using namespace Vixen::AppFlow::Generated;

// A generic single-noun test-double: stands in for a live host-backed provider (e.g. one fed by
// the C++ read-model's HUD sections). The seam is noun-agnostic, so the same path serves it.
namespace {
class SingleNounProvider final : public IViewDataProvider {
public:
    explicit SingleNounProvider(ViewNounId noun, uint32_t value = 0) : noun_(noun), value_(value) {}
    bool ReadU32(ViewNounKey key, uint32_t& out) const override {
        if (key.noun != noun_) return false;
        out = value_;
        return true;
    }
    void WriteU32(ViewNounKey key, uint32_t value) override {
        if (key.noun == noun_) value_ = value;
    }
    uint32_t Value() const { return value_; }
private:
    ViewNounId noun_;
    uint32_t value_;
};
}  // namespace

// The loader fills the DataTargetTable from the generated dataTargets(); the sole live entry
// (Data -> EditorNouns_layerMask) lands under FlowActionId::Data. The nullptr default skips it.
TEST(AppFlowDataSeam, LoaderPopulatesDataTargets) {
    FlowStateMachine fsm; ActionStack st; BindingStore bindings; InputProfile input;
    DataTargetTable targets;
    ASSERT_EQ(AppFlowLoader::Load(AppFlowContainerView{}, fsm, st, bindings, input, &targets),
              LoadResult::Ok);
    auto it = targets.find(static_cast<uint16_t>(FlowActionId::Data));
    ASSERT_NE(it, targets.end());
    EXPECT_EQ(it->second, ViewNounId::EditorNouns_layerMask);

    // Back-compat: the nullptr default leaves no data leg (and every pre-M2c caller unaffected).
    FlowStateMachine fsm2; ActionStack st2; BindingStore b2; InputProfile in2;
    EXPECT_EQ(AppFlowLoader::Load(AppFlowContainerView{}, fsm2, st2, b2, in2), LoadResult::Ok);
}

// Noun-agnosticism: rebind the Data action onto an undertow HUD noun and drive it through a
// test-double provider fed HUD-shaped data. No editor concept is involved -- the identical
// DispatchData/ReadData path services the undertow face. (The generated target is the editor's
// layerMask; wiring undertow's OWN Data action live is the AppFlow-migration follow-on the spec
// defers -- this proves the seam already accepts the undertow noun.)
TEST(AppFlowDataSeam, UndertowHudNounRoundTripsThroughProvider) {
    AppFlowRuntime rt(nullptr, /*sender*/1);
    SingleNounProvider provider(ViewNounId::UndertowHud_bodyCount, /*value*/7);
    ASSERT_EQ(rt.Load(&provider), LoadResult::Ok);
    rt.BindDataTarget(FlowActionId::Data, ViewNounId::UndertowHud_bodyCount);

    uint32_t bodies = 0;
    ASSERT_TRUE(rt.ReadData(FlowActionId::Data, bodies));
    EXPECT_EQ(bodies, 7u);                            // read the seeded HUD-shaped value

    EXPECT_EQ(rt.DispatchData(FlowActionId::Data, 12u), DispatchResult::Ok);
    EXPECT_EQ(provider.Value(), 12u);                 // written through the provider
}

// Fallible when the provider rejects the wired noun: a LayerController provider given the Data
// action still bound to the undertow bodyCount noun reports absent on read, and the write is a
// dropped no-op -- never a crash, never a wrong-mask corruption.
TEST(AppFlowDataSeam, ProviderRejectsMismatchedNounFallibly) {
    AppFlowRuntime rt(nullptr, /*sender*/1);
    LayerController layers; layers.SetLayerCount(4);   // mask 0b1111
    LayerControllerViewDataProvider provider(layers);  // serves EditorNouns_layerMask only
    ASSERT_EQ(rt.Load(&provider), LoadResult::Ok);
    rt.BindDataTarget(FlowActionId::Data, ViewNounId::UndertowHud_bodyCount);  // mismatched noun

    uint32_t v = 123;
    EXPECT_FALSE(rt.ReadData(FlowActionId::Data, v));  // provider reports the noun absent
    const uint32_t before = layers.Mask();
    // WriteU32 has no failure channel, so DispatchData returns Ok; the provider drops the
    // mismatched noun, leaving the real mask untouched.
    EXPECT_EQ(rt.DispatchData(FlowActionId::Data, 5u), DispatchResult::Ok);
    EXPECT_EQ(layers.Mask(), before);
}
