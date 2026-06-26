#include <gtest/gtest.h>
#include "ShaderCompiler.h"
#include <fstream>
#include <sstream>

using namespace ShaderManagement;

// SDF_CORE_KERNELS_HLSL_PATH is injected by CMake (absolute path to the vendored .g.hlsl)
#ifndef SDF_CORE_KERNELS_HLSL_PATH
#  error "SDF_CORE_KERNELS_HLSL_PATH must be defined via CMake compile_definitions"
#endif

TEST(HlslIngestion, GeneratedKernelsCompileToSpirv) {
    // Read the vendored generated HLSL (proves generator→HLSL→SPIR-V pipeline).
    std::ifstream f(SDF_CORE_KERNELS_HLSL_PATH);
    ASSERT_TRUE(f.good()) << "Cannot open vendored HLSL: " << SDF_CORE_KERNELS_HLSL_PATH;
    std::ostringstream ss; ss << f.rdbuf();
    // Append a compute main that calls both kernels — exercises the full generated API surface.
    ss << R"(
RWStructuredBuffer<float> outBuf : register(u0);
[numthreads(1,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    float d = SdfCore_Union(
        SdfCore_Sphere(float3(0,0,0), float3(-1,0,0), 1.0),
        SdfCore_Sphere(float3(0,0,0), float3( 1,0,0), 1.0));
    outBuf[0] = d;
}
)";
    ShaderCompiler c;
    CompilationOptions opt;
    opt.sourceLanguage = CompilationOptions::SourceLanguage::HLSL;
    opt.validateSpirv = true;
    auto out = c.Compile(ShaderStage::Compute, ss.str(), "main", opt);
    ASSERT_TRUE(out.success) << out.GetFullLog();
    EXPECT_FALSE(out.spirv.empty());
}
