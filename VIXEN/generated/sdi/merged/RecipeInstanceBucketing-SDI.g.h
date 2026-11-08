// ============================================================================
// Compact merged SDI declarations (graph-facing ABI)
// ============================================================================
//
// Program: RecipeInstanceBucketing
// Generated from the merged shader reflection; do not edit.
// Layout mirrors live in RecipeInstanceBucketing-SDI.layout.g.h.
//
// ============================================================================

#pragma once

#include <cstdint>

namespace ShaderInterface {
namespace RecipeInstanceBucketing {

enum class Access : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

namespace Set0 {

    struct Binding0 {
        static constexpr const char* NAME = "BodyInstanceBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding1 {
        static constexpr const char* NAME = "RecipeBoundSphereBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding2 {
        static constexpr const char* NAME = "BucketCountBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 2;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding3 {
        static constexpr const char* NAME = "BucketIndicesBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 3;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding4 {
        static constexpr const char* NAME = "BucketCoverageMinXBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 4;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding5 {
        static constexpr const char* NAME = "BucketCoverageMinYBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 5;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding6 {
        static constexpr const char* NAME = "BucketCoverageMaxXBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 6;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding7 {
        static constexpr const char* NAME = "BucketCoverageMaxYBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 7;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding8 {
        static constexpr const char* NAME = "BucketIndirectCommandBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 8;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding9 {
        static constexpr const char* NAME = "PrecisionBucketCountBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 9;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding10 {
        static constexpr const char* NAME = "PrecisionBucketIndicesBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 10;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadWrite;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct Binding11 {
        static constexpr const char* NAME = "BodyInstanceTransformBuffer";
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 11;
        static constexpr uint32_t COUNT = 1;
        static constexpr Access ACCESS = Access::ReadOnly;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Set0

namespace Bind {
using BodyInstanceBuffer = Set0::Binding0;
using RecipeBoundSphereBuffer = Set0::Binding1;
using BucketCountBuffer = Set0::Binding2;
using BucketIndicesBuffer = Set0::Binding3;
using BucketCoverageMinXBuffer = Set0::Binding4;
using BucketCoverageMinYBuffer = Set0::Binding5;
using BucketCoverageMaxXBuffer = Set0::Binding6;
using BucketCoverageMaxYBuffer = Set0::Binding7;
using BucketIndirectCommandBuffer = Set0::Binding8;
using PrecisionBucketCountBuffer = Set0::Binding9;
using PrecisionBucketIndicesBuffer = Set0::Binding10;
using BodyInstanceTransformBuffer = Set0::Binding11;
} // namespace Bind

namespace Push {

    static constexpr uint32_t SIZE = 112;

    struct viewProj {
        static constexpr const char* NAME = "viewProj";
        static constexpr uint32_t INDEX = 0;
        static constexpr uint32_t OFFSET = 0;
        static constexpr uint32_t SIZE = 64;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct instanceCount {
        static constexpr const char* NAME = "instanceCount";
        static constexpr uint32_t INDEX = 1;
        static constexpr uint32_t OFFSET = 64;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct maxBuckets {
        static constexpr const char* NAME = "maxBuckets";
        static constexpr uint32_t INDEX = 2;
        static constexpr uint32_t OFFSET = 68;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct maxMembersPerBucket {
        static constexpr const char* NAME = "maxMembersPerBucket";
        static constexpr uint32_t INDEX = 3;
        static constexpr uint32_t OFFSET = 72;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct screenWidth {
        static constexpr const char* NAME = "screenWidth";
        static constexpr uint32_t INDEX = 4;
        static constexpr uint32_t OFFSET = 76;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct screenHeight {
        static constexpr const char* NAME = "screenHeight";
        static constexpr uint32_t INDEX = 5;
        static constexpr uint32_t OFFSET = 80;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct mode {
        static constexpr const char* NAME = "mode";
        static constexpr uint32_t INDEX = 6;
        static constexpr uint32_t OFFSET = 84;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct raySizeCoef {
        static constexpr const char* NAME = "raySizeCoef";
        static constexpr uint32_t INDEX = 7;
        static constexpr uint32_t OFFSET = 88;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct raySizeBias {
        static constexpr const char* NAME = "raySizeBias";
        static constexpr uint32_t INDEX = 8;
        static constexpr uint32_t OFFSET = 92;
        static constexpr uint32_t SIZE = 4;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

    struct cameraPos {
        static constexpr const char* NAME = "cameraPos";
        static constexpr uint32_t INDEX = 9;
        static constexpr uint32_t OFFSET = 96;
        static constexpr uint32_t SIZE = 12;
        static constexpr uint32_t FEATURE_COUNT = 0;
    };

} // namespace Push

struct MemberInfo {
    const char* name;
    bool isPushMember;
    uint32_t set;
    uint32_t binding;
    uint32_t offset;
    Access access;
    uint32_t featureCount;
    const char* const* features;
};


inline constexpr MemberInfo MEMBERS[] = {
    {"BodyInstanceBuffer", false, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"RecipeBoundSphereBuffer", false, 0, 1, 0, Access::ReadOnly, 0, nullptr},
    {"BucketCountBuffer", false, 0, 2, 0, Access::ReadWrite, 0, nullptr},
    {"BucketIndicesBuffer", false, 0, 3, 0, Access::ReadWrite, 0, nullptr},
    {"BucketCoverageMinXBuffer", false, 0, 4, 0, Access::ReadWrite, 0, nullptr},
    {"BucketCoverageMinYBuffer", false, 0, 5, 0, Access::ReadWrite, 0, nullptr},
    {"BucketCoverageMaxXBuffer", false, 0, 6, 0, Access::ReadWrite, 0, nullptr},
    {"BucketCoverageMaxYBuffer", false, 0, 7, 0, Access::ReadWrite, 0, nullptr},
    {"BucketIndirectCommandBuffer", false, 0, 8, 0, Access::ReadWrite, 0, nullptr},
    {"PrecisionBucketCountBuffer", false, 0, 9, 0, Access::ReadWrite, 0, nullptr},
    {"PrecisionBucketIndicesBuffer", false, 0, 10, 0, Access::ReadWrite, 0, nullptr},
    {"BodyInstanceTransformBuffer", false, 0, 11, 0, Access::ReadOnly, 0, nullptr},
    {"viewProj", true, 0, 0, 0, Access::ReadOnly, 0, nullptr},
    {"instanceCount", true, 0, 0, 64, Access::ReadOnly, 0, nullptr},
    {"maxBuckets", true, 0, 0, 68, Access::ReadOnly, 0, nullptr},
    {"maxMembersPerBucket", true, 0, 0, 72, Access::ReadOnly, 0, nullptr},
    {"screenWidth", true, 0, 0, 76, Access::ReadOnly, 0, nullptr},
    {"screenHeight", true, 0, 0, 80, Access::ReadOnly, 0, nullptr},
    {"mode", true, 0, 0, 84, Access::ReadOnly, 0, nullptr},
    {"raySizeCoef", true, 0, 0, 88, Access::ReadOnly, 0, nullptr},
    {"raySizeBias", true, 0, 0, 92, Access::ReadOnly, 0, nullptr},
    {"cameraPos", true, 0, 0, 96, Access::ReadOnly, 0, nullptr},
};

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "RecipeInstanceBucketing";
    static constexpr uint32_t NUM_MEMBERS = 22;
    static constexpr uint32_t NUM_FEATURES = 0;
};

} // namespace RecipeInstanceBucketing
} // namespace ShaderInterface
