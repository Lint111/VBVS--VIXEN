// Lazy-Procedural-Delta-Baseline Inc1 M4 Task 7: drift-guard for the hand-translated
// SdfCoreKernels.glsl against the kernel-framework-generated SdfCoreKernels.g.hlsl. The
// GLSL file is a mechanical, hand-maintained translation (see its own header comment for
// the translation rules) — this test asserts its SdfCore_* function NAME SET matches the
// HLSL core's exactly, so a kernel-side core update (regenerated .g.hlsl gaining/losing/
// renaming a function) fails loudly here instead of silently drifting the GLSL emitter's
// (Task 8) coverage out of sync with what evalRecipe/EmitProceduralComputeShader support.
//
// Pure name-set comparison via regex extraction — no glslang/GPU needed, runs anywhere.

#include <gtest/gtest.h>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#ifndef SDF_CORE_KERNELS_HLSL_PATH
#  error "SDF_CORE_KERNELS_HLSL_PATH must be defined via CMake compile_definitions"
#endif
#ifndef SDF_CORE_KERNELS_GLSL_PATH
#  error "SDF_CORE_KERNELS_GLSL_PATH must be defined via CMake compile_definitions"
#endif

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.good()) {
        ADD_FAILURE() << "Cannot open: " << path;
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Extracts every `SdfCore_<Name>` that appears as a function DEFINITION, i.e. immediately
// followed by '(' on the same identifier at the start of a top-level declaration. Matches
// both `float SdfCore_X(` (HLSL/GLSL scalar return) and `float3`/`vec3` (vector return) —
// return-type spelling differs between the two languages, so the regex only anchors on the
// function name itself, not the preceding return type.
std::set<std::string> ExtractSdfCoreNames(const std::string& src) {
    std::set<std::string> names;
    static const std::regex re(R"(\bSdfCore_[A-Za-z0-9]+\s*\()");
    for (auto it = std::sregex_iterator(src.begin(), src.end(), re); it != std::sregex_iterator(); ++it) {
        std::string match = it->str();
        // Strip the trailing "(" and any whitespace before it.
        match = match.substr(0, match.find('('));
        while (!match.empty() && std::isspace(static_cast<unsigned char>(match.back()))) match.pop_back();
        names.insert(match);
    }
    return names;
}

} // namespace

TEST(SdfCoreGlslNameSet, MatchesHlslCoreExactly) {
    const std::string hlsl = ReadFile(SDF_CORE_KERNELS_HLSL_PATH);
    const std::string glsl = ReadFile(SDF_CORE_KERNELS_GLSL_PATH);
    ASSERT_FALSE(hlsl.empty());
    ASSERT_FALSE(glsl.empty());

    const std::set<std::string> hlslNames = ExtractSdfCoreNames(hlsl);
    const std::set<std::string> glslNames = ExtractSdfCoreNames(glsl);

    ASSERT_GT(hlslNames.size(), 0u) << "HLSL name extraction found nothing — regex broken?";

    std::vector<std::string> missingFromGlsl;
    std::set_difference(hlslNames.begin(), hlslNames.end(), glslNames.begin(), glslNames.end(),
                        std::back_inserter(missingFromGlsl));
    std::vector<std::string> extraInGlsl;
    std::set_difference(glslNames.begin(), glslNames.end(), hlslNames.begin(), hlslNames.end(),
                        std::back_inserter(extraInGlsl));

    auto join = [](const std::vector<std::string>& v) {
        std::string s;
        for (const auto& n : v) { if (!s.empty()) s += ", "; s += n; }
        return s;
    };

    EXPECT_TRUE(missingFromGlsl.empty())
        << "GLSL core missing (present in HLSL, not in GLSL): " << join(missingFromGlsl);
    EXPECT_TRUE(extraInGlsl.empty())
        << "GLSL core has extra names (not in HLSL): " << join(extraInGlsl);
}
