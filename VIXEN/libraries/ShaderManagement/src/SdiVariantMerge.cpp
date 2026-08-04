#include "SdiVariantMerge.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace ShaderManagement {

namespace {

using FeatureSet = std::set<std::string>;

FeatureSet ToSet(const std::vector<std::string>& v) {
    return FeatureSet(v.begin(), v.end());
}

FeatureSet Intersect(const FeatureSet& a, const FeatureSet& b) {
    FeatureSet out;
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
                          std::inserter(out, out.begin()));
    return out;
}

bool IsSubset(const FeatureSet& sub, const FeatureSet& super) {
    return std::includes(super.begin(), super.end(), sub.begin(), sub.end());
}

/**
 * Derive the feature conjunction for a member present in `presentIn` (variant
 * indices). Returns false when no conjunction reproduces the presence pattern.
 */
bool DerivePredicate(const std::vector<FeatureSet>& variantFeatures,
                     const FeatureSet& baseline,
                     const std::set<size_t>& presentIn,
                     std::vector<std::string>& outRequired) {
    outRequired.clear();
    if (presentIn.size() == variantFeatures.size()) {
        return true;  // unconditional
    }

    // Conjunction candidate: defines common to every variant that HAS the
    // member, minus the baseline common to all variants.
    bool first = true;
    FeatureSet required;
    for (size_t idx : presentIn) {
        required = first ? variantFeatures[idx]
                         : Intersect(required, variantFeatures[idx]);
        first = false;
    }
    for (const auto& f : baseline) required.erase(f);

    // The candidate must EXACTLY reproduce presence: required ⊆ features(v)
    // iff v contains the member.
    for (size_t v = 0; v < variantFeatures.size(); ++v) {
        const bool predicted = IsSubset(required, variantFeatures[v]);
        const bool actual = presentIn.count(v) > 0;
        if (predicted != actual) return false;
    }

    outRequired.assign(required.begin(), required.end());
    return true;
}

SdiMergeResult Fail(std::string message) {
    SdiMergeResult r;
    r.success = false;
    r.errorMessage = std::move(message);
    return r;
}

} // namespace

SdiMergeResult MergeSdiVariants(const std::string& programName,
                                const std::vector<SdiVariant>& variants) {
    if (variants.empty()) {
        return Fail("MergeSdiVariants(" + programName + "): no variants given");
    }

    std::vector<FeatureSet> variantFeatures;
    variantFeatures.reserve(variants.size());
    for (const auto& v : variants) variantFeatures.push_back(ToSet(v.features));

    // Baseline = defines common to ALL variants; axis = union − baseline.
    FeatureSet baseline = variantFeatures.front();
    FeatureSet unionSet = variantFeatures.front();
    for (size_t i = 1; i < variantFeatures.size(); ++i) {
        baseline = Intersect(baseline, variantFeatures[i]);
        unionSet.insert(variantFeatures[i].begin(), variantFeatures[i].end());
    }

    SdiMergeResult result;
    result.merged.programName = programName;
    for (const auto& f : unionSet)
        if (!baseline.count(f)) result.merged.featureAxis.push_back(f);

    // --- Descriptor bindings, keyed by (set, binding) --------------------
    struct BindingOccurrences {
        const SpirvDescriptorBinding* representative = nullptr;
        size_t representativeVariant = 0;
        std::set<size_t> presentIn;
    };
    std::map<std::pair<uint32_t, uint32_t>, BindingOccurrences> bindingMap;

    for (size_t v = 0; v < variants.size(); ++v) {
        for (const auto& [setIndex, bindings] : variants[v].data.descriptorSets) {
            for (const auto& b : bindings) {
                auto key = std::make_pair(setIndex, b.binding);
                auto& occ = bindingMap[key];
                if (!occ.representative) {
                    occ.representative = &b;
                    occ.representativeVariant = v;
                } else if (occ.representative->name != b.name ||
                           occ.representative->descriptorType != b.descriptorType ||
                           occ.representative->descriptorCount != b.descriptorCount ||
                           // Access is part of the declaration (S3): a variant
                           // flipping a binding readonly<->writeonly is a real
                           // interface divergence, not a mergeable difference.
                           occ.representative->access != b.access) {
                    std::ostringstream msg;
                    msg << "MergeSdiVariants(" << programName << "): conflicting"
                        << " declarations at set " << setIndex << " binding "
                        << b.binding << ": '" << occ.representative->name
                        << "' vs '" << b.name << "'";
                    return Fail(msg.str());
                }
                occ.presentIn.insert(v);
            }
        }
    }

    // Struct-definition union (deduped by name; layouts must agree).
    std::map<std::string, int> structIndexByName;
    auto internStruct =
        [&](const SpirvStructDefinition& def) -> int {
        auto it = structIndexByName.find(def.name);
        if (it != structIndexByName.end()) {
            const auto& existing = result.merged.structDefinitions[it->second];
            if (existing.sizeInBytes != def.sizeInBytes ||
                existing.members.size() != def.members.size()) {
                return -2;  // layout conflict
            }
            return it->second;
        }
        const int idx = static_cast<int>(result.merged.structDefinitions.size());
        result.merged.structDefinitions.push_back(def);
        structIndexByName.emplace(def.name, idx);
        return idx;
    };

    for (auto& [key, occ] : bindingMap) {
        SdiMergedBinding merged;
        merged.binding = *occ.representative;
        merged.binding.set = key.first;

        if (merged.binding.structDefIndex >= 0) {
            const auto& srcDefs =
                variants[occ.representativeVariant].data.structDefinitions;
            if (merged.binding.structDefIndex <
                static_cast<int>(srcDefs.size())) {
                const int idx =
                    internStruct(srcDefs[merged.binding.structDefIndex]);
                if (idx == -2) {
                    return Fail("MergeSdiVariants(" + programName +
                                "): struct layout conflict for '" +
                                srcDefs[merged.binding.structDefIndex].name + "'");
                }
                merged.binding.structDefIndex = idx;
            } else {
                merged.binding.structDefIndex = -1;
            }
        }

        if (!DerivePredicate(variantFeatures, baseline, occ.presentIn,
                             merged.requiredFeatures)) {
            std::ostringstream msg;
            msg << "MergeSdiVariants(" << programName << "): presence of '"
                << merged.binding.name << "' (set " << key.first << " binding "
                << key.second << ") is not expressible as a feature conjunction";
            return Fail(msg.str());
        }
        result.merged.bindings.push_back(std::move(merged));
    }
    // std::map iteration already yields (set, binding) order.

    // --- Push-constant members, keyed by name ----------------------------
    struct PushOccurrences {
        const SpirvStructMember* representative = nullptr;
        std::set<size_t> presentIn;
    };
    std::map<std::string, PushOccurrences> pushMap;

    for (size_t v = 0; v < variants.size(); ++v) {
        const auto& ranges = variants[v].data.pushConstants;
        if (ranges.empty()) continue;
        if (ranges.size() > 1) {
            return Fail("MergeSdiVariants(" + programName +
                        "): multiple push-constant ranges are not supported");
        }
        const auto& range = ranges.front();
        if (result.merged.pushName.empty()) {
            result.merged.pushName = range.name.empty() ? "pc" : range.name;
        }
        result.merged.pushSize = std::max(result.merged.pushSize, range.size);
        for (const auto& m : range.structDef.members) {
            auto& occ = pushMap[m.name];
            if (!occ.representative) {
                occ.representative = &m;
            } else if (occ.representative->offset != m.offset ||
                       occ.representative->type.sizeInBytes !=
                           m.type.sizeInBytes) {
                std::ostringstream msg;
                msg << "MergeSdiVariants(" << programName
                    << "): push member '" << m.name
                    << "' layout conflict (offset "
                    << occ.representative->offset << " vs " << m.offset << ")";
                return Fail(msg.str());
            }
            occ.presentIn.insert(v);
        }
    }

    for (auto& [name, occ] : pushMap) {
        SdiMergedPushMember merged;
        merged.member = *occ.representative;
        if (!DerivePredicate(variantFeatures, baseline, occ.presentIn,
                             merged.requiredFeatures)) {
            return Fail("MergeSdiVariants(" + programName + "): presence of push"
                        " member '" + name +
                        "' is not expressible as a feature conjunction");
        }
        result.merged.pushMembers.push_back(std::move(merged));
    }
    std::sort(result.merged.pushMembers.begin(), result.merged.pushMembers.end(),
              [](const SdiMergedPushMember& a, const SdiMergedPushMember& b) {
                  return a.member.offset < b.member.offset;
              });

    result.success = true;
    return result;
}

} // namespace ShaderManagement
