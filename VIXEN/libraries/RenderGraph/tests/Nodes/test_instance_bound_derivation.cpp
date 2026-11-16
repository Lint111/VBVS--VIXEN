#include <gtest/gtest.h>

#include "Nodes/InstanceBoundDerivation.h"
#include "Recipe/UberShaderSplice.h"

#include <glm/gtc/matrix_transform.hpp>

#include <bit>
#include <cmath>
#include <regex>
#include <string>

namespace {

using Vixen::RenderGraph::CollectRecipeBoundSpheres;
using Vixen::RenderGraph::DeriveInstanceBound;
using Vixen::RenderGraph::RecipeBoundSphere;
using Vixen::SVO::BodyInstanceGpu;
using Vixen::SVO::RecipeRegistry;
using Vixen::SVO::Recipe::SdfInstruction;
using Vixen::SVO::Recipe::SdfOpCode;

SdfInstruction Sphere(glm::vec3 c, float r) {
    SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(SdfOpCode::Sphere);
    in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z;
    in.data[3] = r;
    return in;
}

// The bound spheres the march actually tests, read back from the spliced GLSL.
std::unordered_map<uint32_t, RecipeBoundSphere> ShaderBoundSpheres(const RecipeRegistry& registry) {
    const std::string spliced = Vixen::SVO::Recipe::SpliceProceduralRecipesIntoSource(
        "#version 460\n// VIXEN_UBER_RECIPE_SPLICE_MARKER\nvoid main() {}\n", registry, nullptr);
    // RecipeGlslFloatLiteral writes every float as uintBitsToFloat(<binary32 word>u).
    const std::string word = R"(uintBitsToFloat\((\d+)u\))";
    const std::regex row(R"(case (\d+)u: center = vec3\()" + word + ", " + word + ", " + word +
                         R"(\); radius = )" + word + ";");
    const auto value = [](const std::ssub_match& bits) {
        return std::bit_cast<float>(static_cast<uint32_t>(std::stoul(bits.str())));
    };
    std::unordered_map<uint32_t, RecipeBoundSphere> spheres;
    for (auto it = std::sregex_iterator(spliced.begin(), spliced.end(), row); it != std::sregex_iterator(); ++it) {
        const auto& m = *it;
        spheres[static_cast<uint32_t>(std::stoul(m[1].str()))] = RecipeBoundSphere{
            glm::vec3(value(m[2]), value(m[3]), value(m[4])), value(m[5])};
    }
    return spheres;
}

BodyInstanceGpu Procedural(uint32_t recipeId, const glm::mat4& localToWorld) {
    BodyInstanceGpu instance{};
    instance.material.providerKind = 1u;
    instance.material.recipeId = recipeId;
    Vixen::SVO::SetInstanceTransform(instance, localToWorld);
    return instance;
}

// The uploaded world sphere must contain the transformed shader sphere.
void ExpectEncloses(const Vixen::Codegen::Gpu::InstanceBound& bound, const RecipeBoundSphere& local,
                    const BodyInstanceGpu& instance, const char* what) {
    const auto& l2w = instance.transform.localToWorld;
    const glm::vec3 shaderCenter = Vixen::SVO::TransformPoint(l2w, local.center);
    const float shaderRadius = local.radius * Vixen::SVO::MaximumAxisScale(l2w);
    const float reach = glm::length(shaderCenter - glm::vec3(bound.centerX, bound.centerY, bound.centerZ)) +
                        shaderRadius;
    EXPECT_LE(reach, bound.radius * (1.0f + 1e-5f) + 1e-5f) << what;
}

}  // namespace

TEST(InstanceBoundDerivation, EnclosesTheMarchBoundSphereOfEveryRegisteredRecipe) {
    RecipeRegistry registry;
    RecipeRegistry::RecipeEntry entry{};
    entry.bytecode = {Sphere({5.0f, 5.0f, 5.0f}, 4.0f)};
    entry.boundCenter = glm::vec3(5.0f, 5.0f, 5.0f);
    entry.boundRadius = 9.0f;  // the look-dev recipes' authored bound
    ASSERT_EQ(registry.Register(100u, entry), RecipeRegistry::RegisterResult::Ok);
    entry.boundCenter = glm::vec3(-3.0f, 12.5f, 0.25f);
    entry.boundRadius = 2.5f;
    ASSERT_EQ(registry.Register(101u, entry), RecipeRegistry::RegisterResult::Ok);
    entry.boundCenter = glm::vec3(0.0f);
    entry.boundRadius = 0.0f;  // "engine default" left unresolved: the shader bakes radius 0
    ASSERT_EQ(registry.Register(7u, entry), RecipeRegistry::RegisterResult::Ok);

    const auto shader = ShaderBoundSpheres(registry);
    ASSERT_EQ(shader.size(), registry.Ids().size()) << "every registered recipe has a baked bound";
    const auto uploaded = CollectRecipeBoundSpheres(registry);

    const glm::mat4 transforms[] = {
        glm::mat4(1.0f),
        glm::translate(glm::mat4(1.0f), glm::vec3(12.0f, 0.0f, -36.0f)),
        glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(-4.0f, 2.0f, 7.0f)),
                               0.7f, glm::normalize(glm::vec3(1.0f, 2.0f, -0.5f))),
                   glm::vec3(0.5f, 2.0f, 1.25f)),
    };
    for (const auto& [id, sphere] : shader) {
        for (const glm::mat4& transform : transforms) {
            const BodyInstanceGpu instance = Procedural(id, transform);
            const auto bound = DeriveInstanceBound(instance, uploaded);
            ExpectEncloses(bound, sphere, instance, ("recipe " + std::to_string(id)).c_str());
        }
    }
    // The unit-cube bound this replaces (centre 0.5, radius 0.866) missed the 9-unit sphere.
    const auto lookdev = DeriveInstanceBound(Procedural(100u, glm::mat4(1.0f)), uploaded);
    EXPECT_FLOAT_EQ(lookdev.radius, 9.0f);
    EXPECT_FLOAT_EQ(lookdev.centerX, 5.0f);
}

TEST(InstanceBoundDerivation, LegacyAndStoredBodiesCoverTheirTracedVolume) {
    const Vixen::RenderGraph::RecipeBoundSpheres none;
    const glm::mat4 transform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -1.0f, 8.0f)),
                                           glm::vec3(2.0f));
    // traceProceduralBody: radius params.x (+|params.y| when displaced) + 0.01 at the local origin.
    BodyInstanceGpu displaced = Procedural(1u, transform);
    displaced.material.recipeParams[0] = 1.5f;
    displaced.material.recipeParams[1] = -0.4f;
    ExpectEncloses(DeriveInstanceBound(displaced, none), RecipeBoundSphere{glm::vec3(0.0f), 1.91f},
                   displaced, "legacy displaced sphere");

    // Stored ESVO: the octree [0,1]^3 grid spans [0,10]^3 instance-local units.
    BodyInstanceGpu stored{};
    Vixen::SVO::SetInstanceTransform(stored, transform);
    const auto bound = DeriveInstanceBound(stored, none);
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 local((corner & 1) ? 10.0f : 0.0f, (corner & 2) ? 10.0f : 0.0f,
                              (corner & 4) ? 10.0f : 0.0f);
        ExpectEncloses(bound, RecipeBoundSphere{local, 0.0f}, stored, "stored octree corner");
    }
}
