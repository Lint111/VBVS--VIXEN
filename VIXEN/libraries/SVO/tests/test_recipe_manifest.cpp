#include <gtest/gtest.h>
#include "Recipe/RecipeManifest.h"
using namespace Vixen::SVO;

TEST(RecipeManifest, ValidManifestPasses) {
    RecipeManifest m = {
        {"game.hull",   1u, "hull.vrc"},
        {"game.engine", 2u, "engine.vrc"},
    };
    std::string err;
    EXPECT_TRUE(ValidateManifest(m, err)) << err;
}

TEST(RecipeManifest, RejectsDuplicateRecipeId) {
    RecipeManifest m = {
        {"game.hull",   1u, "hull.vrc"},
        {"game.engine", 1u, "engine.vrc"}, // same recipeId!
    };
    std::string err;
    EXPECT_FALSE(ValidateManifest(m, err));
    EXPECT_NE(err.find("1"), std::string::npos); // message names the clash
}

TEST(RecipeManifest, RejectsDuplicateNamespacedId) {
    RecipeManifest m = {
        {"game.hull", 1u, "hull.vrc"},
        {"game.hull", 2u, "hull2.vrc"}, // same namespacedId!
    };
    std::string err;
    EXPECT_FALSE(ValidateManifest(m, err));
    EXPECT_NE(err.find("game.hull"), std::string::npos);
}

TEST(RecipeManifest, EmptyManifestPasses) {
    RecipeManifest m;
    std::string err;
    EXPECT_TRUE(ValidateManifest(m, err)) << err;
}
