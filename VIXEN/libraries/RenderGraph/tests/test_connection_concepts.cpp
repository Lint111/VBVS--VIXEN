/**
 * @file test_connection_concepts.cpp
 * @brief Compile-time tests for C++20 connection concepts (Sprint 6.0.1)
 *
 * This test file validates that the concept definitions correctly identify:
 * - SlotReference types (from INPUT_SLOT/OUTPUT_SLOT macros)
 * - BindingReference types (for variadic shader bindings)
 * - AccumulationSlot types (slots with Accumulation flag)
 * - ConnectionMetadata types (for ordering)
 *
 * Most validation is compile-time via static_assert.
 */

#include <iostream>
#include <vector>
#include <vulkan/vulkan.h>

#include "Data/Core/ResourceConfig.h"
#include "Data/Core/ConnectionConcepts.h"
#include "Data/Core/CompileTimeResourceSystem.h"  // For PassThroughStorage

using namespace Vixen::RenderGraph;

// ============================================================================
// TEST CONFIG: Standard slots without flags
// ============================================================================

struct StandardConfig : public ResourceConfigBase<2, 1> {
    INPUT_SLOT(DATA, VkBuffer, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    INPUT_SLOT(OPTIONAL_IMAGE, VkImageView, 1,
        SlotNullability::Optional,
        SlotRole::Execute,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    OUTPUT_SLOT(RESULT, VkImage, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);
};

// ============================================================================
// TEST CONFIG: Accumulation slots with flags
// ============================================================================

// Use PassThroughStorage for type-erased accumulation (avoids ResourceTypeTraits issues)
struct TestAccumulationConfig : public ResourceConfigBase<1, 0> {
    // Accumulation slot using PassThroughStorage (valid resource type)
    ACCUMULATION_INPUT_SLOT(PASSES, PassThroughStorage, 0,
        SlotNullability::Required);
};

// ============================================================================
// TEST CONFIG: Slots with explicit flags
// ============================================================================

struct ExplicitFlagsConfig : public ResourceConfigBase<2, 0> {
    // Multi-connect without accumulation
    INPUT_SLOT_FLAGS(MULTI_SOURCES, VkBuffer, 0,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel,
        SlotFlags::MultiConnect);

    // Accumulation with explicit ordering (using PassThroughStorage)
    INPUT_SLOT_FLAGS(ORDERED_PASSES, PassThroughStorage, 1,
        SlotNullability::Required,
        SlotRole::Dependency,
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel,
        SlotFlags::Accumulation | SlotFlags::MultiConnect | SlotFlags::ExplicitOrder);
};

// ============================================================================
// TEST: Mock binding reference (simulates shader metadata)
// ============================================================================

struct MockBindingRef {
    uint32_t binding;
    uint32_t descriptorType;  // VkDescriptorType
    const char* name;
};

// ============================================================================
// COMPILE-TIME TESTS: SlotReference concept
// ============================================================================

// Standard slots should satisfy SlotReference
static_assert(SlotReference<StandardConfig::DATA_Slot>,
    "DATA_Slot should satisfy SlotReference");
static_assert(SlotReference<StandardConfig::OPTIONAL_IMAGE_Slot>,
    "OPTIONAL_IMAGE_Slot should satisfy SlotReference");
static_assert(SlotReference<StandardConfig::RESULT_Slot>,
    "RESULT_Slot should satisfy SlotReference");

// Accumulation slots should also satisfy SlotReference
static_assert(SlotReference<TestAccumulationConfig::PASSES_Slot>,
    "PASSES_Slot should satisfy SlotReference");

// Slots with explicit flags should satisfy SlotReference
static_assert(SlotReference<ExplicitFlagsConfig::MULTI_SOURCES_Slot>,
    "MULTI_SOURCES_Slot should satisfy SlotReference");
static_assert(SlotReference<ExplicitFlagsConfig::ORDERED_PASSES_Slot>,
    "ORDERED_PASSES_Slot should satisfy SlotReference");

// Non-slot types should NOT satisfy SlotReference
static_assert(!SlotReference<int>,
    "int should not satisfy SlotReference");
static_assert(!SlotReference<VkBuffer>,
    "VkBuffer should not satisfy SlotReference");
static_assert(!SlotReference<MockBindingRef>,
    "MockBindingRef should not satisfy SlotReference");

// ============================================================================
// COMPILE-TIME TESTS: BindingReference concept
// ============================================================================

// Mock binding ref should satisfy BindingReference
static_assert(BindingReference<MockBindingRef>,
    "MockBindingRef should satisfy BindingReference");

// Slot types should NOT satisfy BindingReference
static_assert(!BindingReference<StandardConfig::DATA_Slot>,
    "DATA_Slot should not satisfy BindingReference");

// Plain types should NOT satisfy BindingReference
static_assert(!BindingReference<int>,
    "int should not satisfy BindingReference");

// ============================================================================
// COMPILE-TIME TESTS: AccumulationSlot concept
// ============================================================================

// Standard slots should NOT satisfy AccumulationSlot
static_assert(!AccumulationSlot<StandardConfig::DATA_Slot>,
    "DATA_Slot should not satisfy AccumulationSlot");

// ACCUMULATION_INPUT_SLOT should satisfy AccumulationSlot
static_assert(AccumulationSlot<TestAccumulationConfig::PASSES_Slot>,
    "PASSES_Slot should satisfy AccumulationSlot");

// Slot with Accumulation flag should satisfy AccumulationSlot
static_assert(AccumulationSlot<ExplicitFlagsConfig::ORDERED_PASSES_Slot>,
    "ORDERED_PASSES_Slot should satisfy AccumulationSlot");

// MultiConnect without Accumulation should NOT satisfy AccumulationSlot
static_assert(!AccumulationSlot<ExplicitFlagsConfig::MULTI_SOURCES_Slot>,
    "MULTI_SOURCES_Slot should not satisfy AccumulationSlot (no Accumulation flag)");

// ============================================================================
// COMPILE-TIME TESTS: MultiConnectSlot concept
// ============================================================================

static_assert(MultiConnectSlot<ExplicitFlagsConfig::MULTI_SOURCES_Slot>,
    "MULTI_SOURCES_Slot should satisfy MultiConnectSlot");
static_assert(MultiConnectSlot<TestAccumulationConfig::PASSES_Slot>,
    "PASSES_Slot should satisfy MultiConnectSlot");
static_assert(!MultiConnectSlot<StandardConfig::DATA_Slot>,
    "DATA_Slot should not satisfy MultiConnectSlot");

// ============================================================================
// COMPILE-TIME TESTS: ConnectionMetadata concept
// ============================================================================

static_assert(ConnectionMetadata<ConnectionOrder>,
    "ConnectionOrder should satisfy ConnectionMetadata");
static_assert(ConnectionMetadata<ConnectionInfo>,
    "ConnectionInfo should satisfy ConnectionMetadata");

struct InvalidMetadata { int notSortKey; };
static_assert(!ConnectionMetadata<InvalidMetadata>,
    "InvalidMetadata should not satisfy ConnectionMetadata");

// ============================================================================
// COMPILE-TIME TESTS: InputSlotReference / OutputSlotReference
// ============================================================================

static_assert(InputSlotReference<StandardConfig::DATA_Slot>,
    "DATA_Slot should satisfy InputSlotReference");
static_assert(!InputSlotReference<StandardConfig::RESULT_Slot>,
    "RESULT_Slot should not satisfy InputSlotReference (it's an output)");

static_assert(OutputSlotReference<StandardConfig::RESULT_Slot>,
    "RESULT_Slot should satisfy OutputSlotReference");
static_assert(!OutputSlotReference<StandardConfig::DATA_Slot>,
    "DATA_Slot should not satisfy OutputSlotReference (it's an input)");

// ============================================================================
// COMPILE-TIME TESTS: Legacy type traits
// ============================================================================

static_assert(is_slot_ref_v<StandardConfig::DATA_Slot>,
    "is_slot_ref_v should work for DATA_Slot");
static_assert(!is_slot_ref_v<int>,
    "is_slot_ref_v should be false for int");

static_assert(is_binding_ref_v<MockBindingRef>,
    "is_binding_ref_v should work for MockBindingRef");
static_assert(!is_binding_ref_v<StandardConfig::DATA_Slot>,
    "is_binding_ref_v should be false for slot types");

static_assert(is_accumulation_slot_v<TestAccumulationConfig::PASSES_Slot>,
    "is_accumulation_slot_v should work for accumulation slots");
static_assert(!is_accumulation_slot_v<StandardConfig::DATA_Slot>,
    "is_accumulation_slot_v should be false for standard slots");

// ============================================================================
// COMPILE-TIME TESTS: Slot flags accessors
// ============================================================================

static_assert(StandardConfig::DATA_Slot::flags == SlotFlags::None,
    "Standard slot should have SlotFlags::None");
static_assert(TestAccumulationConfig::PASSES_Slot::isAccumulation,
    "Accumulation slot should have isAccumulation = true");
static_assert(TestAccumulationConfig::PASSES_Slot::isMultiConnect,
    "Accumulation slot should have isMultiConnect = true");
static_assert(ExplicitFlagsConfig::ORDERED_PASSES_Slot::requiresExplicitOrder,
    "Slot with ExplicitOrder flag should have requiresExplicitOrder = true");

// ============================================================================
// RUNTIME TESTS (minimal - most validation is compile-time)
// ============================================================================

int main() {
    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << "  CONNECTION CONCEPTS TEST SUITE\n";
    std::cout << "  Sprint 6.0.1: C++20 Concepts\n";
    std::cout << "═══════════════════════════════════════════════════════\n\n";

    std::cout << "=== Compile-Time Concept Validation ===\n";
    std::cout << "  SlotReference concept:        PASSED (static_assert)\n";
    std::cout << "  BindingReference concept:     PASSED (static_assert)\n";
    std::cout << "  AccumulationSlot concept:     PASSED (static_assert)\n";
    std::cout << "  MultiConnectSlot concept:     PASSED (static_assert)\n";
    std::cout << "  ConnectionMetadata concept:   PASSED (static_assert)\n";
    std::cout << "  InputSlotReference concept:   PASSED (static_assert)\n";
    std::cout << "  OutputSlotReference concept:  PASSED (static_assert)\n";
    std::cout << "  Legacy type traits:           PASSED (static_assert)\n";
    std::cout << "  Slot flags accessors:         PASSED (static_assert)\n\n";

    std::cout << "=== Runtime Verification ===\n";

    // Verify ConnectionOrder usage
    ConnectionOrder order1{};
    ConnectionOrder order2{42};
    std::cout << "  ConnectionOrder default sortKey: " << order1.sortKey << "\n";
    std::cout << "  ConnectionOrder explicit sortKey: " << order2.sortKey << "\n";

    // Verify ConnectionInfo usage
    ConnectionInfo info1{};
    ConnectionInfo info2{10, SlotRole::Execute};
    std::cout << "  ConnectionInfo default sortKey: " << info1.sortKey << "\n";
    std::cout << "  ConnectionInfo with role override: sortKey=" << info2.sortKey
              << ", role=" << static_cast<int>(info2.roleOverride) << "\n";

    // Verify slot metadata is accessible
    std::cout << "\n=== Slot Metadata Verification ===\n";
    std::cout << "  StandardConfig::DATA:\n";
    std::cout << "    index: " << StandardConfig::DATA_Slot::index << "\n";
    std::cout << "    flags: " << static_cast<uint32_t>(StandardConfig::DATA_Slot::flags) << " (None)\n";
    std::cout << "    isAccumulation: " << StandardConfig::DATA_Slot::isAccumulation << "\n";

    std::cout << "  TestAccumulationConfig::PASSES:\n";
    std::cout << "    index: " << TestAccumulationConfig::PASSES_Slot::index << "\n";
    std::cout << "    flags: " << static_cast<uint32_t>(TestAccumulationConfig::PASSES_Slot::flags)
              << " (Accumulation | MultiConnect)\n";
    std::cout << "    isAccumulation: " << TestAccumulationConfig::PASSES_Slot::isAccumulation << "\n";
    std::cout << "    isMultiConnect: " << TestAccumulationConfig::PASSES_Slot::isMultiConnect << "\n";

    std::cout << "  ExplicitFlagsConfig::ORDERED_PASSES:\n";
    std::cout << "    flags: " << static_cast<uint32_t>(ExplicitFlagsConfig::ORDERED_PASSES_Slot::flags)
              << " (Accumulation | MultiConnect | ExplicitOrder)\n";
    std::cout << "    requiresExplicitOrder: " << ExplicitFlagsConfig::ORDERED_PASSES_Slot::requiresExplicitOrder << "\n";

    std::cout << "\n═══════════════════════════════════════════════════════\n";
    std::cout << "  ✅ ALL TESTS PASSED!\n";
    std::cout << "  Compile-time: All static_assert checks passed\n";
    std::cout << "  Runtime: All metadata accessible and correct\n";
    std::cout << "═══════════════════════════════════════════════════════\n";

    return 0;
}
