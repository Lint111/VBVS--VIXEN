#include <gtest/gtest.h>
#include "ShaderManagement/SpirvReflector.h"
#include "ShaderManagement/ShaderCompiler.h"

using namespace ShaderManagement;

// SPIRV reflection tests
TEST(SpirvReflectionTest, ReflectDescriptorBindings) {
    std::string source = R"(
        #version 450
        layout(set = 0, binding = 0) uniform UniformBuffer {
            mat4 mvp;
        } ubo;

        layout(location = 0) in vec3 position;

        void main() {
            gl_Position = ubo.mvp * vec4(position, 1.0);
        }
    )";

    ShaderCompiler compiler;
    auto compileResult = compiler.Compile(ShaderStage::Vertex, source);
    ASSERT_TRUE(compileResult.success);

    SpirvReflector reflector;
    auto reflectionData = reflector.Reflect(compileResult.spirv);

    // Should find one descriptor set with one binding
    EXPECT_FALSE(reflectionData.descriptorSets.empty());
}

TEST(SpirvReflectionTest, EmptySpirvFails) {
    SpirvReflector reflector;
    std::vector<uint32_t> emptySpirv;

    // Should not crash on empty SPIRV
    EXPECT_NO_THROW({
        auto data = reflector.Reflect(emptySpirv);
    });
}
