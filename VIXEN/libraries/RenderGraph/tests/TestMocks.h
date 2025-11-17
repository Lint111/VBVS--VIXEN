#pragma once

/**
 * @file TestMocks.h
 * @brief Centralized mock definitions for RenderGraph tests
 *
 * MOTIVATION:
 * Tests were defining local mocks that conflicted with production headers.
 * This caused type redefinition errors and required updating every test file
 * whenever production types changed.
 *
 * SOLUTION:
 * All test mocks in one place. Tests include this header instead of defining
 * their own mocks. Production refactorings only require updating this file.
 *
 * USAGE:
 * ```cpp
 * #include "TestMocks.h"
 * using namespace RenderGraph::TestMocks;
 *
 * auto bundle = Builders::MakeSimplePushBundle();
 * // ... use in test
 * ```
 */

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace RenderGraph::TestMocks {

// ============================================================================
// MOCK SHADER MANAGEMENT TYPES
// ============================================================================
// Mock versions of ShaderManagement types for tests that don't need the real
// ShaderManagement library. Uses different namespace to avoid conflicts.

namespace MockShader {
    /**
     * @brief Mock SPIR-V type information
     * Mimics ShaderManagement::SpirvTypeInfo for testing
     */
    struct TypeInfo {
        enum class BaseType {
            Float,  // 32-bit floating point
            Int,    // 32-bit signed integer
            UInt,   // 32-bit unsigned integer
            Bool    // Boolean
        };

        BaseType baseType = BaseType::Float;
        uint32_t vecSize = 1;       // 1 = scalar, 2/3/4 = vector
        uint32_t matrixRows = 0;    // 0 = not a matrix
        uint32_t matrixCols = 0;    // 0 = not a matrix

        // Convenience constructors
        static TypeInfo Scalar(BaseType type) {
            return {type, 1, 0, 0};
        }
        static TypeInfo Vec(BaseType type, uint32_t size) {
            return {type, size, 0, 0};
        }
        static TypeInfo Mat(BaseType type, uint32_t rows, uint32_t cols) {
            return {type, 1, rows, cols};
        }
    };

    /**
     * @brief Mock SPIR-V struct member
     * Mimics ShaderManagement::SpirvStructMember for testing
     */
    struct StructMember {
        std::string name;
        uint32_t offset = 0;
        uint32_t size = 0;
        TypeInfo typeInfo;

        StructMember() = default;
        StructMember(std::string n, uint32_t off, uint32_t sz, TypeInfo ti)
            : name(std::move(n)), offset(off), size(sz), typeInfo(ti) {}
    };

    /**
     * @brief Mock shader data bundle
     * Mimics ShaderManagement::ShaderDataBundle for testing
     *
     * NOTE: Tests should use std::shared_ptr<MockShader::DataBundle>
     * to match the production API (Phase H migration to shared_ptr).
     */
    struct DataBundle {
        std::vector<StructMember> pushConstantMembers;
        std::vector<StructMember> descriptorMembers;

        // Add additional fields as needed for testing
        std::string shaderName = "test_shader";
        uint32_t pushConstantSize = 0;
    };
}

// ============================================================================
// BUILDER FUNCTIONS (Factory patterns for common test scenarios)
// ============================================================================

namespace Builders {
    /**
     * @brief Create mock shader bundle with custom push constant fields
     *
     * @param fields Vector of (name, offset, size, typeInfo) tuples
     * @return Shared pointer to mock shader bundle
     *
     * Example:
     * ```cpp
     * auto bundle = MakePushConstantBundle({
     *     {"cameraPos", 0, 12, MockShader::TypeInfo::Vec(Float, 3)},
     *     {"time", 16, 4, MockShader::TypeInfo::Scalar(Float)}
     * });
     * ```
     */
    inline std::shared_ptr<MockShader::DataBundle> MakePushConstantBundle(
        const std::vector<std::tuple<std::string, uint32_t, uint32_t, MockShader::TypeInfo>>& fields
    ) {
        auto bundle = std::make_shared<MockShader::DataBundle>();
        for (const auto& [name, offset, size, typeInfo] : fields) {
            bundle->pushConstantMembers.emplace_back(name, offset, size, typeInfo);
        }

        // Calculate total size
        if (!bundle->pushConstantMembers.empty()) {
            const auto& last = bundle->pushConstantMembers.back();
            bundle->pushConstantSize = last.offset + last.size;
        }

        return bundle;
    }

    /**
     * @brief Create simple shader bundle (vec3 cameraPos + float time)
     * Common test case for push constant gathering
     */
    inline std::shared_ptr<MockShader::DataBundle> MakeSimplePushBundle() {
        using BT = MockShader::TypeInfo::BaseType;
        return MakePushConstantBundle({
            {"cameraPos", 0, 12, MockShader::TypeInfo::Vec(BT::Float, 3)},
            {"time", 16, 4, MockShader::TypeInfo::Scalar(BT::Float)}
        });
    }

    /**
     * @brief Create shader bundle with single scalar push constant
     * Minimal test case
     */
    inline std::shared_ptr<MockShader::DataBundle> MakeSingleScalarPushBundle(
        const std::string& name = "value"
    ) {
        using BT = MockShader::TypeInfo::BaseType;
        return MakePushConstantBundle({
            {name, 0, 4, MockShader::TypeInfo::Scalar(BT::Float)}
        });
    }

    /**
     * @brief Create shader bundle with multiple mixed-type push constants
     * Complex test case (vec3 + float + int + mat4)
     */
    inline std::shared_ptr<MockShader::DataBundle> MakeComplexPushBundle() {
        using BT = MockShader::TypeInfo::BaseType;
        return MakePushConstantBundle({
            {"cameraPos", 0, 12, MockShader::TypeInfo::Vec(BT::Float, 3)},
            {"time", 16, 4, MockShader::TypeInfo::Scalar(BT::Float)},
            {"frameCount", 20, 4, MockShader::TypeInfo::Scalar(BT::UInt)},
            {"viewMatrix", 32, 64, MockShader::TypeInfo::Mat(BT::Float, 4, 4)}
        });
    }

    /**
     * @brief Create empty shader bundle
     * Test case for handling shaders without push constants
     */
    inline std::shared_ptr<MockShader::DataBundle> MakeEmptyBundle() {
        return std::make_shared<MockShader::DataBundle>();
    }
}

// ============================================================================
// TYPE ALIASES FOR COMPATIBILITY
// ============================================================================
// Allow tests to use familiar names without namespace prefix

using MockTypeInfo = MockShader::TypeInfo;
using MockStructMember = MockShader::StructMember;
using MockDataBundle = MockShader::DataBundle;

} // namespace RenderGraph::TestMocks
