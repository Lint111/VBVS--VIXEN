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
//
// taarunner: an argument's `slot` picks the current or the previous (history) image of its
// identity; a `sampled` argument binds the view as a sampled image plus a runner-owned sampler at
// its `samplerBinding`; an identity the systems use that no hand pass registers is allocated from
// the manifest's `allocations` (two slots: a ping-pong history pair; one slot: a render target).

#include "Core/RenderGraph.h"
#include "Core/TypedConnection.h"
#include "Connection/ConnectionModifier.h"
#include "Nodes/SdiStageSynthesis.h"
#include "Nodes/ComputeStageNode.h"
#include "Nodes/ImageSyncGathererNode.h"
#include "Nodes/ShaderLibraryNode.h"
#include "Nodes/AccumulationHistoryNode.h"
#include "Nodes/RenderTargetNode.h"
#include "Nodes/SamplerNode.h"
#include "Data/Nodes/ComputeStageNodeConfig.h"
#include "Data/Nodes/ImageSyncGathererNodeConfig.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
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

/// Which image of an identity an argument binds: this frame's, or last frame's (history).
enum class FrameSlot : uint8_t { Current, Previous };

/// A descriptor binding with its type known when the connection is made, so an image producer
/// derives its usage from it before the gatherer reflects its shader (a sampled view adds SAMPLED).
struct GeneratedSystemBinding {
    uint32_t binding = 0;
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    static constexpr const char* name = "generated-system binding";
};

inline VkDescriptorType GpuSystemManifestDescriptorType(const std::string& name) {
    static const std::map<std::string, VkDescriptorType> types{
        {"VK_DESCRIPTOR_TYPE_STORAGE_BUFFER", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        {"VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
        {"VK_DESCRIPTOR_TYPE_STORAGE_IMAGE", VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
        {"VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
        {"VK_DESCRIPTOR_TYPE_SAMPLER", VK_DESCRIPTOR_TYPE_SAMPLER},
    };
    auto it = types.find(name);
    return it == types.end() ? VK_DESCRIPTOR_TYPE_MAX_ENUM : it->second;
}

struct GpuSystemArgument {
    std::string name;  // diagnostics only; binding is by identity
    SchemaIdentity identity;
    std::string form;  // storage | texture | sampled
    uint32_t binding = 0;
    bool read = true;
    bool write = false;
    FrameSlot slot = FrameSlot::Current;  // manifest `slot`: single | current -> Current
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    uint32_t samplerBinding = UINT32_MAX;       // `sampled` form only
};

/// A frame resource the composition declares (manifest `allocations`).
struct GpuSystemAllocation {
    SchemaIdentity identity;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t slots = 1;
    std::string extentSource;
};

/// The kernel's closed texture-format vocabulary (TextureFormatVocabulary.cs), by Vulkan name.
inline VkFormat GpuSystemManifestFormat(const std::string& name) {
    static const std::map<std::string, VkFormat> formats{
        {"VK_FORMAT_R8_UNORM", VK_FORMAT_R8_UNORM},
        {"VK_FORMAT_R8G8B8A8_UNORM", VK_FORMAT_R8G8B8A8_UNORM},
        {"VK_FORMAT_R16G16B16A16_SFLOAT", VK_FORMAT_R16G16B16A16_SFLOAT},
        {"VK_FORMAT_R32G32B32A32_SFLOAT", VK_FORMAT_R32G32B32A32_SFLOAT},
        {"VK_FORMAT_R32_SFLOAT", VK_FORMAT_R32_SFLOAT},
        {"VK_FORMAT_R32G32_SFLOAT", VK_FORMAT_R32G32_SFLOAT},
        {"VK_FORMAT_R32_UINT", VK_FORMAT_R32_UINT},
        {"VK_FORMAT_R32G32_UINT", VK_FORMAT_R32G32_UINT},
    };
    auto it = formats.find(name);
    if (it == formats.end()) throw std::runtime_error("GpuSystemManifest: unknown texture format '" + name + "'");
    return it->second;
}

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
    std::vector<GpuSystemAllocation> allocations;

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
                const std::string slot = argument.value("slot", std::string("single"));
                if (slot == "previous") arg.slot = FrameSlot::Previous;
                else if (slot != "single" && slot != "current")
                    throw std::runtime_error("GpuSystemManifest: " + entry.className + " argument '" + arg.name +
                                             "' has unknown slot '" + slot + "'");
                if (arg.form == "sampled") arg.samplerBinding = argument.at("samplerBinding").get<uint32_t>();
                arg.descriptorType = GpuSystemManifestDescriptorType(argument.value("descriptor", std::string()));
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
        for (const auto& allocation : json.value("allocations", nlohmann::json::array())) {
            GpuSystemAllocation entry;
            entry.identity = {allocation.at("kind").get<std::string>(), allocation.at("member").get<std::string>()};
            entry.format = GpuSystemManifestFormat(allocation.at("format").get<std::string>());
            entry.slots = allocation.at("slots").get<uint32_t>();
            entry.extentSource = allocation.at("extentSource").get<std::string>();
            manifest.allocations.push_back(std::move(entry));
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
    /// A descriptor or push value: (batch, gatherer, binding or push ordinal, descriptor type;
    /// VK_DESCRIPTOR_TYPE_MAX_ENUM when the gatherer resolves it from its shader).
    using DescriptorFn = std::function<void(ConnectionBatch&, NodeHandle, uint32_t, VkDescriptorType)>;

    /// A stage that produces an identity. `ordersAfterGeneratedWriters` marks a hand pass that is
    /// itself ordered after the generated systems whose outputs it reads (the exposure meter behind
    /// the TAA resolve): a generated reader then waits on it rather than on a barrier source.
    struct ProducerEntry {
        NodeHandle stage{};
        bool ordersAfterGeneratedWriters = false;
    };

    /// The descriptor (or push value) a binding of this identity reads.
    template<typename SourceSlot>
    void Provide(const SchemaIdentity& identity, NodeHandle source, SourceSlot slot, SlotRole roles,
                 FrameSlot which = FrameSlot::Current) {
        descriptors_[{identity, which}] = [source, slot, roles](ConnectionBatch& batch, NodeHandle target,
                                                               uint32_t binding, VkDescriptorType type) {
            if (type == VK_DESCRIPTOR_TYPE_MAX_ENUM) {
                batch.Connect(source, slot, target, binding, SlotRoleModifier(roles));
            } else {
                batch.Connect(source, slot, target, GeneratedSystemBinding{binding, type}, SlotRoleModifier(roles));
            }
        };
    }

    /// The render target that declares this identity's image to the sync tracker.
    template<typename SourceSlot>
    void ProvideImage(const SchemaIdentity& identity, NodeHandle source, SourceSlot slot,
                      FrameSlot which = FrameSlot::Current) {
        images_[{identity, which}] = Connector(source, slot, SlotRole::Dependency | SlotRole::Execute);
        if (which != FrameSlot::Current) return;  // last frame's image is only ever read
        imageWrites_[identity] = [source, slot](ConnectionBatch& batch, NodeHandle stage) {
            batch.Connect(source, slot, stage, ComputeStageNodeConfig::IMAGE_WRITE, SlotRoleModifier(SlotRole::Execute));
        };
    }

    /// A sub-graph's output view and render target for an identity the systems only read
    /// (the reconstruction motion module's motion vectors).
    void ProvideSubGraph(const SchemaIdentity& identity, const SubGraphHandle& group, uint32_t viewPort,
                         uint32_t imagePort, SlotRole roles) {
        const auto resolve = [&](uint32_t port) {
            const auto resolved = group.ResolveOutput(port);
            if (!resolved) {
                throw std::runtime_error("SchemaResourceRegistry: " + identity.Text() +
                                         " group output does not resolve to a member output");
            }
            return *resolved;
        };
        const auto [view, viewOutput] = resolve(viewPort);
        const auto [image, imageOutput] = resolve(imagePort);
        descriptors_[{identity, FrameSlot::Current}] =
            [view, viewOutput, roles](ConnectionBatch& batch, NodeHandle target, uint32_t binding, VkDescriptorType) {
                batch.ConnectResolvedOutput(view, viewOutput, target, binding, roles);
            };
        images_[{identity, FrameSlot::Current}] =
            [image, imageOutput](ConnectionBatch& batch, NodeHandle target, uint32_t binding) {
                batch.ConnectResolvedOutput(image, imageOutput, target, binding,
                                            SlotRole::Dependency | SlotRole::Execute);
            };
    }

    /// A hand pass that produces this identity; readers order after it.
    void ProvideProducer(const SchemaIdentity& identity, NodeHandle stage, bool ordersAfterGeneratedWriters = false) {
        producers_[identity] = {stage, ordersAfterGeneratedWriters};
    }

    /// One axis of an extent source: its push value provider and its dispatch size.
    template<typename SourceSlot>
    void ProvideExtent(const std::string& source, uint32_t axis, NodeHandle node, SourceSlot slot, uint32_t value) {
        extents_[{source, axis}] = {Connector(node, slot, SlotRole::Execute), value};
        // The same value sizes a two-slot (history pair) allocation of this extent source.
        historySizes_[{source, axis}] = [node, slot, axis](ConnectionBatch& batch, NodeHandle pair) {
            if (axis == 0) batch.Connect(node, slot, pair, AccumulationHistoryNodeConfig::WIDTH);
            else batch.Connect(node, slot, pair, AccumulationHistoryNodeConfig::HEIGHT);
        };
    }

    /// The render target whose extent an extent source names (sizes one-slot allocations).
    template<typename SourceSlot>
    void ProvideExtentTarget(const std::string& source, NodeHandle node, SourceSlot slot) {
        extentTargets_[source] = [node, slot](ConnectionBatch& batch, NodeHandle target) {
            batch.Connect(node, slot, target, RenderTargetNodeConfig::EXTENT_SOURCE);
        };
    }

    /// Whether a hand pass (or an allocation) provides this identity's current image or buffer.
    bool Has(const SchemaIdentity& identity) const { return descriptors_.contains({identity, FrameSlot::Current}); }

    void ApplyDescriptor(const SchemaIdentity& identity, const std::string& system, ConnectionBatch& batch,
                         NodeHandle target, uint32_t slot, FrameSlot which = FrameSlot::Current,
                         VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM) const {
        auto it = descriptors_.find({identity, which});
        if (it == descriptors_.end()) {
            std::string known;
            for (const auto& [key, _] : descriptors_)
                known += " " + key.first.Text() + (key.second == FrameSlot::Previous ? "(previous)" : "");
            throw std::runtime_error("GeneratedSystemStage(" + system + "): no " +
                                     (which == FrameSlot::Previous ? "previous-frame " : "") +
                                     "resource registered for " + identity.Text() + ". Registered:" + known);
        }
        it->second(batch, target, slot, type);
    }
    const ConnectFn* Image(const SchemaIdentity& identity, FrameSlot which = FrameSlot::Current) const {
        auto it = images_.find({identity, which});
        return it == images_.end() ? nullptr : &it->second;
    }
    const StageConnectFn* ImageWrite(const SchemaIdentity& identity) const {
        auto it = imageWrites_.find(identity);
        return it == imageWrites_.end() ? nullptr : &it->second;
    }
    std::optional<NodeHandle> Producer(const SchemaIdentity& identity) const {
        auto it = producers_.find(identity);
        return it == producers_.end() ? std::nullopt : std::optional<NodeHandle>(it->second.stage);
    }
    std::optional<ProducerEntry> ProducerOf(const SchemaIdentity& identity) const {
        auto it = producers_.find(identity);
        return it == producers_.end() ? std::nullopt : std::optional<ProducerEntry>(it->second);
    }
    void SetProducer(const SchemaIdentity& identity, NodeHandle stage) { producers_[identity] = {stage, false}; }
    const std::pair<ConnectFn, uint32_t>& Extent(const std::string& source, uint32_t axis, const std::string& system) const {
        auto it = extents_.find({source, axis});
        if (it == extents_.end()) {
            throw std::runtime_error("GeneratedSystemStage(" + system + "): no extent provider for '" + source +
                                     "' axis " + std::to_string(axis));
        }
        return it->second;
    }
    const StageConnectFn& HistorySize(const std::string& source, uint32_t axis, const std::string& what) const {
        auto it = historySizes_.find({source, axis});
        if (it == historySizes_.end()) {
            throw std::runtime_error("GeneratedSystemStage: no extent provider for '" + source + "' axis " +
                                     std::to_string(axis) + " to size " + what);
        }
        return it->second;
    }
    const StageConnectFn& ExtentTarget(const std::string& source, const std::string& what) const {
        auto it = extentTargets_.find(source);
        if (it == extentTargets_.end()) {
            throw std::runtime_error("GeneratedSystemStage: no extent target for '" + source + "' to size " + what);
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
    using SlotKey = std::pair<SchemaIdentity, FrameSlot>;
    std::map<SlotKey, DescriptorFn> descriptors_;
    std::map<SlotKey, ConnectFn> images_;
    std::map<SchemaIdentity, StageConnectFn> imageWrites_;
    std::map<SchemaIdentity, ProducerEntry> producers_;
    std::map<std::pair<std::string, uint32_t>, std::pair<ConnectFn, uint32_t>> extents_;
    std::map<std::pair<std::string, uint32_t>, StageConnectFn> historySizes_;
    std::map<std::string, StageConnectFn> extentTargets_;
};

/// The images a system reads (not writes) through its image hazards, once per {identity, slot}:
/// a texture view and a sampled view of one image are one read.
inline std::vector<std::pair<SchemaIdentity, FrameSlot>> GpuSystemImageReads(const GpuSystemEntry& entry) {
    std::vector<std::pair<SchemaIdentity, FrameSlot>> reads;
    for (const auto& argument : entry.arguments) {
        if (argument.form == "storage" || !argument.read || argument.write) continue;
        const std::pair key{argument.identity, argument.slot};
        if (std::find(reads.begin(), reads.end(), key) == reads.end()) reads.push_back(key);
    }
    return reads;
}

/// The manifest allocations the runner makes: identities an instantiated system binds that no
/// hand pass registers. A hand-registered identity stays the hand pass's (R680.4).
inline std::vector<GpuSystemAllocation> GpuSystemAllocationsToMake(const GpuSystemManifest& manifest,
                                                                   const SchemaResourceRegistry& registry) {
    std::set<SchemaIdentity> used;
    for (const auto& system : manifest.systems)
        for (const auto& argument : system.arguments) used.insert(argument.identity);
    std::vector<GpuSystemAllocation> make;
    for (const auto& allocation : manifest.allocations) {
        if (!used.contains(allocation.identity) || registry.Has(allocation.identity)) continue;
        if (allocation.slots != 1 && allocation.slots != 2) {
            throw std::runtime_error("GeneratedSystemStage: allocation " + allocation.identity.Text() + " declares " +
                                     std::to_string(allocation.slots) + " slots; the runner allocates 1 or 2");
        }
        make.push_back(allocation);
    }
    return make;
}

/// Which stage a system waits on. A hand producer that orders after the generated writers it
/// reads wins (it already follows them); otherwise the latest generated system the manifest's
/// barriers put before this one; otherwise the producer of the first current input that has one.
inline std::optional<NodeHandle> GpuSystemOrderingSource(
    const std::optional<SchemaResourceRegistry::ProducerEntry>& handProducer,
    const std::optional<NodeHandle>& barrierSource) {
    if (handProducer && handProducer->ordersAfterGeneratedWriters) return handProducer->stage;
    if (barrierSource) return barrierSource;
    if (handProducer) return handProducer->stage;
    return std::nullopt;
}

/**
 * @brief The generated systems of one composition, instantiated from its manifest.
 *
 * Instantiate() creates the nodes (call where the pass's nodes belong in creation order),
 * RegisterShaders() hands each library its GLSL, Allocate() makes the manifest's unregistered
 * frame resources (Wire() calls it if the host did not), Wire() binds and orders them.
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
        allocated_.reset();
        sampler_.reset();
        for (const auto& entry : manifest_.systems) {
            Stage stage;
            stage.entry = &entry;
            stage.shaderLib = graph->AddNode<ShaderLibraryNodeType>(entry.className + "_shader_lib");
            stage.stage = graph->AddNode<ComputeStageNodeType>(entry.className);
            stage.imageReadCount = GpuSystemImageReads(entry).size();
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

    /// Allocate the manifest resources no hand pass registered and register them. Two slots are a
    /// ping-pong history pair: the current image is written this frame, the previous one is last
    /// frame's; both start zeroed and are recreated (zeroed) on a resize, so a history reader sees
    /// an empty history, never another generation's memory. One slot is a render target that
    /// follows the extent source's target.
    void Allocate(RenderGraph* graph, ConnectionBatch& batch, const SdiStageCommon& common,
                  SchemaResourceRegistry& registry) {
        allocated_.emplace();
        for (const auto& allocation : GpuSystemAllocationsToMake(manifest_, registry)) {
            const std::string what = allocation.identity.Text();
            const std::string name = allocation.identity.member + "_allocation";
            NodeHandle node{};
            if (allocation.slots == 2) {
                node = graph->AddNode<AccumulationHistoryNodeType>(name);
                graph->GetInstance(node)->SetParameter(AccumulationHistoryNodeConfig::PARAM_FORMAT,
                                                       static_cast<uint32_t>(allocation.format));
                batch.Connect(common.device, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                              node, AccumulationHistoryNodeConfig::VULKAN_DEVICE_IN)
                     .Connect(common.commandPool, CommandPoolNodeConfig::COMMAND_POOL,
                              node, AccumulationHistoryNodeConfig::COMMAND_POOL);
                registry.HistorySize(allocation.extentSource, 0, what)(batch, node);
                registry.HistorySize(allocation.extentSource, 1, what)(batch, node);
                registry.Provide(allocation.identity, node, AccumulationHistoryNodeConfig::CURRENT_HISTORY_IMAGE_VIEW,
                                 SlotRole::Execute);
                registry.ProvideImage(allocation.identity, node, AccumulationHistoryNodeConfig::CURRENT_HISTORY_TARGET);
                registry.Provide(allocation.identity, node, AccumulationHistoryNodeConfig::HISTORY_IMAGE_VIEW,
                                 SlotRole::Execute, FrameSlot::Previous);
                registry.ProvideImage(allocation.identity, node, AccumulationHistoryNodeConfig::HISTORY_TARGET,
                                      FrameSlot::Previous);
            } else {
                node = graph->AddNode<RenderTargetNodeType>(name);
                auto* target = graph->GetInstance(node);
                target->SetParameter(RenderTargetNodeConfig::PARAM_FORMAT, static_cast<uint32_t>(allocation.format));
                target->SetParameter(RenderTargetNodeConfig::PARAM_IMAGE_COUNT, 1u);
                batch.Connect(common.device, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                              node, RenderTargetNodeConfig::VULKAN_DEVICE_IN);
                registry.ExtentTarget(allocation.extentSource, what)(batch, node);
                registry.Provide(allocation.identity, node, RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
                registry.ProvideImage(allocation.identity, node, RenderTargetNodeConfig::RENDER_TARGET);
            }
            (*allocated_)[allocation.identity] = node;
        }
    }

    void Wire(RenderGraph* graph, ConnectionBatch& batch, const SdiStageCommon& common,
              SchemaResourceRegistry& registry) {
        if (!allocated_) Allocate(graph, batch, common, registry);
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
                // A sampled view states its type up front (its producer must add SAMPLED usage);
                // storage bindings keep the gatherer's shader-resolved type.
                registry.ApplyDescriptor(argument.identity, entry.className, batch, descGatherer, argument.binding,
                                         argument.slot,
                                         argument.form == "sampled" ? argument.descriptorType
                                                                    : VK_DESCRIPTOR_TYPE_MAX_ENUM);
                if (argument.form == "sampled") {
                    // Bound like the image views (refreshed per frame): the gatherer's compile-time
                    // type check has no sampler resource type.
                    batch.Connect(Sampler(graph, batch, common), SamplerNodeConfig::SAMPLER, descGatherer,
                                  GeneratedSystemBinding{argument.samplerBinding, VK_DESCRIPTOR_TYPE_SAMPLER},
                                  SlotRoleModifier(SlotRole::Execute));
                }
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
            for (const auto& argument : entry.arguments) {
                if (argument.form != "storage" && argument.write) {
                    if (const auto* write = registry.ImageWrite(argument.identity)) (*write)(batch, stage.stage);
                }
            }
            uint32_t readSlot = 0;
            for (const auto& [identity, slot] : GpuSystemImageReads(entry)) {
                if (const auto* image = registry.Image(identity, slot)) (*image)(batch, stage.imageReads, readSlot);
                ++readSlot;
            }
            if (stage.imageReadCount > 0) {
                batch.Connect(stage.imageReads, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                              stage.stage, ComputeStageNodeConfig::IMAGE_READ_ARRAY,
                              SlotRoleModifier(SlotRole::Execute));
            }
            // Order after the producer of this frame's inputs (last frame's history has none).
            std::optional<SchemaResourceRegistry::ProducerEntry> handProducer;
            for (const auto& argument : entry.arguments) {
                if (!argument.read || argument.write || argument.slot == FrameSlot::Previous) continue;
                if ((handProducer = registry.ProducerOf(argument.identity))) break;
            }
            std::optional<NodeHandle> barrierSource;
            for (const auto& barrier : manifest_.barriers) {
                if (barrier.after == entry.id && byId.contains(barrier.before)) barrierSource = byId[barrier.before];
            }
            if (const auto producer = GpuSystemOrderingSource(handProducer, barrierSource)) {
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

    /// The node the runner allocated for an identity (a hand reader binds its outputs).
    std::optional<NodeHandle> Allocation(const SchemaIdentity& identity) const {
        if (!allocated_) return std::nullopt;
        auto it = allocated_->find(identity);
        return it == allocated_->end() ? std::nullopt : std::optional<NodeHandle>(it->second);
    }

    const std::vector<Stage>& Stages() const { return stages_; }

private:
    NodeHandle Sampler(RenderGraph* graph, ConnectionBatch& batch, const SdiStageCommon& common) {
        if (!sampler_) {
            sampler_ = graph->AddNode<SamplerNodeType>("generated_systems_sampler");
            batch.Connect(common.device, DeviceNodeConfig::VULKAN_DEVICE_OUT, *sampler_, SamplerNodeConfig::VULKAN_DEVICE_IN);
        }
        return *sampler_;
    }

    GpuSystemManifest manifest_;
    std::vector<Stage> stages_;
    std::map<SchemaIdentity, NodeHandle> lastWriters_;
    std::optional<std::map<SchemaIdentity, NodeHandle>> allocated_;
    std::optional<NodeHandle> sampler_;
};

} // namespace Vixen::RenderGraph
