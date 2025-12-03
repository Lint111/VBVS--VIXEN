#pragma once

#include "VoxelConfig.h"
#include <vector>
#include <glm/glm.hpp>

namespace  Vixen::VoxelData {

// ============================================================================
// MACRO: Generate Scalar and Array Structures from VoxelConfig
// ============================================================================

/**
 * @brief Generate complete voxel data structures from config
 *
 * Generates:
 * 1. ConfigNameScalar - Single voxel (for individual insertion)
 * 2. ConfigNameArrays - Batch of voxels (for bulk processing)
 * 3. Conversions and accessors
 *
 * Usage:
 * ```cpp
 * VOXEL_CONFIG(StandardVoxel, 3) {
 *     VOXEL_KEY(DENSITY, float, 0);
 *     VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 1);
 *     VOXEL_ATTRIBUTE(COLOR, glm::vec3, 2);
 * };
 *
 * GENERATE_VOXEL_STRUCTS(StandardVoxel,
 *     (DENSITY, float)
 *     (MATERIAL, uint32_t)
 *     (COLOR, glm::vec3)
 * );
 *
 * // Result:
 * struct StandardVoxelScalar {
 *     float density;
 *     uint32_t material;
 *     glm::vec3 color;
 * };
 *
 * struct StandardVoxelArrays {
 *     std::vector<float> density;
 *     std::vector<uint32_t> material;
 *     std::vector<glm::vec3> color;
 *     size_t count() const { return density.size(); }
 * };
 * ```
 */

// Helper macros for member extraction
#define VOXEL_SCALAR_MEMBER(name, type) type name;
#define VOXEL_ARRAY_MEMBER(name, type) std::vector<type> name;
#define VOXEL_PUSH_BACK(name, type) name.push_back(scalar.name);
#define VOXEL_RESERVE(name, type) name.reserve(capacity);
#define VOXEL_GET_AT(name, type) type name = arrays.name[index];
#define VOXEL_SET_AT(name, type) arrays.name[index] = scalar.name;

// Main generator macro
#define GENERATE_VOXEL_STRUCTS(ConfigName, Members) \
    GENERATE_SCALAR_STRUCT(ConfigName, Members) \
    GENERATE_ARRAYS_STRUCT(ConfigName, Members) \
    GENERATE_CONVERSIONS(ConfigName, Members)

// Generate scalar struct (single voxel)
#define GENERATE_SCALAR_STRUCT(ConfigName, Members) \
    struct ConfigName##Scalar { \
        BOOST_PP_SEQ_FOR_EACH(VOXEL_SCALAR_MEMBER_MACRO, _, Members) \
        \
        ConfigName##Scalar() = default; \
        \
        ConfigName##Scalar(BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_TRANSFORM(PARAM_DECL, _, Members))) \
            : BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_TRANSFORM(INIT_LIST, _, Members)) {} \
    };

// Generate arrays struct (batch of voxels)
#define GENERATE_ARRAYS_STRUCT(ConfigName, Members) \
    struct ConfigName##Arrays { \
        BOOST_PP_SEQ_FOR_EACH(VOXEL_ARRAY_MEMBER_MACRO, _, Members) \
        \
        size_t count() const { \
            return BOOST_PP_SEQ_HEAD(Members)##_name.size(); \
        } \
        \
        void reserve(size_t capacity) { \
            BOOST_PP_SEQ_FOR_EACH(VOXEL_RESERVE_MACRO, _, Members) \
        } \
        \
        void push_back(const ConfigName##Scalar& scalar) { \
            BOOST_PP_SEQ_FOR_EACH(VOXEL_PUSH_BACK_MACRO, _, Members) \
        } \
        \
        ConfigName##Scalar operator[](size_t index) const { \
            return ConfigName##Scalar( \
                BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_TRANSFORM(GET_AT, _, Members)) \
            ); \
        } \
        \
        void set(size_t index, const ConfigName##Scalar& scalar) { \
            BOOST_PP_SEQ_FOR_EACH(VOXEL_SET_AT_MACRO, _, Members) \
        } \
    };

// Simplified non-Boost version (manual expansion required)
#define VOXEL_STRUCTS_BEGIN(ConfigName) \
    struct ConfigName##Scalar {

#define VOXEL_MEMBER(name, type) type name;

#define VOXEL_STRUCTS_MIDDLE(ConfigName) \
    }; \
    \
    struct ConfigName##Arrays {

#define VOXEL_ARRAY(name, type) std::vector<type> name;

#define VOXEL_STRUCTS_END(ConfigName) \
        size_t count() const { \
            if (ConfigName##Arrays::_getFirstVector().empty()) return 0; \
            return ConfigName##Arrays::_getFirstVector().size(); \
        } \
        \
        void reserve(size_t capacity); \
        void push_back(const ConfigName##Scalar& scalar); \
        ConfigName##Scalar operator[](size_t index) const; \
        void set(size_t index, const ConfigName##Scalar& scalar); \
    private: \
        const std::vector<float>& _getFirstVector() const; \
    };

} // namespace VoxelData

// ============================================================================
// Simplified Manual Expansion (No Boost.Preprocessor)
// ============================================================================

/**
 * @brief Manual voxel struct generation
 *
 * Usage:
 * ```cpp
 * VOXEL_STRUCTS_BEGIN(StandardVoxel)
 *     VOXEL_MEMBER(density, float)
 *     VOXEL_MEMBER(material, uint32_t)
 *     VOXEL_MEMBER(color, glm::vec3)
 * VOXEL_STRUCTS_MIDDLE(StandardVoxel)
 *     VOXEL_ARRAY(density, float)
 *     VOXEL_ARRAY(material, uint32_t)
 *     VOXEL_ARRAY(color, glm::vec3)
 * VOXEL_STRUCTS_END(StandardVoxel)
 *
 * // Implement methods in .cpp:
 * void StandardVoxelArrays::reserve(size_t cap) {
 *     density.reserve(cap);
 *     material.reserve(cap);
 *     color.reserve(cap);
 * }
 * ```
 */
