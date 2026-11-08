#include "VulkanGraphApplication.h"
#include "VulkanSwapChain.h"
#include "FrameCapture.h"
#include "Nodes/SwapChainNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/RenderTargetNode.h"
#include "Nodes/StorageBufferNode.h"
#include "Nodes/BodyOctreeSceneNode.h"
#include "Nodes/CameraNode.h"
#include "Nodes/PhotonCellParamsConfigNode.h"
#include "Nodes/PhotonCellTableNode.h"
#include "Debug/RenderTargetReadback.h"
#include "HitAccumulation.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

// W3b diag readback (VIXEN_HIT_ACCUM_PROBE_LOG): runs at SHUTDOWN, after the
// last frame completed — an in-loop readback races the flight ring (waitIdle
// also waits the NEXT frame's already-submitted CLEAR and maps a zeroed
// table; found live at the rev-2 gate: CPU-predict 36826 vs GPU 0). Maps the
// table AND the hit records, runs the CPU mirror over the same records with
// the same inputs, logs both sides. Counts are rounding-immune; totals and
// per-mip histograms compare EXACTLY, occupancy as >= (benign duplicates).
void VulkanGraphApplication::RunHitAccumDiagReadback(uint64_t sampleFrame) {
    if (!renderGraph) return;
        auto* tableNode = static_cast<StorageBufferNode*>(renderGraph->GetInstance(hitAccumTableBuffer_));
        auto* hrBufDiag = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("hit_record_buffer"));
        auto* bodySceneDiag = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstanceByName("body_octree_scene"));
        auto* devInstDiag = static_cast<DeviceNode*>(renderGraph->GetInstanceByName("main_device"));
        auto* camInstDiag = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_));
        if (tableNode && hrBufDiag && bodySceneDiag && devInstDiag && devInstDiag->GetVulkanDevice() && camInstDiag) {
            auto* devDiag = devInstDiag->GetVulkanDevice();
            vkDeviceWaitIdle(devDiag->device);

            // --- GPU side: scan the table (two-word key, 14-word stride) ---
            uint32_t gpuOccupied = 0, gpuTotal = 0;
            uint32_t gpuMipHist[16] = {};
            // B2 fix: the target epoch is KNOWN. For the mid-run diag frame
            // (sampleFrame != 0) it's diagSampledEpoch_, captured AT PreTick the
            // instant it was stamped on the sampled frame (batch-24 third layer
            // -- the LIVE hitAccumFrameEpoch_ races ahead by the time this
            // deferred readback fires, since PostTick can trail PreTick by up to
            // MAX_FRAMES_IN_FLIGHT-1 unretired frames). The shutdown call
            // (sampleFrame == 0) has no such race -- it runs after the last
            // frame, so the live counter IS the final epoch.
            uint32_t maxEpoch = (sampleFrame != 0 ? diagSampledEpoch_ : hitAccumFrameEpoch_) & 0xFFFFFu;  // hoisted: the radiance readback below re-scans at this epoch
            constexpr uint32_t kStrideWords = 14;  // HitAccumEntryGpu, 56 B (W3c-2 repInstIdx)
            // Batch-27 JOB 1 task 2: histogram the TABLE'S ACTUAL keyHi epoch
            // stamps at readback time, [maxEpoch-4 .. maxEpoch+4] plus an
            // "other" bucket -- shows exactly which epoch(s) the shader
            // stamped for the sampled frame's work, alongside maxEpoch, so a
            // mismatch between what diagSampledEpoch_ names and what the
            // table actually holds is visible directly instead of inferred
            // from the occupied/ratio numbers alone.
            constexpr int kEpochHistRadius = 4;
            uint32_t epochHist[2 * kEpochHistRadius + 1] = {};
            uint32_t epochHistOther = 0;
            if (void* tmapped = tableNode->MapForReadback(devDiag)) {
                const auto* words = reinterpret_cast<const uint32_t*>(tmapped);
                // Entries persist across frames (epoch-stamped, no clear):
                // scan only entries stamped with the single target epoch above.
                for (uint32_t s = 0; s < kHitAccumTableCapacity; ++s) {
                    const uint32_t keyLo = words[s * kStrideWords + 0];
                    if ((keyLo & 0x80000000u) == 0u) continue;
                    const uint32_t keyHi = words[s * kStrideWords + 1];
                    const uint32_t stampEpoch = keyHi & 0xFFFFFu;
                    const int64_t delta = static_cast<int64_t>(stampEpoch) - static_cast<int64_t>(maxEpoch);
                    if (delta >= -kEpochHistRadius && delta <= kEpochHistRadius) {
                        ++epochHist[delta + kEpochHistRadius];
                    } else {
                        ++epochHistOther;
                    }
                    if (stampEpoch != maxEpoch) continue;  // stale epoch
                    const uint32_t cnt = words[s * kStrideWords + 2];
                    ++gpuOccupied;
                    gpuTotal += cnt;
                    gpuMipHist[(keyHi >> 20) & 0xFu] += cnt;
                }
                tableNode->UnmapReadback(devDiag);
            }
            if (mainLogger) {
                std::string histLine = "[HitAccumEpochHist] sampledEpoch=" + std::to_string(maxEpoch) + " ";
                for (int d = -kEpochHistRadius; d <= kEpochHistRadius; ++d) {
                    histLine += (d == 0 ? "[" : "") + std::to_string(d) + "=" +
                                std::to_string(epochHist[d + kEpochHistRadius]) + (d == 0 ? "]" : "") + " ";
                }
                histLine += "other=" + std::to_string(epochHistOther);
                mainLogger->Info(histLine);
            }

            // --- CPU side: the mirror over the same records ---
            // coef replicated from RaySizeCoefNode.cpp:31 (2·tan((fovYRad/height)·0.5);
            // lastComputed_ is private — SYNC with that line). bias = 0.0 (pinhole,
            // ray_size_bias's own SetValue). detail = the resolved member.
            const float fovYRad = glm::radians(45.0f);
            const float coef = 2.0f * std::tan((fovYRad / static_cast<float>(height)) * 0.5f);
            const float bias = 0.0f;
            const CameraData& diagCam = camInstDiag->GetCurrentCameraData();
            const glm::vec3 camPos = diagCam.cameraPos;
            const glm::vec3 camFwd = diagCam.cameraDir;
            const auto& instancesDiag = bodySceneDiag->GetInstances();
            uint32_t cpuEngaged = 0;
            uint32_t cpuHit = 0;
            uint32_t cpuLeanEligible = 0;  // W-LEAN L1: footprint ≥ 2·detail ⇔ w==1 (fully cell-resolved)
            // NOTE: the wave's CELL_RESOLVED stamp is NOT readable here — the
            // frame is pipelined (march@131 rewrites flags AFTER every
            // consumer), so a shutdown readback always sees post-march
            // records. In-window positive evidence = a temporary stamp
            // visualization in the resolve (the W-LEAN L1 gate's own method).
            uint32_t cpuMipHist[16] = {};
            std::unordered_map<uint64_t, uint32_t> cpuKeys;  // (hi<<32|lo) -> count
            if (void* rmapped = hrBufDiag->MapForReadback(devDiag)) {
                const auto* bytes = reinterpret_cast<const uint8_t*>(rmapped);
                const size_t recCount = static_cast<size_t>(hrBufDiag->GetSizeBytes()) / 64u;
                for (size_t i = 0; i < recCount; ++i) {
                    const uint8_t* rec = bytes + i * 64u;
                    const uint32_t flags = *reinterpret_cast<const uint32_t*>(rec + 44);
                    if ((flags & 0x1u) == 0u) continue;
                    const float hitT = *reinterpret_cast<const float*>(rec + 28);
                    const glm::vec3 worldPos = *reinterpret_cast<const glm::vec3*>(rec + 32);
                    const uint32_t instIdx = *reinterpret_cast<const uint32_t*>(rec + 48);
                    if (instIdx >= instancesDiag.size()) continue;
                    const float footprint = hitT * coef + bias;
                    ++cpuHit;
                    if (footprint >= 2.0f * hitAccumDetailSize0_) ++cpuLeanEligible;
                    using namespace Vixen::SVO::HitAccum;
                    const uint32_t mip = SelectMip(footprint, hitAccumDetailSize0_);
                    if (mip == 0u) continue;
                    const uint32_t recipeId = instancesDiag[instIdx].material.recipeId;
                    const float cellSize = CellSize(mip, hitAccumDetailSize0_);
                    const glm::ivec3 cell = glm::ivec3(glm::floor(worldPos / cellSize));
                    const glm::ivec3 anchor = AnchorCell(camPos, camFwd, coef, hitAccumDetailSize0_, mip);
                    const uint32_t lo = PackCellKeyLo(cell, anchor);
                    if (lo == 0u) continue;  // fail-soft, same as the GPU
                    const uint32_t hi = PackCellKeyHi(recipeId, mip);
                    if (hi == 0u) continue;
                    ++cpuEngaged;
                    cpuMipHist[mip] += 1u;
                    cpuKeys[(static_cast<uint64_t>(hi) << 32) | lo] += 1u;
                }
                hrBufDiag->UnmapReadback(devDiag);
            }

            auto histStr = [](const uint32_t* h) {
                std::string s;
                for (int m = 1; m < 16; ++m) if (h[m]) s += " m" + std::to_string(m) + "=" + std::to_string(h[m]);
                return s.empty() ? std::string(" (none)") : s;
            };
            if (mainLogger) {
                const std::string frameTag = sampleFrame > 0
                    ? (" frame=" + std::to_string(sampleFrame))
                    : " frame=shutdown";
                mainLogger->Info("[HitAccumDiag]" + frameTag + " GPU: occupied=" + std::to_string(gpuOccupied) +
                                 " total=" + std::to_string(gpuTotal) + " mips:" + histStr(gpuMipHist));
                mainLogger->Info("[HitAccumDiag] CPU-predict: distinctKeys=" + std::to_string(cpuKeys.size()) +
                                 " engaged=" + std::to_string(cpuEngaged) + " mips:" + histStr(cpuMipHist) +
                                 " (detail=" + std::to_string(hitAccumDetailSize0_) + " coef=" + std::to_string(coef) + ")");
                mainLogger->Info("[HitAccumDiag] Lean: eligible=" + std::to_string(cpuLeanEligible) +
                                 " of hit=" + std::to_string(cpuHit) +
                                 (hitAccumLeanEnabled_ ? " (LEAN ON — eligible pixels skip their per-pixel traces)"
                                                       : " (lean off)"));
            }

            // W3c-2/3: the cell shade's output. Since W3c-3 the radiance is
            // STICKY (dead slots keep their last value+key for the EMA), so
            // "shaded" is gated on TABLE liveness + a bit-exact key match
            // between the radiance alpha and the slot's keyLo — the same
            // reachability rule the resolve enforces. Gate: shaded == the
            // table's live occupancy.
            if (hitAccumResolveEnabled_) {
                auto* radNode = static_cast<StorageBufferNode*>(renderGraph->GetInstance(hitAccumCellRadianceBuffer_));
                if (radNode) {
                    void* rmapped2 = radNode->MapForReadback(devDiag);
                    void* tmapped2 = tableNode->MapForReadback(devDiag);
                    if (rmapped2 && tmapped2) {
                        const auto* cells = reinterpret_cast<const float*>(rmapped2);
                        const auto* words = reinterpret_cast<const uint32_t*>(tmapped2);
                        uint32_t shaded = 0, lit = 0;
                        for (uint32_t s = 0; s < kHitAccumTableCapacity; ++s) {
                            const uint32_t keyLo = words[s * kStrideWords + 0];
                            if ((keyLo & 0x80000000u) == 0u) continue;
                            const uint32_t keyHi = words[s * kStrideWords + 1];
                            if ((keyHi & 0xFFFFFu) != maxEpoch) continue;
                            uint32_t radKey;
                            std::memcpy(&radKey, &cells[s * 4u + 3u], 4);
                            if (radKey != keyLo) continue;
                            ++shaded;
                            if (cells[s * 4u + 0u] + cells[s * 4u + 1u] + cells[s * 4u + 2u] > 0.0f) ++lit;
                        }
                        if (mainLogger) {
                            mainLogger->Info("[HitAccumDiag] CellShade: shaded=" + std::to_string(shaded) +
                                             " lit=" + std::to_string(lit) +
                                             " (tableLiveOccupied=" + std::to_string(gpuOccupied) + ")");
                        }
                    }
                    if (rmapped2) radNode->UnmapReadback(devDiag);
                    if (tmapped2) tableNode->UnmapReadback(devDiag);
                }
            }
        }
}

void VulkanGraphApplication::RunPhotonCellDiagReadback(uint64_t sampleFrame) {
    // Composition-level hook only: the table node owns all readback and
    // HitRecord-to-cell comparison logic.
    if (!renderGraph) return;
    if (!photonCellTableBuffer_.IsValid() || !photonCellParamsBuffer_.IsValid()) return;
    auto* tableNode = static_cast<PhotonCellTableNode*>(
        renderGraph->GetInstance(photonCellTableBuffer_));
    auto* paramsNode = static_cast<PhotonCellParamsConfigNode*>(
        renderGraph->GetInstance(photonCellParamsBuffer_));
    auto* hitRecordNode = static_cast<StorageBufferNode*>(
        renderGraph->GetInstanceByName("hit_record_buffer"));
    auto* deviceNode = static_cast<DeviceNode*>(
        renderGraph->GetInstanceByName("main_device"));
    if (!tableNode || !paramsNode || !hitRecordNode || !deviceNode) return;
    tableNode->RunDiagnostic(*hitRecordNode, *paramsNode,
                             deviceNode->GetVulkanDevice(), mainLogger.get(),
                             sampleFrame);
}

void VulkanGraphApplication::PollPendingCaptures() {
    if (!pendingHudCapturePath_.empty()) {
        std::string captureErr;
        const std::string path = pendingHudCapturePath_;
        if (!CaptureHudFrameToPng(path, captureErr) &&
            !pendingHudCapture_ && pendingOffscreenCaptures_.empty() && mainLogger) {
            mainLogger->Error("[VulkanGraphApplication] CaptureHudFrameToPng failed for " + path + ": " + captureErr);
        }
    }
    for (auto it = pendingOffscreenCaptures_.begin(); it != pendingOffscreenCaptures_.end();) {
        std::string captureErr;
        if (!it->request->Poll(captureErr)) {
            ++it;
            continue;
        }
        const std::string path = it->path;
        it = pendingOffscreenCaptures_.erase(it);
        if (!captureErr.empty() && mainLogger) {
            mainLogger->Error("[VulkanGraphApplication] CaptureOffscreenFrameToPng failed for " + path + ": " + captureErr);
        }
    }
}

void VulkanGraphApplication::RunHdrMeterDebugReadback() {
    static uint64_t lastLoggedFrame = ~uint64_t(0);
    if (lastLoggedFrame == frameCounter_ || !renderGraph) return;
    // VIXEN_HDR_METER_DEBUG=<n> logs the first n frames (any non-number: 120).
    const char* framesEnv = std::getenv("VIXEN_HDR_METER_DEBUG");
    const uint64_t loggedFrames = framesEnv && std::strtoull(framesEnv, nullptr, 10) > 1
        ? std::strtoull(framesEnv, nullptr, 10) : 120u;
    if (frameCounter_ >= loggedFrames) return;
    auto* meter = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("exposure_meter_buffer"));
    auto* deviceNode = static_cast<DeviceNode*>(renderGraph->GetInstanceByName("main_device"));
    if (!meter || !deviceNode || !deviceNode->GetVulkanDevice()) return;
    auto* device = deviceNode->GetVulkanDevice();
    vkDeviceWaitIdle(device->device); // observation-only settle point; never used by the meter
    if (void* mapped = meter->MapForReadback(device)) {
        // ExposureHistogram.glsl ExposureMeterState: applied log2 exposure, state word,
        // metered scene EV100 and the clamped log2 target the adaptation approaches.
        float state[4];
        std::memcpy(state, mapped, sizeof(state));
        meter->UnmapReadback(device);
        std::cout << "[HDRMeter] frame=" << frameCounter_ << " log2Exposure=" << state[0]
                  << " meteredEV100=" << state[2] << " targetLog2Exposure=" << state[3]
                  << " exposure=" << std::exp2(state[0]) << "\n";
        lastLoggedFrame = frameCounter_;
    }
}

bool VulkanGraphApplication::CaptureFrameToPng(const std::string& path) {
    // M4b: headless GPU-frame snapshot. Reuses Vixen::Profiler::FrameCapture (already ships the
    // barrier -> vkCmdCopyImageToBuffer -> map -> BGRA->RGBA swizzle -> stbi_write_png sequence,
    // proven in the BenchmarkRunner path) instead of re-deriving that readback here.
    if (!renderGraph) {
        if (mainLogger) mainLogger->Error("[CaptureFrameToPng] RenderGraph not initialized");
        return false;
    }
    // "main_swapchain" is registered by BuildRenderGraph.cpp. SwapChainNode::CompileImpl already
    // calls SetDevice() on itself from its VULKAN_DEVICE_IN connection, so the node IS the device
    // handle we need too — no separate "main_device" lookup.
    auto* swapChainNode = static_cast<Vixen::RenderGraph::SwapChainNode*>(
        renderGraph->GetNodeByName("main_swapchain"));
    if (!swapChainNode) {
        if (mainLogger) mainLogger->Error("[CaptureFrameToPng] 'main_swapchain' node not found");
        return false;
    }
    auto* device = swapChainNode->GetDevice();
    SwapChainPublicVariables* swapVars = swapChainNode->GetSwapchainPublic();
    if (!device || !swapVars) {
        if (mainLogger) mainLogger->Error("[CaptureFrameToPng] swapchain has no device/public vars yet");
        return false;
    }

    if (!pendingFrameCapturePath_.empty() && pendingFrameCapturePath_ != path) {
        if (mainLogger) mainLogger->Error("[CaptureFrameToPng] another capture is pending");
        return false;
    }
    if (!frameCapture_) frameCapture_ = std::make_unique<Vixen::Profiler::FrameCapture>();
    if (!frameCapture_->IsInitialized() && !frameCapture_->Initialize(
            device, device->device, *device->gpu, device->queue, device->graphicsQueueIndex,
            swapVars->Extent.width, swapVars->Extent.height)) {
        if (mainLogger) mainLogger->Error("[CaptureFrameToPng] FrameCapture::Initialize failed");
        return false;
    }

    // FrameCapture writes to <outputPath>/debug_images/<testName>_frame<N>.png (its test-harness
    // convention) rather than an exact path — capture into the caller's directory under that
    // convention, then rename to the exact path requested.
    std::filesystem::path want(path);
    Vixen::Profiler::CaptureConfig cfg;
    cfg.outputPath = want.parent_path().empty() ? std::filesystem::path(".") : want.parent_path();
    cfg.testName = want.stem().string();
    cfg.frameNumber = 0;
    Vixen::Profiler::CaptureResult result;
    if (!pendingFrameCapturePath_.empty()) {
        if (!frameCapture_->Poll(result)) return false;
        pendingFrameCapturePath_.clear();
    } else {
        result = frameCapture_->Capture(swapVars, swapChainNode->GetCurrentImageIndex(), cfg);
        if (result.pending) {
            pendingFrameCapturePath_ = path;
            return false;
        }
    }
    if (!result.success) {
        if (mainLogger) mainLogger->Error("[CaptureFrameToPng] capture failed: " + result.errorMessage);
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(result.savedPath, want, ec);
    if (ec) {
        if (mainLogger) mainLogger->Error("[CaptureFrameToPng] rename to '" + path + "' failed: " + ec.message());
        return false;
    }
    if (mainLogger) mainLogger->Info("[CaptureFrameToPng] wrote " + path + " (" +
                                      std::to_string(result.capturedWidth) + "x" +
                                      std::to_string(result.capturedHeight) + ")");
    return true;
}

bool VulkanGraphApplication::SwapchainImageIsValid() const {
    // Mirrors CaptureFrameToPng's node/vars lookup, but only checks that the currently-acquired
    // image index is in range. An aborted frame (swapchain recreation) leaves currentImageIndex at
    // UINT32_MAX; capturing then fails with "Image index out of range". The host polls this to pick
    // a good frame.
    if (!renderGraph) return false;
    auto* swapChainNode = static_cast<Vixen::RenderGraph::SwapChainNode*>(
        renderGraph->GetNodeByName("main_swapchain"));
    if (!swapChainNode) return false;
    SwapChainPublicVariables* swapVars = swapChainNode->GetSwapchainPublic();
    if (!swapVars) return false;
    return swapChainNode->GetCurrentImageIndex() < swapVars->colorBuffers.size();
}

bool VulkanGraphApplication::CaptureHudFrameToPng(const std::string& path, std::string& err) {
    if (pendingHudCapture_) {
        if (pendingHudCapturePath_ != path) {
            err = "CaptureHudFrameToPng: another capture is pending";
            return false;
        }
        if (!pendingHudCapture_->Poll(err)) return false;
        pendingHudCapture_.reset();
        pendingHudCapturePath_.clear();
        return err.empty();
    }
    if (presentationTarget_ == PresentationTarget::Offscreen) {
        return CaptureOffscreenFrameToPng(path, err);
    }

    // Live lookups every call (mirrors EditorApplication::CaptureFrameToPng / GetWindowHandle's
    // rule) -- the target and device node persist across recompile, but re-resolving by name is
    // this codebase's established pattern for host-facing capture lookups.
    //
    // Reads "main_swapchain", NOT "compute_render_target": the compute dispatch blits its
    // offscreen compute_render_target INTO the swapchain, and UIRenderNode's composite pass then
    // LOADs and draws the HUD directly onto that SAME swapchain image (see BuildRenderGraph.cpp's
    // UI-composite-pass comment) -- compute_render_target is a physically separate VkImage that
    // never receives the HUD draw. Readback derives and restores the composite target's layout.
    if (!renderGraph) {
        err = "CaptureHudFrameToPng: no render graph";
        return false;
    }
    static constexpr const char* kCaptureTargetName = "main_swapchain";
    auto* targetInst = renderGraph->GetInstanceByName(kCaptureTargetName);
    if (!targetInst) {
        err = std::string("CaptureHudFrameToPng: instance '") + kCaptureTargetName + "' not found";
        return false;
    }
    Resource* targetOutput = targetInst->GetOutput(1, 0);  // SWAPCHAIN_PUBLIC (slot 1)
    if (!targetOutput) {
        err = "CaptureHudFrameToPng: capture target has no SWAPCHAIN_PUBLIC output yet (graph not compiled?)";
        return false;
    }
    auto* renderTarget = targetOutput->GetHandle<Vixen::Vulkan::Resources::IRenderTarget*>();
    if (!renderTarget) {
        err = "CaptureHudFrameToPng: SWAPCHAIN_PUBLIC output handle is null";
        return false;
    }

    auto* deviceInst = static_cast<DeviceNode*>(renderGraph->GetInstanceByName("main_device"));
    if (!deviceInst || !deviceInst->GetVulkanDevice()) {
        err = "CaptureHudFrameToPng: 'main_device' not found or has no VulkanDevice";
        return false;
    }
    auto* device = deviceInst->GetVulkanDevice();

    pendingHudCapture_ = Vixen::RenderGraph::Debug::BeginCaptureRenderTargetToPng(
        device, renderTarget, device->queue, device->graphicsQueueIndex, path, err);
    if (!pendingHudCapture_) return false;
    pendingHudCapturePath_ = path;
    err.clear();
    return true;
}

bool VulkanGraphApplication::CaptureOffscreenFrameToPng(const std::string& path, std::string& err) {
    for (const auto& pending : pendingOffscreenCaptures_) {
        if (pending.path == path) {
            err.clear();
            return true;
        }
    }
    if (presentationTarget_ != PresentationTarget::Offscreen) {
        err = "CaptureOffscreenFrameToPng: graph presentation target is not offscreen";
        return false;
    }
    if (!renderGraph) {
        err = "CaptureOffscreenFrameToPng: no render graph";
        return false;
    }
    auto* targetNode = renderGraph->GetInstance(offscreenTargetNode_);
    if (!targetNode) {
        err = "CaptureOffscreenFrameToPng: selected offscreen render target not found";
        return false;
    }
    Resource* targetOutput = targetNode->GetOutput(0, 0);
    if (!targetOutput) {
        err = "CaptureOffscreenFrameToPng: target output is unavailable (graph not compiled?)";
        return false;
    }
    auto* target = targetOutput->GetHandle<Vixen::Vulkan::Resources::IRenderTarget*>();
    if (!target) {
        err = "CaptureOffscreenFrameToPng: target output is null";
        return false;
    }
    auto* deviceNode = static_cast<DeviceNode*>(renderGraph->GetInstance(offscreenDeviceNode_));
    if (!deviceNode || !deviceNode->GetVulkanDevice()) {
        err = "CaptureOffscreenFrameToPng: selected offscreen device not found or has no VulkanDevice";
        return false;
    }
    auto* device = deviceNode->GetVulkanDevice();
    auto pending = Vixen::RenderGraph::Debug::BeginCaptureRenderTargetToPng(
        device, target, device->queue, device->graphicsQueueIndex, path, err);
    if (!pending) return false;
    pendingOffscreenCaptures_.push_back(PendingOffscreenCapture{std::move(pending), path});
    err.clear();
    return true;
}
