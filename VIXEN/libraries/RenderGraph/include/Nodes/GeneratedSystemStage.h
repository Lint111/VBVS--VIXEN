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
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
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

struct GpuSystemArgument {
    std::string name;  // diagnostics only; binding is by identity
    SchemaIdentity identity;
    std::string form;  // storage | texture | sampled
    uint32_t binding = 0;
    bool read = true;
    bool write = false;
};

struct GpuSystemPushField {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
    bool extent = false;
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

/// The parsed `yeroket-gpu-system-manifest/v1` of one composition.
struct GpuSystemManifest {
    std::vector<GpuSystemEntry> systems;
    std::vector<GpuSystemBarrier> barriers;

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
                if (!push.extent) {
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

    /// `enabled` lets a module host leave out the systems of providers it did not select (an
    /// anti-aliasing provider's resolve when `none` won); every other system is instantiated.
    void Instantiate(RenderGraph* graph, GpuSystemManifest manifest,
                     const std::function<bool(const GpuSystemEntry&)>& enabled = {}) {
        if (enabled) std::erase_if(manifest.systems, [&](const GpuSystemEntry& entry) { return !enabled(entry); });
        manifest_ = std::move(manifest);
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

    const std::vector<Stage>& Stages() const { return stages_; }

private:
    GpuSystemManifest manifest_;
    std::vector<Stage> stages_;
    std::map<SchemaIdentity, NodeHandle> lastWriters_;
};

} // namespace Vixen::RenderGraph
