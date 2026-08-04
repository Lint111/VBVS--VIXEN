#pragma once

// Semantic Shader Wiring S3 sub-slice 2 — hazard census + derived edges,
// OBSERVER ONLY. (undertow docs/plans/2026-08-03-semantic-shader-wiring.md)
//
// For every synthesized stage we know each descriptor member's ACCESS (from
// the merged SDI, sub-slice 1) and its provider's (node, slot) source (from
// the registry). The census collects (stage, resource, access) records; the
// derivation produces candidate sync edges: for each resource recorded by 2+
// stages, every writer (access != ReadOnly) -> every reader (access !=
// WriteOnly) on a different stage.
//
// Derived edges are COMPARED against the scheduler's baked SyncEdges and
// reported — never fed into the graph. Switching any stage from hand-declared
// to derived sync is a later sub-slice, gated on baked-SyncEdges identity
// (the failure mode there is a reordered frame, not a missing connect).
//
// Known-underivable case the observer exists to measure: bucketing's three
// mode stages share ONE interface whose SSBOs are plain read-write — the hand
// W/R split encodes per-`mode` roles the declaration cannot express.

#include "CleanupStack.h"  // NodeHandle

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Vixen::RenderGraph {

/// Access vocabulary, value-compatible with every generated per-program
/// `Access` enum (the emitter writes 0/1/2 by construction).
enum class SdiAccess : uint32_t { ReadWrite = 0, ReadOnly = 1, WriteOnly = 2 };

/// Resource identity for cross-stage correlation: the PROVIDER's world-side
/// (node, output slot). Member NAMES cannot correlate — the same buffer is
/// legitimately provided under different per-shader block names.
struct SdiResourceKey {
    NodeHandle node{};
    uint32_t slot = 0;

    bool operator<(const SdiResourceKey& o) const {
        if (node != o.node) return node < o.node;
        return slot < o.slot;
    }
    bool operator==(const SdiResourceKey& o) const {
        return node == o.node && slot == o.slot;
    }
};

struct SdiOpaqueMember {
    NodeHandle stage{};
    std::string memberName;
};

class SdiHazardCensus {
public:
    struct Record_ {
        NodeHandle stage{};
        SdiAccess access = SdiAccess::ReadWrite;
    };

    void Record(NodeHandle stage, SdiResourceKey resource, SdiAccess access) {
        byResource_[resource].push_back({stage, access});
        stages_.insert(stage);
    }

    /// A member whose provider has no recorded source (ProvideCustom) —
    /// excluded from derivation, surfaced for the coverage report.
    void RecordOpaque(NodeHandle stage, std::string memberName) {
        opaque_.push_back({stage, std::move(memberName)});
        stages_.insert(stage);
    }

    /// Every stage that recorded anything — the report's reverse check
    /// (baked-but-not-derived) scopes to these.
    const std::set<NodeHandle>& Stages() const { return stages_; }

    const std::map<SdiResourceKey, std::vector<Record_>>& Resources() const {
        return byResource_;
    }
    const std::vector<SdiOpaqueMember>& OpaqueMembers() const { return opaque_; }

private:
    // std::map/set: deterministic derivation + report order.
    std::map<SdiResourceKey, std::vector<Record_>> byResource_;
    std::vector<SdiOpaqueMember> opaque_;
    std::set<NodeHandle> stages_;
};

struct SdiDerivedEdge {
    NodeHandle from{};
    NodeHandle to{};
    SdiResourceKey resource{};

    bool operator<(const SdiDerivedEdge& o) const {
        if (from != o.from) return from < o.from;
        if (to != o.to) return to < o.to;
        return resource < o.resource;
    }
    bool operator==(const SdiDerivedEdge& o) const {
        return from == o.from && to == o.to && resource == o.resource;
    }
};

inline std::vector<SdiDerivedEdge> DeriveHazardEdges(const SdiHazardCensus& census) {
    std::set<SdiDerivedEdge> edges;
    for (const auto& [resource, records] : census.Resources()) {
        for (const auto& writer : records) {
            if (writer.access == SdiAccess::ReadOnly) continue;
            for (const auto& reader : records) {
                if (reader.access == SdiAccess::WriteOnly) continue;
                if (reader.stage == writer.stage) continue;
                edges.insert({writer.stage, reader.stage, resource});
            }
        }
    }
    return {edges.begin(), edges.end()};
}

/**
 * @brief Walk one synthesized stage's merged-SDI members into the census.
 *
 * Descriptor members only (push constants are CPU-written values, not
 * resources). Feature-filtered with the same presence rule the wire plan
 * uses. Members whose provider has no recorded source are RecordOpaque'd.
 *
 * @tparam Registry duck-typed: needs Has(name) + SourceOf(name) returning a
 *         type with {node, slot, known} (SdiProviderRegistry's shape).
 *
 * @param annotatedExclusions member names excluded from derivation by explicit
 *        app-side annotation — the measured false-positive classes, e.g.
 *        trace-plumbing buffers whose writes are define-gated and therefore
 *        invisible to qualifier-based access reflection. Excluded members are
 *        surfaced through the opaque channel with their annotation, never
 *        silently dropped.
 */
template<typename Meta, const auto& Members, typename Registry, typename Features>
void CensusStageFromSdi(SdiHazardCensus& census, NodeHandle stage,
                        const Registry& registry, const Features& activeFeatures,
                        const std::map<std::string, std::string>* annotatedExclusions = nullptr) {
    for (const auto& m : Members) {
        if (m.isPushMember) continue;
        bool present = true;
        for (uint32_t i = 0; i < m.featureCount; ++i) {
            if (!activeFeatures.Contains(m.features[i])) { present = false; break; }
        }
        if (!present) continue;
        if (!registry.Has(m.name)) continue;  // wire plan already hard-errors
        if (annotatedExclusions) {
            auto it = annotatedExclusions->find(m.name);
            if (it != annotatedExclusions->end()) {
                census.RecordOpaque(stage,
                                    std::string(m.name) + " [" + it->second + "]");
                continue;
            }
        }
        const auto src = registry.SourceOf(m.name);
        if (!src.known) {
            census.RecordOpaque(stage, m.name);
            continue;
        }
        census.Record(stage, {src.node, src.slot},
                      static_cast<SdiAccess>(static_cast<uint32_t>(m.access)));
    }
}

} // namespace Vixen::RenderGraph
