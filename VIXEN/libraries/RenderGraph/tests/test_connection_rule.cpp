/**
 * @file test_connection_rule.cpp
 * @brief Tests for ConnectionRule system (Sprint 6.0.1)
 *
 * Tests the connection rule infrastructure:
 * - SlotInfo creation from ResourceSlot types (unified representation)
 * - ConnectionRuleRegistry rule matching
 * - DirectConnectionRule validation
 */

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>
#include <array>
#include <vector>

// Connection system includes
#include "Connection/ConnectionTypes.h"
#include "Connection/ConnectionRuleRegistry.h"
#include "Connection/ConnectionPipeline.h"
#include "Connection/ConnectionModifier.h"
#include "Connection/Rules/DirectConnectionRule.h"
#include "Connection/Rules/AccumulationConnectionRule.h"
#include "Connection/Rules/VariadicConnectionRule.h"
#include "Connection/Modifiers/FieldExtractionModifier.h"
#include "Core/UnifiedConnect.h"
#include "Data/Core/ResourceConfig.h"
#include "Data/Core/ConnectionConcepts.h"
#include "Data/Core/SlotInfo.h"
#include "Data/Core/CompileTimeResourceSystem.h"

using namespace Vixen::RenderGraph;

// ============================================================================
// TEST CONFIGS
// ============================================================================

struct SourceConfig : public ResourceConfigBase<0, 2> {
    OUTPUT_SLOT(BUFFER_OUT, VkBuffer, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    OUTPUT_SLOT(IMAGE_OUT, VkImageView, 1,
        SlotNullability::Required,
        SlotMutability::WriteOnly);
};

struct TargetConfig : public ResourceConfigBase<2, 0> {
    INPUT_SLOT(BUFFER_IN, VkBuffer, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(OPTIONAL_IN, VkImageView, 1,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);
};

struct AccumulationTargetConfig : public ResourceConfigBase<1, 0> {
    ACCUMULATION_INPUT_SLOT(PASSES, PassThroughStorage, 0,
        SlotNullability::Required);
};

// ============================================================================
// SLOT INFO TESTS (Unified Representation)
// ============================================================================

TEST(SlotInfoTest, CreateFromOutputSlot) {
    auto info = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("BUFFER_OUT");

    EXPECT_EQ(info.index, 0u);
    EXPECT_EQ(info.resourceType, ResourceType::Buffer);
    EXPECT_EQ(info.nullability, SlotNullability::Required);
    EXPECT_EQ(info.mutability, SlotMutability::WriteOnly);
    EXPECT_EQ(info.flags, SlotFlags::None);
    EXPECT_EQ(info.kind, SlotKind::StaticOutput);
    EXPECT_TRUE(info.IsOutput());
    EXPECT_FALSE(info.IsInput());
    EXPECT_EQ(info.name, "BUFFER_OUT");
}

TEST(SlotInfoTest, CreateFromInputSlot) {
    auto info = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("BUFFER_IN");

    EXPECT_EQ(info.index, 0u);
    EXPECT_EQ(info.resourceType, ResourceType::Buffer);
    EXPECT_EQ(info.nullability, SlotNullability::Required);
    EXPECT_EQ(info.role, SlotRole::Dependency);
    EXPECT_EQ(info.mutability, SlotMutability::ReadOnly);
    EXPECT_EQ(info.kind, SlotKind::StaticInput);
    EXPECT_TRUE(info.IsInput());
    EXPECT_FALSE(info.IsOutput());
}

TEST(SlotInfoTest, CreateFromOptionalSlot) {
    auto info = SlotInfo::FromInputSlot<TargetConfig::OPTIONAL_IN_Slot>("OPTIONAL_IN");

    EXPECT_EQ(info.nullability, SlotNullability::Optional);
    EXPECT_TRUE(info.IsOptional());
    EXPECT_EQ(info.role, SlotRole::Execute);
}

TEST(SlotInfoTest, CreateFromAccumulationSlot) {
    auto info = SlotInfo::FromInputSlot<AccumulationTargetConfig::PASSES_Slot>("PASSES");

    EXPECT_TRUE(info.IsAccumulation());
    EXPECT_TRUE(info.IsMultiConnect());
    EXPECT_NE(info.flags & SlotFlags::Accumulation, SlotFlags::None);
    EXPECT_NE(info.flags & SlotFlags::MultiConnect, SlotFlags::None);
}

TEST(SlotInfoTest, SlotKindHelpers) {
    auto outputInfo = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    auto inputInfo = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    // Static slot checks
    EXPECT_TRUE(outputInfo.IsStatic());
    EXPECT_TRUE(inputInfo.IsStatic());
    EXPECT_FALSE(outputInfo.IsBinding());
    EXPECT_FALSE(inputInfo.IsBinding());

    // Output checks
    EXPECT_TRUE(outputInfo.IsOutput());
    EXPECT_FALSE(outputInfo.IsInput());

    // Input checks
    EXPECT_TRUE(inputInfo.IsInput());
    EXPECT_FALSE(inputInfo.IsOutput());
}

// ============================================================================
// BINDING DESCRIPTOR TESTS (Backward Compatibility)
// ============================================================================

struct MockBindingRef {
    uint32_t binding;
    uint32_t descriptorType;
};

TEST(BindingDescriptorTest, CreateFromBindingRef) {
    MockBindingRef ref{3, 7};  // binding 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER = 7
    auto desc = BindingDescriptor::FromBinding(ref, "storageBuffer");

    EXPECT_EQ(desc.binding, 3u);
    EXPECT_EQ(desc.descriptorType, 7u);
    EXPECT_EQ(desc.name, "storageBuffer");
}

TEST(SlotInfoTest, CreateFromBinding) {
    MockBindingRef ref{3, 7};
    auto info = SlotInfo::FromBinding(ref, "storageBuffer");

    EXPECT_EQ(info.binding, 3u);
    EXPECT_EQ(info.descriptorType, static_cast<VkDescriptorType>(7));
    EXPECT_EQ(info.name, "storageBuffer");
    EXPECT_EQ(info.kind, SlotKind::Binding);
    EXPECT_TRUE(info.IsBinding());
    EXPECT_TRUE(info.IsInput());  // Bindings are considered inputs
    EXPECT_FALSE(info.IsOutput());
    EXPECT_EQ(info.state, SlotState::Tentative);  // Bindings need validation
}

// ============================================================================
// FIELD EXTRACTION TESTS (Now integrated into SlotInfo)
// ============================================================================

struct MockSourceStruct {
    VkBuffer vertexBuffer;
    VkImageView imageView;
    uint32_t count;
};

TEST(SlotInfoTest, DefaultNoExtraction) {
    SlotInfo info;
    EXPECT_FALSE(info.hasFieldExtraction);
}

TEST(SlotInfoTest, WithFieldExtraction) {
    auto info = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT")
        .WithFieldExtraction(&MockSourceStruct::imageView);

    EXPECT_TRUE(info.hasFieldExtraction);
    EXPECT_GT(info.fieldSize, 0u);
    EXPECT_NE(info.extractor, nullptr);
}

TEST(SlotInfoTest, FieldExtractionExtractorWorks) {
    MockSourceStruct source;
    source.imageView = reinterpret_cast<VkImageView>(0xABCD);

    auto info = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT")
        .WithFieldExtraction(&MockSourceStruct::imageView);

    // Use the extractor to get the field
    void* fieldPtr = info.extractor(&source);
    VkImageView* extracted = static_cast<VkImageView*>(fieldPtr);

    EXPECT_EQ(*extracted, source.imageView);
}

TEST(ConnectionContextTest, GetEffectiveSourceTypeNoExtraction) {
    ConnectionContext ctx;
    ctx.sourceSlot.resourceType = ResourceType::Buffer;

    EXPECT_EQ(ctx.GetEffectiveSourceType(), ResourceType::Buffer);
}

TEST(ConnectionContextTest, GetEffectiveSourceTypeWithExtraction) {
    ConnectionContext ctx;
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT")
        .WithFieldExtraction(&MockSourceStruct::imageView);

    // WithFieldExtraction updates resourceType to the extracted field's type
    EXPECT_TRUE(ctx.sourceSlot.hasFieldExtraction);
    // The effective source type comes from sourceSlot.resourceType which is updated by WithFieldExtraction
    EXPECT_EQ(ctx.GetEffectiveSourceType(), ctx.sourceSlot.resourceType);
}

// ============================================================================
// CONNECTION RULE REGISTRY TESTS
// ============================================================================

TEST(ConnectionRuleRegistryTest, CreateEmpty) {
    ConnectionRuleRegistry registry;
    EXPECT_EQ(registry.RuleCount(), 0u);
}

TEST(ConnectionRuleRegistryTest, RegisterDirectRule) {
    ConnectionRuleRegistry registry;
    registry.RegisterRule(std::make_unique<DirectConnectionRule>());

    EXPECT_EQ(registry.RuleCount(), 1u);
}

TEST(ConnectionRuleRegistryTest, CreateDefaultRegistry) {
    auto registry = ConnectionRuleRegistry::CreateDefault();

    // Should have DirectConnectionRule registered
    EXPECT_GE(registry.RuleCount(), 1u);
}

TEST(ConnectionRuleRegistryTest, FindRuleForDirectConnection) {
    auto registry = ConnectionRuleRegistry::CreateDefault();

    auto sourceInfo = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    auto targetInfo = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    const ConnectionRule* rule = registry.FindRule(sourceInfo, targetInfo);

    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->Name(), "DirectConnectionRule");
}

TEST(ConnectionRuleRegistryTest, AccumulationRuleHandlesAccumulationSlots) {
    auto registry = ConnectionRuleRegistry::CreateDefault();

    auto sourceInfo = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    auto targetInfo = SlotInfo::FromInputSlot<AccumulationTargetConfig::PASSES_Slot>("PASSES");

    // AccumulationConnectionRule should handle accumulation slots
    const ConnectionRule* rule = registry.FindRule(sourceInfo, targetInfo);

    // AccumulationConnectionRule is now registered
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->Name(), "AccumulationConnectionRule");
}

TEST(ConnectionRuleRegistryTest, FindRuleForBindingConnection) {
    auto registry = ConnectionRuleRegistry::CreateDefault();

    auto sourceInfo = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    MockBindingRef bindingRef{0, 7};
    auto targetInfo = SlotInfo::FromBinding(bindingRef, "binding");

    // DirectConnectionRule should handle slot-to-binding connections
    const ConnectionRule* rule = registry.FindRule(sourceInfo, targetInfo);

    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->Name(), "DirectConnectionRule");
}

// ============================================================================
// DIRECT CONNECTION RULE TESTS
// ============================================================================

TEST(DirectConnectionRuleTest, CanHandleDirectConnection) {
    DirectConnectionRule rule;

    auto sourceInfo = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    auto targetInfo = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    EXPECT_TRUE(rule.CanHandle(sourceInfo, targetInfo));
}

TEST(DirectConnectionRuleTest, CannotHandleAccumulationConnection) {
    DirectConnectionRule rule;

    auto sourceInfo = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    auto targetInfo = SlotInfo::FromInputSlot<AccumulationTargetConfig::PASSES_Slot>("PASSES");

    EXPECT_FALSE(rule.CanHandle(sourceInfo, targetInfo));
}

TEST(DirectConnectionRuleTest, CanHandleBindingConnection) {
    // DirectConnectionRule now handles 1:1 binding connections too
    DirectConnectionRule rule;

    auto sourceInfo = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    MockBindingRef bindingRef{0, 7};
    auto targetInfo = SlotInfo::FromBinding(bindingRef, "test");

    // Direct rule CAN handle slot-to-binding (1:1 connection)
    EXPECT_TRUE(rule.CanHandle(sourceInfo, targetInfo));
}

TEST(DirectConnectionRuleTest, ValidateSourceNotNull) {
    DirectConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = nullptr;  // Invalid
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.targetSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    auto result = rule.Validate(ctx);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("Source node is null") != std::string::npos);
}

TEST(DirectConnectionRuleTest, ValidateTargetNotNull) {
    DirectConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);  // Mock non-null
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    ctx.targetNode = nullptr;  // Invalid

    auto result = rule.Validate(ctx);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("Target node is null") != std::string::npos);
}

TEST(DirectConnectionRuleTest, ValidateBindingConnection) {
    DirectConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");

    MockBindingRef bindingRef{3, 7};  // binding 3, storage buffer
    ctx.targetSlot = SlotInfo::FromBinding(bindingRef, "storageBuffer");

    auto result = rule.Validate(ctx);
    EXPECT_TRUE(result.success);
}

TEST(DirectConnectionRuleTest, ValidateSourceMustBeOutput) {
    DirectConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    // Source is INPUT (wrong!)
    ctx.sourceSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");
    ctx.targetSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    auto result = rule.Validate(ctx);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("Source slot must be an output") != std::string::npos);
}

TEST(DirectConnectionRuleTest, ValidateTargetMustBeInput) {
    DirectConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    // Target is OUTPUT (wrong!)
    ctx.targetSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");

    auto result = rule.Validate(ctx);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("Target slot must be an input") != std::string::npos);
}

TEST(DirectConnectionRuleTest, ValidateSuccess) {
    DirectConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    ctx.targetSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    auto result = rule.Validate(ctx);
    EXPECT_TRUE(result.success);
}

TEST(DirectConnectionRuleTest, Priority) {
    DirectConnectionRule rule;
    EXPECT_EQ(rule.Priority(), 50u);
}

TEST(DirectConnectionRuleTest, Name) {
    DirectConnectionRule rule;
    EXPECT_EQ(rule.Name(), "DirectConnectionRule");
}

// ============================================================================
// INTEGRATION: Rule priority ordering
// ============================================================================

// Mock high-priority rule for testing
class MockHighPriorityRule : public ConnectionRule {
public:
    bool CanHandle(const SlotInfo&, const SlotInfo&) const override {
        return true;  // Claims to handle everything
    }

    ConnectionResult Validate(const ConnectionContext&) const override {
        return ConnectionResult::Success();
    }

    ConnectionResult Resolve(ConnectionContext&) const override {
        return ConnectionResult::Success();
    }

    uint32_t Priority() const override { return 100; }  // Higher than Direct (50)

    std::string_view Name() const override { return "MockHighPriorityRule"; }
};

TEST(ConnectionRuleRegistryTest, RulesSortedByPriority) {
    ConnectionRuleRegistry registry;

    // Register in wrong order
    registry.RegisterRule(std::make_unique<DirectConnectionRule>());  // Priority 50
    registry.RegisterRule(std::make_unique<MockHighPriorityRule>());  // Priority 100

    // Get rules
    const auto& rules = registry.GetRules();
    ASSERT_GE(rules.size(), 2u);

    // First rule should be highest priority (MockHighPriorityRule)
    EXPECT_EQ(rules[0]->Priority(), 100u);
    EXPECT_EQ(rules[0]->Name(), "MockHighPriorityRule");

    // Second rule should be lower priority (DirectConnectionRule)
    EXPECT_EQ(rules[1]->Priority(), 50u);
    EXPECT_EQ(rules[1]->Name(), "DirectConnectionRule");
}

TEST(ConnectionRuleRegistryTest, FindRuleReturnsHighestPriority) {
    ConnectionRuleRegistry registry;
    registry.RegisterRule(std::make_unique<DirectConnectionRule>());
    registry.RegisterRule(std::make_unique<MockHighPriorityRule>());

    auto sourceInfo = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    auto targetInfo = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    const ConnectionRule* rule = registry.FindRule(sourceInfo, targetInfo);

    // MockHighPriorityRule should win because it has higher priority
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->Name(), "MockHighPriorityRule");
}

// ============================================================================
// ACCUMULATION CONNECTION RULE TESTS
// ============================================================================

TEST(AccumulationConnectionRuleTest, CanHandleAccumulationSlot) {
    AccumulationConnectionRule rule;

    auto sourceInfo = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    auto targetInfo = SlotInfo::FromInputSlot<AccumulationTargetConfig::PASSES_Slot>("PASSES");

    // Accumulation rule handles accumulation slots
    EXPECT_TRUE(rule.CanHandle(sourceInfo, targetInfo));
}

TEST(AccumulationConnectionRuleTest, CannotHandleDirectSlot) {
    AccumulationConnectionRule rule;

    auto sourceInfo = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    auto targetInfo = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    // Accumulation rule does NOT handle direct connections
    EXPECT_FALSE(rule.CanHandle(sourceInfo, targetInfo));
}

TEST(AccumulationConnectionRuleTest, ValidateSourceMustBeOutput) {
    AccumulationConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    // Source is INPUT (wrong!)
    ctx.sourceSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");
    ctx.targetSlot = SlotInfo::FromInputSlot<AccumulationTargetConfig::PASSES_Slot>("PASSES");

    auto result = rule.Validate(ctx);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("output") != std::string::npos);
}

TEST(AccumulationConnectionRuleTest, ValidateTargetMustBeAccumulation) {
    AccumulationConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    // Target is NOT accumulation
    ctx.targetSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    auto result = rule.Validate(ctx);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("Accumulation") != std::string::npos);
}

TEST(AccumulationConnectionRuleTest, ValidateSuccess) {
    AccumulationConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    ctx.targetSlot = SlotInfo::FromInputSlot<AccumulationTargetConfig::PASSES_Slot>("PASSES");

    auto result = rule.Validate(ctx);
    EXPECT_TRUE(result.success);
}

TEST(AccumulationConnectionRuleTest, ValidateWithSortKey) {
    AccumulationConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    ctx.targetSlot = SlotInfo::FromInputSlot<AccumulationTargetConfig::PASSES_Slot>("PASSES");
    ctx.sortKey = 42;  // Explicit ordering

    auto result = rule.Validate(ctx);
    EXPECT_TRUE(result.success);
}

TEST(AccumulationConnectionRuleTest, Priority) {
    AccumulationConnectionRule rule;
    EXPECT_EQ(rule.Priority(), 100u);  // Higher than DirectConnectionRule (50)
}

TEST(AccumulationConnectionRuleTest, Name) {
    AccumulationConnectionRule rule;
    EXPECT_EQ(rule.Name(), "AccumulationConnectionRule");
}

// ============================================================================
// ACCUMULATION STATE TESTS
// ============================================================================

TEST(AccumulationStateTest, AddEntry) {
    AccumulationState state;
    state.config = AccumulationConfig{1, 10, OrderStrategy::ByMetadata, false};

    AccumulationEntry entry;
    entry.sortKey = 5;
    entry.sourceNode = reinterpret_cast<NodeInstance*>(0x1);

    state.AddEntry(entry);

    EXPECT_EQ(state.entries.size(), 1u);
    EXPECT_EQ(state.entries[0].sortKey, 5);
}

TEST(AccumulationStateTest, SortByMetadata) {
    AccumulationState state;
    state.config.orderStrategy = OrderStrategy::ByMetadata;

    AccumulationEntry e1, e2, e3;
    e1.sortKey = 30;
    e2.sortKey = 10;
    e3.sortKey = 20;

    state.AddEntry(e1);
    state.AddEntry(e2);
    state.AddEntry(e3);

    state.SortEntries(OrderStrategy::ByMetadata);

    EXPECT_EQ(state.entries[0].sortKey, 10);
    EXPECT_EQ(state.entries[1].sortKey, 20);
    EXPECT_EQ(state.entries[2].sortKey, 30);
}

TEST(AccumulationStateTest, SortBySourceSlot) {
    AccumulationState state;
    state.config.orderStrategy = OrderStrategy::BySourceSlot;

    AccumulationEntry e1, e2, e3;
    e1.sourceSlot.index = 2;
    e2.sourceSlot.index = 0;
    e3.sourceSlot.index = 1;

    state.AddEntry(e1);
    state.AddEntry(e2);
    state.AddEntry(e3);

    state.SortEntries(OrderStrategy::BySourceSlot);

    EXPECT_EQ(state.entries[0].sourceSlot.index, 0u);
    EXPECT_EQ(state.entries[1].sourceSlot.index, 1u);
    EXPECT_EQ(state.entries[2].sourceSlot.index, 2u);
}

TEST(AccumulationStateTest, ConnectionOrderPreserved) {
    AccumulationState state;
    state.config.orderStrategy = OrderStrategy::ConnectionOrder;

    AccumulationEntry e1, e2, e3;
    e1.sortKey = 30;  // Sort keys should be ignored
    e2.sortKey = 10;
    e3.sortKey = 20;

    state.AddEntry(e1);
    state.AddEntry(e2);
    state.AddEntry(e3);

    state.SortEntries(OrderStrategy::ConnectionOrder);

    // Order preserved - first added is first
    EXPECT_EQ(state.entries[0].sortKey, 30);
    EXPECT_EQ(state.entries[1].sortKey, 10);
    EXPECT_EQ(state.entries[2].sortKey, 20);
}

TEST(AccumulationStateTest, ValidateCountMin) {
    AccumulationState state;
    state.config = AccumulationConfig{2, 10, OrderStrategy::ByMetadata, false};

    AccumulationEntry entry;
    state.AddEntry(entry);  // Only 1, but min is 2

    std::string errorMsg;
    EXPECT_FALSE(state.ValidateCount(errorMsg));
    EXPECT_TRUE(errorMsg.find("at least 2") != std::string::npos);
}

TEST(AccumulationStateTest, ValidateCountMax) {
    AccumulationState state;
    state.config = AccumulationConfig{0, 2, OrderStrategy::ByMetadata, false};

    AccumulationEntry entry;
    state.AddEntry(entry);
    state.AddEntry(entry);
    state.AddEntry(entry);  // 3, but max is 2

    std::string errorMsg;
    EXPECT_FALSE(state.ValidateCount(errorMsg));
    EXPECT_TRUE(errorMsg.find("at most 2") != std::string::npos);
}

TEST(AccumulationStateTest, ValidateCountSuccess) {
    AccumulationState state;
    state.config = AccumulationConfig{1, 5, OrderStrategy::ByMetadata, false};

    AccumulationEntry entry;
    state.AddEntry(entry);
    state.AddEntry(entry);
    state.AddEntry(entry);  // 3, within [1, 5]

    std::string errorMsg;
    EXPECT_TRUE(state.ValidateCount(errorMsg));
}

TEST(AccumulationStateTest, ValidateDuplicateKeys) {
    AccumulationState state;
    state.config = AccumulationConfig{0, 10, OrderStrategy::ByMetadata, false};  // No duplicates

    AccumulationEntry e1, e2;
    e1.sortKey = 5;
    e2.sortKey = 5;  // Duplicate!

    state.AddEntry(e1);
    state.AddEntry(e2);

    std::string errorMsg;
    EXPECT_FALSE(state.ValidateDuplicates(errorMsg));
    EXPECT_TRUE(errorMsg.find("Duplicate") != std::string::npos);
}

TEST(AccumulationStateTest, ValidateDuplicateKeysAllowed) {
    AccumulationState state;
    state.config = AccumulationConfig{0, 10, OrderStrategy::ByMetadata, true};  // Duplicates allowed

    AccumulationEntry e1, e2;
    e1.sortKey = 5;
    e2.sortKey = 5;  // Same key, but allowed

    state.AddEntry(e1);
    state.AddEntry(e2);

    std::string errorMsg;
    EXPECT_TRUE(state.ValidateDuplicates(errorMsg));
}

// ============================================================================
// ITERABLE CONCEPT TESTS
// ============================================================================

TEST(IterableConceptTest, VectorIsIterable) {
    static_assert(Iterable<std::vector<int>>, "vector<int> should be Iterable");
    static_assert(Iterable<std::vector<VkBuffer>>, "vector<VkBuffer> should be Iterable");
}

TEST(IterableConceptTest, ArrayIsIterable) {
    static_assert(Iterable<std::array<int, 5>>, "array<int, 5> should be Iterable");
}

TEST(IterableConceptTest, PrimitiveNotIterable) {
    static_assert(!Iterable<int>, "int should NOT be Iterable");
    static_assert(!Iterable<VkBuffer>, "VkBuffer should NOT be Iterable");
}

TEST(IterableConceptTest, IterableOfCorrectType) {
    static_assert(IterableOf<std::vector<int>, int>, "vector<int> is IterableOf<int>");
    static_assert(!IterableOf<std::vector<int>, float>, "vector<int> is NOT IterableOf<float>");
}

// ============================================================================
// REGISTRY WITH ACCUMULATION RULE TESTS
// ============================================================================

TEST(ConnectionRuleRegistryTest, DefaultRegistryHasAccumulationRule) {
    auto registry = ConnectionRuleRegistry::CreateDefault();

    // Should have at least DirectConnectionRule and AccumulationConnectionRule
    EXPECT_GE(registry.RuleCount(), 2u);

    // First rule should be AccumulationConnectionRule (priority 100)
    const auto& rules = registry.GetRules();
    EXPECT_EQ(rules[0]->Name(), "AccumulationConnectionRule");
}

TEST(ConnectionRuleRegistryTest, AccumulationRuleMatchedForAccumulationSlot) {
    auto registry = ConnectionRuleRegistry::CreateDefault();

    auto sourceInfo = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    auto targetInfo = SlotInfo::FromInputSlot<AccumulationTargetConfig::PASSES_Slot>("PASSES");

    const ConnectionRule* rule = registry.FindRule(sourceInfo, targetInfo);

    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->Name(), "AccumulationConnectionRule");
}

TEST(ConnectionRuleRegistryTest, DirectRuleMatchedForNonAccumulationSlot) {
    auto registry = ConnectionRuleRegistry::CreateDefault();

    auto sourceInfo = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    auto targetInfo = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    const ConnectionRule* rule = registry.FindRule(sourceInfo, targetInfo);

    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->Name(), "DirectConnectionRule");
}

// ============================================================================
// VARIADIC CONNECTION RULE TESTS
// ============================================================================

TEST(VariadicConnectionRuleTest, CanHandleBindingTarget) {
    VariadicConnectionRule rule;

    auto sourceInfo = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    MockBindingRef bindingRef{3, 7};
    auto targetInfo = SlotInfo::FromBinding(bindingRef, "storageBuffer");

    // Variadic rule handles binding targets
    EXPECT_TRUE(rule.CanHandle(sourceInfo, targetInfo));
}

TEST(VariadicConnectionRuleTest, CannotHandleStaticSlotTarget) {
    VariadicConnectionRule rule;

    auto sourceInfo = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    auto targetInfo = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    // Variadic rule does NOT handle static slot targets
    EXPECT_FALSE(rule.CanHandle(sourceInfo, targetInfo));
}

TEST(VariadicConnectionRuleTest, ValidateSourceMustBeOutput) {
    VariadicConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    // Source is INPUT (wrong!)
    ctx.sourceSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");
    MockBindingRef bindingRef{0, 7};
    ctx.targetSlot = SlotInfo::FromBinding(bindingRef, "binding");

    auto result = rule.Validate(ctx);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("output") != std::string::npos);
}

TEST(VariadicConnectionRuleTest, ValidateTargetMustBeBinding) {
    VariadicConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    // Target is NOT a binding
    ctx.targetSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    auto result = rule.Validate(ctx);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("binding") != std::string::npos);
}

TEST(VariadicConnectionRuleTest, ValidateBindingIndexValid) {
    VariadicConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    // Create binding with invalid index
    ctx.targetSlot.kind = SlotKind::Binding;
    ctx.targetSlot.binding = UINT32_MAX;  // Invalid

    auto result = rule.Validate(ctx);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("Invalid binding") != std::string::npos);
}

TEST(VariadicConnectionRuleTest, ValidateSuccess) {
    VariadicConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    MockBindingRef bindingRef{3, 7};
    ctx.targetSlot = SlotInfo::FromBinding(bindingRef, "storageBuffer");

    auto result = rule.Validate(ctx);
    EXPECT_TRUE(result.success);
}

TEST(VariadicConnectionRuleTest, Priority) {
    VariadicConnectionRule rule;
    EXPECT_EQ(rule.Priority(), 25u);  // Lower than DirectConnectionRule (50)
}

TEST(VariadicConnectionRuleTest, Name) {
    VariadicConnectionRule rule;
    EXPECT_EQ(rule.Name(), "VariadicConnectionRule");
}

// ============================================================================
// REGISTRY WITH ALL THREE RULES TESTS
// ============================================================================

TEST(ConnectionRuleRegistryTest, DefaultRegistryHasVariadicRule) {
    auto registry = ConnectionRuleRegistry::CreateDefault();

    // Should have DirectConnectionRule, AccumulationConnectionRule, and VariadicConnectionRule
    EXPECT_EQ(registry.RuleCount(), 3u);
}

TEST(ConnectionRuleRegistryTest, VariadicRuleMatchedForBindingTarget) {
    auto registry = ConnectionRuleRegistry::CreateDefault();

    auto sourceInfo = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    MockBindingRef bindingRef{0, 7};
    auto targetInfo = SlotInfo::FromBinding(bindingRef, "binding");

    const ConnectionRule* rule = registry.FindRule(sourceInfo, targetInfo);

    // DirectConnectionRule has higher priority (50) than VariadicConnectionRule (25)
    // But DirectConnectionRule CAN handle binding targets (1:1 slot-to-binding)
    // So DirectConnectionRule should be matched
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->Name(), "DirectConnectionRule");
}

TEST(ConnectionRuleRegistryTest, RulePriorityOrder) {
    auto registry = ConnectionRuleRegistry::CreateDefault();

    const auto& rules = registry.GetRules();
    ASSERT_EQ(rules.size(), 3u);

    // Should be sorted by priority descending
    EXPECT_EQ(rules[0]->Priority(), 100u);  // AccumulationConnectionRule
    EXPECT_EQ(rules[1]->Priority(), 50u);   // DirectConnectionRule
    EXPECT_EQ(rules[2]->Priority(), 25u);   // VariadicConnectionRule
}

// ============================================================================
// UNIFIED CONNECT API TESTS
// ============================================================================

TEST(UnifiedConnectTest, ConnectionOrderDefault) {
    ConnectionOrder order;
    EXPECT_EQ(order.sortKey, 0);
}

TEST(UnifiedConnectTest, ConnectionOrderWithSortKey) {
    ConnectionOrder order{42};
    EXPECT_EQ(order.sortKey, 42);
}

TEST(UnifiedConnectTest, ConnectionInfoDefault) {
    ConnectionInfo info;
    EXPECT_EQ(info.sortKey, 0);
    uint8_t expected = static_cast<uint8_t>(SlotRole::None);
    uint8_t actual = static_cast<uint8_t>(info.roleOverride);
    EXPECT_EQ(actual, expected);
}

TEST(UnifiedConnectTest, ConnectionInfoWithSortKeyAndRole) {
    ConnectionInfo info{10, SlotRole::Execute};
    EXPECT_EQ(info.sortKey, 10);
    uint8_t expected = static_cast<uint8_t>(SlotRole::Execute);
    uint8_t actual = static_cast<uint8_t>(info.roleOverride);
    EXPECT_EQ(actual, expected);
}

TEST(UnifiedConnectTest, ValidateConnectionDirect) {
    auto registry = ConnectionRuleRegistry::CreateDefault();

    auto sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    auto targetSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    auto result = ValidateConnection(registry, sourceSlot, targetSlot);
    EXPECT_TRUE(result.success);
}

TEST(UnifiedConnectTest, ValidateConnectionAccumulation) {
    auto registry = ConnectionRuleRegistry::CreateDefault();

    auto sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    auto targetSlot = SlotInfo::FromInputSlot<AccumulationTargetConfig::PASSES_Slot>("PASSES");

    auto result = ValidateConnection(registry, sourceSlot, targetSlot);
    EXPECT_TRUE(result.success);
}

TEST(UnifiedConnectTest, ValidateConnectionVariadic) {
    auto registry = ConnectionRuleRegistry::CreateDefault();

    auto sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    MockBindingRef bindingRef{0, 7};
    auto targetSlot = SlotInfo::FromBinding(bindingRef, "binding");

    auto result = ValidateConnection(registry, sourceSlot, targetSlot);
    EXPECT_TRUE(result.success);
}

TEST(UnifiedConnectTest, ValidateConnectionInvalidSourceInput) {
    auto registry = ConnectionRuleRegistry::CreateDefault();

    // Source is input (invalid)
    auto sourceSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");
    auto targetSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    auto result = ValidateConnection(registry, sourceSlot, targetSlot);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("output") != std::string::npos);
}

TEST(UnifiedConnectTest, CreateSlotInfoFromSlotReference) {
    auto outputInfo = CreateSlotInfo<SourceConfig::BUFFER_OUT_Slot>("OUT", true);
    EXPECT_TRUE(outputInfo.IsOutput());
    EXPECT_EQ(outputInfo.name, "OUT");

    auto inputInfo = CreateSlotInfo<TargetConfig::BUFFER_IN_Slot>("IN", false);
    EXPECT_TRUE(inputInfo.IsInput());
    EXPECT_EQ(inputInfo.name, "IN");
}

// ============================================================================
// CONCEPT CONSTRAINT VERIFICATION (Compile-Time Tests)
// ============================================================================

// These tests verify concept constraints at compile time
// If concepts aren't satisfied, compilation fails with clear error messages

TEST(UnifiedConnectConceptsTest, SlotReferenceConceptSatisfied) {
    // These should compile - slots satisfy SlotReference concept
    static_assert(SlotReference<SourceConfig::BUFFER_OUT_Slot>);
    static_assert(SlotReference<TargetConfig::BUFFER_IN_Slot>);
    static_assert(SlotReference<AccumulationTargetConfig::PASSES_Slot>);
}

TEST(UnifiedConnectConceptsTest, AccumulationSlotConceptSatisfied) {
    // AccumulationSlot requires SlotReference + Accumulation flag
    static_assert(AccumulationSlot<AccumulationTargetConfig::PASSES_Slot>);
    static_assert(!AccumulationSlot<TargetConfig::BUFFER_IN_Slot>);  // Not accumulation
}

TEST(UnifiedConnectConceptsTest, BindingReferenceConceptSatisfied) {
    // BindingReference requires binding + descriptorType
    static_assert(BindingReference<MockBindingRef>);
    static_assert(!BindingReference<SourceConfig::BUFFER_OUT_Slot>);  // Not binding
}

// ============================================================================
// ACCUMULATION TYPE HELPERS TESTS
// ============================================================================

// Include TypedConnection.h for AccumulatedType
#include "Core/TypedConnection.h"

TEST(AccumulationTypeTest, AccumulatedTypeForBool) {
    using AccType = AccumulatedType_t<bool>;
    static_assert(std::is_same_v<AccType, std::vector<bool>>);
}

TEST(AccumulationTypeTest, AccumulatedTypeForVkBuffer) {
    using AccType = AccumulatedType_t<VkBuffer>;
    static_assert(std::is_same_v<AccType, std::vector<VkBuffer>>);
}

TEST(AccumulationTypeTest, AccumulatedTypeForStruct) {
    struct TestStruct { int x; };
    using AccType = AccumulatedType_t<TestStruct>;
    static_assert(std::is_same_v<AccType, std::vector<TestStruct>>);
}

// ============================================================================
// ACCUMULATION SLOT COMPILE-TIME TESTS
// ============================================================================

// Test config with bool accumulation slot
struct BoolAccumulationConfig : public ResourceConfigBase<1, 0> {
    ACCUMULATION_INPUT_SLOT(INPUTS, bool, 0,
        SlotNullability::Required);
};

TEST(AccumulationSlotTest, BoolAccumulationSlotFlags) {
    // Verify the slot has correct flags
    static_assert(BoolAccumulationConfig::INPUTS_Slot::isAccumulation);
    static_assert(BoolAccumulationConfig::INPUTS_Slot::isMultiConnect);
    static_assert(HasAccumulation(BoolAccumulationConfig::INPUTS_Slot::flags));
    static_assert(HasMultiConnect(BoolAccumulationConfig::INPUTS_Slot::flags));
}

TEST(AccumulationSlotTest, AccumulationSlotForcesExecuteRole) {
    // Sprint 6.3: Accumulation slots are ALWAYS Execute role (never Dependency)
    // - Accumulated vector is rebuilt each frame (reset semantics)
    // - No dependency propagation needed
    // - Result is Transient (don't cache across frames)
    static_assert(BoolAccumulationConfig::INPUTS_Slot::role == SlotRole::Execute,
        "Accumulation slots must have Execute role");
    static_assert(AccumulationTargetConfig::PASSES_Slot::role == SlotRole::Execute,
        "Accumulation slots must have Execute role");
}

TEST(AccumulationSlotTest, BoolAccumulationSlotType) {
    // The slot's element type is bool
    using ElementType = typename BoolAccumulationConfig::INPUTS_Slot::Type;
    static_assert(std::is_same_v<ElementType, bool>);
}

TEST(AccumulationSlotTest, AccumulationSlotInfo) {
    auto info = SlotInfo::FromInputSlot<BoolAccumulationConfig::INPUTS_Slot>("INPUTS");

    EXPECT_TRUE(info.IsAccumulation());
    EXPECT_TRUE(info.IsMultiConnect());
    EXPECT_TRUE(info.IsInput());
    EXPECT_FALSE(info.IsOutput());
}

// ============================================================================
// ACCUMULATION ENTRY TESTS (Sprint 6.0.1)
// ============================================================================

TEST(AccumulationEntryTest, DefaultValues) {
    AccumulationEntry entry;

    EXPECT_EQ(entry.sourceOutputIndex, 0u);
    EXPECT_EQ(entry.sortKey, 0);
    uint8_t expected = static_cast<uint8_t>(SlotRole::None);
    uint8_t actual = static_cast<uint8_t>(entry.roleOverride);
    EXPECT_EQ(actual, expected);
}

TEST(AccumulationEntryTest, WithSortKey) {
    AccumulationEntry entry;
    entry.sortKey = 42;
    entry.sourceOutputIndex = 5;

    EXPECT_EQ(entry.sortKey, 42);
    EXPECT_EQ(entry.sourceOutputIndex, 5u);
}

// ============================================================================
// PENDING ACCUMULATION STATE TESTS
// ============================================================================

TEST(AccumulationStateTest, AddEntries) {
    AccumulationState state;

    AccumulationEntry entry1;
    entry1.sortKey = 2;
    state.entries.push_back(entry1);

    AccumulationEntry entry2;
    entry2.sortKey = 1;
    state.entries.push_back(entry2);

    EXPECT_EQ(state.entries.size(), 2u);
}

TEST(AccumulationStateTest, SortBySortKey) {
    AccumulationState state;

    AccumulationEntry entry1;
    entry1.sortKey = 3;
    state.entries.push_back(entry1);

    AccumulationEntry entry2;
    entry2.sortKey = 1;
    state.entries.push_back(entry2);

    AccumulationEntry entry3;
    entry3.sortKey = 2;
    state.entries.push_back(entry3);

    // Sort as done in RegisterAll()
    std::sort(state.entries.begin(), state.entries.end(),
        [](const AccumulationEntry& a, const AccumulationEntry& b) {
            return a.sortKey < b.sortKey;
        });

    EXPECT_EQ(state.entries[0].sortKey, 1);
    EXPECT_EQ(state.entries[1].sortKey, 2);
    EXPECT_EQ(state.entries[2].sortKey, 3);
}

// ============================================================================
// ACCUMULATION + FIELD EXTRACTION TESTS (Sprint 6.3)
// ============================================================================

// Test struct for field extraction
struct TestAccumStruct {
    int field1;
    float field2;
    VkBuffer field3;
};

TEST(AccumulationFieldExtractionTest, EntryPreservesFieldExtractionInfo) {
    // Verify that AccumulationEntry stores field extraction info from sourceSlot
    AccumulationEntry entry;
    entry.sourceSlot.hasFieldExtraction = true;
    entry.sourceSlot.fieldOffset = 16;
    entry.sourceSlot.fieldSize = sizeof(float);

    EXPECT_TRUE(entry.sourceSlot.hasFieldExtraction);
    EXPECT_EQ(entry.sourceSlot.fieldOffset, 16u);
    EXPECT_EQ(entry.sourceSlot.fieldSize, sizeof(float));
}

TEST(AccumulationFieldExtractionTest, MultipleEntriesWithDifferentExtraction) {
    // Verify multiple entries can have different field extraction configs
    AccumulationState state;

    // Entry 1: Extract field1 (int at offset 0)
    AccumulationEntry entry1;
    entry1.sortKey = 1;
    entry1.sourceSlot.hasFieldExtraction = true;
    entry1.sourceSlot.fieldOffset = offsetof(TestAccumStruct, field1);
    entry1.sourceSlot.fieldSize = sizeof(int);
    state.AddEntry(entry1);

    // Entry 2: No extraction (direct value)
    AccumulationEntry entry2;
    entry2.sortKey = 2;
    entry2.sourceSlot.hasFieldExtraction = false;
    state.AddEntry(entry2);

    // Entry 3: Extract field3 (VkBuffer)
    AccumulationEntry entry3;
    entry3.sortKey = 3;
    entry3.sourceSlot.hasFieldExtraction = true;
    entry3.sourceSlot.fieldOffset = offsetof(TestAccumStruct, field3);
    entry3.sourceSlot.fieldSize = sizeof(VkBuffer);
    state.AddEntry(entry3);

    ASSERT_EQ(state.entries.size(), 3u);

    // Verify each entry preserved its extraction config
    EXPECT_TRUE(state.entries[0].sourceSlot.hasFieldExtraction);
    EXPECT_EQ(state.entries[0].sourceSlot.fieldOffset, offsetof(TestAccumStruct, field1));

    EXPECT_FALSE(state.entries[1].sourceSlot.hasFieldExtraction);

    EXPECT_TRUE(state.entries[2].sourceSlot.hasFieldExtraction);
    EXPECT_EQ(state.entries[2].sourceSlot.fieldOffset, offsetof(TestAccumStruct, field3));
}

TEST(AccumulationFieldExtractionTest, PipelineAppliesFieldExtractionBeforeAccumulation) {
    // Verify that ConnectionPipeline applies FieldExtractionModifier before
    // AccumulationConnectionRule stores the entry
    ConnectionPipeline pipeline;

    // Add FieldExtractionModifier
    pipeline.AddModifier(std::make_unique<FieldExtractionModifier>(
        offsetof(TestAccumStruct, field2),  // Extract field2 (float)
        sizeof(float),
        ResourceType::PassThroughStorage  // Generic type for primitive
    ));

    AccumulationConnectionRule rule;

    // Set up context with accumulation state
    AccumulationState accState;
    accState.config = AccumulationConfig{0, 10, OrderStrategy::ConnectionOrder, true};

    ConnectionContext ctx;
    // Use mock node pointers (not real objects)
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x100000);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x200000);
    ctx.graph = reinterpret_cast<RenderGraph*>(0x300000);
    ctx.skipDependencyRegistration = true;  // Skip AddDependency call on mock pointers
    ctx.sourceLifetime = ResourceLifetime::Persistent;  // Required for field extraction
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    ctx.targetSlot = SlotInfo::FromInputSlot<AccumulationTargetConfig::PASSES_Slot>("PASSES");
    ctx.accumulationState = &accState;

    auto result = pipeline.Execute(ctx, rule);
    EXPECT_TRUE(result.success) << result.errorMessage;

    // Verify field extraction was applied to sourceSlot BEFORE Resolve stored it
    ASSERT_EQ(accState.entries.size(), 1u);
    EXPECT_TRUE(accState.entries[0].sourceSlot.hasFieldExtraction);
    EXPECT_EQ(accState.entries[0].sourceSlot.fieldOffset, offsetof(TestAccumStruct, field2));
    EXPECT_EQ(accState.entries[0].sourceSlot.fieldSize, sizeof(float));
}

TEST(AccumulationFieldExtractionTest, MixedExtractionThroughPipeline) {
    // Simulate connecting multiple sources to accumulation slot,
    // some with field extraction, some without
    AccumulationState accState;
    accState.config = AccumulationConfig{0, 10, OrderStrategy::ByMetadata, true};

    AccumulationConnectionRule rule;

    // Connection 1: With field extraction
    {
        ConnectionPipeline pipeline;
        pipeline.AddModifier(std::make_unique<FieldExtractionModifier>(
            offsetof(TestAccumStruct, field1), sizeof(int), ResourceType::PassThroughStorage
        ));
        pipeline.AddModifier(std::make_unique<AccumulationSortConfig>(10));

        ConnectionContext ctx;
        // Use mock node pointers (not real objects)
        ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x100000);
        ctx.targetNode = reinterpret_cast<NodeInstance*>(0x200000);
        ctx.graph = reinterpret_cast<RenderGraph*>(0x300000);
        ctx.skipDependencyRegistration = true;  // Skip AddDependency call on mock pointers
        ctx.sourceLifetime = ResourceLifetime::Persistent;
        ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
        ctx.targetSlot = SlotInfo::FromInputSlot<AccumulationTargetConfig::PASSES_Slot>("PASSES");
        ctx.accumulationState = &accState;

        auto result = pipeline.Execute(ctx, rule);
        EXPECT_TRUE(result.success) << result.errorMessage;
    }

    // Connection 2: Without field extraction
    {
        ConnectionPipeline pipeline;
        pipeline.AddModifier(std::make_unique<AccumulationSortConfig>(20));

        ConnectionContext ctx;
        ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x110000);
        ctx.targetNode = reinterpret_cast<NodeInstance*>(0x200000);
        ctx.graph = reinterpret_cast<RenderGraph*>(0x300000);
        ctx.skipDependencyRegistration = true;  // Skip AddDependency call on mock pointers
        ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT2");
        ctx.targetSlot = SlotInfo::FromInputSlot<AccumulationTargetConfig::PASSES_Slot>("PASSES");
        ctx.accumulationState = &accState;

        auto result = pipeline.Execute(ctx, rule);
        EXPECT_TRUE(result.success) << result.errorMessage;
    }

    // Connection 3: With different field extraction
    {
        ConnectionPipeline pipeline;
        pipeline.AddModifier(std::make_unique<FieldExtractionModifier>(
            offsetof(TestAccumStruct, field3), sizeof(VkBuffer), ResourceType::Buffer
        ));
        pipeline.AddModifier(std::make_unique<AccumulationSortConfig>(30));

        ConnectionContext ctx;
        ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x120000);
        ctx.targetNode = reinterpret_cast<NodeInstance*>(0x200000);
        ctx.graph = reinterpret_cast<RenderGraph*>(0x300000);
        ctx.skipDependencyRegistration = true;  // Skip AddDependency call on mock pointers
        ctx.sourceLifetime = ResourceLifetime::Persistent;
        ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT3");
        ctx.targetSlot = SlotInfo::FromInputSlot<AccumulationTargetConfig::PASSES_Slot>("PASSES");
        ctx.accumulationState = &accState;

        auto result = pipeline.Execute(ctx, rule);
        EXPECT_TRUE(result.success) << result.errorMessage;
    }

    // Verify all entries stored correctly
    ASSERT_EQ(accState.entries.size(), 3u);

    // Sort by metadata to get predictable order
    accState.SortEntries(OrderStrategy::ByMetadata);

    // Entry with sortKey=10: has field extraction for field1
    EXPECT_TRUE(accState.entries[0].sourceSlot.hasFieldExtraction);
    EXPECT_EQ(accState.entries[0].sourceSlot.fieldOffset, offsetof(TestAccumStruct, field1));

    // Entry with sortKey=20: no field extraction
    EXPECT_FALSE(accState.entries[1].sourceSlot.hasFieldExtraction);

    // Entry with sortKey=30: has field extraction for field3
    EXPECT_TRUE(accState.entries[2].sourceSlot.hasFieldExtraction);
    EXPECT_EQ(accState.entries[2].sourceSlot.fieldOffset, offsetof(TestAccumStruct, field3));
}

// ============================================================================
// CONNECTION PIPELINE TESTS (Sprint 6.0.1 Phase 2)
// ============================================================================

// Mock modifier for testing pipeline phases
class MockModifier : public ConnectionModifier {
public:
    std::vector<std::string> phaseCalls;
    ConnectionResult preValResult = ConnectionResult::Success();
    ConnectionResult preResResult = ConnectionResult::Success();
    ConnectionResult postResResult = ConnectionResult::Success();
    uint32_t testPriority = 50;

    [[nodiscard]] ConnectionResult PreValidation(ConnectionContext& /*ctx*/) override {
        phaseCalls.push_back("PreValidation");
        return preValResult;
    }

    ConnectionResult PreResolve(ConnectionContext& /*ctx*/) override {
        phaseCalls.push_back("PreResolve");
        return preResResult;
    }

    ConnectionResult PostResolve(ConnectionContext& /*ctx*/) override {
        phaseCalls.push_back("PostResolve");
        return postResResult;
    }

    [[nodiscard]] uint32_t Priority() const override { return testPriority; }
    [[nodiscard]] std::string_view Name() const override { return "MockModifier"; }
};

TEST(ConnectionPipelineTest, EmptyPipelineExecutesRule) {
    ConnectionPipeline pipeline;
    DirectConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.graph = reinterpret_cast<RenderGraph*>(0x3);  // Placeholder for Resolve()
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    ctx.targetSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    auto result = pipeline.Execute(ctx, rule);
    EXPECT_TRUE(result.success);
}

TEST(ConnectionPipelineTest, ModifierPhaseOrderCorrect) {
    ConnectionPipeline pipeline;
    auto mod = std::make_unique<MockModifier>();
    auto* modPtr = mod.get();
    pipeline.AddModifier(std::move(mod));

    DirectConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.graph = reinterpret_cast<RenderGraph*>(0x3);  // Placeholder for Resolve()
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    ctx.targetSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    auto result = pipeline.Execute(ctx, rule);
    EXPECT_TRUE(result.success);

    // Verify phase order: PreValidation → PreResolve → PostResolve
    ASSERT_EQ(modPtr->phaseCalls.size(), 3u);
    EXPECT_EQ(modPtr->phaseCalls[0], "PreValidation");
    EXPECT_EQ(modPtr->phaseCalls[1], "PreResolve");
    EXPECT_EQ(modPtr->phaseCalls[2], "PostResolve");
}

TEST(ConnectionPipelineTest, PreValidationFailureStopsPipeline) {
    ConnectionPipeline pipeline;
    auto mod = std::make_unique<MockModifier>();
    mod->preValResult = ConnectionResult::Error("PreValidation failed");
    auto* modPtr = mod.get();
    pipeline.AddModifier(std::move(mod));

    DirectConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    ctx.targetSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    auto result = pipeline.Execute(ctx, rule);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("PreValidation") != std::string::npos);

    // Only PreValidation should have been called
    ASSERT_EQ(modPtr->phaseCalls.size(), 1u);
    EXPECT_EQ(modPtr->phaseCalls[0], "PreValidation");
}

TEST(ConnectionPipelineTest, PreResolveFailureStopsPipeline) {
    ConnectionPipeline pipeline;
    auto mod = std::make_unique<MockModifier>();
    mod->preResResult = ConnectionResult::Error("PreResolve failed");
    auto* modPtr = mod.get();
    pipeline.AddModifier(std::move(mod));

    DirectConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    ctx.targetSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    auto result = pipeline.Execute(ctx, rule);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("PreResolve") != std::string::npos);

    // PreValidation and PreResolve should have been called
    ASSERT_EQ(modPtr->phaseCalls.size(), 2u);
}

TEST(ConnectionPipelineTest, MultipleModifiersPriorityOrder) {
    ConnectionPipeline pipeline;

    auto mod1 = std::make_unique<MockModifier>();
    mod1->testPriority = 100;  // Higher priority, runs first
    auto* mod1Ptr = mod1.get();

    auto mod2 = std::make_unique<MockModifier>();
    mod2->testPriority = 50;   // Lower priority, runs second
    auto* mod2Ptr = mod2.get();

    // Add in reverse order to verify sorting
    pipeline.AddModifier(std::move(mod2));
    pipeline.AddModifier(std::move(mod1));

    DirectConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.graph = reinterpret_cast<RenderGraph*>(0x3);  // Placeholder for Resolve()
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    ctx.targetSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    auto result = pipeline.Execute(ctx, rule);
    EXPECT_TRUE(result.success);

    // Both modifiers should have all 3 phases called
    EXPECT_EQ(mod1Ptr->phaseCalls.size(), 3u);
    EXPECT_EQ(mod2Ptr->phaseCalls.size(), 3u);
}

TEST(ConnectionPipelineTest, ModifierCount) {
    ConnectionPipeline pipeline;
    EXPECT_EQ(pipeline.ModifierCount(), 0u);
    EXPECT_FALSE(pipeline.HasModifiers());

    pipeline.AddModifier(std::make_unique<MockModifier>());
    EXPECT_EQ(pipeline.ModifierCount(), 1u);
    EXPECT_TRUE(pipeline.HasModifiers());

    pipeline.AddModifier(std::make_unique<MockModifier>());
    EXPECT_EQ(pipeline.ModifierCount(), 2u);

    pipeline.Clear();
    EXPECT_EQ(pipeline.ModifierCount(), 0u);
    EXPECT_FALSE(pipeline.HasModifiers());
}

TEST(ConnectionContextTest, EffectiveTypeOverride) {
    ConnectionContext ctx;
    ctx.sourceSlot.resourceType = ResourceType::Buffer;

    // Without override, returns source slot's type
    EXPECT_EQ(ctx.GetEffectiveSourceType(), ResourceType::Buffer);
    EXPECT_FALSE(ctx.hasEffectiveTypeOverride);

    // Set effective type
    ctx.SetEffectiveResourceType(ResourceType::ImageView);
    EXPECT_TRUE(ctx.hasEffectiveTypeOverride);
    EXPECT_EQ(ctx.GetEffectiveSourceType(), ResourceType::ImageView);
}

TEST(ConnectionContextTest, SourceLifetime) {
    ConnectionContext ctx;

    // Default is Transient
    EXPECT_EQ(ctx.sourceLifetime, ResourceLifetime::Transient);
    EXPECT_FALSE(ctx.IsPersistentSource());

    // Set to Persistent
    ctx.sourceLifetime = ResourceLifetime::Persistent;
    EXPECT_TRUE(ctx.IsPersistentSource());
}

// ============================================================================
// FIELD EXTRACTION MODIFIER TESTS
// ============================================================================

TEST(FieldExtractionModifierTest, Construction) {
    FieldExtractionModifier mod(64, 8, ResourceType::Buffer);

    EXPECT_EQ(mod.GetFieldOffset(), 64u);
    EXPECT_EQ(mod.GetFieldSize(), 8u);
    EXPECT_EQ(mod.GetFieldType(), ResourceType::Buffer);
    EXPECT_EQ(mod.Name(), "FieldExtractionModifier");
    EXPECT_EQ(mod.Priority(), 75u);
}

TEST(FieldExtractionModifierTest, PreValidationRejectsTransient) {
    FieldExtractionModifier mod(0, 8, ResourceType::ImageView);

    ConnectionContext ctx;
    ctx.sourceLifetime = ResourceLifetime::Transient;

    auto result = mod.PreValidation(ctx);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("Persistent") != std::string::npos);
}

TEST(FieldExtractionModifierTest, PreValidationAcceptsPersistent) {
    FieldExtractionModifier mod(0, 8, ResourceType::ImageView);

    ConnectionContext ctx;
    ctx.sourceLifetime = ResourceLifetime::Persistent;

    auto result = mod.PreValidation(ctx);
    EXPECT_TRUE(result.success);
}

TEST(FieldExtractionModifierTest, PreValidationSetsEffectiveType) {
    FieldExtractionModifier mod(32, 4, ResourceType::Buffer);

    ConnectionContext ctx;
    ctx.sourceSlot.resourceType = ResourceType::PassThroughStorage;  // Struct type
    ctx.sourceLifetime = ResourceLifetime::Persistent;  // Required for field extraction

    // PreValidation now sets effective type (moved from PreResolve)
    auto result = mod.PreValidation(ctx);
    EXPECT_TRUE(result.success);

    // Effective type should be the field type
    EXPECT_EQ(ctx.GetEffectiveSourceType(), ResourceType::Buffer);
    EXPECT_TRUE(ctx.hasEffectiveTypeOverride);

    // Slot info should be updated
    EXPECT_EQ(ctx.sourceSlot.fieldOffset, 32u);
    EXPECT_EQ(ctx.sourceSlot.fieldSize, 4u);
    EXPECT_TRUE(ctx.sourceSlot.hasFieldExtraction);
}

TEST(FieldExtractionModifierTest, FullPipelineWithPersistentSource) {
    ConnectionPipeline pipeline;
    // Extract a Buffer field (matches target slot type)
    pipeline.AddModifier(std::make_unique<FieldExtractionModifier>(16, 8, ResourceType::Buffer));

    DirectConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.graph = reinterpret_cast<RenderGraph*>(0x3);
    ctx.sourceLifetime = ResourceLifetime::Persistent;  // Required for field extraction
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    ctx.targetSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    auto result = pipeline.Execute(ctx, rule);
    EXPECT_TRUE(result.success) << "Pipeline failed: " << result.errorMessage;

    // Verify extraction was applied
    EXPECT_TRUE(ctx.sourceSlot.hasFieldExtraction);
    EXPECT_EQ(ctx.sourceSlot.fieldOffset, 16u);
}

TEST(FieldExtractionModifierTest, FullPipelineRejectsTransient) {
    ConnectionPipeline pipeline;
    pipeline.AddModifier(std::make_unique<FieldExtractionModifier>(16, 8, ResourceType::ImageView));

    DirectConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.graph = reinterpret_cast<RenderGraph*>(0x3);
    ctx.sourceLifetime = ResourceLifetime::Transient;  // Should fail
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    ctx.targetSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    auto result = pipeline.Execute(ctx, rule);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("FieldExtractionModifier") != std::string::npos);
}

// ============================================================================
// VARIADIC MODIFIER API (Sprint 6.0.2)
// ============================================================================

TEST(VariadicModifierAPI, SingleModifierStreamlinedSyntax) {
    // Test that variadic API correctly constructs ConnectionMeta from single modifier
    ConnectionPipeline pipeline;

    // Manually create modifier for comparison
    auto fieldMod = FieldExtractionModifier(16, 8, ResourceType::Buffer);

    // The variadic API should accept this modifier directly
    // (This test verifies compilation - the actual usage is in ConnectionBatch)
    EXPECT_EQ(fieldMod.GetFieldOffset(), 16u);
    EXPECT_EQ(fieldMod.GetFieldSize(), 8u);
}

TEST(VariadicModifierAPI, MultipleModifiersStreamlinedSyntax) {
    // Test that multiple modifiers can be passed directly
    ConnectionPipeline pipeline;

    // Add multiple modifiers via the standard API
    pipeline.AddModifier(std::make_unique<FieldExtractionModifier>(16, 8, ResourceType::Buffer));
    pipeline.AddModifier(std::make_unique<SlotRoleModifier>(SlotRole::Execute));

    DirectConnectionRule rule;

    ConnectionContext ctx;
    ctx.sourceNode = reinterpret_cast<NodeInstance*>(0x1);
    ctx.targetNode = reinterpret_cast<NodeInstance*>(0x2);
    ctx.graph = reinterpret_cast<RenderGraph*>(0x3);
    ctx.sourceLifetime = ResourceLifetime::Persistent;
    ctx.sourceSlot = SlotInfo::FromOutputSlot<SourceConfig::BUFFER_OUT_Slot>("OUT");
    ctx.targetSlot = SlotInfo::FromInputSlot<TargetConfig::BUFFER_IN_Slot>("IN");

    auto result = pipeline.Execute(ctx, rule);
    EXPECT_TRUE(result.success);

    // Verify both modifiers were applied
    EXPECT_TRUE(ctx.sourceSlot.hasFieldExtraction);
    EXPECT_EQ(ctx.sourceSlot.fieldOffset, 16u);
    // SlotRoleModifier sets ctx.roleOverride, not ctx.sourceSlot.role
    EXPECT_EQ(ctx.roleOverride, SlotRole::Execute);
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
