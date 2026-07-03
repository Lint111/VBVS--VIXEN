#include "Nodes/UIRenderNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"           // GetOwningGraph()->GetFrameSyncSchedule()
#include "Core/FrameSyncSchedule.h"     // SubmitGroup, SyncEdge, FindGroupForNode

#include "VulkanDevice.h"
#include "VulkanSwapChain.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Factory.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>

namespace Vixen::RenderGraph {

namespace {
// Resolve an RmlUi asset path. vixen_stage_assets stages assets next to demo binaries, so the
// configured relative path (e.g. "assets/ui/hud.rml") resolves when the CWD is the exe dir. A consumer
// that runs from elsewhere (the UNDERTOW host) wouldn't find it; fall back to the engine source tree
// via VIXEN_UI_ASSET_SOURCE_DIR (which points at .../RenderGraph/assets, so strip a leading "assets/").
std::string ResolveUiAsset(const std::string& configured) {
    // Dev override (highest priority): point the UI loader at an authoritative content tree on disk
    // (e.g. <repo>/core/content/core) via VIXEN_UI_SOURCE_DIR, so the watched/loaded files are the
    // authored ones. The configured path is relative under "assets/" (e.g. "assets/ui/hud.rml"); strip
    // that prefix and resolve against the source dir (→ <dir>/ui/hud.rml). Pairs with VIXEN_UI_LIVE.
    if (const char* srcDir = std::getenv("VIXEN_UI_SOURCE_DIR")) {
        std::string rel = configured;
        const std::string prefix = "assets/";
        if (rel.rfind(prefix, 0) == 0) rel = rel.substr(prefix.size());
        std::filesystem::path candidate = std::filesystem::path(srcDir) / rel;
        if (std::filesystem::exists(candidate)) return candidate.string();
    }
    if (std::filesystem::exists(configured)) return configured;
#ifdef VIXEN_UI_ASSET_SOURCE_DIR
    std::string rel = configured;
    const std::string prefix = "assets/";
    if (rel.rfind(prefix, 0) == 0) rel = rel.substr(prefix.size());
    std::filesystem::path candidate = std::filesystem::path(VIXEN_UI_ASSET_SOURCE_DIR) / rel;
    if (std::filesystem::exists(candidate)) return candidate.string();
#endif
    return configured;  // let RmlUi log the miss against the configured path
}

// Newest mtime across the RML document and every sibling *.rcss (the stylesheets it may @import or be
// linked to). Used by the live hot-reload to detect an on-disk edit to either the doc or its styles.
std::filesystem::file_time_type LatestUiMtime(const std::string& rmlPath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::file_time_type latest = fs::last_write_time(rmlPath, ec);
    fs::path dir = fs::path(rmlPath).parent_path();
    std::error_code dec;
    for (fs::directory_iterator it(dir, dec), end; it != end; it.increment(dec)) {
        if (it->path().extension() == ".rcss") {
            std::error_code fec;
            auto t = fs::last_write_time(it->path(), fec);
            if (!fec && t > latest) latest = t;
        }
    }
    return latest;
}

// Make a memory-loaded RML document self-contained: inject the ALREADY-CONCATENATED RCSS text (the
// host already joined the ui_document's ordered Rcss list into one stylesheet string) as a <style>
// block in <head>, so LoadDocumentFromMemory needs no FileInterface path resolution for the stylesheet.
// Any <link type="text/rcss" .../> in the source RML is dropped (the inline <style> replaces it).
std::string InlineRcss(const std::string& rml, const std::string& rcss) {
    std::string out;
    out.reserve(rml.size() + rcss.size() + 32);
    size_t pos = 0;
    while (pos < rml.size()) {
        // Drop any <link ... type="text/rcss" ...> line (the on-disk stylesheet reference).
        size_t linkPos = rml.find("<link", pos);
        size_t headEnd = rml.find("</head>", pos);
        if (linkPos != std::string::npos && (headEnd == std::string::npos || linkPos < headEnd)) {
            size_t tagEnd = rml.find('>', linkPos);
            if (tagEnd == std::string::npos) { out.append(rml, pos, std::string::npos); break; }
            std::string tag = rml.substr(linkPos, tagEnd - linkPos + 1);
            if (tag.find("text/rcss") != std::string::npos) {
                out.append(rml, pos, linkPos - pos);
                pos = tagEnd + 1;
                continue;
            }
            // Not an RCSS link — copy through up to and including this tag, keep scanning.
            out.append(rml, pos, tagEnd - pos + 1);
            pos = tagEnd + 1;
            continue;
        }
        break;
    }
    out.append(rml, pos, std::string::npos);

    if (!rcss.empty()) {
        size_t headEnd = out.find("</head>");
        std::string styleBlock = "<style>" + rcss + "</style>";
        if (headEnd != std::string::npos)
            out.insert(headEnd, styleBlock);
        else
            out = styleBlock + out;   // no <head> — prepend so the style still registers
    }
    return out;
}
}  // namespace

std::unique_ptr<NodeInstance> UIRenderNodeType::CreateInstance(const std::string& instanceName) const {
    return std::make_unique<UIRenderNode>(instanceName, const_cast<UIRenderNodeType*>(this));
}

UIRenderNode::UIRenderNode(const std::string& instanceName, NodeType* nodeType)
    : TypedNode<UIRenderNodeConfig>(instanceName, nodeType) {}

void UIRenderNode::SetupImpl(TypedSetupContext& /*ctx*/) {}

void UIRenderNode::FreeCommandBuffers() {
    if (!commandBuffers_.empty() && commandPool_ && device_ != VK_NULL_HANDLE)
        vkFreeCommandBuffers(device_, commandPool_, static_cast<uint32_t>(commandBuffers_.size()), commandBuffers_.data());
    commandBuffers_.clear();
}

void UIRenderNode::CompileImpl(TypedCompileContext& ctx) {
    VulkanDevice* device = ctx.In(UIRenderNodeConfig::VULKAN_DEVICE);
    Vixen::Vulkan::Resources::IRenderTarget* sc = ctx.In(UIRenderNodeConfig::SWAPCHAIN_INFO);
    commandPool_ = ctx.In(UIRenderNodeConfig::COMMAND_POOL);
    renderPass_ = ctx.In(UIRenderNodeConfig::RENDER_PASS);

    device_ = device->device;
    queue_ = device->queue;
    fpQueueSubmit2_ = device->fpQueueSubmit2;  // per-device PFN, refreshed each compile (valid after device-loss recovery)
    extent_ = sc->GetExtent();

    composite_ = GetParameterValue<bool>(UIRenderNodeConfig::PARAM_COMPOSITE, false);

    if (!initialized_) {
        // One-time: the RmlUi render interface/pipeline (built against the consumed render pass —
        // render passes of the same colour format are compatible, so the pipeline survives a resize),
        // RmlUi global init, the context, and the document.
        renderInterface_.Init(device->device, *device->gpu, device->queue, device->graphicsQueueIndex,
                              device->gpuMemoryProperties, commandPool_, renderPass_);
        Rml::SetSystemInterface(&systemInterface_);
        Rml::SetRenderInterface(&renderInterface_);
        Rml::Initialise();
        const std::string fontPath = ResolveUiAsset(GetParameterValue<std::string>(UIRenderNodeConfig::FONT_PATH, "assets/ui/LatoLatin-Regular.ttf"));
        const std::string docPath = ResolveUiAsset(GetParameterValue<std::string>(UIRenderNodeConfig::RML_DOCUMENT_PATH, "assets/ui/demo.rml"));
        Rml::LoadFontFace(fontPath);
        context_ = Rml::CreateContext("vixen_ui", Rml::Vector2i(static_cast<int>(extent_.width), static_cast<int>(extent_.height)));
        if (context_) {
            // S1b: construct the "hud" data model with scalars + struct/array list bindings.
            // RegisterStruct<T> / RegisterArray<Container> must be called before Bind() or
            // LoadDocument() — type info must exist before the document references the vars.
            if (Rml::DataModelConstructor c = context_->CreateDataModel("hud")) {
                if (auto fh = c.RegisterStruct<HudFaction>()) {
                    fh.RegisterMember("name",          &HudFaction::name);
                    fh.RegisterMember("grievance",     &HudFaction::grievance);
                    fh.RegisterMember("focused",       &HudFaction::focused);
                    fh.RegisterMember("known",         &HudFaction::known);
                    fh.RegisterMember("inLens",        &HudFaction::inLens);
                    // T3 Juice: recentChanged drives data-class-changed on the faction row → .changed CSS pulse.
                    fh.RegisterMember("recentChanged", &HudFaction::recentChanged);
                }
                if (auto eh = c.RegisterStruct<HudEvent>()) {
                    eh.RegisterMember("kind", &HudEvent::kind);
                    eh.RegisterMember("tick", &HudEvent::tick);
                }
                if (auto ih = c.RegisterStruct<HudInspect>()) {
                    ih.RegisterMember("selected",     &HudInspect::selected);
                    ih.RegisterMember("name",         &HudInspect::name);
                    ih.RegisterMember("maxGrievance", &HudInspect::maxGrievance);
                    ih.RegisterMember("strength",     &HudInspect::strength);
                    ih.RegisterMember("topRelName",   &HudInspect::topRelName);
                    ih.RegisterMember("topRelSig",    &HudInspect::topRelSig);
                    ih.RegisterMember("cause",        &HudInspect::cause);
                }
                c.RegisterArray<std::vector<HudFaction>>();
                c.RegisterArray<std::vector<HudEvent>>();
                c.Bind("tick",            &tick_);
                c.Bind("bodyCount",       &bodyCount_);
                c.Bind("activeLensName",  &activeLensName_);
                c.Bind("activeLensCount", &activeLensCount_);
                c.Bind("factions",        &factions_);
                c.Bind("events",          &events_);
                c.Bind("inspect",         &inspect_);
                hudModel_ = c.GetModelHandle();
            }
            if (haveDocSource_) {
                // Baked delivery (Phase A): the host already fetched core:hud from the content pack and
                // pushed it via SetDocumentSource. Inline the RCSS so the memory doc is self-contained
                // (no FileInterface path resolution needed).
                std::string rml = InlineRcss(docSourceRml_, docSourceRcss_);
                document_ = context_->LoadDocumentFromMemory(rml, "core:hud");
            } else {
                document_ = context_->LoadDocument(docPath);
            }
            if (document_) document_->Show();
            // Cache the resolved path + initial mtime so the recompile branch can detect on-disk edits
            // (live hot-reload; dormant unless VIXEN_UI_LIVE is set). Meaningless (and skipped) for the
            // memory-loaded path — there's no on-disk file to watch.
            if (!haveDocSource_) {
                resolvedDocPath_ = docPath;
                lastUiWriteTime_ = LatestUiMtime(docPath);
            }
        }
        initialized_ = true;
    } else if (context_) {
        // Recompile (window resize): RenderPassNode/FramebufferNode rebuilt the render pass +
        // framebuffers for the new extent; just re-fit the RmlUi document to the new size.
        context_->SetDimensions(Rml::Vector2i(static_cast<int>(extent_.width), static_cast<int>(extent_.height)));

        // Live hot-reload (dev only). CPU-SIDE DOCUMENT SWAP ONLY — never touch the persistent GPU sync
        // objects (the destroy-while-in-flight race that kernel-panics WSL). Old document geometry routes
        // through the render interface's frames-in-flight deferred-delete; UnloadDocument defers the C++
        // destroy to the next Context::Update(). The "hud" data model is Context-level so it survives the
        // reload — the new document re-binds via data-model="hud", and SetHudView keeps feeding it.
        // ClearStyleSheetCache() must precede LoadDocument so edited RCSS actually takes effect.
        if (std::getenv("VIXEN_UI_LIVE") && !resolvedDocPath_.empty()) {
            std::filesystem::file_time_type mtime = LatestUiMtime(resolvedDocPath_);
            if (mtime > lastUiWriteTime_) {
                Rml::Factory::ClearStyleSheetCache();
                if (document_) context_->UnloadDocument(document_);
                document_ = context_->LoadDocument(resolvedDocPath_);
                if (document_) document_->Show();
                lastUiWriteTime_ = mtime;
            }
        }
    }

    // (Re)build the per-image GPU sync objects (command buffers + composite present semaphores) ONLY
    // when the swapchain image count actually changes (or on first compile). This node is a per-frame
    // recompile target in composite mode: the host marks the voxel scene dirty every Play-mode frame,
    // and the recompile cascades provider→dependents down the compute→UI handoff edge, so CompileImpl
    // runs ~every frame. Destroying these objects unconditionally each recompile would free a command
    // buffer that is still pending execution and — worse — a present-wait semaphore the presentation
    // engine may still hold (the recompile path skips vkDeviceWaitIdle by design). On WSL/Dozen that
    // destroy-while-in-flight race faults dxgkrnl and kernel-panics the whole VM. The image count is
    // stable across a pure recompile (it only changes on a real swapchain format/size change, which
    // carries its own pause+recreation), so guarding on it makes the steady state a no-op while still
    // rebuilding correctly on an actual resize.
    const uint32_t imageCount = sc->GetImageCount();
    const bool rebuildSync = (imageCount != syncImageCount_) || commandBuffers_.empty();
    if (rebuildSync) {
        FreeCommandBuffers();
        commandBuffers_.resize(imageCount);
        VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbai.commandPool = commandPool_;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
        vkAllocateCommandBuffers(device_, &cbai, commandBuffers_.data());

        // Composite mode: one owned "ui complete" semaphore per swapchain image. This is the present-wait
        // semaphore — kept distinct from the per-image compute→UI handoff this node waits on, since a
        // binary semaphore is one-signal/one-wait.
        DestroyCompositeSemaphores();
        if (composite_) {
            uiCompleteSemaphores_.resize(imageCount);
            VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            for (auto& sem : uiCompleteSemaphores_) {
                vkCreateSemaphore(device_, &sci, nullptr, &sem);
            }
        }
        syncImageCount_ = imageCount;
    }
}

void UIRenderNode::DestroyCompositeSemaphores() {
    if (device_ != VK_NULL_HANDLE) {
        for (VkSemaphore sem : uiCompleteSemaphores_) {
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device_, sem, nullptr);
        }
    }
    uiCompleteSemaphores_.clear();
}

void UIRenderNode::RecordFrame(VkCommandBuffer cmd, VkFramebuffer framebuffer) {
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &bi);

    VkClearValue clear{};
    clear.color = {{0.05f, 0.05f, 0.08f, 1.0f}};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = renderPass_;
    rp.framebuffer = framebuffer;
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = extent_;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    renderInterface_.BeginFrame(cmd, extent_);
    if (context_) {
        context_->SetDimensions(Rml::Vector2i(static_cast<int>(extent_.width), static_cast<int>(extent_.height)));
        context_->Update();
        context_->Render();
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

void UIRenderNode::ExecuteImpl(TypedExecuteContext& ctx) {
    const uint32_t imageIndex = ctx.In(UIRenderNodeConfig::IMAGE_INDEX);
    const uint32_t currentFrameIndex = ctx.In(UIRenderNodeConfig::CURRENT_FRAME_INDEX);
    const std::vector<VkSemaphore>& imageAvailable = ctx.In(UIRenderNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    const std::vector<VkSemaphore>& renderComplete = ctx.In(UIRenderNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
    VkFence inFlightFence = ctx.In(UIRenderNodeConfig::IN_FLIGHT_FENCE);
    const std::vector<VkFramebuffer>& framebuffers = ctx.In(UIRenderNodeConfig::FRAMEBUFFERS);

    if (imageIndex == UINT32_MAX || imageIndex >= commandBuffers_.size() || imageIndex >= framebuffers.size()) return;

    // Composite: the compute→UI ordering is carried SOLELY by the baked timeline waitEdge (P5b M3) —
    // no binary handoff wait here. This node signals its own per-image semaphore for present. Standalone
    // (S0, no timeline): wait imageAvailable[frame] (the acquire), signal renderComplete[image].
    if (composite_ && imageIndex >= uiCompleteSemaphores_.size()) return;
    VkSemaphore signalSem = composite_ ? uiCompleteSemaphores_[imageIndex] : renderComplete[imageIndex];

    // This UI submit is the frame's last submit, so it resets + owns the frame fence (in composite mode
    // the upstream compute submitted with no fence). Safe: FrameSyncNode already waited on it.
    vkResetFences(device_, 1, &inFlightFence);

    VkCommandBuffer cmd = commandBuffers_[imageIndex];
    RecordFrame(cmd, framebuffers[imageIndex]);

    // P5b M1: read timeline primitives (Optional — VK_NULL_HANDLE / 0 if not wired)
    VkSemaphore timelineSem = ctx.In(UIRenderNodeConfig::TIMELINE_SEMAPHORE_IN);
    uint64_t frameBase = ctx.In(UIRenderNodeConfig::TIMELINE_FRAME_BASE_IN);

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmd;

    std::vector<VkSemaphoreSubmitInfo> waits, signals;

    // Standalone (S0) ONLY: wait the binary WSI acquire (imageAvailable). The composite graph has no
    // imageAvailable wait here — the upstream compute waits the acquire, and compute→UI is ordered by
    // the timeline waitEdge below (P5b M3 dropped the binary compute→UI handoff).
    if (!composite_) {
        VkSemaphoreSubmitInfo binaryWait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        binaryWait.semaphore = imageAvailable[currentFrameIndex];
        binaryWait.value     = 0;  // binary semaphore: value ignored
        binaryWait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        waits.push_back(binaryWait);
    }

    // Timeline WAITS (UI is the consumer): one per baked waitEdge. In composite this is now the SOLE
    // compute→UI ordering (the compute's GENERAL write → UI's GENERAL color-attachment access edge).
    if (timelineSem != VK_NULL_HANDLE) {
        const FrameSyncSchedule& sched = GetOwningGraph()->GetFrameSyncSchedule();
        if (const SubmitGroup* grp = FindGroupForNode(sched, this)) {
            for (uint32_t idx : grp->waitEdges) {
                VkSemaphoreSubmitInfo twait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                twait.semaphore = timelineSem;
                twait.value     = sched.edges[idx].timelineOffset + frameBase;
                twait.stageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                waits.push_back(twait);
            }
        }
    }

    // Binary signal (uiComplete / renderComplete — present waits on this)
    VkSemaphoreSubmitInfo binSig{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    binSig.semaphore = signalSem;
    binSig.value     = 0;  // binary semaphore: value ignored
    binSig.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signals.push_back(binSig);

    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount   = static_cast<uint32_t>(waits.size());
    si.pWaitSemaphoreInfos      = waits.data();
    si.commandBufferInfoCount   = 1;
    si.pCommandBufferInfos      = &cmdInfo;
    si.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
    si.pSignalSemaphoreInfos    = signals.data();
    fpQueueSubmit2_(queue_, 1, &si, inFlightFence);

    ctx.Out(UIRenderNodeConfig::COMMAND_BUFFERS, cmd);
    ctx.Out(UIRenderNodeConfig::RENDER_COMPLETE_SEMAPHORE, signalSem);
}

void UIRenderNode::SetHudView(int tick, int bodyCount, int activeLens, int activeLensCount,
                              std::span<const HudFactionIn> factions,
                              std::span<const HudEventIn> events,
                              const HudInspectIn& inspect) {
    tick_      = tick;
    bodyCount_ = bodyCount;
    // Map the raw LensKind (0-3) to its display name for the HUD label (matches the C# LensKind enum).
    static const char* const kLensNames[] = { "None", "Intel", "Logistics", "Threat" };
    activeLensName_  = (activeLens >= 0 && activeLens < 4) ? kLensNames[activeLens] : "None";
    activeLensCount_ = activeLensCount;

    // T3 Juice: a faction is "recently changed" when its recentEventAge is within the K-tick window
    // (matching the C# RecentEventK constant in UndertowSim.ProjectFrame; 255 = no recent event).
    static constexpr uint8_t kJuiceK = 20;
    factions_.clear();
    factions_.reserve(factions.size());
    for (const HudFactionIn& f : factions)
        factions_.push_back({f.name ? Rml::String(f.name) : Rml::String{},
                             f.grievance, f.focused, f.known, f.inLens,
                             f.recentEventAge < kJuiceK});

    events_.clear();
    events_.reserve(events.size());
    for (const HudEventIn& e : events)
        events_.push_back({e.kind ? Rml::String(e.kind) : Rml::String{}, e.tick});

    inspect_.selected     = inspect.selected;
    inspect_.name         = inspect.name ? inspect.name : "";
    inspect_.maxGrievance = inspect.maxGrievance;
    inspect_.strength     = inspect.strength;
    inspect_.topRelName   = inspect.topRelName ? inspect.topRelName : "";
    inspect_.topRelSig    = inspect.topRelSig;
    inspect_.cause        = inspect.cause ? inspect.cause : "";

    if (hudModel_) {
        hudModel_.DirtyVariable("tick");
        hudModel_.DirtyVariable("bodyCount");
        hudModel_.DirtyVariable("activeLensName");
        hudModel_.DirtyVariable("activeLensCount");
        hudModel_.DirtyVariable("factions");
        hudModel_.DirtyVariable("events");
        hudModel_.DirtyVariable("inspect");
    }
}

void UIRenderNode::SetHudData(int tick, int bodyCount) {
    SetHudView(tick, bodyCount, 0, 0, {}, {}, HudInspectIn{false, "", 0.0f, 0.0f, "", 0.0f, ""});
}

void UIRenderNode::SetDocumentSource(const std::string& rml, const std::string& rcss) {
    docSourceRml_ = rml;
    docSourceRcss_ = rcss;
    haveDocSource_ = !rml.empty();
    if (initialized_) MarkNeedsRecompile();   // re-load on the next compile so the doc swaps in
}

void UIRenderNode::CleanupImpl(TypedCleanupContext& ctx) {
    if (device_ == VK_NULL_HANDLE) return;

    // The per-image command buffers AND the composite present semaphores are PERSISTENT across a
    // recompile and are torn down only on FINAL teardown. Rationale:
    //   * The recompile path runs WITHOUT vkDeviceWaitIdle by design (a wait on a submit still blocked
    //     on an un-signalled acquire semaphore would deadlock — the documented resize freeze).
    //   * In composite mode this node is a per-frame recompile target (the host marks the voxel scene
    //     dirty every Play-mode frame and the recompile cascades down the compute→UI handoff edge), so
    //     destroying here would free a command buffer still pending execution and a present-wait
    //     semaphore the presentation engine may still hold — a destroy-while-in-flight race that faults
    //     dxgkrnl and kernel-panics the WSL VM. Keeping them persistent makes the steady-state recompile
    //     a no-op (CompileImpl rebuilds them only when the swapchain image count actually changes, which
    //     happens under the swapchain-recreation render pause where prior frames have already drained).
    //
    // We likewise do NOT tear RmlUi (context/document/pipeline) down on recompile: a resize triggers a
    // recompile (Cleanup → Compile), and distinguishing that from a true shutdown via NeedsRecompile()
    // is unreliable under the cascading recompiles a resize produces. RmlUi is torn down only on FINAL
    // teardown, in the strict order required by RmlUi (see below).
    // KI-004 root cause lived here: this early-return previously covered EVERY non-final reason,
    // INCLUDING CleanupReason::DeviceLost — so the per-image command buffers (allocated from the
    // now-destroyed device's pool) and RmlUi's GPU objects survived a device-loss teardown, and
    // CompileImpl's image-count guard (count unchanged post-recovery) reused them: the first
    // post-recovery frame hit vkBeginCommandBuffer on a freed handle (loader-level
    // invalid-commandBuffer abort). Persistence is only valid while the DEVICE survives, i.e.
    // across a Recompile. On DeviceLost the full teardown below is safe: RecoverFromDeviceLoss
    // calls WaitForGraphDevicesIdle() before the teardown pass, and vkDestroy*/vkFree* on a lost
    // device are spec-required to remain safe.
    if (ctx.reason == CleanupReason::Recompile) {
        return;  // recompile: keep all persistent resources (command buffers + present semaphores + RmlUi)
    }

    // Final teardown or device loss: the device was drained (RenderGraph DeInitialize's
    // vkDeviceWaitIdle, or RecoverFromDeviceLoss's WaitForGraphDevicesIdle), so freeing is safe.
    FreeCommandBuffers();
    DestroyCompositeSemaphores();  // owned per-image present semaphores (composite mode)
    syncImageCount_ = 0;

    // RmlUi teardown — STRICT ORDER (RmlUi asserts otherwise, RenderInterface.cpp:45 "RenderInterface is
    // being destroyed but still actively referenced ... destroy it AFTER Rml::Shutdown"). renderInterface_
    // is a member, so it is destructed by ~UIRenderNode; RmlUi keeps a live reference to it (set via
    // Rml::SetRenderInterface in CompileImpl) until Rml::Shutdown() drops it. We must therefore:
    //   1. RemoveContext  — release the context + its document (their geometry/textures live in our
    //                       render interface, so they must go before the interface's GPU objects).
    //   2. Rml::Shutdown  — RmlUi releases its references to the render + system interfaces.
    //   3. renderInterface_.Shutdown — now safe to destroy our pipeline/descriptors/textures (device idle).
    // Guard on initialized_ so a teardown that never reached the one-time RmlUi init is a no-op, and reset
    // it so a second FinalTeardown (defensive) does not double-shutdown RmlUi.
    if (initialized_) {
        if (context_) {
            Rml::RemoveContext("vixen_ui");  // releases context_ + document_ (owned by the context)
            context_ = nullptr;
            document_ = nullptr;
        }
        Rml::Shutdown();              // RmlUi drops its references to renderInterface_ / systemInterface_
        renderInterface_.Shutdown(); // destroy our GPU objects AFTER RmlUi no longer references the interface
        initialized_ = false;
    }
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::UIRenderNodeType);
