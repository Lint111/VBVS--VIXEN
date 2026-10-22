#pragma once

#include "RecipeContentHash.h"
#include "TypedCacher.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

namespace CashSystem {

/**
 * @brief Creation-info input for `RecipeContentCacher::GetOrCreate`.
 *
 * `ComputeKey` only needs `bytecode` (hashed via M1's `ComputeRecipeBytecodeHash`); `recipeId`
 * rides along so `Create`/`GetOrCreate`'s caller-side bookkeeping (`firstRecipeId`/
 * `memberRecipeIds` on a repeat hit) has the registering recipe's identity available without a
 * second lookup.
 */
struct RecipeContentCacheCreateInfo {
    uint32_t recipeId = 0;
    std::vector<Vixen::SVO::Recipe::SdfInstruction> bytecode;
};

/**
 * @brief `TypedCacher<RecipeFamilyRecord, RecipeContentCacheCreateInfo>` for Increment 1 of the
 * recipe pipeline cache.
 *
 * Device-independent (no `VkPipeline`, no GPU resource — see `RecipeFamilyRecord`'s own doc
 * comment): register via `MainCacher::RegisterCacher<...>(..., isDeviceDependent=false)` and
 * fetch via `MainCacher::GetDeviceIndependentCacher<...>` (or `GetCacher<...>` with no device
 * argument), mirroring `ComputePipelineNode`'s `RegisterCacher`/`GetCacher` pattern
 * (`ComputePipelineNode.cpp:239-253`) but on the global/device-independent path instead of the
 * per-device one.
 *
 * `TypedCacher::GetOrCreate` already does the "first registration creates the entry, every
 * subsequent identical-bytecode registration returns the SAME entry" collapsing for free once
 * `ComputeKey` is content-based — this class only supplies `ComputeKey` (M1's hash) and `Create`
 * (builds the `RecipeFamilyRecord` on first sight of a content hash). Membership tracking
 * (`memberRecipeIds` growing on a repeat hit) needs one extra step beyond bare `GetOrCreate`,
 * because `TypedCacher` returns the SAME cached `shared_ptr<RecipeFamilyRecord>` on a hit rather
 * than calling `Create` again — see `RegisterRecipe` below.
 */
class RecipeContentCacher : public TypedCacher<RecipeFamilyRecord, RecipeContentCacheCreateInfo> {
public:
    RecipeContentCacher() = default;
    ~RecipeContentCacher() override = default;

    std::string_view name() const noexcept override { return "RecipeContentCacher"; }

    /**
     * @brief Register a recipe's bytecode with the cache, returning its (possibly shared) family
     * record.
     *
     * First recipe to present a given content hash creates the family (`firstRecipeId` =
     * `recipeId`, `memberRecipeIds` = `{recipeId}`). Every later recipe presenting the SAME
     * content hash is appended to that SAME family's `memberRecipeIds` (guarded so a recipeId
     * already present — e.g. a caller retrying the same registration — is not double-added).
     * This is the "real call site proving the cache is genuinely exercised" (plan §0/Task 4): the
     * returned record's `memberRecipeIds.size()` growing past 1 is the exact-dup-collapse proof.
     *
     * Owner phase: membership is appended by the graph-build owner thread (the recipe registration
     * loops in VulkanGraphApplication), and `memberRecipeIds` is read only after registration.
     * There is no lock; a debug build asserts if two registrations ever overlap.
     */
    PtrT RegisterRecipe(uint32_t recipeId,
                         const std::vector<Vixen::SVO::Recipe::SdfInstruction>& bytecode) {
        RecipeContentCacheCreateInfo ci;
        ci.recipeId = recipeId;
        ci.bytecode = bytecode;

        PtrT record = GetOrCreate(ci);

        [[maybe_unused]] const bool wasRegistering = m_registering.exchange(true, std::memory_order_acquire);
        assert(!wasRegistering && "RecipeContentCacher::RegisterRecipe is owner-phase (single writer)");
        if (record->firstRecipeId != recipeId) {
            bool alreadyMember = false;
            for (uint32_t id : record->memberRecipeIds) {
                if (id == recipeId) { alreadyMember = true; break; }
            }
            if (!alreadyMember) {
                record->memberRecipeIds.push_back(recipeId);
            }
        }
        m_registering.store(false, std::memory_order_release);
        return record;
    }

protected:
    PtrT Create(const RecipeContentCacheCreateInfo& ci) override {
        auto record = std::make_shared<RecipeFamilyRecord>();
        record->contentHash = ComputeRecipeBytecodeHash(ci.bytecode);
        record->firstRecipeId = ci.recipeId;
        record->memberRecipeIds.push_back(ci.recipeId);
        return record;
    }

    std::uint64_t ComputeKey(const RecipeContentCacheCreateInfo& ci) const override {
        return ComputeRecipeBytecodeHash(ci.bytecode);
    }

private:
    // Debug overlap detector for the owner-phase contract on RegisterRecipe (never blocks).
    std::atomic<bool> m_registering{false};
};

} // namespace CashSystem
