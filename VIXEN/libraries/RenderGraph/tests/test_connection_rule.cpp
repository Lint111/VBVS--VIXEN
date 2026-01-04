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

#include "Core/ConnectionRule.h"
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
        SlotNullability::Required,
        SlotRole::Dependency);
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
// MAIN
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
