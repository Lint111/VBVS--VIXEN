#pragma once
/**
 * @file RecipeBootIngest.h
 * @brief Spec B I3/Task 6 — parse the host's packed recipe-blob buffer, register every blob into a
 *        RecipeRegistry, and bake to a pool. Boot-time only; fail-loud (the caller aborts on !ok).
 *
 * Wire shape (from HostAbi.PackRecipeBlobs / ut_recipe_blob_buffer):
 *   [count u32][ (recipeId u32, len u32, bytes[len]) ... ]   little-endian, host-native (same process).
 *
 * Reuses IngestBlob (RecipeIngest.h, parse-from-memory) + BakeRegistryToPool (RecipeBaker.h) verbatim —
 * this header only walks the buffer and turns per-blob IngestResult into one fail-loud outcome.
 */
#include <cstdint>
#include <cstring>
#include <string>

#include "Recipe/RecipeIngest.h"
#include "Recipe/RecipeBaker.h"

namespace Vixen::SVO {

struct RecipeBootIngestResult {
    RecipeRegistry     registry;
    RecipeBakeResult   bake;
    bool               ok = true;
    std::string        err;
};

// ParseAndBakeRecipeBlobBuffer — walk the packed buffer, IngestBlob each entry in the order the
// buffer lists them (the C# side writes the pack's own insertion order, already recipeId-ascending
// per Spec B's determinism note), then BakeRegistryToPool. Fails loud on a truncated buffer, a bad
// blob (BadContainer), or a registry rejection (RegistryRejected) — names the offending recipeId.
inline RecipeBootIngestResult ParseAndBakeRecipeBlobBuffer(const uint8_t* buf, int32_t len,
                                                            const RecipeBakeConfig& cfg = {}) {
    RecipeBootIngestResult res;
    if (buf == nullptr || len < 4) {
        res.ok = false; res.err = "recipe blob buffer truncated (missing count)";
        return res;
    }

    uint32_t count = 0;
    std::memcpy(&count, buf, sizeof(count));
    size_t offset = sizeof(count);
    const size_t total = static_cast<size_t>(len);

    for (uint32_t i = 0; i < count; ++i) {
        if (offset + 8 > total) {
            res.ok = false; res.err = "recipe blob buffer truncated (entry " + std::to_string(i) + " header)";
            return res;
        }
        uint32_t recipeId = 0, blobLen = 0;
        std::memcpy(&recipeId, buf + offset, sizeof(recipeId)); offset += sizeof(recipeId);
        std::memcpy(&blobLen,  buf + offset, sizeof(blobLen));  offset += sizeof(blobLen);
        if (offset + blobLen > total) {
            res.ok = false; res.err = "recipe blob buffer truncated (recipeId " + std::to_string(recipeId) + " bytes)";
            return res;
        }

        const IngestResult ir = IngestBlob(recipeId, buf + offset, blobLen, res.registry);
        offset += blobLen;
        if (ir != IngestResult::Ok) {
            res.ok = false;
            res.err = "recipe " + std::to_string(recipeId) + ": "
                     + (ir == IngestResult::BadContainer ? "bad VRC1 container" : "registry rejected");
            return res;
        }
    }

    res.bake = BakeRegistryToPool(res.registry, cfg);
    if (!res.bake.ok) { res.ok = false; res.err = res.bake.err; }
    return res;
}

} // namespace Vixen::SVO
