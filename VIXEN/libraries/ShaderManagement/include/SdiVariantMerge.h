#pragma once

// Semantic Shader Wiring S0 — feature-tagged SDI variant merge.
//
// SDI reflects ONE compiled variant per SPIR-V; #ifdef-gated bindings are not
// an axis of the interface, so feature gating leaks into graph-builder code as
// hand-duplicated flag blocks. This merge takes N (featureSet, reflection)
// variants of one program and produces ONE interface where every member
// carries the feature predicate derived mechanically by diffing the variants.
//
// Predicate model: a member's requiredFeatures is a conjunction of defines —
// the member is present exactly when ALL of them are active. Defines common
// to every compiled variant are the baseline and never appear as requirements.
// Presence that no conjunction reproduces is an ERROR, not a guess.

#include "SpirvReflectionData.h"

#include <string>
#include <vector>

namespace ShaderManagement {

/**
 * @brief One compiled variant of a program: the injected defines + reflection.
 */
struct SdiVariant {
    std::vector<std::string> features;   // defines injected for this compile
    SpirvReflectionData data;
};

/**
 * @brief A descriptor binding with its derived feature predicate.
 */
struct SdiMergedBinding {
    SpirvDescriptorBinding binding;              // representative declaration
    std::vector<std::string> requiredFeatures;   // sorted; empty = unconditional
};

/**
 * @brief A push-constant member with its derived feature predicate.
 */
struct SdiMergedPushMember {
    SpirvStructMember member;
    std::vector<std::string> requiredFeatures;   // sorted; empty = unconditional
};

/**
 * @brief The merged, feature-tagged interface of a program.
 */
struct SdiMergedInterface {
    std::string programName;
    std::vector<std::string> featureAxis;        // union minus baseline, sorted
    std::vector<SdiMergedBinding> bindings;      // sorted by (set, binding)
    std::string pushName;                        // empty when no push constants
    uint32_t pushSize = 0;                       // max range size over variants
    std::vector<SdiMergedPushMember> pushMembers; // sorted by offset
    std::vector<SpirvStructDefinition> structDefinitions; // deduped by name
};

struct SdiMergeResult {
    bool success = false;
    SdiMergedInterface merged;
    std::string errorMessage;
};

/**
 * @brief Merge N compiled variants of one program into a feature-tagged interface.
 *
 * Hard-errors (success=false, human-readable errorMessage) on:
 *  - conflicting declarations at the same (set, binding) across variants
 *  - conflicting push-member offset/size for the same member name
 *  - member presence not expressible as a feature conjunction
 *  - same-named struct definitions with differing layouts
 */
SdiMergeResult MergeSdiVariants(const std::string& programName,
                                const std::vector<SdiVariant>& variants);

} // namespace ShaderManagement
