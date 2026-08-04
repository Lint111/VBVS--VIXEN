#pragma once

// Semantic Shader Wiring S2 — provider registry + SDI-driven stage wiring.
// (undertow docs/plans/2026-08-03-semantic-shader-wiring.md)
//
// The shader is the declaration; the builder declares only PROVIDERS. A
// provider maps a shader-side member name ("BodyInstanceBuffer", "dims") to
// a world-side (node, output slot, roles) — registered ONCE per graph build.
// BuildSdiWirePlan walks a merged-SDI MEMBERS table (feature-filtered) and
// resolves every member to its gatherer slot; WireStageFromSdi applies the
// plan through the provider closures, emitting exactly the Connects the hand
// blocks used to write. Unmatched member = configure-time hard error naming
// the shader, the member, and the candidates.
//
// Push-slot semantics: a push member's gatherer slot is its field ORDINAL
// among the members PRESENT in the compiled variant (a feature-gated push
// field that is compiled out shifts later ordinals — reflection reality).
// No live shader gates a push member today; this counts present-only from
// the start so the first one that does is already correct here.

#include "Core/TypedConnection.h"
#include "Connection/ConnectionModifier.h"
#include "Connection/ShaderFeature.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace Vixen::RenderGraph {

struct SdiWirePlanEntry {
    const char* name;
    bool isPush;
    uint32_t targetSlot;   // descriptor: shader binding; push: present-only ordinal
};

struct SdiWirePlan {
    std::vector<SdiWirePlanEntry> entries;
};

/**
 * @brief Which half of a plan to apply.
 *
 * Multi-stage programs (bucketing's three modes) share ONE descriptor
 * gatherer but have per-stage push gatherers: descriptors wire once,
 * push wires per stage.
 */
enum class SdiWireSet { All, DescriptorsOnly, PushOnly };

inline SdiWirePlan FilterPlan(const SdiWirePlan& plan, SdiWireSet set) {
    if (set == SdiWireSet::All) return plan;
    SdiWirePlan out;
    for (const auto& e : plan.entries) {
        if ((set == SdiWireSet::PushOnly) == e.isPush) out.entries.push_back(e);
    }
    return out;
}

/**
 * @brief Resolve a merged-SDI MEMBERS table against provider names.
 *
 * @tparam Meta     The generated Metadata type (PROGRAM_NAME for errors)
 * @tparam Members  The generated MEMBERS array (duck-typed rows: name /
 *                  isPushMember / binding / featureCount / features)
 */
template<typename Meta, const auto& Members>
SdiWirePlan BuildSdiWirePlan(const SdiFeatureSet& activeFeatures,
                             const std::unordered_set<std::string>& providerNames,
                             SdiWireSet scope = SdiWireSet::All) {
    SdiWirePlan plan;
    uint32_t pushOrdinal = 0;

    for (const auto& m : Members) {
        bool present = true;
        for (uint32_t i = 0; i < m.featureCount; ++i) {
            if (!activeFeatures.Contains(m.features[i])) { present = false; break; }
        }
        if (!present) continue;

        const uint32_t slot = m.isPushMember ? pushOrdinal : m.binding;
        if (m.isPushMember) ++pushOrdinal;

        // Scope gates RESOLUTION, not just output: a DescriptorsOnly plan
        // must not demand push providers (and vice versa).
        if (scope == SdiWireSet::DescriptorsOnly && m.isPushMember) continue;
        if (scope == SdiWireSet::PushOnly && !m.isPushMember) continue;

        if (providerNames.count(m.name) == 0) {
            std::ostringstream msg;
            msg << "SdiStageWiring(" << Meta::PROGRAM_NAME << "): no provider for "
                << (m.isPushMember ? "push member '" : "binding '") << m.name
                << "'. Available providers:";
            std::vector<std::string> sorted(providerNames.begin(), providerNames.end());
            std::sort(sorted.begin(), sorted.end());
            for (const auto& p : sorted) msg << " " << p;
            throw std::runtime_error(msg.str());
        }

        plan.entries.push_back({m.name, m.isPushMember, slot});
    }
    return plan;
}

/**
 * @brief Name -> (node, output slot, roles) registry, one per graph build.
 *
 * The source slot is a compile-time TYPE in the typed-connection system, so
 * Provide() captures it in a closure at registration time; application later
 * only needs the target gatherer + slot index.
 */
/**
 * @brief A provider's world-side identity, recorded for the hazard census
 *        (S3): the same RESOURCE provided under different per-shader member
 *        names correlates by (node, slot), never by name. ProvideCustom
 *        closures are opaque — known=false, excluded from derivation.
 */
struct SdiProviderSource {
    NodeHandle node{};
    uint32_t slot = 0;
    bool known = false;
};

class SdiProviderRegistry {
public:
    using ConnectFn = std::function<void(ConnectionBatch&, NodeHandle, uint32_t)>;

    /**
     * @brief Escape hatch: register a fully custom Connect closure.
     *
     * For providers that need connection modifiers beyond SlotRoleModifier —
     * e.g. ExtractField(&CameraData::cameraPos) — the closure writes the
     * Connect itself; the registry only routes the target gatherer + slot.
     */
    void ProvideCustom(const std::string& name, ConnectFn fn) {
        providers_[name] = std::move(fn);
        sources_.erase(name);  // opaque — no census identity
    }

    /**
     * @brief Register a provider under the shader-side member name.
     *
     * @param roles Slot roles the Connect carries (the provider knows its own
     *              stability: persistent buffers Dependency|Execute, per-frame
     *              re-emitted views Execute-only).
     */
    template<typename SourceSlot>
    void Provide(const std::string& name, NodeHandle source, SourceSlot sourceSlot,
                 SlotRole roles) {
        providers_[name] = [source, sourceSlot, roles](ConnectionBatch& batch,
                                                       NodeHandle target,
                                                       uint32_t targetSlot) {
            batch.Connect(source, sourceSlot, target, targetSlot,
                          SlotRoleModifier(roles));
        };
        sources_[name] = SdiProviderSource{source, SourceSlot::index, true};
    }

    /// The provider's (node, slot) identity for the hazard census; known=false
    /// for ProvideCustom entries and unknown names.
    SdiProviderSource SourceOf(const std::string& name) const {
        auto it = sources_.find(name);
        return it != sources_.end() ? it->second : SdiProviderSource{};
    }

    bool Has(const std::string& name) const { return providers_.count(name) > 0; }

    std::unordered_set<std::string> Names() const {
        std::unordered_set<std::string> names;
        for (const auto& [name, fn] : providers_) names.insert(name);
        return names;
    }

    void Apply(const std::string& name, ConnectionBatch& batch, NodeHandle target,
               uint32_t targetSlot) const {
        providers_.at(name)(batch, target, targetSlot);
    }

private:
    // std::map: deterministic iteration wherever the registry is enumerated.
    // Copyable by design — the per-stage overlay pattern copies the shared
    // registry and adds stage-local providers (e.g. bucketing's `mode`).
    std::map<std::string, ConnectFn> providers_;
    std::map<std::string, SdiProviderSource> sources_;
};

/**
 * @brief Wire one compute stage's gatherers from its merged SDI.
 *
 * Emits the same descriptor/push Connects the hand-written block would, with
 * slot indices derived from the shader's own reflected interface.
 */
template<typename Meta, const auto& Members>
void WireStageFromSdi(ConnectionBatch& batch, const SdiProviderRegistry& registry,
                      NodeHandle descGatherer, NodeHandle pushGatherer,
                      const SdiFeatureSet& activeFeatures,
                      SdiWireSet wireSet = SdiWireSet::All) {
    const SdiWirePlan plan =
        BuildSdiWirePlan<Meta, Members>(activeFeatures, registry.Names(), wireSet);
    for (const auto& entry : plan.entries) {
        registry.Apply(entry.name, batch,
                       entry.isPush ? pushGatherer : descGatherer,
                       entry.targetSlot);
    }
}

} // namespace Vixen::RenderGraph
