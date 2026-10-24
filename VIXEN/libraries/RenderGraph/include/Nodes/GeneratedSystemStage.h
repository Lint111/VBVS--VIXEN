#pragma once

// renderbridge S1 (R677, R680): one data-driven runner for generated GPU systems.
//
// A composition's `--gaia-gpu-schedule --out-manifest` lists every GPU-domain [KernelSystem]
// in schedule order with its GLSL, local size, extent, push layout and arguments. Each argument
// names the resource it binds by SCHEMA IDENTITY ({kind, member}: a frame kind's member, or a
// row kind / [GpuStruct] as a whole "*"), never by argument name. The runner instantiates one
// shader library + compute stage per system, binds every argument from the SchemaResourceRegistry,
// fills the push extents, sizes the dispatch from the manifest's local size, and orders the stage
// after the registered producers of what it reads. Core code never names a generated system.

#include "Core/RenderGraph.h"
#include "Core/TypedConnection.h"
#include "Connection/ConnectionModifier.h"
#include "Nodes/SdiStageSynthesis.h"
#include "Nodes/ComputeStageNode.h"
#include "Nodes/ImageSyncGathererNode.h"
#include "Nodes/ShaderLibraryNode.h"
#include "Data/Nodes/ComputeStageNodeConfig.h"
#include "Data/Nodes/ImageSyncGathererNodeConfig.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace Vixen::RenderGraph {

/// A resource's schema identity: a frame kind's member, or a row kind as a whole ("*").
struct SchemaIdentity {
    std::string kind;
    std::string member;
    auto operator<=>(const SchemaIdentity&) const = default;
    std::string Text() const { return kind + "." + member; }
};

struct GpuComponentRegion {
    std::string member;
    uint32_t offset = 0;        // byte offset of this region's base-address word in the buffer header
    uint32_t sourceOffset = 0;  // byte offset when the changed cursor exposes an AoS singleton
    uint32_t stride = 0;
    uint32_t size = 0;
    uint32_t alignment = 1;
};

struct GpuSystemArgument {
    std::string name;  // diagnostics only; binding is by identity
    SchemaIdentity identity;
    std::string form;  // storage | texture | sampled
    uint32_t binding = 0;
    bool read = true;
    bool write = false;
    std::vector<GpuComponentRegion> regions;
};

struct GpuSystemPushField {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
    bool extent = false;
    bool frame = false;
    SchemaIdentity identity;  // when !extent
};

struct GpuSystemEntry {
    std::string id;
    std::string className;
    std::string glsl;
    uint32_t axes = 1;
    std::array<uint32_t, 3> localSize{1, 1, 1};
    std::string extentSource;
    std::vector<GpuSystemPushField> push;
    std::vector<GpuSystemArgument> arguments;
};

struct GpuSystemBarrier {
    std::string before;
    std::string after;
};

enum class GpuTransferDirection { CpuToGpu, GpuToCpu };
enum class GpuTransferClass { Hot, Cold };

struct GpuTransferPlanRow {
    std::string before;
    std::string after;
    GpuTransferDirection direction = GpuTransferDirection::CpuToGpu;
    SchemaIdentity identity;
    std::string system;
    std::string argument;
    int32_t binding = -1;
    GpuTransferClass transferClass = GpuTransferClass::Hot;
    std::vector<GpuComponentRegion> regions;
};

/// The parsed `yeroket-gpu-system-manifest/v1` of one composition.
struct GpuSystemManifest {
    std::vector<GpuSystemEntry> systems;
    std::vector<GpuSystemBarrier> barriers;
    std::vector<GpuTransferPlanRow> transferPlan;

    static GpuSystemManifest Parse(const std::string& text) {
        const auto json = nlohmann::json::parse(text);
        if (json.at("schema").get<std::string>() != "yeroket-gpu-system-manifest/v1") {
            throw std::runtime_error("GpuSystemManifest: unsupported schema '" +
                                     json.at("schema").get<std::string>() + "'");
        }
        GpuSystemManifest manifest;
        for (const auto& system : json.at("systems")) {
            GpuSystemEntry entry;
            entry.id = system.at("id").get<std::string>();
            entry.className = system.at("class").get<std::string>();
            entry.glsl = system.at("glsl").get<std::string>();
            entry.axes = system.at("axes").get<uint32_t>();
            for (size_t i = 0; i < 3; ++i) entry.localSize[i] = system.at("localSize").at(i).get<uint32_t>();
            entry.extentSource = system.at("extent").at("source").get<std::string>();
            for (const auto& field : system.at("push").at("fields")) {
                GpuSystemPushField push;
                push.name = field.at("name").get<std::string>();
                push.offset = field.at("offset").get<uint32_t>();
                push.size = field.at("size").get<uint32_t>();
                push.extent = field.at("source").get<std::string>() == "extent";
                push.frame = field.at("source").get<std::string>() == "frame";
                if (!push.extent && !push.frame) {
                    push.identity = {field.at("kind").get<std::string>(), field.at("member").get<std::string>()};
                }
                entry.push.push_back(std::move(push));
            }
            for (const auto& argument : system.at("arguments")) {
                GpuSystemArgument arg;
                arg.name = argument.at("name").get<std::string>();
                arg.identity = {argument.at("kind").get<std::string>(), argument.at("member").get<std::string>()};
                arg.form = argument.at("form").get<std::string>();
                arg.binding = argument.at("binding").get<uint32_t>();
                arg.read = argument.value("read", argument.at("access").get<std::string>() == "in" || arg.form == "storage");
                arg.write = argument.value("write", argument.at("access").get<std::string>() == "ref");
                for (const auto& region : argument.value("regions", nlohmann::json::array())) {
                    arg.regions.push_back({region.at("member").get<std::string>(),
                                           region.at("offset").get<uint32_t>(),
                                           region.value("sourceOffset", 0u),
                                           region.at("stride").get<uint32_t>(),
                                           region.at("size").get<uint32_t>(),
                                           region.at("alignment").get<uint32_t>()});
                }
                if (arg.identity.kind.empty()) {
                    throw std::runtime_error("GpuSystemManifest: " + entry.className + " argument '" + arg.name +
                                             "' has no schema identity; declare its element as a kind or [GpuStruct]");
                }
                entry.arguments.push_back(std::move(arg));
            }
            manifest.systems.push_back(std::move(entry));
        }
        for (const auto& barrier : json.at("barriers")) {
            manifest.barriers.push_back({barrier.at("before").get<std::string>(), barrier.at("after").get<std::string>()});
        }
        for (const auto& transfer : json.value("transferPlan", nlohmann::json::array())) {
            GpuTransferPlanRow row;
            row.before = transfer.at("before").get<std::string>();
            row.after = transfer.at("after").get<std::string>();
            row.direction = transfer.at("direction").get<std::string>() == "cpuToGpu"
                                ? GpuTransferDirection::CpuToGpu
                                : GpuTransferDirection::GpuToCpu;
            row.identity = {transfer.at("kind").get<std::string>(), transfer.at("member").get<std::string>()};
            row.system = transfer.at("system").get<std::string>();
            row.argument = transfer.at("argument").get<std::string>();
            row.binding = transfer.at("binding").get<int32_t>();
            row.transferClass = transfer.at("class").get<std::string>() == "cold"
                                    ? GpuTransferClass::Cold
                                    : GpuTransferClass::Hot;
            for (const auto& region : transfer.at("regions")) {
                row.regions.push_back({region.at("member").get<std::string>(),
                                       region.at("offset").get<uint32_t>(),
                                       region.value("sourceOffset", 0u),
                                       region.at("stride").get<uint32_t>(),
                                       region.at("size").get<uint32_t>(),
                                       region.at("alignment").get<uint32_t>()});
            }
            manifest.transferPlan.push_back(std::move(row));
        }
        return manifest;
    }

    /// Load the first existing candidate path.
    static GpuSystemManifest Load(const std::vector<std::filesystem::path>& candidates) {
        for (const auto& path : candidates) {
            if (!std::filesystem::exists(path)) continue;
            std::ifstream in(path);
            std::stringstream text;
            text << in.rdbuf();
            return Parse(text.str());
        }
        std::string tried;
        for (const auto& path : candidates) tried += " " + path.string();
        throw std::runtime_error("GpuSystemManifest: no manifest found; tried" + tried);
    }
};

/// One observation of a typed `.changed<T>()` cursor. Component returns a contiguous SoA
/// plane for the requested generated region; singleton helpers expose the corresponding field
/// directly, so Fill always performs one span copy per region and never an AoS transpose.
struct TransferChangedSnapshot {
    uint32_t rowCount = 0;
    uint64_t generation = 0;
    bool writerRan = false;
    std::function<std::span<const std::byte>(const GpuComponentRegion&)> component;
};

class TransferSourceRegistry {
public:
    using ChangedCursor = std::function<TransferChangedSnapshot()>;

    void ProvideChanged(const SchemaIdentity& identity, ChangedCursor cursor) {
        changed_[identity] = std::move(cursor);
    }

    /// A one-row settings record is already a one-element component span. Its generation is
    /// normally advanced by the owning `.changed<T>()` cursor.
    template<typename T>
    void ProvideSingleton(const SchemaIdentity& identity, const T* value,
                          std::function<uint64_t()> generation,
                          std::function<bool()> writerRan = [] { return false; }) {
        static_assert(std::is_trivially_copyable_v<T>);
        ProvideChanged(identity, [value, generation = std::move(generation),
                                  writerRan = std::move(writerRan)] {
            TransferChangedSnapshot snapshot;
            snapshot.rowCount = 1;
            snapshot.generation = generation();
            snapshot.writerRan = writerRan();
            snapshot.component = [value](const GpuComponentRegion& region) {
                const auto bytes = std::as_bytes(std::span<const T>(value, 1));
                if (static_cast<size_t>(region.sourceOffset) + region.size > bytes.size()) {
                    throw std::runtime_error("TransferPlan: singleton component region exceeds its record");
                }
                return bytes.subspan(region.sourceOffset, region.size);
            };
            return snapshot;
        });
    }

    /// The type-erased runtime face of `.changed<T>()`; generated Fill invokes it once per row.
    TransferChangedSnapshot changed(const SchemaIdentity& identity) const {
        auto it = changed_.find(identity);
        if (it == changed_.end()) {
            throw std::runtime_error("TransferPlan: no .changed<T>() cursor registered for " + identity.Text());
        }
        return it->second();
    }

private:
    std::map<SchemaIdentity, ChangedCursor> changed_;
};

struct TransferFillLedger {
    uint64_t steady_state_bytes_uploaded = 0;
    uint32_t buffersUploaded = 0;
};

/// Runtime projection of the generated transfer-plan rows. Cold settings are stamped per
/// destination ring slot; a generation change refreshes each of the four slots once. Hot rows
/// upload only when their CPU writer ran in this schedule execution.
class TransferPlan {
public:
    static constexpr uint32_t GenerationSlots = 4;
    using Upload = std::function<void(const GpuTransferPlanRow&, uint32_t,
                                      std::span<const std::byte>)>;

    explicit TransferPlan(std::vector<GpuTransferPlanRow> rows) : rows_(std::move(rows)) {}
    explicit TransferPlan(const GpuSystemManifest& manifest) : TransferPlan(manifest.transferPlan) {}

    static size_t BufferSize(const GpuTransferPlanRow& row, uint32_t rowCount = 1) {
        size_t cursor = row.regions.size() * sizeof(uint32_t);
        for (const auto& region : row.regions) {
            cursor = AlignUp(cursor, region.alignment);
            cursor += static_cast<size_t>(region.stride) * rowCount;
        }
        return cursor;
    }

    TransferFillLedger Fill(uint64_t frame, const TransferSourceRegistry& sources,
                            const Upload& upload) {
        TransferFillLedger ledger;
        const uint32_t slot = static_cast<uint32_t>(frame % GenerationSlots);
        std::map<std::string, bool> filled;
        for (const auto& row : rows_) {
            if (row.direction != GpuTransferDirection::CpuToGpu || row.regions.empty()) continue;
            const std::string key = row.identity.Text();
            if (filled.contains(key)) continue;

            TransferChangedSnapshot snapshot = sources.changed(row.identity);
            auto [stampIt, inserted] = generations_.try_emplace(key);
            if (inserted) stampIt->second.fill(std::numeric_limits<uint64_t>::max());
            if (row.transferClass == GpuTransferClass::Hot && !snapshot.writerRan) continue;
            if (row.transferClass == GpuTransferClass::Cold && stampIt->second[slot] == snapshot.generation) continue;

            size_t cursor = row.regions.size() * sizeof(uint32_t);
            std::vector<uint32_t> offsets;
            offsets.reserve(row.regions.size());
            for (const auto& region : row.regions) {
                cursor = AlignUp(cursor, region.alignment);
                offsets.push_back(static_cast<uint32_t>(cursor));
                cursor += static_cast<size_t>(region.stride) * snapshot.rowCount;
            }
            std::vector<std::byte> buffer(cursor);
            for (size_t regionIndex = 0; regionIndex < row.regions.size(); ++regionIndex) {
                const auto& region = row.regions[regionIndex];
                if (static_cast<size_t>(region.offset) + sizeof(uint32_t) > row.regions.size() * sizeof(uint32_t)) {
                    throw std::runtime_error("TransferPlan: component-region header offset is out of range");
                }
                std::memcpy(buffer.data() + region.offset, &offsets[regionIndex], sizeof(uint32_t));
                const auto source = snapshot.component(region);
                const size_t expected = static_cast<size_t>(region.stride) * snapshot.rowCount;
                if (source.size() != expected) {
                    throw std::runtime_error("TransferPlan: .changed<T>() component span for " +
                                             row.identity.Text() + "." + region.member + " is " +
                                             std::to_string(source.size()) + " bytes; expected " +
                                             std::to_string(expected));
                }
                std::memcpy(buffer.data() + offsets[regionIndex], source.data(), expected);
                ledger.steady_state_bytes_uploaded += expected;
            }
            upload(row, slot, buffer);
            ++ledger.buffersUploaded;
            filled[key] = true;
            stampIt->second[slot] = snapshot.generation;
        }
        return ledger;
    }

private:
    static size_t AlignUp(size_t value, size_t alignment) {
        if (alignment == 0) throw std::runtime_error("TransferPlan: component-region alignment is zero");
        return (value + alignment - 1) / alignment * alignment;
    }

    std::vector<GpuTransferPlanRow> rows_;
    std::map<std::string, std::array<uint64_t, GenerationSlots>> generations_;
};

/**
 * @brief Resources the runner binds, keyed by schema identity.
 *
 * Hand passes register the outputs a generated system reads (R680.4) under the identity the
 * manifest names; the registrations shrink as each hand pass converts.
 */
class SchemaResourceRegistry {
public:
    using ConnectFn = std::function<void(ConnectionBatch&, NodeHandle, uint32_t)>;
    using StageConnectFn = std::function<void(ConnectionBatch&, NodeHandle)>;

    /// The descriptor (or push value) a binding of this identity reads.
    template<typename SourceSlot>
    void Provide(const SchemaIdentity& identity, NodeHandle source, SourceSlot slot, SlotRole roles) {
        descriptors_[identity] = Connector(source, slot, roles);
        sources_[identity] = source;
    }

    /// The node that provides this identity's resource.
    NodeHandle Source(const SchemaIdentity& identity) const {
        auto it = sources_.find(identity);
        if (it == sources_.end()) {
            throw std::runtime_error("SchemaResourceRegistry: no resource registered for " + identity.Text());
        }
        return it->second;
    }

    /// The render target that declares this identity's image to the sync tracker.
    template<typename SourceSlot>
    void ProvideImage(const SchemaIdentity& identity, NodeHandle source, SourceSlot slot) {
        images_[identity] = Connector(source, slot, SlotRole::Dependency | SlotRole::Execute);
        imageWrites_[identity] = [source, slot](ConnectionBatch& batch, NodeHandle stage) {
            batch.Connect(source, slot, stage, ComputeStageNodeConfig::IMAGE_WRITE, SlotRoleModifier(SlotRole::Execute));
        };
    }

    /// A hand pass that produces this identity; readers order after it.
    void ProvideProducer(const SchemaIdentity& identity, NodeHandle stage) { producers_[identity] = stage; }

    /// One axis of an extent source: its push value provider and its dispatch size.
    template<typename SourceSlot>
    void ProvideExtent(const std::string& source, uint32_t axis, NodeHandle node, SourceSlot slot, uint32_t value) {
        extents_[{source, axis}] = {Connector(node, slot, SlotRole::Execute), value};
    }

    void ApplyDescriptor(const SchemaIdentity& identity, const std::string& system, ConnectionBatch& batch,
                         NodeHandle target, uint32_t slot) const {
        Require(descriptors_, identity, system, "resource")(batch, target, slot);
    }
    const ConnectFn* Image(const SchemaIdentity& identity) const { return Find(images_, identity); }
    const StageConnectFn* ImageWrite(const SchemaIdentity& identity) const {
        auto it = imageWrites_.find(identity);
        return it == imageWrites_.end() ? nullptr : &it->second;
    }
    std::optional<NodeHandle> Producer(const SchemaIdentity& identity) const {
        auto it = producers_.find(identity);
        return it == producers_.end() ? std::nullopt : std::optional<NodeHandle>(it->second);
    }
    void SetProducer(const SchemaIdentity& identity, NodeHandle stage) { producers_[identity] = stage; }
    const std::pair<ConnectFn, uint32_t>& Extent(const std::string& source, uint32_t axis, const std::string& system) const {
        auto it = extents_.find({source, axis});
        if (it == extents_.end()) {
            throw std::runtime_error("GeneratedSystemStage(" + system + "): no extent provider for '" + source +
                                     "' axis " + std::to_string(axis));
        }
        return it->second;
    }

private:
    template<typename SourceSlot>
    static ConnectFn Connector(NodeHandle source, SourceSlot slot, SlotRole roles) {
        return [source, slot, roles](ConnectionBatch& batch, NodeHandle target, uint32_t targetSlot) {
            batch.Connect(source, slot, target, targetSlot, SlotRoleModifier(roles));
        };
    }
    static const ConnectFn* Find(const std::map<SchemaIdentity, ConnectFn>& map, const SchemaIdentity& identity) {
        auto it = map.find(identity);
        return it == map.end() ? nullptr : &it->second;
    }
    static const ConnectFn& Require(const std::map<SchemaIdentity, ConnectFn>& map, const SchemaIdentity& identity,
                                    const std::string& system, const char* what) {
        if (const ConnectFn* found = Find(map, identity)) return *found;
        std::string known;
        for (const auto& [key, _] : map) known += " " + key.Text();
        throw std::runtime_error("GeneratedSystemStage(" + system + "): no " + what + " registered for " +
                                 identity.Text() + ". Registered:" + known);
    }

    std::map<SchemaIdentity, ConnectFn> descriptors_;
    std::map<SchemaIdentity, NodeHandle> sources_;
    std::map<SchemaIdentity, ConnectFn> images_;
    std::map<SchemaIdentity, StageConnectFn> imageWrites_;
    std::map<SchemaIdentity, NodeHandle> producers_;
    std::map<std::pair<std::string, uint32_t>, std::pair<ConnectFn, uint32_t>> extents_;
};

/**
 * @brief The generated systems of one composition, instantiated from its manifest.
 *
 * Instantiate() creates the nodes (call where the pass's nodes belong in creation order),
 * RegisterShaders() hands each library its GLSL, Wire() binds and orders them.
 */
class GeneratedSystemStages {
public:
    struct Stage {
        const GpuSystemEntry* entry = nullptr;
        NodeHandle shaderLib{};
        NodeHandle stage{};
        NodeHandle imageReads{};
        size_t imageReadCount = 0;
    };

    void Instantiate(RenderGraph* graph, GpuSystemManifest manifest) {
        manifest_ = std::move(manifest);
        transferPlan_.emplace(manifest_);
        stages_.clear();
        lastWriters_.clear();
        for (const auto& entry : manifest_.systems) {
            Stage stage;
            stage.entry = &entry;
            stage.shaderLib = graph->AddNode<ShaderLibraryNodeType>(entry.className + "_shader_lib");
            stage.stage = graph->AddNode<ComputeStageNodeType>(entry.className);
            for (const auto& argument : entry.arguments) {
                if (argument.form != "storage" && argument.read && !argument.write) ++stage.imageReadCount;
            }
            if (stage.imageReadCount > 0) {
                stage.imageReads = graph->AddNode<ImageSyncGathererNodeType>(entry.className + "_image_reads");
                static_cast<ImageSyncGathererNode*>(graph->GetInstance(stage.imageReads))
                    ->PreRegisterImageSlots(stage.imageReadCount);
            }
            stages_.push_back(stage);
        }
    }

    /// register(shaderLib, glslFileName, programName): the caller owns shader search paths.
    void RegisterShaders(const std::function<void(NodeHandle, const std::string&, const std::string&)>& registerShader) const {
        for (const auto& stage : stages_) registerShader(stage.shaderLib, stage.entry->glsl, stage.entry->className);
    }

    void Wire(RenderGraph* graph, ConnectionBatch& batch, const SdiStageCommon& common,
              SchemaResourceRegistry& registry) {
        std::map<std::string, NodeHandle> byId;
        for (const auto& stage : stages_) {
            const GpuSystemEntry& entry = *stage.entry;
            byId[entry.id] = stage.stage;
            NodeHandle descGatherer = graph->AddNode<DescriptorResourceGathererNodeType>(entry.className + "_desc_gatherer");
            NodeHandle pushGatherer = graph->AddNode<PushConstantGathererNodeType>(entry.className + "_push_gatherer");
            NodeHandle descriptorSet = graph->AddNode<DescriptorSetNodeType>(entry.className + "_descriptors");
            NodeHandle pipeline = graph->AddNode<ComputePipelineNodeType>(entry.className + "_pipeline");
            Detail::WireSdiQuintetChain(batch, common, stage.shaderLib, descGatherer, descriptorSet, pipeline);
            Detail::WireSdiStageCommons(batch, common, stage.shaderLib, descriptorSet, pipeline, stage.stage);
            Detail::WireSdiPushPlumbing(batch, stage.shaderLib, pushGatherer, stage.stage);

            for (const auto& argument : entry.arguments) {
                registry.ApplyDescriptor(argument.identity, entry.className, batch, descGatherer, argument.binding);
            }
            std::array<uint32_t, 3> extent{1, 1, 1};
            uint32_t axis = 0;
            for (uint32_t ordinal = 0; ordinal < entry.push.size(); ++ordinal) {
                const auto& field = entry.push[ordinal];
                if (field.extent) {
                    const auto& [connect, value] = registry.Extent(entry.extentSource, axis, entry.className);
                    connect(batch, pushGatherer, ordinal);
                    extent[axis++] = value;
                } else if (field.frame) {
                    batch.Connect(common.frameSync, FrameSyncNodeConfig::FRAME_COUNTER,
                                  pushGatherer, ordinal, SlotRoleModifier(SlotRole::Execute));
                } else {
                    registry.ApplyDescriptor(field.identity, entry.className, batch, pushGatherer, ordinal);
                }
            }

            // Image hazards: one written image binds IMAGE_WRITE, read images gather into IMAGE_READ_ARRAY.
            uint32_t readSlot = 0;
            std::optional<NodeHandle> producer;
            for (const auto& argument : entry.arguments) {
                if (argument.form != "storage" && argument.write) {
                    if (const auto* write = registry.ImageWrite(argument.identity)) (*write)(batch, stage.stage);
                } else if (argument.form != "storage" && argument.read) {
                    if (const auto* image = registry.Image(argument.identity)) (*image)(batch, stage.imageReads, readSlot);
                    ++readSlot;
                }
                if (argument.read && !argument.write && !producer) producer = registry.Producer(argument.identity);
            }
            if (stage.imageReadCount > 0) {
                batch.Connect(stage.imageReads, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                              stage.stage, ComputeStageNodeConfig::IMAGE_READ_ARRAY,
                              SlotRoleModifier(SlotRole::Execute));
            }
            // Order after the latest generated system this one follows in the manifest's barriers,
            // otherwise after the registered producer of the first input that has one.
            for (const auto& barrier : manifest_.barriers) {
                if (barrier.after == entry.id && byId.contains(barrier.before)) producer = byId[barrier.before];
            }
            if (producer) {
                batch.Connect(*producer, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                              stage.stage, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
            }
            for (const auto& argument : entry.arguments) {
                if (argument.write) {
                    registry.SetProducer(argument.identity, stage.stage);
                    lastWriters_[argument.identity] = stage.stage;
                }
            }

            auto* compute = static_cast<ComputeStageNode*>(graph->GetInstance(stage.stage));
            compute->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
            compute->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, (extent[0] + entry.localSize[0] - 1u) / entry.localSize[0]);
            compute->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, (extent[1] + entry.localSize[1] - 1u) / entry.localSize[1]);
            compute->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, (extent[2] + entry.localSize[2] - 1u) / entry.localSize[2]);
        }
    }

    /// The generated stage that last writes an identity (a hand consumer orders after it).
    std::optional<NodeHandle> LastWriter(const SchemaIdentity& identity) const {
        auto it = lastWriters_.find(identity);
        return it == lastWriters_.end() ? std::nullopt : std::optional<NodeHandle>(it->second);
    }

    TransferFillLedger Fill(uint64_t frame, const TransferSourceRegistry& sources,
                            const TransferPlan::Upload& upload) {
        if (!transferPlan_) throw std::runtime_error("GeneratedSystemStages::Fill called before Instantiate");
        return transferPlan_->Fill(frame, sources, upload);
    }

    const std::vector<Stage>& Stages() const { return stages_; }

private:
    GpuSystemManifest manifest_;
    std::optional<TransferPlan> transferPlan_;
    std::vector<Stage> stages_;
    std::map<SchemaIdentity, NodeHandle> lastWriters_;
};

} // namespace Vixen::RenderGraph
