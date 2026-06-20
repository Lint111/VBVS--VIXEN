// test_accumulation_gather.cpp — typed runtime accumulation-gather (fan-in).
//
// Exercises the InAll<T> mechanism end-to-end with a minimal CPU-only graph:
//   * 3 producer nodes, each outputting a distinct int32_t (10, 20, 30).
//   * 1 consumer node with a single accumulation input slot (element type int32_t).
//   * The producers are wired to the consumer's accumulation slot via the real
//     AccumulationConnectionRule (the accumulation-connect path). The rule records
//     each producer on the consumer (NodeInstance::RegisterAccumulationSource) and
//     adds an ordering dependency edge.
//   * We Execute() the producers (populating their per-frame outputs) then the
//     consumer. The consumer's ExecuteImpl reads ctx.InAll(INPUTS) and the gather
//     assembles std::vector<int32_t> from all 3 producers' current outputs.
//
// No GPU/device required: this is pure RenderGraph CPU logic. Node lifecycle is
// driven directly (NodeInstance::Execute()), mirroring test_multiple_producers.cpp
// and the rule usage in test_connection_rule.cpp — neither builds a full graph via
// RenderGraph::Compile()/Execute() (which would pull in a device/cacher).

#include <gtest/gtest.h>

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Connection/Rules/AccumulationConnectionRule.h"
#include "Connection/ConnectionTypes.h"
#include "Data/Core/ResourceConfig.h"
#include "Data/Core/SlotInfo.h"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Vixen::RenderGraph;

namespace {

// ---------------------------------------------------------------------------
// Producer: one int32_t output. Its ExecuteImpl writes a configurable value.
// ---------------------------------------------------------------------------
namespace ValueProducerCounts {
    static constexpr size_t INPUTS = 0;
    static constexpr size_t OUTPUTS = 1;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(ValueProducerConfig,
                      ValueProducerCounts::INPUTS,
                      ValueProducerCounts::OUTPUTS,
                      ValueProducerCounts::ARRAY_MODE) {
    OUTPUT_SLOT(VALUE, std::int32_t, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    ValueProducerConfig() {
        HandleDescriptor desc{"int32"};
        INIT_OUTPUT_DESC(VALUE, "value", ResourceLifetime::Transient, desc);
    }

    static_assert(VALUE_Slot::index == 0, "VALUE must be at index 0");
    static_assert(std::is_same_v<VALUE_Slot::Type, std::int32_t>);
};

class ValueProducerNode : public TypedNode<ValueProducerConfig> {
public:
    ValueProducerNode(const std::string& instanceName, NodeType* nodeType)
        : TypedNode<ValueProducerConfig>(instanceName, nodeType) {}

    void SetValue(std::int32_t v) { value_ = v; }

protected:
    void ExecuteImpl(TypedExecuteContext& ctx) override {
        ctx.Out(ValueProducerConfig::VALUE, value_);
    }

private:
    std::int32_t value_ = 0;
};

class ValueProducerNodeType : public TypedNodeType<ValueProducerConfig> {
public:
    ValueProducerNodeType() : TypedNodeType<ValueProducerConfig>("ValueProducer") {}

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<ValueProducerNode>(
            instanceName, const_cast<ValueProducerNodeType*>(this));
    }
};

// ---------------------------------------------------------------------------
// Consumer: one accumulation input slot (element type int32_t). Its ExecuteImpl
// gathers std::vector<int32_t> from all connected producers and stores it.
// ---------------------------------------------------------------------------
namespace GatherConsumerCounts {
    static constexpr size_t INPUTS = 1;
    static constexpr size_t OUTPUTS = 0;
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

CONSTEXPR_NODE_CONFIG(GatherConsumerConfig,
                      GatherConsumerCounts::INPUTS,
                      GatherConsumerCounts::OUTPUTS,
                      GatherConsumerCounts::ARRAY_MODE) {
    // Accumulation input slot: gathers int32_t elements into std::vector<int32_t>.
    ACCUMULATION_INPUT_SLOT_V2(VALUES, std::vector<std::int32_t>, std::int32_t, 0,
        SlotNullability::Required,
        SlotStorageStrategy::Value);

    GatherConsumerConfig() {
        HandleDescriptor desc{"std::vector<int32>"};
        INIT_INPUT_DESC(VALUES, "values", ResourceLifetime::Transient, desc);
    }

    static_assert(VALUES_Slot::index == 0, "VALUES must be at index 0");
    static_assert(VALUES_Slot::isAccumulation, "VALUES must be an accumulation slot");
    static_assert(std::is_same_v<VALUES_Slot::Type, std::vector<std::int32_t>>);
};

class GatherConsumerNode : public TypedNode<GatherConsumerConfig> {
public:
    GatherConsumerNode(const std::string& instanceName, NodeType* nodeType)
        : TypedNode<GatherConsumerConfig>(instanceName, nodeType) {}

    // Last gathered vector (populated by ExecuteImpl). Read by the test.
    std::vector<std::int32_t> gathered;

protected:
    void ExecuteImpl(TypedExecuteContext& ctx) override {
        gathered = ctx.InAll(GatherConsumerConfig::VALUES);
    }
};

class GatherConsumerNodeType : public TypedNodeType<GatherConsumerConfig> {
public:
    GatherConsumerNodeType() : TypedNodeType<GatherConsumerConfig>("GatherConsumer") {}

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<GatherConsumerNode>(
            instanceName, const_cast<GatherConsumerNodeType*>(this));
    }
};

// Wire producer.VALUE -> consumer.VALUES through the real accumulation rule.
void ConnectAccumulation(const AccumulationConnectionRule& rule,
                         NodeInstance* producer,
                         NodeInstance* consumer,
                         int32_t sortKey = 0) {
    ConnectionContext ctx;
    ctx.sourceNode = producer;
    ctx.targetNode = consumer;
    ctx.sourceSlot = SlotInfo::FromOutputSlot<ValueProducerConfig::VALUE_Slot>("VALUE");
    ctx.sourceSlot.index = ValueProducerConfig::VALUE_Slot::index;
    ctx.targetSlot = SlotInfo::FromInputSlot<GatherConsumerConfig::VALUES_Slot>("VALUES");
    ctx.targetSlot.index = GatherConsumerConfig::VALUES_Slot::index;
    ctx.sortKey = sortKey;

    auto validate = rule.Validate(ctx);
    ASSERT_TRUE(validate.success) << "Validate failed: " << validate.errorMessage;

    auto result = rule.Resolve(ctx);
    ASSERT_TRUE(result.success) << "Resolve failed: " << result.errorMessage;
}

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Three producers -> one accumulation slot. The consumer reads a std::vector<int32_t>
// containing all three produced values.
TEST(RenderGraph_AccumulationGather, GathersAllProducers) {
    ValueProducerNodeType producerType;
    GatherConsumerNodeType consumerType;

    auto p1 = producerType.CreateInstance("p1");
    auto p2 = producerType.CreateInstance("p2");
    auto p3 = producerType.CreateInstance("p3");
    auto consumer = consumerType.CreateInstance("consumer");

    static_cast<ValueProducerNode*>(p1.get())->SetValue(10);
    static_cast<ValueProducerNode*>(p2.get())->SetValue(20);
    static_cast<ValueProducerNode*>(p3.get())->SetValue(30);

    AccumulationConnectionRule rule;
    // Connection order p1, p2, p3 (sortKey 0 => stable connection order in the gather).
    ConnectAccumulation(rule, p1.get(), consumer.get());
    ConnectAccumulation(rule, p2.get(), consumer.get());
    ConnectAccumulation(rule, p3.get(), consumer.get());

    // The rule records the producers and adds ordering dependencies.
    EXPECT_EQ(consumer->GetDependencies().size(), 3u);
    const auto* sources = consumer->GetAccumulationSources(
        GatherConsumerConfig::VALUES_Slot::index);
    ASSERT_NE(sources, nullptr);
    EXPECT_EQ(sources->size(), 3u);

    // Producers run first (their outputs are populated), then the consumer gathers.
    p1->Execute();
    p2->Execute();
    p3->Execute();
    consumer->Execute();

    auto* cons = static_cast<GatherConsumerNode*>(consumer.get());
    ASSERT_EQ(cons->gathered.size(), 3u);
    EXPECT_EQ(cons->gathered[0], 10);
    EXPECT_EQ(cons->gathered[1], 20);
    EXPECT_EQ(cons->gathered[2], 30);
}

// An unconnected accumulation slot gathers an empty vector (not a crash).
TEST(RenderGraph_AccumulationGather, EmptyWhenUnconnected) {
    GatherConsumerNodeType consumerType;
    auto consumer = consumerType.CreateInstance("consumer");

    EXPECT_EQ(consumer->GetAccumulationSources(
        GatherConsumerConfig::VALUES_Slot::index), nullptr);

    consumer->Execute();

    auto* cons = static_cast<GatherConsumerNode*>(consumer.get());
    EXPECT_TRUE(cons->gathered.empty());
}

// The sort key orders gathered elements (ByMetadata); insertion order breaks ties.
TEST(RenderGraph_AccumulationGather, SortKeyOrdersGather) {
    ValueProducerNodeType producerType;
    GatherConsumerNodeType consumerType;

    auto p1 = producerType.CreateInstance("p1");
    auto p2 = producerType.CreateInstance("p2");
    auto p3 = producerType.CreateInstance("p3");
    auto consumer = consumerType.CreateInstance("consumer");

    static_cast<ValueProducerNode*>(p1.get())->SetValue(10);
    static_cast<ValueProducerNode*>(p2.get())->SetValue(20);
    static_cast<ValueProducerNode*>(p3.get())->SetValue(30);

    AccumulationConnectionRule rule;
    // Connect in order p1,p2,p3 but assign sort keys that reverse them: 3,2,1.
    ConnectAccumulation(rule, p1.get(), consumer.get(), /*sortKey=*/3);
    ConnectAccumulation(rule, p2.get(), consumer.get(), /*sortKey=*/2);
    ConnectAccumulation(rule, p3.get(), consumer.get(), /*sortKey=*/1);

    p1->Execute();
    p2->Execute();
    p3->Execute();
    consumer->Execute();

    auto* cons = static_cast<GatherConsumerNode*>(consumer.get());
    ASSERT_EQ(cons->gathered.size(), 3u);
    // Gathered by ascending sort key: p3(30), p2(20), p1(10).
    EXPECT_EQ(cons->gathered[0], 30);
    EXPECT_EQ(cons->gathered[1], 20);
    EXPECT_EQ(cons->gathered[2], 10);
}
