/**
 * @file test_task_profile.cpp
 * @brief Unit tests for ITaskProfile interface and TaskProfileRegistry
 *
 * Sprint 6.3: Phase 3.2a - Polymorphic TaskProfile System
 *
 * Tests:
 * - ITaskProfile interface (pressure valve, cost estimation)
 * - SimpleTaskProfile concrete implementation
 * - ResolutionTaskProfile concrete implementation
 * - TaskProfileRegistry (registration, lookup, adjustment)
 * - Priority-based pressure adjustment
 * - Category operations
 * - Change notification callbacks
 * - Factory pattern for deserialization
 */

#include <gtest/gtest.h>
#include "Core/ITaskProfile.h"
#include "Core/TaskProfiles/SimpleTaskProfile.h"
#include "Core/TaskProfiles/ResolutionTaskProfile.h"
#include "Core/TaskProfileRegistry.h"

using namespace Vixen::RenderGraph;

// ============================================================================
// SIMPLE TASK PROFILE TESTS
// ============================================================================

class SimpleTaskProfileTest : public ::testing::Test {
protected:
    std::unique_ptr<SimpleTaskProfile> profile;

    void SetUp() override {
        profile = std::make_unique<SimpleTaskProfile>("testTask", "test");
        profile->SetBounds(-5, +5);
    }
};

TEST_F(SimpleTaskProfileTest, DefaultConstruction) {
    SimpleTaskProfile p;
    EXPECT_EQ(p.GetWorkUnits(), 0);       // 0 = baseline
    EXPECT_EQ(p.GetMinWorkUnits(), -5);
    EXPECT_EQ(p.GetMaxWorkUnits(), +5);
    EXPECT_EQ(p.GetPriority(), 128);
    EXPECT_FALSE(p.IsCalibrated());
    EXPECT_EQ(p.GetEstimatedCostNs(), 0);
}

TEST_F(SimpleTaskProfileTest, ConstructionWithIdentity) {
    SimpleTaskProfile p("myTask", "myCategory");
    EXPECT_EQ(p.GetTaskId(), "myTask");
    EXPECT_EQ(p.GetCategory(), "myCategory");
}

TEST_F(SimpleTaskProfileTest, PressureValveIncrease) {
    EXPECT_TRUE(profile->CanIncrease());
    EXPECT_TRUE(profile->Increase());
    EXPECT_EQ(profile->GetWorkUnits(), 1);

    // Increase to max
    profile->SetWorkUnits(5);
    EXPECT_FALSE(profile->CanIncrease());
    EXPECT_FALSE(profile->Increase());
    EXPECT_EQ(profile->GetWorkUnits(), 5);  // Unchanged
}

TEST_F(SimpleTaskProfileTest, PressureValveDecrease) {
    EXPECT_TRUE(profile->CanDecrease());
    EXPECT_TRUE(profile->Decrease());
    EXPECT_EQ(profile->GetWorkUnits(), -1);

    // Decrease to min
    profile->SetWorkUnits(-5);
    EXPECT_FALSE(profile->CanDecrease());
    EXPECT_FALSE(profile->Decrease());
    EXPECT_EQ(profile->GetWorkUnits(), -5);  // Unchanged
}

TEST_F(SimpleTaskProfileTest, GetPressure) {
    profile->SetWorkUnits(0);
    EXPECT_FLOAT_EQ(profile->GetPressure(), 0.0f);  // At baseline

    profile->SetWorkUnits(5);
    EXPECT_FLOAT_EQ(profile->GetPressure(), 1.0f);  // At max

    profile->SetWorkUnits(-5);
    EXPECT_FLOAT_EQ(profile->GetPressure(), -1.0f);  // At min

    profile->SetWorkUnits(2);  // 40% of max
    EXPECT_FLOAT_EQ(profile->GetPressure(), 0.4f);

    profile->SetWorkUnits(-3);  // 60% of min (toward -1)
    EXPECT_FLOAT_EQ(profile->GetPressure(), -0.6f);
}

TEST_F(SimpleTaskProfileTest, SetWorkUnitsClamped) {
    profile->SetWorkUnits(10);   // Above max
    EXPECT_EQ(profile->GetWorkUnits(), 5);

    profile->SetWorkUnits(-10);  // Below min
    EXPECT_EQ(profile->GetWorkUnits(), -5);

    profile->SetWorkUnits(3);    // Valid
    EXPECT_EQ(profile->GetWorkUnits(), 3);

    profile->SetWorkUnits(-2);   // Valid negative
    EXPECT_EQ(profile->GetWorkUnits(), -2);
}

TEST_F(SimpleTaskProfileTest, FirstMeasurementCalibration) {
    EXPECT_FALSE(profile->IsCalibrated());
    EXPECT_EQ(profile->GetEstimatedCostNs(), 0);

    // Record first measurement at baseline (0 units)
    profile->RecordMeasurement(2'500'000);  // 2.5ms

    EXPECT_TRUE(profile->IsCalibrated());
    EXPECT_EQ(profile->GetSampleCount(), 1);
    // Cost at baseline should be 2.5ms
    EXPECT_EQ(profile->GetBaselineCostNs(), 2'500'000);
    EXPECT_EQ(profile->GetEstimatedCostNs(), 2'500'000);
}

TEST_F(SimpleTaskProfileTest, CostEstimationWithCostPerUnit) {
    // Calibrate baseline
    profile->RecordMeasurement(2'000'000);  // 2ms at baseline
    EXPECT_EQ(profile->GetBaselineCostNs(), 2'000'000);

    // Manually set costPerUnit (normally learned from measurements)
    profile->SetCostPerUnitNs(500'000);  // 0.5ms per unit

    // At baseline (workUnits=0): cost = 2ms + 0*0.5ms = 2ms
    profile->SetWorkUnits(0);
    EXPECT_EQ(profile->GetEstimatedCostNs(), 2'000'000);

    // At +2 units: cost = 2ms + 2*0.5ms = 3ms
    profile->SetWorkUnits(2);
    EXPECT_EQ(profile->GetEstimatedCostNs(), 3'000'000);

    // At -2 units: cost = 2ms + (-2)*0.5ms = 1ms
    profile->SetWorkUnits(-2);
    EXPECT_EQ(profile->GetEstimatedCostNs(), 1'000'000);
}

TEST_F(SimpleTaskProfileTest, EMASmoothingOnBaselineMeasurements) {
    // First measurement
    profile->RecordMeasurement(2'000'000);  // 2ms
    uint64_t initial = profile->GetBaselineCostNs();
    EXPECT_EQ(initial, 2'000'000);

    // Second measurement at baseline (different value)
    profile->SetWorkUnits(0);
    profile->RecordMeasurement(4'000'000);  // 4ms

    // EMA: 2ms * 0.9 + 4ms * 0.1 = 1.8ms + 0.4ms = 2.2ms
    uint64_t expected = static_cast<uint64_t>(2'000'000 * 0.9 + 4'000'000 * 0.1);
    EXPECT_EQ(profile->GetBaselineCostNs(), expected);
}

TEST_F(SimpleTaskProfileTest, ResetCalibration) {
    profile->RecordMeasurement(2'000'000);
    EXPECT_TRUE(profile->IsCalibrated());
    EXPECT_GT(profile->GetSampleCount(), 0u);

    profile->ResetCalibration();

    EXPECT_FALSE(profile->IsCalibrated());
    EXPECT_EQ(profile->GetSampleCount(), 0u);
    EXPECT_EQ(profile->GetBaselineCostNs(), 0u);
    EXPECT_EQ(profile->GetWorkUnits(), 0);  // Reset to baseline
}

TEST_F(SimpleTaskProfileTest, HasReliableCalibration) {
    EXPECT_FALSE(profile->HasReliableCalibration());

    // Less than 10 samples
    for (int i = 0; i < 5; ++i) {
        profile->RecordMeasurement(1'000'000);
    }
    EXPECT_FALSE(profile->HasReliableCalibration());

    // 10+ samples
    for (int i = 0; i < 5; ++i) {
        profile->RecordMeasurement(1'000'000);
    }
    EXPECT_TRUE(profile->HasReliableCalibration());
}

TEST_F(SimpleTaskProfileTest, PeakMeasurement) {
    profile->RecordMeasurement(1'000'000);
    EXPECT_EQ(profile->GetPeakMeasuredCostNs(), 1'000'000);

    profile->RecordMeasurement(3'000'000);
    EXPECT_EQ(profile->GetPeakMeasuredCostNs(), 3'000'000);

    profile->RecordMeasurement(2'000'000);  // Lower than peak
    EXPECT_EQ(profile->GetPeakMeasuredCostNs(), 3'000'000);  // Peak unchanged
}

TEST_F(SimpleTaskProfileTest, TypeName) {
    EXPECT_EQ(profile->GetTypeName(), "SimpleTaskProfile");
}

TEST_F(SimpleTaskProfileTest, StateDescription) {
    profile->RecordMeasurement(2'000'000);  // 2ms
    std::string desc = profile->GetStateDescription();
    EXPECT_NE(desc.find("testTask"), std::string::npos);
    EXPECT_NE(desc.find("calibrated"), std::string::npos);
}

TEST_F(SimpleTaskProfileTest, SaveLoadState) {
    profile->RecordMeasurement(2'000'000);
    profile->SetCostPerUnitNs(500'000);
    profile->SetWorkUnits(3);

    nlohmann::json j;
    profile->SaveState(j);

    // Create new profile and load
    SimpleTaskProfile loaded;
    loaded.LoadState(j);

    EXPECT_EQ(loaded.GetTaskId(), "testTask");
    EXPECT_EQ(loaded.GetCategory(), "test");
    EXPECT_EQ(loaded.GetWorkUnits(), 3);
    EXPECT_EQ(loaded.GetBaselineCostNs(), 2'000'000);
    EXPECT_EQ(loaded.GetCostPerUnitNs(), 500'000);
    EXPECT_TRUE(loaded.IsCalibrated());
}

// ============================================================================
// RESOLUTION TASK PROFILE TESTS
// ============================================================================

class ResolutionTaskProfileTest : public ::testing::Test {
protected:
    std::unique_ptr<ResolutionTaskProfile> profile;
    std::array<uint32_t, 11> resolutions;

    void SetUp() override {
        // Resolution table: 128 at -5 up to 4096 at +5
        resolutions = {128, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 4096};
        profile = std::make_unique<ResolutionTaskProfile>("shadowMap", "shadow", resolutions);
    }
};

TEST_F(ResolutionTaskProfileTest, Construction) {
    EXPECT_EQ(profile->GetTaskId(), "shadowMap");
    EXPECT_EQ(profile->GetCategory(), "shadow");
    EXPECT_EQ(profile->GetWorkUnitType(), WorkUnitType::Resolution);
    EXPECT_EQ(profile->GetWorkUnits(), 0);  // Baseline
    EXPECT_EQ(profile->GetResolution(), 1024);  // Index 5 = workUnits 0
}

TEST_F(ResolutionTaskProfileTest, ResolutionChangesWithWorkUnits) {
    profile->SetWorkUnits(-5);
    EXPECT_EQ(profile->GetResolution(), 128);

    profile->SetWorkUnits(0);
    EXPECT_EQ(profile->GetResolution(), 1024);

    profile->SetWorkUnits(+5);
    EXPECT_EQ(profile->GetResolution(), 4096);

    profile->SetWorkUnits(+2);  // Index 7
    EXPECT_EQ(profile->GetResolution(), 2048);
}

TEST_F(ResolutionTaskProfileTest, GetResolutionAtLevel) {
    EXPECT_EQ(profile->GetResolutionAtLevel(-5), 128);
    EXPECT_EQ(profile->GetResolutionAtLevel(0), 1024);
    EXPECT_EQ(profile->GetResolutionAtLevel(+5), 4096);
}

TEST_F(ResolutionTaskProfileTest, QuadraticCostEstimation) {
    // Record baseline cost at 1024x1024
    profile->SetWorkUnits(0);
    profile->RecordMeasurement(1'000'000);  // 1ms at 1024

    // At 2048 resolution (workUnits=+2), cost should scale by (2048/1024)^2 = 4x
    profile->SetWorkUnits(2);
    uint64_t estimated = profile->GetEstimatedCostNs();
    // 2048/1024 = 2, 2^2 = 4, so ~4ms expected
    // Allow some tolerance due to measurement storage
    EXPECT_GT(estimated, 3'500'000);
    EXPECT_LT(estimated, 4'500'000);
}

TEST_F(ResolutionTaskProfileTest, PerLevelMeasuredCosts) {
    // Record measurements at specific levels
    profile->SetWorkUnits(0);
    profile->RecordMeasurement(1'000'000);

    profile->SetWorkUnits(-2);
    profile->RecordMeasurement(300'000);  // Measured at lower resolution

    // Now the measured cost at level -2 should be used
    EXPECT_EQ(profile->GetMeasuredCostAtLevel(-2), 300'000);
    EXPECT_EQ(profile->GetCalibratedLevelCount(), 2);
}

TEST_F(ResolutionTaskProfileTest, TypeName) {
    EXPECT_EQ(profile->GetTypeName(), "ResolutionTaskProfile");
}

TEST_F(ResolutionTaskProfileTest, SaveLoadState) {
    profile->SetWorkUnits(2);
    profile->RecordMeasurement(4'000'000);  // At resolution 2048

    nlohmann::json j;
    profile->SaveState(j);

    // Verify JSON has resolution-specific data
    EXPECT_TRUE(j.contains("resolutions"));
    EXPECT_TRUE(j.contains("measuredCostsPerLevel"));
    EXPECT_TRUE(j.contains("currentResolution"));

    // Load into new profile
    ResolutionTaskProfile loaded;
    loaded.LoadState(j);

    EXPECT_EQ(loaded.GetTaskId(), "shadowMap");
    EXPECT_EQ(loaded.GetCategory(), "shadow");
    EXPECT_EQ(loaded.GetWorkUnits(), 2);
    EXPECT_EQ(loaded.GetResolution(), 2048);
}

// ============================================================================
// TASK PROFILE REGISTRY TESTS
// ============================================================================

class TaskProfileRegistryTest : public ::testing::Test {
protected:
    TaskProfileRegistry registry;

    std::unique_ptr<SimpleTaskProfile> CreateSimpleProfile(
        const std::string& id, uint8_t priority,
        int32_t workUnits = 0, int32_t min = -5, int32_t max = +5) {
        auto p = std::make_unique<SimpleTaskProfile>(id, "");
        p->SetPriority(priority);
        p->SetBounds(min, max);
        p->SetWorkUnits(workUnits);
        return p;
    }

    void SetUp() override {
        // Register factories for polymorphic deserialization
        registry.RegisterFactory("SimpleTaskProfile", []() {
            return std::make_unique<SimpleTaskProfile>();
        });
        registry.RegisterFactory("ResolutionTaskProfile", []() {
            return std::make_unique<ResolutionTaskProfile>();
        });
    }
};

TEST_F(TaskProfileRegistryTest, RegisterAndRetrieve) {
    auto profile = CreateSimpleProfile("task1", 100);
    registry.RegisterTask(std::move(profile));

    EXPECT_TRUE(registry.HasTask("task1"));
    EXPECT_FALSE(registry.HasTask("task2"));
    EXPECT_EQ(registry.GetTaskCount(), 1u);

    auto* retrieved = registry.GetProfile("task1");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->GetTaskId(), "task1");
    EXPECT_EQ(retrieved->GetPriority(), 100);
}

TEST_F(TaskProfileRegistryTest, UnregisterTask) {
    registry.RegisterTask(CreateSimpleProfile("task1", 100));
    EXPECT_TRUE(registry.HasTask("task1"));

    registry.UnregisterTask("task1");
    EXPECT_FALSE(registry.HasTask("task1"));
}

TEST_F(TaskProfileRegistryTest, GetTaskIds) {
    registry.RegisterTask(CreateSimpleProfile("task1", 100));
    registry.RegisterTask(CreateSimpleProfile("task2", 200));
    registry.RegisterTask(CreateSimpleProfile("task3", 50));

    auto ids = registry.GetTaskIds();
    EXPECT_EQ(ids.size(), 3u);

    // Check all IDs are present (order may vary)
    EXPECT_NE(std::find(ids.begin(), ids.end(), "task1"), ids.end());
    EXPECT_NE(std::find(ids.begin(), ids.end(), "task2"), ids.end());
    EXPECT_NE(std::find(ids.begin(), ids.end(), "task3"), ids.end());
}

TEST_F(TaskProfileRegistryTest, RecordMeasurement) {
    registry.RegisterTask(CreateSimpleProfile("task1", 100));

    EXPECT_TRUE(registry.RecordMeasurement("task1", 1'000'000));
    EXPECT_FALSE(registry.RecordMeasurement("nonexistent", 1'000'000));

    auto* profile = registry.GetProfile("task1");
    EXPECT_TRUE(profile->IsCalibrated());
}

TEST_F(TaskProfileRegistryTest, DecreaseLowestPriority) {
    // Register tasks with different priorities (all at baseline 0)
    registry.RegisterTask(CreateSimpleProfile("low", 50, 0));    // Lowest priority
    registry.RegisterTask(CreateSimpleProfile("mid", 100, 0));
    registry.RegisterTask(CreateSimpleProfile("high", 200, 0));

    // Should decrease lowest priority task
    std::string adjusted = registry.DecreaseLowestPriority();
    EXPECT_EQ(adjusted, "low");
    EXPECT_EQ(registry.GetProfile("low")->GetWorkUnits(), -1);
    EXPECT_EQ(registry.GetProfile("mid")->GetWorkUnits(), 0);  // Unchanged
    EXPECT_EQ(registry.GetProfile("high")->GetWorkUnits(), 0); // Unchanged
}

TEST_F(TaskProfileRegistryTest, DecreaseLowestPrioritySkipsAtMin) {
    // Register low priority at min, mid priority can decrease
    registry.RegisterTask(CreateSimpleProfile("low", 50, -5, -5, +5));   // At min
    registry.RegisterTask(CreateSimpleProfile("mid", 100, 0, -5, +5));

    // Should skip "low" (at min) and decrease "mid"
    std::string adjusted = registry.DecreaseLowestPriority();
    EXPECT_EQ(adjusted, "mid");
    EXPECT_EQ(registry.GetProfile("mid")->GetWorkUnits(), -1);
}

TEST_F(TaskProfileRegistryTest, IncreaseHighestPriority) {
    registry.RegisterTask(CreateSimpleProfile("low", 50, 0));
    registry.RegisterTask(CreateSimpleProfile("mid", 100, 0));
    registry.RegisterTask(CreateSimpleProfile("high", 200, 0));

    // Should increase highest priority task
    std::string adjusted = registry.IncreaseHighestPriority();
    EXPECT_EQ(adjusted, "high");
    EXPECT_EQ(registry.GetProfile("high")->GetWorkUnits(), 1);
    EXPECT_EQ(registry.GetProfile("mid")->GetWorkUnits(), 0);  // Unchanged
    EXPECT_EQ(registry.GetProfile("low")->GetWorkUnits(), 0);  // Unchanged
}

TEST_F(TaskProfileRegistryTest, IncreaseHighestPrioritySkipsAtMax) {
    registry.RegisterTask(CreateSimpleProfile("high", 200, +5, -5, +5));  // At max
    registry.RegisterTask(CreateSimpleProfile("mid", 100, 0, -5, +5));

    // Should skip "high" (at max) and increase "mid"
    std::string adjusted = registry.IncreaseHighestPriority();
    EXPECT_EQ(adjusted, "mid");
    EXPECT_EQ(registry.GetProfile("mid")->GetWorkUnits(), 1);
}

TEST_F(TaskProfileRegistryTest, ApplyPressureOverBudget) {
    registry.RegisterTask(CreateSimpleProfile("task1", 50, 0));
    registry.RegisterTask(CreateSimpleProfile("task2", 100, 0));

    // Over budget (110% > 90% target)
    uint32_t adjusted = registry.ApplyPressure(1.10f, 0.9f);
    EXPECT_EQ(adjusted, 1u);
    EXPECT_EQ(registry.GetProfile("task1")->GetWorkUnits(), -1);  // Decreased
}

TEST_F(TaskProfileRegistryTest, ApplyPressureUnderBudget) {
    registry.RegisterTask(CreateSimpleProfile("task1", 50, 0));
    registry.RegisterTask(CreateSimpleProfile("task2", 100, 0));

    // Under budget (70% < 90% target)
    uint32_t adjusted = registry.ApplyPressure(0.70f, 0.9f);
    EXPECT_EQ(adjusted, 1u);
    EXPECT_EQ(registry.GetProfile("task2")->GetWorkUnits(), 1);  // Increased (higher priority)
}

TEST_F(TaskProfileRegistryTest, ApplyPressureWithinDeadband) {
    registry.RegisterTask(CreateSimpleProfile("task1", 100, 0));

    // Within deadband (88% is within ±5% of 90%)
    uint32_t adjusted = registry.ApplyPressure(0.88f, 0.9f);
    EXPECT_EQ(adjusted, 0u);
    EXPECT_EQ(registry.GetProfile("task1")->GetWorkUnits(), 0);  // Unchanged
}

TEST_F(TaskProfileRegistryTest, CategoryOperations) {
    auto p1 = CreateSimpleProfile("shadow1", 50, 0);
    p1->SetCategory("shadow");
    auto p2 = CreateSimpleProfile("shadow2", 60, 0);
    p2->SetCategory("shadow");
    auto p3 = CreateSimpleProfile("postProcess", 100, 0);
    p3->SetCategory("post");

    registry.RegisterTask(std::move(p1));
    registry.RegisterTask(std::move(p2));
    registry.RegisterTask(std::move(p3));

    // Get by category
    auto shadows = registry.GetTasksByCategory("shadow");
    EXPECT_EQ(shadows.size(), 2u);

    // Decrease category
    uint32_t decreased = registry.DecreaseCategoryWorkUnits("shadow");
    EXPECT_EQ(decreased, 2u);
    EXPECT_EQ(registry.GetProfile("shadow1")->GetWorkUnits(), -1);
    EXPECT_EQ(registry.GetProfile("shadow2")->GetWorkUnits(), -1);
    EXPECT_EQ(registry.GetProfile("postProcess")->GetWorkUnits(), 0);  // Unchanged
}

TEST_F(TaskProfileRegistryTest, SetCategoryPriority) {
    auto p1 = CreateSimpleProfile("shadow1", 50);
    p1->SetCategory("shadow");
    auto p2 = CreateSimpleProfile("shadow2", 60);
    p2->SetCategory("shadow");

    registry.RegisterTask(std::move(p1));
    registry.RegisterTask(std::move(p2));

    registry.SetCategoryPriority("shadow", 200);

    EXPECT_EQ(registry.GetProfile("shadow1")->GetPriority(), 200);
    EXPECT_EQ(registry.GetProfile("shadow2")->GetPriority(), 200);
}

TEST_F(TaskProfileRegistryTest, ChangeCallback) {
    int callbackCount = 0;
    std::string lastTaskId;
    int32_t lastOldUnits = 0;
    int32_t lastNewUnits = 0;

    registry.SetChangeCallback([&](const std::string& taskId, int32_t oldUnits, int32_t newUnits) {
        ++callbackCount;
        lastTaskId = taskId;
        lastOldUnits = oldUnits;
        lastNewUnits = newUnits;
    });

    registry.RegisterTask(CreateSimpleProfile("task1", 100, 0));

    registry.DecreaseLowestPriority();

    EXPECT_EQ(callbackCount, 1);
    EXPECT_EQ(lastTaskId, "task1");
    EXPECT_EQ(lastOldUnits, 0);
    EXPECT_EQ(lastNewUnits, -1);  // Decreased from baseline
}

TEST_F(TaskProfileRegistryTest, Statistics) {
    registry.RegisterTask(CreateSimpleProfile("task1", 100));
    registry.RegisterTask(CreateSimpleProfile("task2", 200));

    // Record measurements
    registry.RecordMeasurement("task1", 1'000'000);  // 1ms
    registry.RecordMeasurement("task2", 2'000'000);  // 2ms

    EXPECT_EQ(registry.GetCalibratedCount(), 2u);
    EXPECT_EQ(registry.GetTotalEstimatedCostNs(), 3'000'000u);
}

TEST_F(TaskProfileRegistryTest, ClearRegistry) {
    registry.RegisterTask(CreateSimpleProfile("task1", 100));
    registry.RegisterTask(CreateSimpleProfile("task2", 200));
    EXPECT_EQ(registry.GetTaskCount(), 2u);

    registry.Clear();
    EXPECT_EQ(registry.GetTaskCount(), 0u);
}

TEST_F(TaskProfileRegistryTest, SaveLoadState) {
    // Register and calibrate a profile
    auto profile = std::make_unique<SimpleTaskProfile>("task1", "test");
    profile->SetPriority(100);
    profile->SetWorkUnits(2);
    profile->RecordMeasurement(1'500'000);
    registry.RegisterTask(std::move(profile));

    // Save state
    nlohmann::json savedState;
    registry.SaveState(savedState);

    // Clear and reload
    registry.Clear();
    EXPECT_EQ(registry.GetTaskCount(), 0u);

    size_t loaded = registry.LoadState(savedState);
    EXPECT_EQ(loaded, 1u);

    // Verify loaded profile
    auto* loadedProfile = registry.GetProfile("task1");
    ASSERT_NE(loadedProfile, nullptr);
    EXPECT_EQ(loadedProfile->GetTaskId(), "task1");
    EXPECT_EQ(loadedProfile->GetWorkUnits(), 2);
    EXPECT_TRUE(loadedProfile->IsCalibrated());
}

TEST_F(TaskProfileRegistryTest, PolymorphicSaveLoad) {
    // Register a ResolutionTaskProfile
    std::array<uint32_t, 11> resolutions = {128, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 4096};
    auto resProfile = std::make_unique<ResolutionTaskProfile>("shadowMap", "shadow", resolutions);
    resProfile->SetWorkUnits(2);
    resProfile->RecordMeasurement(4'000'000);
    registry.RegisterTask(std::move(resProfile));

    // Save state
    nlohmann::json savedState;
    registry.SaveState(savedState);

    // Clear and reload
    registry.Clear();
    size_t loaded = registry.LoadState(savedState);
    EXPECT_EQ(loaded, 1u);

    // Verify loaded as correct type
    auto* loadedProfile = registry.GetProfile("shadowMap");
    ASSERT_NE(loadedProfile, nullptr);
    EXPECT_EQ(loadedProfile->GetTypeName(), "ResolutionTaskProfile");

    // Cast to ResolutionTaskProfile and verify
    auto* resLoaded = dynamic_cast<ResolutionTaskProfile*>(loadedProfile);
    ASSERT_NE(resLoaded, nullptr);
    EXPECT_EQ(resLoaded->GetResolution(), 2048);  // workUnits=2 → resolution 2048
}

TEST_F(TaskProfileRegistryTest, FactoryRegistration) {
    EXPECT_TRUE(registry.HasFactory("SimpleTaskProfile"));
    EXPECT_TRUE(registry.HasFactory("ResolutionTaskProfile"));
    EXPECT_FALSE(registry.HasFactory("NonExistentProfile"));
}

// ============================================================================
// WORK UNIT TYPE TESTS
// ============================================================================

TEST(WorkUnitTypeTest, StringConversion) {
    EXPECT_STREQ(WorkUnitTypeToString(WorkUnitType::BatchSize), "BatchSize");
    EXPECT_STREQ(WorkUnitTypeToString(WorkUnitType::Resolution), "Resolution");
    EXPECT_STREQ(WorkUnitTypeToString(WorkUnitType::ThreadCount), "ThreadCount");
    EXPECT_STREQ(WorkUnitTypeToString(WorkUnitType::IterationLimit), "IterationLimit");
    EXPECT_STREQ(WorkUnitTypeToString(WorkUnitType::LODLevel), "LODLevel");
    EXPECT_STREQ(WorkUnitTypeToString(WorkUnitType::Custom), "Custom");
}

TEST(WorkUnitTypeTest, FromString) {
    EXPECT_EQ(WorkUnitTypeFromString("BatchSize"), WorkUnitType::BatchSize);
    EXPECT_EQ(WorkUnitTypeFromString("Resolution"), WorkUnitType::Resolution);
    EXPECT_EQ(WorkUnitTypeFromString("ThreadCount"), WorkUnitType::ThreadCount);
    EXPECT_EQ(WorkUnitTypeFromString("IterationLimit"), WorkUnitType::IterationLimit);
    EXPECT_EQ(WorkUnitTypeFromString("LODLevel"), WorkUnitType::LODLevel);
    EXPECT_EQ(WorkUnitTypeFromString("Custom"), WorkUnitType::Custom);
    EXPECT_EQ(WorkUnitTypeFromString("Unknown"), WorkUnitType::Custom);  // Default
}

// ============================================================================
// CALIBRATION STORE TESTS
// ============================================================================

#include "Core/CalibrationStore.h"
#include <filesystem>

class CalibrationStoreTest : public ::testing::Test {
protected:
    std::filesystem::path testDir;
    std::unique_ptr<CalibrationStore> store;
    TaskProfileRegistry registry;

    void SetUp() override {
        // Create temp directory for tests
        testDir = std::filesystem::temp_directory_path() / "vixen_calibration_test";
        std::filesystem::create_directories(testDir);

        store = std::make_unique<CalibrationStore>(testDir);
        store->SetGPU({"Test_GPU", 1234, 5678});

        // Register factories
        registry.RegisterFactory("SimpleTaskProfile", []() {
            return std::make_unique<SimpleTaskProfile>();
        });
        registry.RegisterFactory("ResolutionTaskProfile", []() {
            return std::make_unique<ResolutionTaskProfile>();
        });
    }

    void TearDown() override {
        // Clean up test directory
        std::filesystem::remove_all(testDir);
    }
};

TEST_F(CalibrationStoreTest, GPUIdentifierFilename) {
    GPUIdentifier gpu{"NVIDIA GeForce RTX 3080", 4318, 8710};
    std::string filename = gpu.ToFilename();

    // Should replace spaces with underscores
    EXPECT_EQ(filename.find(' '), std::string::npos);
    // Should contain vendor/device IDs
    EXPECT_NE(filename.find("4318"), std::string::npos);
    EXPECT_NE(filename.find("8710"), std::string::npos);
}

TEST_F(CalibrationStoreTest, SaveAndLoad) {
    // Add calibrated profile
    auto profile = std::make_unique<SimpleTaskProfile>("task1", "test");
    profile->RecordMeasurement(1'000'000);
    profile->SetWorkUnits(2);
    registry.RegisterTask(std::move(profile));

    // Save
    auto saveResult = store->Save(registry);
    EXPECT_TRUE(saveResult.success) << saveResult.message;
    EXPECT_EQ(saveResult.profileCount, 1u);

    // Verify file exists
    EXPECT_TRUE(store->Exists());

    // Clear registry
    registry.Clear();
    EXPECT_EQ(registry.GetTaskCount(), 0u);

    // Load
    auto loadResult = store->Load(registry);
    EXPECT_TRUE(loadResult.success) << loadResult.message;
    EXPECT_EQ(loadResult.profileCount, 1u);

    // Verify loaded data
    auto* loaded = registry.GetProfile("task1");
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->GetWorkUnits(), 2);
    EXPECT_TRUE(loaded->IsCalibrated());
}

TEST_F(CalibrationStoreTest, LoadNonExistent) {
    // Delete any existing file
    store->Delete();
    EXPECT_FALSE(store->Exists());

    // Load should succeed with 0 profiles
    auto result = store->Load(registry);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.profileCount, 0u);
}

TEST_F(CalibrationStoreTest, DeleteFile) {
    // Create a file
    auto profile = std::make_unique<SimpleTaskProfile>("task1", "test");
    registry.RegisterTask(std::move(profile));
    store->Save(registry);
    EXPECT_TRUE(store->Exists());

    // Delete
    EXPECT_TRUE(store->Delete());
    EXPECT_FALSE(store->Exists());

    // Delete again (already gone)
    EXPECT_TRUE(store->Delete());
}

TEST_F(CalibrationStoreTest, PolymorphicPersistence) {
    // Add ResolutionTaskProfile
    std::array<uint32_t, 11> resolutions = {128, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 4096};
    auto resProfile = std::make_unique<ResolutionTaskProfile>("shadowMap", "shadow", resolutions);
    resProfile->SetWorkUnits(3);
    resProfile->RecordMeasurement(2'000'000);
    registry.RegisterTask(std::move(resProfile));

    // Save
    store->Save(registry);

    // Clear and reload
    registry.Clear();
    store->Load(registry);

    // Verify correct type restored
    auto* loaded = registry.GetProfile("shadowMap");
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->GetTypeName(), "ResolutionTaskProfile");

    // Cast and verify resolution
    auto* resLoaded = dynamic_cast<ResolutionTaskProfile*>(loaded);
    ASSERT_NE(resLoaded, nullptr);
    EXPECT_EQ(resLoaded->GetWorkUnits(), 3);
    EXPECT_EQ(resLoaded->GetResolution(), 3072);  // workUnits=3 → index 8 → 3072
}

TEST_F(CalibrationStoreTest, ListCalibrationFiles) {
    // Save with current GPU
    auto profile1 = std::make_unique<SimpleTaskProfile>("task1", "test");
    registry.RegisterTask(std::move(profile1));
    store->Save(registry);

    // Save with different GPU
    store->SetGPU({"Other_GPU", 9999, 1111});
    auto profile2 = std::make_unique<SimpleTaskProfile>("task2", "test");
    registry.RegisterTask(std::move(profile2));
    store->Save(registry);

    // List files
    auto files = store->ListCalibrationFiles();
    EXPECT_EQ(files.size(), 2u);
}

TEST_F(CalibrationStoreTest, GetFilePath) {
    auto path = store->GetFilePath();
    EXPECT_EQ(path.extension(), ".json");
    EXPECT_TRUE(path.string().find("Test_GPU") != std::string::npos);
}
