#pragma once

#include "Generated/InstanceBound.g.h"
#include "Recipe/RecipeRegistry.h"
#include "ShellOctreeGpu.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>

namespace Vixen::RenderGraph {

// GB1 (R857): the world-space bound sphere the tile scatter bins for one body. It must
// enclose everything the primary march (TraceWorld.glsl) can hit for that instance,
// otherwise the march never visits the instance in the tiles it covers:
//   - registry recipes (procedural, recipeId >= 2) are traced inside
//     getRecipeBoundSphere(recipeId), whose centre/radius UberShaderSplice.h bakes from
//     RecipeEntry::boundCenter/boundRadius, in instance-local space;
//   - legacy procedural recipes 0/1 (traceProceduralBody) are traced inside a sphere of
//     radius params.x (+ |params.y| for the displaced sphere) + 0.01 at the local origin;
//   - stored ESVO bodies occupy the octree's [0,1]^3 grid scaled to the fixed 10-unit
//     instance-local span (kWorldGridSize, BodyOctreeSceneNode.cpp).
struct RecipeBoundSphere {
    glm::vec3 center{0.0f};
    float radius = 0.0f;
};
using RecipeBoundSpheres = std::unordered_map<uint32_t, RecipeBoundSphere>;

// The bound spheres SpliceProceduralRecipesIntoSource bakes into getRecipeBoundSphere.
inline RecipeBoundSpheres CollectRecipeBoundSpheres(const Vixen::SVO::RecipeRegistry& registry) {
    RecipeBoundSpheres spheres;
    for (uint32_t id : registry.Ids()) {
        if (const auto* entry = registry.Get(id))
            spheres[id] = RecipeBoundSphere{entry->boundCenter, entry->boundRadius};
    }
    return spheres;
}

inline Vixen::Codegen::Gpu::InstanceBound DeriveInstanceBound(const Vixen::SVO::BodyInstanceGpu& instance,
                                                              const RecipeBoundSpheres& recipes) {
    constexpr uint32_t kProviderProcedural = 1u;  // SceneBindings.glsl PROVIDER_PROCEDURAL
    constexpr uint32_t kFirstRegistryRecipe = 2u;  // ids 0/1 are the legacy analytic spheres
    constexpr float kStoredLocalSpan = 10.0f;      // kWorldGridSize: octree [0,1]^3 -> local units

    const auto& material = instance.material;
    RecipeBoundSphere local;
    if (material.providerKind != kProviderProcedural) {
        local.center = glm::vec3(0.5f * kStoredLocalSpan);
        local.radius = 0.5f * kStoredLocalSpan * std::sqrt(3.0f);
    } else if (material.recipeId < kFirstRegistryRecipe) {
        const float displacement = material.recipeId == 1u ? std::fabs(material.recipeParams[1]) : 0.0f;
        local.radius = material.recipeParams[0] + displacement + 0.01f;
    } else if (const auto found = recipes.find(material.recipeId); found != recipes.end()) {
        local = found->second;
    }
    // An unregistered recipe id keeps a zero radius: the shader's default case returns a
    // zero-radius bound, so the march never hits it either.

    const auto& localToWorld = instance.transform.localToWorld;
    const glm::vec3 center = Vixen::SVO::TransformPoint(localToWorld, local.center);
    Vixen::Codegen::Gpu::InstanceBound bound{};
    bound.centerX = center.x;
    bound.centerY = center.y;
    bound.centerZ = center.z;
    bound.radius = std::max(local.radius, 0.0f) * Vixen::SVO::MaximumAxisScale(localToWorld);
    return bound;
}

} // namespace Vixen::RenderGraph
