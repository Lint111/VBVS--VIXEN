#include <gtest/gtest.h>

#include "Core/TypedNodeInstance.h"
#include "Core/VariadicTypedNode.h"

using namespace Vixen::RenderGraph;

struct LifecycleProbeConfig {
    static constexpr size_t INPUT_COUNT = 0;
    static constexpr size_t OUTPUT_COUNT = 0;
};

// This models an external consumer: it derives from the public typed base and
// declares no using-declarations for NodeInstance's lifecycle overloads.
class StrictTypedNodeConsumer : public TypedNode<LifecycleProbeConfig> {
protected:
    void TypedSetupImpl(TypedSetupContext&) override {}
    void TypedCompileImpl(TypedCompileContext&) override {}
    void TypedExecuteImpl(TypedExecuteContext&) override {}
    void TypedCleanupImpl(TypedCleanupContext&) override {}
};

class StrictVariadicNodeConsumer : public VariadicTypedNode<LifecycleProbeConfig> {
protected:
    void VariadicSetupImpl(VariadicSetupContext&) override {}
    void VariadicCompileImpl(VariadicCompileContext&) override {}
    void VariadicExecuteImpl(VariadicExecuteContext&) override {}
    void VariadicCleanupImpl(VariadicCleanupContext&) override {}
};

static_assert(sizeof(StrictTypedNodeConsumer) > 0);
static_assert(sizeof(StrictVariadicNodeConsumer) > 0);

TEST(TypedNodeLifecycle, StrictConsumerCallbacksCompileWithoutOverloadHiding) {
    SUCCEED();
}
