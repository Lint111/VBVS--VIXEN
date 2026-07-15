#pragma once

#include "RecipeContentHash.h"
#include "TypedCacher.h"

#include <cstdint>
#include <memory>
#include <mutex>
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
     */
    PtrT RegisterRecipe(uint32_t recipeId,
                         const std::vector<Vixen::SVO::Recipe::SdfInstruction>& bytecode) {
        RecipeContentCacheCreateInfo ci;
        ci.recipeId = recipeId;
        ci.bytecode = bytecode;

        PtrT record = GetOrCreate(ci);

        std::lock_guard lock(m_memberLock);
        if (record->firstRecipeId != recipeId) {
            bool alreadyMember = false;
            for (uint32_t id : record->memberRecipeIds) {
                if (id == recipeId) { alreadyMember = true; break; }
            }
            if (!alreadyMember) {
                record->memberRecipeIds.push_back(recipeId);
            }
        }
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
    // Guards mutation of an already-cached record's memberRecipeIds (TypedCacher's own m_lock is
    // private to the base and does not cover post-GetOrCreate mutation of the returned resource).
    std::mutex m_memberLock;
};

} // namespace CashSystem
