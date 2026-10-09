#include <gtest/gtest.h>
#include <bit>
#include <cstring>

#include "Recipe/RecipeTileSpecialization.h"
#include "Recipe/RecipeWholeDomainCompaction.h"
#include "Recipe/SdfRecipeEval.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using namespace Vixen::SVO::Recipe;

SdfInstruction Sphere(float x, float y, float z, float radius) {
    SdfInstruction instruction{};
    instruction.opCode = static_cast<std::uint8_t>(SdfOpCode::Sphere);
    instruction.data[0] = x;
    instruction.data[1] = y;
    instruction.data[2] = z;
    instruction.data[3] = radius;
    return instruction;
}

SdfInstruction Box(float x, float y, float z) {
    SdfInstruction instruction{};
    instruction.opCode = static_cast<std::uint8_t>(SdfOpCode::Box);
    instruction.data[0] = x;
    instruction.data[1] = y;
    instruction.data[2] = z;
    return instruction;
}

SdfInstruction Combine(SdfOpCode opcode) {
    SdfInstruction instruction{};
    instruction.opCode = static_cast<std::uint8_t>(opcode);
    return instruction;
}

const RecipeTileDomain kNearOrigin{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};

void ExpectSameField(const std::vector<SdfInstruction>& source,
    const std::vector<SdfInstruction>& specialized, const RecipeTileDomain& domain) {
    constexpr int kSteps = 9;
    for (int z = 0; z < kSteps; ++z) {
        for (int y = 0; y < kSteps; ++y) {
            for (int x = 0; x < kSteps; ++x) {
                const glm::vec3 point(
                    glm::mix(domain.minimum.x, domain.maximum.x, static_cast<float>(x) / (kSteps - 1)),
                    glm::mix(domain.minimum.y, domain.maximum.y, static_cast<float>(y) / (kSteps - 1)),
                    glm::mix(domain.minimum.z, domain.maximum.z, static_cast<float>(z) / (kSteps - 1)));
                EXPECT_EQ(std::bit_cast<std::uint32_t>(evalRecipe(source.data(), static_cast<std::uint32_t>(source.size()), point)),
                    std::bit_cast<std::uint32_t>(evalRecipe(specialized.data(), static_cast<std::uint32_t>(specialized.size()), point)))
                    << "at " << point.x << "," << point.y << "," << point.z;
            }
        }
    }
}

void ExpectSameInstructions(const std::vector<SdfInstruction>& a,
    const std::vector<SdfInstruction>& b) {
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].opCode, b[i].opCode) << "instruction " << i;
        EXPECT_EQ(std::memcmp(a[i].data, b[i].data, sizeof(a[i].data)), 0)
            << "instruction " << i;
    }
}

RecipeWholeDomainCompactionRequest DeclaredWholeDomainRequest(
    const RecipeTileDomain& domain, std::uint64_t sourceRevision = 7) {
    RecipeWholeDomainCompactionRequest request;
    request.sourceRevision = sourceRevision;
    request.domain = domain;
    request.domainIsEnforced = true;
    request.requiredOutputChannels = kAllRecipeOutputChannels;
    request.channelDependencies = {
        RecipeChannelDependency::GeometryWinner,
        RecipeChannelDependency::Independent,
        RecipeChannelDependency::Independent,
        RecipeChannelDependency::Independent,
        RecipeChannelDependency::Independent,
    };
    return request;
}

TEST(RecipeIntervalPruning, SphereAndBoxLeavesUseStrictHardUnionProofs) {
    const std::vector<SdfInstruction> sphereProgram = {
        Sphere(0.0f, 0.0f, 0.0f, 8.0f),
        Sphere(80.0f, 0.0f, 0.0f, 2.0f),
        Combine(SdfOpCode::Union),
    };
    const auto sphere = SpecializeRecipeTapeForTile(sphereProgram, kNearOrigin);
    ASSERT_TRUE(sphere.stats.specialized);
    ASSERT_EQ(sphere.instructions.size(), 1u);
    EXPECT_EQ(sphere.instructions.front().opCode, static_cast<std::uint8_t>(SdfOpCode::Sphere));
    EXPECT_EQ(sphere.stats.prunedInstructions, 2u);
    ExpectSameField(sphereProgram, sphere.instructions, kNearOrigin);

    const std::vector<SdfInstruction> boxProgram = {
        Box(8.0f, 8.0f, 8.0f),
        Sphere(80.0f, 0.0f, 0.0f, 2.0f),
        Combine(SdfOpCode::Union),
    };
    const auto box = SpecializeRecipeTapeForTile(boxProgram, kNearOrigin);
    ASSERT_TRUE(box.stats.specialized);
    ASSERT_EQ(box.instructions.size(), 1u);
    EXPECT_EQ(box.instructions.front().opCode, static_cast<std::uint8_t>(SdfOpCode::Box));
    ExpectSameField(boxProgram, box.instructions, kNearOrigin);
}

TEST(RecipeIntervalPruning, SubtractionOnlyDropsAProvenIrrelevantCutter) {
    const std::vector<SdfInstruction> distantCutter = {
        Sphere(0.0f, 0.0f, 0.0f, 8.0f),
        Sphere(80.0f, 0.0f, 0.0f, 2.0f),
        Combine(SdfOpCode::Subtract),
    };
    const auto result = SpecializeRecipeTapeForTile(distantCutter, kNearOrigin);
    ASSERT_TRUE(result.stats.specialized);
    ASSERT_EQ(result.instructions.size(), 1u);
    EXPECT_EQ(result.instructions.front().opCode, static_cast<std::uint8_t>(SdfOpCode::Sphere));
    ExpectSameField(distantCutter, result.instructions, kNearOrigin);

    const std::vector<SdfInstruction> dominantCutter = {
        Sphere(0.0f, 0.0f, 0.0f, 8.0f),
        Sphere(0.0f, 0.0f, 0.0f, 8.0f),
        Combine(SdfOpCode::Subtract),
    };
    const auto retained = SpecializeRecipeTapeForTile(dominantCutter, kNearOrigin);
    EXPECT_FALSE(retained.stats.specialized);
    EXPECT_EQ(retained.instructions.size(), dominantCutter.size());
    ExpectSameField(dominantCutter, retained.instructions, kNearOrigin);
}

TEST(RecipeIntervalPruning, EqualityAndUnsupportedOpcodesRetainTheOriginalTape) {
    const std::vector<SdfInstruction> equalOperands = {
        Sphere(0.0f, 0.0f, 0.0f, 3.0f),
        Sphere(0.0f, 0.0f, 0.0f, 3.0f),
        Combine(SdfOpCode::Union),
    };
    const auto equal = SpecializeRecipeTapeForTile(equalOperands, kNearOrigin);
    EXPECT_FALSE(equal.stats.specialized);
    EXPECT_EQ(equal.instructions.size(), equalOperands.size());

    std::vector<SdfInstruction> unsupported = equalOperands;
    unsupported.back() = Combine(SdfOpCode::SmoothUnion);
    unsupported.back().data[0] = 0.5f;
    const auto fallback = SpecializeRecipeTapeForTile(unsupported, kNearOrigin);
    EXPECT_EQ(fallback.stats.fallback, RecipeTileFallback::UnsupportedOpcode);
    ASSERT_EQ(fallback.instructions.size(), unsupported.size());
    EXPECT_EQ(std::memcmp(fallback.instructions.data(), unsupported.data(),
        unsupported.size() * sizeof(SdfInstruction)), 0);
}

TEST(RecipeIntervalPruning, EditHeavyBuriedBranchesReduceOnlyTheTileTape) {
    std::vector<SdfInstruction> source;
    source.reserve(700);
    source.push_back(Sphere(0.0f, 0.0f, 0.0f, 4.5f));

    // A buried patch subtree contains overlapping patches hundreds of units from this
    // object-local tile. The proof removes the whole subtree after its parent union.
    source.push_back(Sphere(100.0f, 0.0f, 0.0f, 12.0f));
    source.push_back(Sphere(102.0f, 0.0f, 0.0f, 12.0f));
    source.push_back(Combine(SdfOpCode::Union));
    source.push_back(Combine(SdfOpCode::Union));

    // The edit history stays in `source`; each overlapping distant cut or patch is
    // considered in accepted order and removed only from the disposable tile tape.
    for (int edit = 0; edit < 160; ++edit) {
        const float offset = static_cast<float>(edit % 11) * 0.75f;
        source.push_back(Sphere(100.0f + offset, 0.0f, 0.0f, 10.0f));
        source.push_back(Combine(edit < 80 ? SdfOpCode::Subtract : SdfOpCode::Union));
    }

    const auto result = SpecializeRecipeTapeForTile(source, kNearOrigin);
    ASSERT_TRUE(result.stats.specialized);
    ASSERT_EQ(result.instructions.size(), 1u);
    EXPECT_EQ(result.stats.sourceInstructions, source.size());
    EXPECT_LT(result.stats.retainedInstructions, result.stats.sourceInstructions / 4u);
    EXPECT_GT(result.stats.intervalEvaluations, 200u);
    ExpectSameField(source, result.instructions, kNearOrigin);
}

TEST(RecipeWholeDomainCompaction, RemovesFarBranchesOnlyInsideDeclaredEnforcedDomain) {
    const std::vector<SdfInstruction> source = {
        Sphere(0.0f, 0.0f, 0.0f, 1.0f),
        Sphere(80.0f, 0.0f, 0.0f, 2.0f),
        Combine(SdfOpCode::Union),
    };
    const auto sourceBefore = source;
    const RecipeTileDomain domain{{-2.0f, -2.0f, -2.0f}, {2.0f, 2.0f, 2.0f}};
    const auto result = CompactRecipeOverWholeDomain(
        source, DeclaredWholeDomainRequest(domain, 41));

    ASSERT_EQ(result.sourceRevision, 41u);
    ASSERT_EQ(result.instructions.size(), 1u);
    EXPECT_EQ(result.stats.removedByIntervalProof, 2u);
    EXPECT_TRUE(result.stats.intervalProofApplied);
    ExpectSameInstructions(source, sourceBefore);
    ExpectSameField(source, result.instructions, domain);
}

TEST(RecipeWholeDomainCompaction, UndeclaredEmissionBlocksGlobalBranchRemoval) {
    const std::vector<SdfInstruction> source = {
        Sphere(0.0f, 0.0f, 0.0f, 1.0f),
        Sphere(80.0f, 0.0f, 0.0f, 2.0f),
        Combine(SdfOpCode::Union),
    };
    auto request = DeclaredWholeDomainRequest(
        RecipeTileDomain{{-2.0f, -2.0f, -2.0f}, {2.0f, 2.0f, 2.0f}});
    request.channelDependencies[static_cast<std::size_t>(RecipeOutputChannel::Emission)] =
        RecipeChannelDependency::Undeclared;

    const auto result = CompactRecipeOverWholeDomain(source, request);
    EXPECT_EQ(result.instructions.size(), source.size());
    EXPECT_EQ(result.stats.removedByIntervalProof, 0u);
    EXPECT_FALSE(result.stats.intervalProofApplied);
    EXPECT_EQ(result.stats.blockedChannels,
        (std::vector<RecipeOutputChannel>{RecipeOutputChannel::Emission}));
}

TEST(RecipeWholeDomainCompaction, UnenforcedBoundsBlockIntervalBranchRemoval) {
    const std::vector<SdfInstruction> source = {
        Sphere(0.0f, 0.0f, 0.0f, 1.0f),
        Sphere(80.0f, 0.0f, 0.0f, 2.0f),
        Combine(SdfOpCode::Union),
    };
    auto request = DeclaredWholeDomainRequest(
        RecipeTileDomain{{-2.0f, -2.0f, -2.0f}, {2.0f, 2.0f, 2.0f}});
    request.domainIsEnforced = false;

    const auto result = CompactRecipeOverWholeDomain(source, request);
    EXPECT_EQ(result.instructions.size(), source.size());
    EXPECT_EQ(result.stats.removedByIntervalProof, 0u);
    EXPECT_FALSE(result.stats.intervalProofApplied);
    EXPECT_TRUE(result.stats.domainNotEnforced);
}

TEST(RecipeWholeDomainCompaction, RemovesGeneratedExactNoOpsWithoutChannelAssumptions) {
    const std::vector<SdfInstruction> source = {
        Sphere(0.0f, 0.0f, 0.0f, 1.0f),
        Combine(SdfOpCode::Output),
        Combine(SdfOpCode::ComposeFloat3),
        Combine(SdfOpCode::Passthrough),
    };
    RecipeWholeDomainCompactionRequest request;
    request.domainIsEnforced = false;
    request.requiredOutputChannels = kAllRecipeOutputChannels;

    const auto result = CompactRecipeOverWholeDomain(source, request);
    ASSERT_EQ(result.instructions.size(), 1u);
    EXPECT_EQ(result.stats.removedIdentityNoOps, 3u);
    EXPECT_EQ(result.instructions.front().opCode, static_cast<std::uint8_t>(SdfOpCode::Sphere));
}

TEST(RecipeWholeDomainCompaction, DuplicateUnionRequiresMatchingWinnerMetadata) {
    const std::vector<SdfInstruction> duplicateUnion = {
        Sphere(0.0f, 0.0f, 0.0f, 1.0f),
        Sphere(0.0f, 0.0f, 0.0f, 1.0f),
        Combine(SdfOpCode::Union),
    };
    auto request = DeclaredWholeDomainRequest(kNearOrigin);
    request.channelDependencies[static_cast<std::size_t>(RecipeOutputChannel::Material)] =
        RecipeChannelDependency::GeometryWinner;
    request.channelDependencies[static_cast<std::size_t>(RecipeOutputChannel::Provenance)] =
        RecipeChannelDependency::GeometryWinner;

    const auto tieSensitive = CompactRecipeOverWholeDomain(duplicateUnion, request);
    EXPECT_EQ(tieSensitive.instructions.size(), duplicateUnion.size());
    EXPECT_EQ(tieSensitive.stats.removedIdempotentDuplicateInstructions, 0u);

    request.duplicateSelectionAndProvenanceEquivalent = true;
    const auto tieEquivalent = CompactRecipeOverWholeDomain(duplicateUnion, request);
    EXPECT_EQ(tieEquivalent.instructions.size(), 1u);
    EXPECT_EQ(tieEquivalent.stats.removedIdempotentDuplicateInstructions, 2u);
}

TEST(RecipeWholeDomainCompaction, InvalidPrimitivePreflightPreventsDuplicateRemoval) {
    const SdfInstruction invalid = Sphere(0.0f, 0.0f, 0.0f,
        std::numeric_limits<float>::quiet_NaN());
    const std::vector<SdfInstruction> source = {
        invalid,
        invalid,
        Combine(SdfOpCode::Union),
    };
    auto request = DeclaredWholeDomainRequest(kNearOrigin);
    request.duplicateSelectionAndProvenanceEquivalent = true;

    const auto result = CompactRecipeOverWholeDomain(source, request);
    ExpectSameInstructions(result.instructions, source);
    EXPECT_EQ(result.stats.intervalFallback, RecipeTileFallback::InvalidPrimitive);
    EXPECT_EQ(result.stats.removedIdempotentDuplicateInstructions, 0u);
}

TEST(RecipeWholeDomainCompaction, DoesNotCancelCutAndRefillAsAnAlgebraicPair) {
    const std::vector<SdfInstruction> cutAndRefill = {
        Sphere(0.0f, 0.0f, 0.0f, 2.0f),
        Sphere(0.0f, 0.0f, 0.0f, 1.0f),
        Combine(SdfOpCode::Subtract),
        Sphere(0.0f, 0.0f, 0.0f, 1.0f),
        Combine(SdfOpCode::Union),
    };
    auto request = DeclaredWholeDomainRequest(
        RecipeTileDomain{{-2.0f, -2.0f, -2.0f}, {2.0f, 2.0f, 2.0f}});
    request.duplicateSelectionAndProvenanceEquivalent = true;

    const auto result = CompactRecipeOverWholeDomain(cutAndRefill, request);
    EXPECT_EQ(result.instructions.size(), cutAndRefill.size());
    EXPECT_EQ(result.stats.removedByIntervalProof, 0u);
    ExpectSameField(cutAndRefill, result.instructions, request.domain);
}

TEST(RecipeWholeDomainCompaction, RejectsRemovalOfAVisibleCornerTerm) {
    const std::vector<SdfInstruction> source = {
        Sphere(0.0f, 0.0f, 0.0f, 1.0f),
        Sphere(1.1f, 0.0f, 0.0f, 0.5f),
        Combine(SdfOpCode::Union),
    };
    const RecipeTileDomain domain{{-2.0f, -2.0f, -2.0f}, {2.0f, 2.0f, 2.0f}};
    const auto compacted = CompactRecipeOverWholeDomain(
        source, DeclaredWholeDomainRequest(domain));
    const std::vector<SdfInstruction> deliberatelyWrong = {source.front()};
    const glm::vec3 visibleCorner(1.3f, 0.0f, 0.0f);
    const float sourceValue = evalRecipe(source.data(), static_cast<std::uint32_t>(source.size()), visibleCorner);
    const float wrongValue = evalRecipe(deliberatelyWrong.data(),
        static_cast<std::uint32_t>(deliberatelyWrong.size()), visibleCorner);

    EXPECT_EQ(compacted.instructions.size(), source.size());
    EXPECT_EQ(compacted.stats.removedByIntervalProof, 0u);
    EXPECT_LT(sourceValue, 0.0f);
    EXPECT_GT(wrongValue, 0.0f);
    std::cout << "[RecipeWholeDomain] rejected visible-corner removal at (1.3,0,0): source="
        << sourceValue << " wrong=" << wrongValue << " intervalRemoved=0" << std::endl;
}

} // namespace

TEST(RecipeIntervalPruning, UnknownGpuBackendRetainsTheOracleTape) {
    const std::vector<SdfInstruction> source{Sphere(0,0,0,8), Sphere(80,0,0,2), Combine(SdfOpCode::Union)};
    const auto result=SpecializeRecipeTapeForTile(source,kNearOrigin,
        Yeroket::Sdf::Generated::RecipeProofBackend::GpuUncertified);
    EXPECT_EQ(result.stats.fallback,RecipeTileFallback::BackendContract);
    ASSERT_EQ(result.instructions.size(),source.size());
    EXPECT_EQ(std::memcmp(result.instructions.data(),source.data(),source.size()*sizeof(SdfInstruction)),0);
}

TEST(RecipeWholeDomainCompaction, UncertifiedGpuDuplicatesRetainTheOperator) {
    SdfInstruction value{};
    value.opCode=static_cast<std::uint8_t>(SdfOpCode::PushParam);
    value.data[0]=std::numeric_limits<float>::denorm_min();
    const std::array<std::vector<SdfInstruction>,1> fixtures{{
        {value,value,Combine(SdfOpCode::Union)}
    }};
    for(auto backend:{Yeroket::Sdf::Generated::RecipeProofBackend::GpuDznPrecise,
        Yeroket::Sdf::Generated::RecipeProofBackend::GpuUncertified})
        for(const auto& source:fixtures) {
            auto request=DeclaredWholeDomainRequest(kNearOrigin);
            request.backend=backend;
            request.duplicateSelectionAndProvenanceEquivalent=true;
            const auto result=CompactRecipeOverWholeDomain(source,request);
            ExpectSameInstructions(source,result.instructions);
            EXPECT_EQ(result.stats.removedIdentityNoOps,0u);
            EXPECT_EQ(result.stats.removedIdempotentDuplicateInstructions,0u);
        }
}
