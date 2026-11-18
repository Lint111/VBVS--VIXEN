#include "VulkanGraphApplication.h"
#include "FrameTrace.h"
#include "MainCacher.h"
#include "Recipe/RecipeServices.h"            // R841: kernel-owned recipe analysis/GPU services
#include "Recipe/RecipeChunkBounds.h"
#include "RecipeContentCacher.h"
#include "RecipeBucketDataCacher.h"
#include "Nodes/BodyOctreeSceneNode.h"
#include "Nodes/CameraNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/StorageBufferNode.h"
#include "Nodes/HitAccumParamsConfigNode.h"
#include "Nodes/PhotonCellParamsConfigNode.h"
#include "Nodes/PhotonCellTableNode.h"
#include "Nodes/FrameSyncNode.h"
#include "Nodes/ReservoirConfigNode.h"
#include "Nodes/LightTreeBufferNode.h"
#include "Generated/ReservoirRecord.g.h"
#include "LightTree.h"
#include "Nodes/MultiDispatchNode.h"
#include "Nodes/ConstantNode.h"
#include "Nodes/ShaderLibraryNode.h"
#include "Nodes/ComputePipelineNode.h"
#include "Nodes/CommandPoolNode.h"
#include "Recipe/SpecializedRecipeShaderGlsl.h"
#include "ShaderCompiler.h"
#include "Data/Nodes/RenderTargetNodeConfig.h"
#include "ShaderBundleBuilder.h"
#include "ComputePipelineCacher.h"
#include "DescriptorSetLayoutCacher.h"
#include "Hash.h"
#include "FrameTrace.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <typeindex>

void VulkanGraphApplication::RunRecipeBucketedDispatchPreTick() {
    if (!renderGraph) return;
    auto* targetNode = renderGraph->GetInstanceByName("compute_render_target");
    if (!targetNode) return;
    auto* targetWidth = targetNode->GetOutput(RenderTargetNodeConfig::WIDTH_OUT_Slot::index, 0);
    auto* targetHeight = targetNode->GetOutput(RenderTargetNodeConfig::HEIGHT_OUT_Slot::index, 0);
    if (!targetWidth || !targetHeight) return;
    const uint32_t bucketWidth = targetWidth->GetHandle<uint32_t>();
    const uint32_t bucketHeight = targetHeight->GetHandle<uint32_t>();
    if (!bucketWidth || !bucketHeight) return;
    const auto reflectTiles = [](auto& cached, const auto& bundle) {
        for (const auto& range : bundle.GetPushConstants()) {
            cached.pushSize = std::max(cached.pushSize, range.offset + range.size);
            for (const auto& member : range.structDef.members) {
                if (member.name != "tileOrigin") continue;
                if (member.type.baseType != ShaderManagement::SpirvTypeInfo::BaseType::UInt ||
                    member.type.width != 32 || member.type.vecSize != 2 || member.type.arraySize != 0)
                    throw std::runtime_error("Specialized recipe tileOrigin must be uvec2");
                cached.tileOriginOffset = member.offset;
            }
        }
        if (!cached.tileOriginOffset || cached.tileOriginOffset + sizeof(glm::uvec2) > cached.pushSize)
            throw std::runtime_error("Specialized recipe has no reflected tileOrigin");
    };
    const auto boundedTiles = [&](uint32_t offset) {
        DispatchPass::TilePartition tile;
        tile.groupEdge = DispatchPass::BoundedGroupEdge;
        tile.grid = { (bucketWidth + tile.localSize * tile.groupEdge - 1u) / (tile.localSize * tile.groupEdge),
                      (bucketHeight + tile.localSize * tile.groupEdge - 1u) / (tile.localSize * tile.groupEdge), 1u };
        tile.originPushOffset = offset;
        return tile;
    };

    auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstanceByName("body_octree_scene"));
    auto* deviceInst = static_cast<DeviceNode*>(renderGraph->GetInstanceByName("main_device"));
    if (!bodyScene || !deviceInst || !deviceInst->GetVulkanDevice()) return;
    Vixen::Vulkan::Resources::VulkanDevice* device = deviceInst->GetVulkanDevice();

    // Refresh the bucketing shader's viewProj push-constant field every frame -- see
    // recipeBucketingViewProjConstant's own creation-site comment (BuildRenderGraph.cpp) for
    // why this goes through a ConstantNode instead of wiring CameraNode's own reference-typed
    // CURRENT_VIEW_PROJ output directly into the push-constant gatherer.
    if (auto* cameraInst = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
        if (auto* viewProjConst = static_cast<ConstantNode*>(renderGraph->GetInstance(recipeBucketingViewProjConstant_))) {
            viewProjConst->SetValue<glm::mat4>(cameraInst->GetCurrentViewProj());
        }
    }

    // --- Step 1: pure-CPU grouping + hotness decision (no GPU readback needed) ---
    const std::vector<Vixen::SVO::BodyInstanceGpu>& instances = bodyScene->GetInstances();
    std::unordered_map<uint32_t, std::vector<uint32_t>> instancesByRecipe;
    std::vector<uint32_t> instanceRecipeIds;
    instanceRecipeIds.reserve(instances.size());
    for (uint32_t i = 0; i < instances.size(); ++i) {
        instanceRecipeIds.push_back(instances[i].material.recipeId);
        instancesByRecipe[instances[i].material.recipeId].push_back(i);
    }
    if (recipeBucketInstanceGeneration_ == 0 || instanceRecipeIds != recipeBucketLastInstanceRecipes_) {
        ++recipeBucketInstanceGeneration_;
        recipeBucketLastInstanceRecipes_ = instanceRecipeIds;
    }
    std::vector<uint32_t> hotRecipeIds;
    for (const auto& [recipeId, memberIndices] : instancesByRecipe) {
        if (memberIndices.size() >= kRecipeBucketingHotnessThreshold && recipeId < kRecipeBucketingMaxBuckets) {
            hotRecipeIds.push_back(recipeId);
        }
    }
    std::sort(hotRecipeIds.begin(), hotRecipeIds.end());  // deterministic dispatch order

    // --- Step 2-4: build one canonical generation-keyed snapshot and publish only dirty ranges.
    // The low six mask words are CPU-owned; B1 owns the high six words. Meta retains the old
    // writer's stale entries for cold recipes, preserving the established GPU-visible contents.
    auto* skipMaskNode = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("instance_skip_mask_buffer"));
    auto* boundSphereNode = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("recipe_bound_sphere_buffer"));
    auto* bucketMetaNode = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("recipe_bucket_meta_buffer"));
    if (!skipMaskNode || !boundSphereNode || !bucketMetaNode) return;

    // A device recovery or an allocation growth creates new ring handles and zeroed memory. The
    // canonical cacher survives that event, but each slot must be republished once into the new
    // allocations; ordinary graph recompiles keep the handles and retain zero-write behavior.
    const bool ringBuffersChanged =
        recipeBucketLastSkipMaskBuffer_ != skipMaskNode->GetBufferHandle(0) ||
        recipeBucketLastBoundSphereBuffer_ != boundSphereNode->GetBufferHandle(0) ||
        recipeBucketLastMetaBuffer_ != bucketMetaNode->GetBufferHandle(0);
    if (ringBuffersChanged) {
        recipeBucketFrameStates_.clear();
        recipeBucketLastSkipMaskBuffer_ = skipMaskNode->GetBufferHandle(0);
        recipeBucketLastBoundSphereBuffer_ = boundSphereNode->GetBufferHandle(0);
        recipeBucketLastMetaBuffer_ = bucketMetaNode->GetBufferHandle(0);
    }

    auto& mainCacher = renderGraph->GetMainCacher();
    const auto cacherType = std::type_index(typeid(CashSystem::RecipeBucketSnapshot));
    if (!mainCacher.IsRegistered(cacherType)) {
        mainCacher.RegisterCacher<
            CashSystem::RecipeBucketDataCacher,
            CashSystem::RecipeBucketSnapshot,
            CashSystem::RecipeBucketCacheCreateInfo
        >(cacherType, "RecipeBucketData", /*isDeviceDependent=*/false);
    }
    auto* bucketCacher = mainCacher.GetCacher<
        CashSystem::RecipeBucketDataCacher,
        CashSystem::RecipeBucketSnapshot,
        CashSystem::RecipeBucketCacheCreateInfo
    >(cacherType);
    if (!bucketCacher) return;

    // Rebuild the canonical payload only when one of its two declared generations changes. The
    // unchanged-frame path reuses the immutable snapshot and does not re-fill or compare any
    // table; frame slots retain their own pending ranges below.
    const uint64_t registryGeneration = proceduralRecipes_.GetGeneration();
    std::shared_ptr<const CashSystem::RecipeBucketSnapshot> snapshot = recipeBucketLastSnapshot_;
    if (!snapshot || snapshot->registryGeneration != registryGeneration ||
        snapshot->instanceGeneration != recipeBucketInstanceGeneration_) {
        CashSystem::RecipeBucketCacheCreateInfo cacheInfo;
        cacheInfo.registryGeneration = registryGeneration;
        cacheInfo.instanceGeneration = recipeBucketInstanceGeneration_;
        if (recipeBucketLastSnapshot_) {
            cacheInfo.bucketMeta = recipeBucketLastSnapshot_->bucketMeta;
        }

        // Load-Tier Contract M1/M2: zero remains the real "not opted in" value for both thresholds.
        for (uint32_t recipeId : proceduralRecipes_.Ids()) {
            if (recipeId >= cacheInfo.boundSpheres.size()) continue;
            const auto* entry = proceduralRecipes_.Get(recipeId);
            if (!entry) continue;
            auto& sphere = cacheInfo.boundSpheres[recipeId];
            sphere.center[0] = entry->boundCenter.x;
            sphere.center[1] = entry->boundCenter.y;
            sphere.center[2] = entry->boundCenter.z;
            sphere.radius = entry->boundRadius > 0.0f ? entry->boundRadius : 1.0f;
            sphere.relaxation = entry->stepRelaxation > 0.0f ? entry->stepRelaxation : 1.0f;
            sphere.gateFootprintThreshold = entry->gateFootprintThreshold;
            sphere.precisionFootprintThreshold = entry->precisionFootprintThreshold;
        }

        for (uint32_t hotId : hotRecipeIds) {
            if (hotId >= cacheInfo.bucketMeta.size()) continue;
            const auto* entry = proceduralRecipes_.Get(hotId);
            auto& meta = cacheInfo.bucketMeta[hotId];
            meta.memberCount = static_cast<uint32_t>(instancesByRecipe[hotId].size());
            meta.rectMinX = 0;
            meta.rectMinY = 0;
            meta.boundRadius = entry && entry->boundRadius > 0.0f ? entry->boundRadius : 1.0f;
            meta.stepRelaxation = entry && entry->stepRelaxation > 0.0f ? entry->stepRelaxation : 1.0f;
        }

        for (uint32_t hotId : hotRecipeIds) {
            for (uint32_t instIdx : instancesByRecipe[hotId]) {
                const size_t wordIdx = instIdx / 32u;
                if (wordIdx < CashSystem::kRecipeBucketCpuMaskWordCount) {
                    cacheInfo.instanceSkipMask[wordIdx] |= (1u << (instIdx % 32u));
                }
            }
        }

        snapshot = bucketCacher->GetOrCreate(cacheInfo);
        if (!snapshot) return;
        recipeBucketLastSnapshot_ = snapshot;
    }

    const auto* frameSyncInst = static_cast<FrameSyncNode*>(
        renderGraph->GetInstanceByName("frame_sync"));
    const uint32_t frameIndex = frameSyncInst
        ? frameSyncInst->GetCurrentFrameIndex() % FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT
        : 0u;
    if (recipeBucketFrameStates_.size() != FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT) {
        recipeBucketFrameStates_.assign(FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT, {});
    }
    auto& frameState = recipeBucketFrameStates_[frameIndex];
    if (frameState.snapshot != snapshot) {
        CashSystem::RecipeBucketSnapshot zeroSnapshot{};
        frameState.pending = CashSystem::ComputeRecipeBucketDirtyRanges(
            frameState.snapshot ? *frameState.snapshot : zeroSnapshot, *snapshot);
    }

    auto publishRanges = [frameIndex](StorageBufferNode* node,
                                       const std::vector<CashSystem::RecipeBucketByteRange>& ranges,
                                       const void* source) {
        if (ranges.empty()) return true;
        void* mapped = node->MapCurrentForWrite(frameIndex);
        if (!mapped) return false;
        for (const auto& range : ranges) {
            std::memcpy(static_cast<std::uint8_t*>(mapped) + range.offset,
                        static_cast<const std::uint8_t*>(source) + range.offset,
                        range.size);
        }
        node->UnmapCurrentForWrite();
        return true;
    };
    const bool published =
        publishRanges(skipMaskNode, frameState.pending.instanceSkipMask, snapshot->instanceSkipMask.data()) &&
        publishRanges(boundSphereNode, frameState.pending.boundSpheres, snapshot->boundSpheres.data()) &&
        publishRanges(bucketMetaNode, frameState.pending.bucketMeta, snapshot->bucketMeta.data());
    if (!published) return;
    frameState.snapshot = snapshot;
    frameState.pending = {};

    // The bucketing pre-pass (ComputeStageNode trio, wired in BuildRenderGraph.cpp) has already
    // run by the time this PreTick reads its previous-frame indirect output. One frame of latency
    // remains intentional for the specialized-dispatch gate.
    if (hotRecipeIds.empty()) return;

    auto* bucketCountNode = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("recipe_bucket_count_buffer"));
    auto* bucketIndirectNode = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("recipe_bucket_indirect_command_buffer"));
    auto* hitRecordNode = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("hit_record_buffer"));
    auto* bucketIndicesNode = static_cast<StorageBufferNode*>(renderGraph->GetInstanceByName("recipe_bucket_indices_buffer"));
    auto* dispatchNode = static_cast<MultiDispatchNode*>(renderGraph->GetInstanceByName("recipe_specialized_dispatch"));
    if (!bucketMetaNode || !bucketCountNode || !bucketIndirectNode || !hitRecordNode || !bucketIndicesNode || !dispatchNode) return;

    // BucketMembersBuffer (specialized shader binding 1) is the bucketing pass's OWN
    // recipe_bucket_indices_buffer output -- no separate CPU population needed, just wire it as
    // a descriptor below (it's already written by the GPU bucketing pass every frame).

    // --- Step 5: for each hot recipe, compile (once, cached) the specialized shader and queue
    // its indirect dispatch. ---
    auto* commandPoolInst = static_cast<CommandPoolNode*>(renderGraph->GetInstanceByName("main_cmd_pool"));
    if (!commandPoolInst) return;

    for (uint32_t recipeId : hotRecipeIds) {
        const auto* entry = proceduralRecipes_.Get(recipeId);
        if (!entry) continue;

        auto cacheIt = recipeSpecializedPipelineCache_.find(recipeId);
        if (cacheIt == recipeSpecializedPipelineCache_.end()) {
            // First promotion of this recipeId: compile its specialized shader now.
            std::vector<std::filesystem::path> corePaths = {
#ifdef VIXEN_SVO_SHADER_SOURCE_DIR
                std::filesystem::path(VIXEN_SVO_SHADER_SOURCE_DIR) / "recipe" / "SdfCoreKernels.g.glsl",
#endif
                std::filesystem::path("libraries/SVO/shaders/recipe/SdfCoreKernels.g.glsl"),
                std::filesystem::path("../libraries/SVO/shaders/recipe/SdfCoreKernels.g.glsl"),
            };
            std::filesystem::path corePath;
            for (const auto& p : corePaths) { if (std::filesystem::exists(p)) { corePath = p; break; } }
            if (corePath.empty()) {
                if (mainLogger) mainLogger->Error("[RecipeBucketedDispatch] SdfCoreKernels.g.glsl not found, skipping recipeId=" + std::to_string(recipeId));
                continue;
            }
            std::ifstream coreFile(corePath);
            std::ostringstream coreBuf;
            coreBuf << coreFile.rdbuf();

            // W2c: when the bucket-shade pass owns material, the march compiles LEAN —
            // no gradient taps; the per-recipe shade kernel (queued after all march
            // passes below) computes the identical normal on the final winner.
            const std::string source = Vixen::SVO::Recipe::EmitSpecializedRecipeComputeShader(
                *entry, recipeId, coreBuf.str(), /*emitGradientNormal=*/!bucketedShadeEnabled_,
                proceduralRecipes_.GlyphInstanceBase(recipeId));

            // AddStageFromSpirv is a stub in this codebase today (ShaderBundleBuilder.cpp's own
            // "TODO: Store SPIRV separately, for now this is a placeholder" -- it discards the
            // SPIR-V and stores an EMPTY source string, so Build() silently tries to compile ""
            // as GLSL and fails with a confusing "Unable to parse built-ins" error, found live
            // during this milestone's own gate run). Use AddStage (text) instead -- Build()
            // compiles AND reflects in one step from source text, the same path the main
            // march's own ShaderLibraryNode builder uses, and IS proven working.
            ShaderManagement::CompilationOptions opts;
            opts.validateSpirv = false;  // matches test_recipe_bucketed_indirect_dispatch.cpp's own working call
            ShaderManagement::ShaderBundleBuilder bundleBuilder;
            bundleBuilder.SetProgramName("RecipeSpecialized_" + std::to_string(recipeId))
                         .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
                         .AddStage(ShaderManagement::ShaderStage::Compute, source, "main", opts);
            auto buildResult = bundleBuilder.Build();
            if (!buildResult.success || !buildResult.bundle) {
                if (mainLogger) mainLogger->Error("[RecipeBucketedDispatch] shader build/reflect failed for recipeId=" +
                                                   std::to_string(recipeId) + ": " + buildResult.errorMessage);
                continue;
            }
            const std::vector<uint32_t>& spirv = buildResult.bundle->GetSpirv(ShaderManagement::ShaderStage::Compute);
            if (spirv.empty()) {
                if (mainLogger) mainLogger->Error("[RecipeBucketedDispatch] empty SPIR-V for recipeId=" + std::to_string(recipeId));
                continue;
            }

            VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            moduleInfo.codeSize = spirv.size() * sizeof(uint32_t);
            moduleInfo.pCode = spirv.data();
            VkShaderModule shaderModule = VK_NULL_HANDLE;
            if (vkCreateShaderModule(device->device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
                if (mainLogger) mainLogger->Error("[RecipeBucketedDispatch] vkCreateShaderModule failed for recipeId=" + std::to_string(recipeId));
                continue;
            }

            VkDescriptorSetLayout setLayout = CashSystem::BuildDescriptorSetLayoutFromReflection(device, *buildResult.bundle, 0);
            std::vector<VkPushConstantRange> pcRanges = CashSystem::ExtractPushConstantsFromReflection(*buildResult.bundle);
            const uint32_t frameSetCount = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;
            std::vector<VkDescriptorPoolSize> poolSizes = CashSystem::CalculateDescriptorPoolSizes(
                *buildResult.bundle, 0, frameSetCount);

            VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            poolInfo.maxSets = frameSetCount;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            VkDescriptorPool descPool = VK_NULL_HANDLE;
            if (vkCreateDescriptorPool(device->device, &poolInfo, nullptr, &descPool) != VK_SUCCESS) {
                if (mainLogger) mainLogger->Error("[RecipeBucketedDispatch] vkCreateDescriptorPool failed for recipeId=" + std::to_string(recipeId));
                vkDestroyShaderModule(device->device, shaderModule, nullptr);
                continue;
            }
            VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocInfo.descriptorPool = descPool;
            allocInfo.descriptorSetCount = frameSetCount;
            std::vector<VkDescriptorSetLayout> setLayouts(frameSetCount, setLayout);
            allocInfo.pSetLayouts = setLayouts.data();
            std::vector<VkDescriptorSet> descriptorSets(frameSetCount, VK_NULL_HANDLE);
            if (vkAllocateDescriptorSets(device->device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
                if (mainLogger) mainLogger->Error("[RecipeBucketedDispatch] vkAllocateDescriptorSets failed for recipeId=" + std::to_string(recipeId));
                vkDestroyDescriptorPool(device->device, descPool, nullptr);
                vkDestroyShaderModule(device->device, shaderModule, nullptr);
                continue;
            }

            // Allocate one immutable descriptor set per frame slot. The ring handles are written
            // once below after pipeline creation; subsequent dispatches select the set matching
            // the FrameSync-predicted slot and never update an in-flight set.

            CashSystem::ComputePipelineCreateParams pipelineParams;
            pipelineParams.shaderModule = shaderModule;
            pipelineParams.entryPoint = "main";
            pipelineParams.descriptorSetLayout = setLayout;
            pipelineParams.pushConstantRanges = pcRanges;
            const std::string contentDigest = Vixen::SVO::Recipe::RecipeContentDigest(
                entry->bytecode, entry->pathInstances);
            pipelineParams.shaderKey = "RecipeSpecialized:" + contentDigest + ":" +
                                       ShaderManagement::ComputeSHA256HexFromUint32Vec(spirv);
            pipelineParams.layoutKey = "RecipeSpecializedLayout:" + contentDigest;
            pipelineParams.workgroupSizeX = 8;
            pipelineParams.workgroupSizeY = 8;
            pipelineParams.workgroupSizeZ = 1;

            auto& mainCacher = renderGraph->GetMainCacher();
            if (!mainCacher.IsRegistered(typeid(CashSystem::ComputePipelineWrapper))) {
                mainCacher.RegisterCacher<CashSystem::ComputePipelineCacher, CashSystem::ComputePipelineWrapper,
                                          CashSystem::ComputePipelineCreateParams>(
                    typeid(CashSystem::ComputePipelineWrapper), "ComputePipeline", true);
            }
            auto* computeCacher = mainCacher.GetCacher<CashSystem::ComputePipelineCacher, CashSystem::ComputePipelineWrapper,
                                                       CashSystem::ComputePipelineCreateParams>(
                typeid(CashSystem::ComputePipelineWrapper), device);
            if (!computeCacher) {
                if (mainLogger) mainLogger->Error("[RecipeBucketedDispatch] failed to get ComputePipelineCacher for recipeId=" + std::to_string(recipeId));
                vkDestroyDescriptorPool(device->device, descPool, nullptr);
                vkDestroyShaderModule(device->device, shaderModule, nullptr);
                continue;
            }
            auto pipelineWrapper = computeCacher->GetOrCreate(pipelineParams);
            if (!pipelineWrapper || pipelineWrapper->pipeline == VK_NULL_HANDLE) {
                if (mainLogger) mainLogger->Error("[RecipeBucketedDispatch] pipeline creation failed for recipeId=" + std::to_string(recipeId));
                vkDestroyDescriptorPool(device->device, descPool, nullptr);
                vkDestroyShaderModule(device->device, shaderModule, nullptr);
                continue;
            }

            SpecializedRecipePipeline cached;
            cached.shaderModule = shaderModule;
            cached.descriptorSetLayout = setLayout;
            cached.descriptorPool = descPool;
            cached.descriptorSet = descriptorSets[0];
            cached.descriptorSets = descriptorSets;
            cached.pipelineLayout = pipelineWrapper->pipelineLayoutWrapper ? pipelineWrapper->pipelineLayoutWrapper->layout : VK_NULL_HANDLE;
            cached.pipeline = pipelineWrapper->pipeline;
            reflectTiles(cached, *buildResult.bundle);

            // Write this recipe's descriptor-set ring ONCE, here at first promotion, not every
            // frame. Body/meta use their persistent frame slots; bucket members and hit records
            // remain static handles. None of these handles moves for the pipeline lifetime --
            // only their CONTENT changes frame to frame.
            // Re-writing the SAME binding values into a descriptor set every frame was a real
            // bug (VUID-vkUpdateDescriptorSets-None-03047, found live during this milestone's
            // own gate run): vkUpdateDescriptorSets on a set still referenced by a PENDING
            // command buffer (this same set, from a prior frame's not-yet-completed dispatch)
            // is illegal without VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, which this set's
            // layout does not opt into (BuildDescriptorSetLayoutFromReflection's own default).
            VkBuffer bucketMembersBuf = bucketIndicesNode->GetBufferHandle();
            VkBuffer hitRecordBuf = hitRecordNode->GetBufferHandle();
            const uint32_t bindingCount = entry->glyphInstances.empty() ? 5u : 6u;
            std::vector<VkDescriptorBufferInfo> bufInfos(frameSetCount * bindingCount);
            std::vector<VkWriteDescriptorSet> writes(frameSetCount * bindingCount);
            for (uint32_t frameSlot = 0; frameSlot < frameSetCount; ++frameSlot) {
                VkBuffer buffers[6] = {
                    bodyScene->GetInstanceBufferHandle(frameSlot),
                    bucketMembersBuf,
                    hitRecordBuf,
                    bucketMetaNode->GetBufferHandle(frameSlot),
                    bodyScene->GetInstanceTransformBufferHandle(frameSlot),
                    bodyScene->GetGlyphOutlineBufferHandle(),
                };
                for (uint32_t binding = 0; binding < bindingCount; ++binding) {
                    const size_t writeIndex = static_cast<size_t>(frameSlot) * bindingCount + binding;
                    bufInfos[writeIndex] = { buffers[binding], 0, VK_WHOLE_SIZE };
                    writes[writeIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[writeIndex].dstSet = descriptorSets[frameSlot];
                    writes[writeIndex].dstBinding = binding;
                    writes[writeIndex].descriptorCount = 1;
                    writes[writeIndex].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    writes[writeIndex].pBufferInfo = &bufInfos[writeIndex];
                }
            }
            // A device-wide wait before this ONE-TIME (first-promotion only, not per-frame)
            // descriptor write -- found live (VUID-vkUpdateDescriptorSets-None-03047) that a
            // freshly-allocated VkDescriptorSet could still be flagged "in use" by an in-flight
            // command buffer from this same frame's recipe_specialized_dispatch ring without
            // this wait. Acceptable here specifically because it fires at most once per
            // recipeId's lifetime (guarded by the recipeSpecializedPipelineCache_ cache-miss
            // check above), not a per-frame cost.
            vkDeviceWaitIdle(device->device);
            vkUpdateDescriptorSets(device->device, static_cast<uint32_t>(writes.size()),
                                   writes.data(), 0, nullptr);

            recipeSpecializedPipelineCache_[recipeId] = cached;

            if (mainLogger) mainLogger->Info("[RecipeBucketedDispatch] compiled specialized pipeline for recipeId=" +
                                             std::to_string(recipeId) + " (" + std::to_string(instancesByRecipe[recipeId].size()) +
                                             " instances, promoted hot)");
        }

        const auto& cached = recipeSpecializedPipelineCache_[recipeId];

        // Queue this recipe's indirect dispatch: workgroup counts come from
        // recipe_bucket_indirect_command_buffer (the bucketing pass's mode==2 output), at this
        // recipeId's own 3xuint32 slot.
        Vixen::RenderGraph::DispatchPass pass;
        pass.pipeline = cached.pipeline;
        pass.layout = cached.pipelineLayout;
        pass.descriptorSets = { cached.DescriptorSetForFrame(frameIndex) };
        pass.indirectBuffer = bucketIndirectNode->GetBufferHandle();
        pass.tiles = boundedTiles(cached.tileOriginOffset);
        const uint32_t tileCount = pass.tiles->grid.x * pass.tiles->grid.y;
        pass.indirectBufferOffset = VkDeviceSize(kRecipeBucketingMaxBuckets + recipeId * tileCount) * sizeof(VkDispatchIndirectCommand);  // 3x uint32 per bucket
        pass.debugName = "recipe_specialized_" + std::to_string(recipeId);

        // Push constants: SpecializedRecipeShaderGlsl.h's own Push block (96 bytes) --
        // cameraPos/_p0/cameraDir/fov/cameraUp/aspect/cameraRight/_p1/screenWidth/screenHeight/
        // maxMembersPerBucket/recipeId. Found live (VUID-vkCmdDispatchIndirect-maintenance4-08602)
        // that leaving DispatchPass.pushConstants unset is a real gap -- the shader statically
        // uses these fields, so a value must be pushed before every dispatch.
        struct SpecializedPushCpu {
            glm::vec3 cameraPos; float _p0;
            glm::vec3 cameraDir; float fov;
            glm::vec3 cameraUp;  float aspect;
            glm::vec3 cameraRight; float _p1;
            uint32_t screenWidth;
            uint32_t screenHeight;
            uint32_t maxMembersPerBucket;
            uint32_t recipeIdField;
        };
        static_assert(sizeof(SpecializedPushCpu) == 80, "SpecializedPushCpu must match the GLSL Push block's 80-byte std430 layout (4x vec3+float pairs=64B, 4x uint32_t=16B)");
        SpecializedPushCpu pushData{};
        if (auto* camInst = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
            const CameraData& cam = camInst->GetCurrentCameraData();
            pushData.cameraPos = cam.cameraPos;
            pushData.cameraDir = cam.cameraDir;
            pushData.cameraUp = cam.cameraUp;
            pushData.cameraRight = cam.cameraRight;
            pushData.fov = cam.fov;
            pushData.aspect = cam.aspect;
        }
        pushData.screenWidth = bucketWidth;
        pushData.screenHeight = bucketHeight;
        pushData.maxMembersPerBucket = kRecipeBucketingMaxMembersPerBucket;
        pushData.recipeIdField = recipeId;

        Vixen::RenderGraph::PushConstantData pcData;
        pcData.data.resize(cached.pushSize);
        std::memcpy(pcData.data.data(), &pushData, sizeof(SpecializedPushCpu));
        pcData.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcData.offset = 0;
        pass.pushConstants = std::move(pcData);

        dispatchNode->QueueDispatch(std::move(pass));
    }

    // --- W2c (wavefront epoch, VIXEN_BUCKETED_SHADE opt-in): per-recipe
    // MATERIAL (bucket-shade) kernels — the W2b identity skeleton grown into
    // the real traversal/shade split. The march kernels above compiled LEAN
    // (no gradient taps); each hot recipeId gets a specialized shade kernel
    // (EmitSpecializedRecipeShadeShader — field function + the march's
    // gradient, token-identical, so lean-march + shade reproduces the full
    // march's records BIT-EXACTLY), compiled once into its own cached
    // pipeline and queued AFTER every march pass on the SAME dispatch node
    // (autoBarriers_ serializes same-HitRecord passes — march→shade ordering
    // inherited, zero graph changes). Interface/descriptors/push are the
    // march kernels' own (kSpecializedRecipeInterfaceGlsl).
    if (bucketedShadeEnabled_ && !hotRecipeIds.empty()) {
        for (uint32_t shadeRecipeId : hotRecipeIds) {
            if (bucketShadePipelineCache_.count(shadeRecipeId)) continue;
            const auto* entry = proceduralRecipes_.Get(shadeRecipeId);
            if (!entry) continue;
            std::vector<std::filesystem::path> corePaths = {
#ifdef VIXEN_SVO_SHADER_SOURCE_DIR
                std::filesystem::path(VIXEN_SVO_SHADER_SOURCE_DIR) / "recipe" / "SdfCoreKernels.g.glsl",
#endif
                std::filesystem::path("libraries/SVO/shaders/recipe/SdfCoreKernels.g.glsl"),
                std::filesystem::path("../libraries/SVO/shaders/recipe/SdfCoreKernels.g.glsl"),
            };
            std::filesystem::path corePath;
            for (const auto& p : corePaths) { if (std::filesystem::exists(p)) { corePath = p; break; } }
            if (corePath.empty()) {
                if (mainLogger) mainLogger->Error("[BucketShade] SdfCoreKernels.g.glsl not found, skipping shade for recipeId=" + std::to_string(shadeRecipeId));
                continue;
            }
            std::ifstream coreFile(corePath);
            std::ostringstream coreBuf;
            coreBuf << coreFile.rdbuf();
            const std::string source = Vixen::SVO::Recipe::EmitSpecializedRecipeShadeShader(
                *entry, shadeRecipeId, coreBuf.str(), proceduralRecipes_.GlyphInstanceBase(shadeRecipeId));
            ShaderManagement::CompilationOptions opts;
            opts.validateSpirv = false;
            ShaderManagement::ShaderBundleBuilder bundleBuilder;
            bundleBuilder.SetProgramName("RecipeShade_" + std::to_string(shadeRecipeId))
                         .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
                         .AddStage(ShaderManagement::ShaderStage::Compute, source, "main", opts);
            auto buildResult = bundleBuilder.Build();
            if (!buildResult.success || !buildResult.bundle) {
                if (mainLogger) mainLogger->Error("[BucketShade] shade build/reflect failed for recipeId=" +
                                                   std::to_string(shadeRecipeId) + ": " + buildResult.errorMessage);
                continue;
            }
            {
                    const std::vector<uint32_t>& spirv = buildResult.bundle->GetSpirv(ShaderManagement::ShaderStage::Compute);
                    VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                    moduleInfo.codeSize = spirv.size() * sizeof(uint32_t);
                    moduleInfo.pCode = spirv.data();
                    VkShaderModule shaderModule = VK_NULL_HANDLE;
                    if (!spirv.empty() &&
                        vkCreateShaderModule(device->device, &moduleInfo, nullptr, &shaderModule) == VK_SUCCESS) {
                        VkDescriptorSetLayout setLayout = CashSystem::BuildDescriptorSetLayoutFromReflection(device, *buildResult.bundle, 0);
                        std::vector<VkPushConstantRange> pcRanges = CashSystem::ExtractPushConstantsFromReflection(*buildResult.bundle);
                        const uint32_t frameSetCount = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;
                        std::vector<VkDescriptorPoolSize> poolSizes = CashSystem::CalculateDescriptorPoolSizes(
                            *buildResult.bundle, 0, frameSetCount);
                        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                        poolInfo.maxSets = frameSetCount;
                        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
                        poolInfo.pPoolSizes = poolSizes.data();
                        VkDescriptorPool descPool = VK_NULL_HANDLE;
                        if (vkCreateDescriptorPool(device->device, &poolInfo, nullptr, &descPool) == VK_SUCCESS) {
                            VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                            allocInfo.descriptorPool = descPool;
                            allocInfo.descriptorSetCount = frameSetCount;
                            std::vector<VkDescriptorSetLayout> setLayouts(frameSetCount, setLayout);
                            allocInfo.pSetLayouts = setLayouts.data();
                            std::vector<VkDescriptorSet> descriptorSets(frameSetCount, VK_NULL_HANDLE);
                            if (vkAllocateDescriptorSets(device->device, &allocInfo, descriptorSets.data()) == VK_SUCCESS) {
                                CashSystem::ComputePipelineCreateParams pipelineParams;
                                pipelineParams.shaderModule = shaderModule;
                                pipelineParams.entryPoint = "main";
                                pipelineParams.descriptorSetLayout = setLayout;
                                pipelineParams.pushConstantRanges = pcRanges;
                                const std::string contentDigest = Vixen::SVO::Recipe::RecipeContentDigest(
                                    entry->bytecode, entry->pathInstances);
                                pipelineParams.shaderKey = "RecipeShade:" + contentDigest + ":" +
                                    ShaderManagement::ComputeSHA256HexFromUint32Vec(spirv);
                                pipelineParams.layoutKey = "RecipeShadeLayout:" + contentDigest;
                                pipelineParams.workgroupSizeX = 8;
                                pipelineParams.workgroupSizeY = 8;
                                pipelineParams.workgroupSizeZ = 1;
                                auto& mainCacher = renderGraph->GetMainCacher();
                                auto* computeCacher = mainCacher.GetCacher<CashSystem::ComputePipelineCacher,
                                                                           CashSystem::ComputePipelineWrapper,
                                                                           CashSystem::ComputePipelineCreateParams>(
                                    typeid(CashSystem::ComputePipelineWrapper), device);
                                auto pipelineWrapper = computeCacher ? computeCacher->GetOrCreate(pipelineParams) : nullptr;
                                if (pipelineWrapper && pipelineWrapper->pipeline != VK_NULL_HANDLE) {
                                    const VkBuffer bucketMembersBuf = bucketIndicesNode->GetBufferHandle();
                                    const VkBuffer hitRecordBuf = hitRecordNode->GetBufferHandle();
                                    const uint32_t bindingCount = entry->glyphInstances.empty() ? 5u : 6u;
                                    std::vector<VkDescriptorBufferInfo> bufInfos(frameSetCount * bindingCount);
                                    std::vector<VkWriteDescriptorSet> writes(frameSetCount * bindingCount);
                                    for (uint32_t frameSlot = 0; frameSlot < frameSetCount; ++frameSlot) {
                                        const VkBuffer bufHandles[6] = {
                                            bodyScene->GetInstanceBufferHandle(frameSlot),
                                            bucketMembersBuf,
                                            hitRecordBuf,
                                            bucketMetaNode->GetBufferHandle(frameSlot),
                                            bodyScene->GetInstanceTransformBufferHandle(frameSlot),
                                            bodyScene->GetGlyphOutlineBufferHandle(),
                                        };
                                        for (uint32_t binding = 0; binding < bindingCount; ++binding) {
                                            const size_t writeIndex = static_cast<size_t>(frameSlot) * bindingCount + binding;
                                            bufInfos[writeIndex] = { bufHandles[binding], 0, VK_WHOLE_SIZE };
                                            writes[writeIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                                            writes[writeIndex].dstSet = descriptorSets[frameSlot];
                                            writes[writeIndex].dstBinding = binding;
                                            writes[writeIndex].descriptorCount = 1;
                                            writes[writeIndex].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                                            writes[writeIndex].pBufferInfo = &bufInfos[writeIndex];
                                        }
                                    }
                                    vkDeviceWaitIdle(device->device);  // one-shot per recipeId (first promotion), same rationale as the march block
                                    vkUpdateDescriptorSets(device->device, static_cast<uint32_t>(writes.size()),
                                                           writes.data(), 0, nullptr);
                                    SpecializedRecipePipeline cachedShade;
                                    cachedShade.shaderModule = shaderModule;
                                    cachedShade.descriptorSetLayout = setLayout;
                                    cachedShade.descriptorPool = descPool;
                                    cachedShade.descriptorSet = descriptorSets[0];
                                    cachedShade.descriptorSets = descriptorSets;
                                    cachedShade.pipelineLayout = pipelineWrapper->pipelineLayoutWrapper
                                        ? pipelineWrapper->pipelineLayoutWrapper->layout : VK_NULL_HANDLE;
                                    cachedShade.pipeline = pipelineWrapper->pipeline;
                                    reflectTiles(cachedShade, *buildResult.bundle);
                                    bucketShadePipelineCache_[shadeRecipeId] = cachedShade;
                                    if (mainLogger) mainLogger->Info("[BucketShade] compiled specialized shade pipeline for recipeId=" +
                                                                     std::to_string(shadeRecipeId));
                                } else {
                                    if (mainLogger) mainLogger->Error("[BucketShade] pipeline creation failed");
                                    vkDestroyDescriptorPool(device->device, descPool, nullptr);
                                    vkDestroyShaderModule(device->device, shaderModule, nullptr);
                                }
                            } else {
                                if (mainLogger) mainLogger->Error("[BucketShade] vkAllocateDescriptorSets failed");
                                vkDestroyDescriptorPool(device->device, descPool, nullptr);
                                vkDestroyShaderModule(device->device, shaderModule, nullptr);
                            }
                        } else {
                            if (mainLogger) mainLogger->Error("[BucketShade] vkCreateDescriptorPool failed");
                            vkDestroyShaderModule(device->device, shaderModule, nullptr);
                        }
                    } else {
                        if (mainLogger) mainLogger->Error("[BucketShade] vkCreateShaderModule failed (or empty SPIR-V)");
                    }
            }
        }
        // Queue the shade passes — one per hot recipeId, AFTER every march pass
        // above (queue order + autoBarriers_ = the ordering guarantee).
        struct BucketShadePushCpu {  // identical 80-B layout to SpecializedPushCpu above
            glm::vec3 cameraPos; float _p0;
            glm::vec3 cameraDir; float fov;
            glm::vec3 cameraUp;  float aspect;
            glm::vec3 cameraRight; float _p1;
            uint32_t screenWidth;
            uint32_t screenHeight;
            uint32_t maxMembersPerBucket;
            uint32_t recipeIdField;
        };
        static_assert(sizeof(BucketShadePushCpu) == 80, "must match the shader's 80-byte Push block");
        BucketShadePushCpu shadePush{};
        if (auto* camInst = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode_))) {
            const CameraData& cam = camInst->GetCurrentCameraData();
            shadePush.cameraPos = cam.cameraPos;
            shadePush.cameraDir = cam.cameraDir;
            shadePush.cameraUp = cam.cameraUp;
            shadePush.cameraRight = cam.cameraRight;
            shadePush.fov = cam.fov;
            shadePush.aspect = cam.aspect;
        }
        shadePush.screenWidth = bucketWidth;
        shadePush.screenHeight = bucketHeight;
        shadePush.maxMembersPerBucket = kRecipeBucketingMaxMembersPerBucket;
        for (uint32_t recipeId : hotRecipeIds) {
            auto shadeIt = bucketShadePipelineCache_.find(recipeId);
            if (shadeIt == bucketShadePipelineCache_.end()) continue;
            const auto& shade = shadeIt->second;
            Vixen::RenderGraph::DispatchPass shadePass;
            shadePass.pipeline = shade.pipeline;
            shadePass.layout = shade.pipelineLayout;
            shadePass.descriptorSets = { shade.DescriptorSetForFrame(frameIndex) };
            shadePass.indirectBuffer = bucketIndirectNode->GetBufferHandle();
            shadePass.tiles = boundedTiles(shade.tileOriginOffset);
            const uint32_t tileCount = shadePass.tiles->grid.x * shadePass.tiles->grid.y;
            shadePass.indirectBufferOffset = VkDeviceSize(kRecipeBucketingMaxBuckets + recipeId * tileCount) * sizeof(VkDispatchIndirectCommand);
            shadePass.debugName = "recipe_shade_" + std::to_string(recipeId);
            shadePush.recipeIdField = recipeId;
            Vixen::RenderGraph::PushConstantData shadePcData;
            shadePcData.data.resize(shade.pushSize);
            std::memcpy(shadePcData.data.data(), &shadePush, sizeof(BucketShadePushCpu));
            shadePcData.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            shadePcData.offset = 0;
            shadePass.pushConstants = std::move(shadePcData);
            dispatchNode->QueueDispatch(std::move(shadePass));
        }
    }
}



namespace {

// Mirrors this file's own kResidencyBoundingRadius (anonymous namespace further down); an
// authored-bound-free procedural recipe gets the same "reasonable default sphere" the Stored
// path uses. If that constant changes, update this one to match.
constexpr float kDefaultProceduralBoundRadius = 24.0f;

bool HasRuntimeParameters(const Vixen::SVO::RecipeRegistry::RecipeEntry& entry) {
    return std::any_of(entry.bytecode.begin(), entry.bytecode.end(), [](const auto& instruction) {
        const auto op = static_cast<Vixen::SVO::Recipe::SdfOpCode>(instruction.opCode);
        return op == Vixen::SVO::Recipe::SdfOpCode::ReadParam ||
               op == Vixen::SVO::Recipe::SdfOpCode::ReadParamFloat3;
    });
}

// A bound the kernel analysis derives replaces the authored one (R892.3: on by default, no
// switch). Runtime-parameter recipes remain on their authored/default bound because a
// zero-parameter derivation is not valid over their parameter range, and never receive an
// occupancy grid: the grid samples one parameter value, so its cells could incorrectly cull a
// changed field.
Vixen::SVO::Recipe::RecipeRegistrationOptions RegistrationOptionsFor(
    const Vixen::SVO::RecipeRegistry::RecipeEntry& entry, bool lodShells) {
    const bool hasRuntimeParameters = HasRuntimeParameters(entry);
    Vixen::SVO::Recipe::RecipeRegistrationOptions options;
    options.preferDerivedBound = entry.boundRadius > 0.0f && !hasRuntimeParameters;
    options.occupancy = !hasRuntimeParameters;
    // R892.23: a recipe with baked glyph rows gets its certified shells derived and stored with the
    // entry (the glyph rows leave the coarse shell). Other recipes keep today's registration cost
    // until a lane selects shells per instance (RL0).
    // R860 RL0: with a shell tolerance set, every recipe without runtime parameters gets its shells,
    // which the registry then registers and the binning pass selects per instance.
    options.shells = !hasRuntimeParameters && (lodShells ||
        std::any_of(entry.glyphBakedRows.begin(), entry.glyphBakedRows.end(),
                    [](const Vixen::SVO::Recipe::GlyphBakedRow& row) { return row.Baked(); }));
    return options;
}

// Test switch (R892.23 witness; nothing selects a shell per instance yet): VIXEN_RECIPE_FORCE_SHELL_LEVEL=n
// renders every recipe that has at least n derived shells from its shell n (1 = finest) instead of
// its own tape. 0, unset or unparsable: off.
std::uint32_t ForcedShellLevel() {
    const char* text = std::getenv("VIXEN_RECIPE_FORCE_SHELL_LEVEL");
    if (text == nullptr) return 0u;
    char* end = nullptr;
    const unsigned long level = std::strtoul(text, &end, 10);
    return end != text && *end == '\0' && level <= 8ul ? static_cast<std::uint32_t>(level) : 0u;
}

// M6 Task 13: the kernel recipe analysis service finalizes the bound (authored, derived or engine
// default) and derives the coarse occupancy grid from that bound. A program the service cannot
// prove leaves occupancyGridDim==0 (the splice emits a gridDim=0u case; the shader skips the
// occupancy fast-path for it).
Vixen::SVO::Recipe::RecipeRegistrationAnalysis AnalyzeEntry(Vixen::SVO::RecipeRegistry::RecipeEntry& entry,
                                                             bool lodShells = false) {
    Vixen::SVO::Recipe::RecipeRegistrationOptions options = RegistrationOptionsFor(entry, lodShells);
    auto analysis = Vixen::SVO::Recipe::AnalyzeRecipeRegistration(
        entry, kDefaultProceduralBoundRadius, /*defaultStepRelaxation=*/0.9f, 64u, 16u, options);
    const std::uint32_t forced = ForcedShellLevel();
    if (forced != 0u) {
        for (std::size_t level = 0; level < entry.derivedShells.size(); ++level)
            std::printf("[RECIPE_SHELL] level=%zu records=%zu of %zu certified_error=%.9g bound_radius=%.9g rules=%u certificate=%u tolerance_scale=%.3g%s\n",
                        level + 1, entry.derivedShells[level].bytecode.size(), entry.bytecode.size(),
                        static_cast<double>(entry.derivedShells[level].certifiedError),
                        static_cast<double>(entry.derivedShellBoundRadius), entry.derivedShells[level].rules,
                        entry.derivedShells[level].certificate,
                        static_cast<double>(entry.derivedShells[level].toleranceScale), level + 1 == forced ? " FORCED" : "");
        if (entry.derivedShells.empty()) std::printf("[RECIPE_SHELL] none (records=%zu)\n", entry.bytecode.size());
        std::fflush(stdout);
    }
    if (forced != 0u && forced <= entry.derivedShells.size()) {
        // The shell is a recipe of its own: analyse it as one (bound and grid of the shell's tape),
        // and keep the derived data of the recipe it stands for.
        auto shells = std::move(entry.derivedShells);
        const float shellBoundRadius = entry.derivedShellBoundRadius;
        entry.bytecode = shells[forced - 1u].bytecode;
        options.shells = false;
        analysis = Vixen::SVO::Recipe::AnalyzeRecipeRegistration(
            entry, kDefaultProceduralBoundRadius, /*defaultStepRelaxation=*/0.9f, 64u, 16u, options);
        entry.derivedShells = std::move(shells);
        entry.derivedShellBoundRadius = shellBoundRadius;
    }
    return analysis;
}

}  // namespace

void VulkanGraphApplication::RegisterRecipeContent(
    uint32_t recipeId, const Vixen::SVO::RecipeRegistry::RecipeEntry& entry) {
    // Recipe Pipeline Cache Inc1 Task 4: RecipeRegistry (SVO) must not link CashSystem, so this is
    // the lowest layer that can populate the content-hash cache.
    auto& mainCacher = renderGraph->GetMainCacher();
    const auto cacherType = std::type_index(typeid(CashSystem::RecipeFamilyRecord));
    if (!mainCacher.IsRegistered(cacherType)) {
        mainCacher.RegisterCacher<
            CashSystem::RecipeContentCacher,
            CashSystem::RecipeFamilyRecord,
            CashSystem::RecipeContentCacheCreateInfo
        >(cacherType, "RecipeContent", /*isDeviceDependent=*/false);
    }
    auto* recipeCacher = mainCacher.GetCacher<
        CashSystem::RecipeContentCacher,
        CashSystem::RecipeFamilyRecord,
        CashSystem::RecipeContentCacheCreateInfo
    >(cacherType);
    if (recipeCacher) {
        recipeCacher->RegisterRecipe(recipeId, entry.bytecode, entry.pathInstances);
    } else if (mainLogger) {
        mainLogger->Warning("[VulkanGraphApplication::RegisterRecipeContent] recipeId=" +
                            std::to_string(recipeId) + " failed to get RecipeContentCacher — cache not populated");
    }
}

Vixen::SVO::RecipeRegistry::RegisterResult VulkanGraphApplication::SubmitRecipeMutation(
    uint32_t recipeId, Vixen::SVO::RecipeRegistry::RecipeEntry entry, bool replace) {
    // Provisional entry: the previous analysis fields carry over (a replaced recipe keeps its old,
    // conservative bound and grid until the new analysis is published; a new id gets the engine
    // default bound and no grid). The shader sources are not rebuilt before the publish.
    Vixen::SVO::RecipeRegistry::RecipeEntry provisional = entry;
    const Vixen::SVO::RecipeRegistry::RecipeEntry* previous = proceduralRecipes_.Get(recipeId);
    if (previous != nullptr) {
        provisional.boundCenter = previous->boundCenter;
        provisional.boundRadius = previous->boundRadius;
        provisional.stepRelaxation = previous->stepRelaxation;
        provisional.occupancyGridValues = previous->occupancyGridValues;
        provisional.occupancyGridDim = previous->occupancyGridDim;
        provisional.occupancyGridAabbMin = previous->occupancyGridAabbMin;
        provisional.occupancyGridCellSize = previous->occupancyGridCellSize;
    } else {
        provisional.boundCenter = glm::vec3(0.0f);
        provisional.boundRadius = kDefaultProceduralBoundRadius;
        provisional.stepRelaxation = 0.9f;
        provisional.occupancyGridValues.clear();
        provisional.occupancyGridDim = 0u;
    }
    const auto result = replace ? proceduralRecipes_.Replace(recipeId, provisional)
                                : proceduralRecipes_.Register(recipeId, provisional);
    if (result != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) return result;
    RegisterRecipeContent(recipeId, provisional);

    PendingRecipeAnalysis pending;
    pending.generation = ++recipeAnalysisGeneration_;
    pending.analysed = std::async(std::launch::async, [job = std::move(entry)]() mutable {
        VIXEN_FRAME_TRACE_SCOPE("recipe-edit", "analysis-worker");
        (void)AnalyzeEntry(job);
        return job;
    });
    // A newer edit of the same id supersedes the older analysis (its future still joins on destruction).
    pendingRecipeAnalyses_[recipeId] = std::move(pending);
    return result;
}

void VulkanGraphApplication::PublishReadyRecipeAnalyses(bool wait) {
    for (auto it = pendingRecipeAnalyses_.begin(); it != pendingRecipeAnalyses_.end();) {
        auto& pending = it->second;
        if (!wait && pending.analysed.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        Vixen::SVO::RecipeRegistry::RecipeEntry analysed = pending.analysed.get();
        const uint32_t recipeId = it->first;
        const auto result = proceduralRecipes_.Replace(recipeId, analysed);
        if (result != Vixen::SVO::RecipeRegistry::RegisterResult::Ok && mainLogger) {
            mainLogger->Warning("[VulkanGraphApplication::PublishReadyRecipeAnalyses] recipeId=" +
                                std::to_string(recipeId) + " analysed entry rejected; provisional entry kept");
        } else if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[VulkanGraphApplication::PublishReadyRecipeAnalyses] recipeId=" +
                             std::to_string(recipeId) + " boundRadius=" + std::to_string(analysed.boundRadius) +
                             " occupancyGrid=" + std::to_string(analysed.occupancyGridDim) + "^3");
        }
        it = pendingRecipeAnalyses_.erase(it);
    }
}

Vixen::SVO::RecipeRegistry::RegisterResult VulkanGraphApplication::RegisterProceduralRecipe(
    uint32_t recipeId, Vixen::SVO::RecipeRegistry::RecipeEntry entry) {
    // Before the graph compiles, the first Compile() reads the registry: derive in place.
    if (graphCompiled) {
        const auto result = SubmitRecipeMutation(recipeId, std::move(entry), /*replace=*/false);
        if (result == Vixen::SVO::RecipeRegistry::RegisterResult::Ok) AdvanceRecipeEditSerial();
        return result;
    }

    const auto analysis = AnalyzeEntry(entry, renderPathConfig_.shellTolerancePx > 0.0f);
    if (mainLogger && mainLogger->IsEnabled()) {
        auto sourceName = [](Vixen::SVO::Recipe::RecipeRegistrationAnalysis::BoundSource s) {
            switch (s) {
                case Vixen::SVO::Recipe::RecipeRegistrationAnalysis::BoundSource::Authored: return "authored";
                case Vixen::SVO::Recipe::RecipeRegistrationAnalysis::BoundSource::Derived: return "derived";
                case Vixen::SVO::Recipe::RecipeRegistrationAnalysis::BoundSource::EngineDefault: return "engine-default";
            }
            return "?";
        };
        mainLogger->Info("[VulkanGraphApplication::RegisterProceduralRecipe] recipeId=" +
                         std::to_string(recipeId) + " boundRadius=" + std::to_string(entry.boundRadius) +
                         " (" + sourceName(analysis.boundSource) + ") stepRelaxation=" +
                         std::to_string(entry.stepRelaxation) + " (service-analysis)" +
                         (HasRuntimeParameters(entry) ? " runtimeParameters=range-bound-required" : ""));
        mainLogger->Info("[VulkanGraphApplication::RegisterProceduralRecipe] recipeId=" +
                         std::to_string(recipeId) + " occupancyGrid=" +
                         (analysis.occupancyAvailable ? (std::to_string(entry.occupancyGridDim) + "^3") :
                          std::string("none (unavailable from the kernel analysis service)")));
    }
    // A glyph or path leaf that the generated field cannot guard is walked at every field sample.
    // That is a cost the author can remove (intersect the leaf with a box that bounds it), so it is
    // reported with the recipe and the instruction, never silently paid.
    const auto rowLeaves = Vixen::SVO::Recipe::RecipeGpuRowWalkingLeaves(entry.bytecode, entry.pathInstances);
    if (rowLeaves.count != 0u && mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[VulkanGraphApplication::RegisterProceduralRecipe] recipeId=" +
                         std::to_string(recipeId) + " rowWalkingLeaves=" + std::to_string(rowLeaves.count) +
                         " exactSkip=" + std::to_string(rowLeaves.guarded));
        for (const std::uint32_t instruction : rowLeaves.unguardedInstructions)
            mainLogger->Warning("[VulkanGraphApplication::RegisterProceduralRecipe] recipeId=" +
                                std::to_string(recipeId) + " instruction " + std::to_string(instruction) +
                                ": this glyph/path leaf is evaluated at every field sample; intersect it with a " +
                                "bounding box and merge it with Subtract or Union so the field can skip it exactly");
    }
    const auto result = proceduralRecipes_.Register(recipeId, entry);
    if (result == Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
        RegisterRecipeContent(recipeId, entry);
        if (renderPathConfig_.shellTolerancePx > 0.0f) RegisterDerivedShells(recipeId, entry);
        AdvanceRecipeEditSerial();
    }
    return result;
}

// R860 RL0: each derived shell is a recipe of its own (its bound and grid analysed from its tape),
// registered once per content digest under a key the registry allocates (R841.2). The recipe's
// ladder then names those keys, finest first. A shell the registry refuses ends the ladder there:
// a coarser shell is only reached through the finer ones.
void VulkanGraphApplication::RegisterDerivedShells(uint32_t recipeId,
                                                   const Vixen::SVO::RecipeRegistry::RecipeEntry& recipe) {
    std::vector<uint32_t> keys;
    for (const auto& shell : recipe.derivedShells) {
        Vixen::SVO::RecipeRegistry::RecipeEntry shellEntry{};
        shellEntry.bytecode = shell.bytecode;
        shellEntry.parameterValues = recipe.parameterValues;
        // A registered recipe must lower to a complete GPU tape: the operand refresh after a live edit
        // concatenates every registered tape, and one that does not lower record for record makes it
        // refuse the whole refresh (the march then reads stale operands). Such a shell ends the ladder.
        const auto lowered = Vixen::SVO::Recipe::RecipeGpuTape(shellEntry.bytecode);
        if (lowered.status != YK_RECIPE_GPU_OK || lowered.instructions.size() != shellEntry.bytecode.size()) {
            std::printf("[LOD_SHELLS] recipe=%u shell %zu does not lower to a complete GPU tape (%zu of %zu): %s\n",
                        recipeId, keys.size() + 1, lowered.instructions.size(), shellEntry.bytecode.size(),
                        lowered.diagnostic.c_str());
            break;
        }
        const std::string digest = Vixen::SVO::Recipe::RecipeContentDigest(shellEntry.bytecode);
        (void)AnalyzeEntry(shellEntry, /*lodShells=*/false);
        uint32_t key = 0u;
        const std::size_t registeredBefore = proceduralRecipes_.Ids().size();
        const auto result = proceduralRecipes_.RegisterDerivedShell(digest, shellEntry, key);
        if (result != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
            if (mainLogger)
                mainLogger->Warning("[VulkanGraphApplication::RegisterDerivedShells] recipeId=" +
                                    std::to_string(recipeId) + " shell " + std::to_string(keys.size() + 1) +
                                    " was refused (status " + std::to_string(static_cast<int>(result)) +
                                    "); the ladder stops at the finer shells");
            break;
        }
        if (proceduralRecipes_.Ids().size() > registeredBefore)  // a new digest, not a shared shell
            RegisterRecipeContent(key, *proceduralRecipes_.Get(key));
        keys.push_back(key);
    }
    std::printf("[LOD_SHELLS] recipe=%u shells=%zu keys=", recipeId, keys.size());
    for (const uint32_t key : keys) std::printf("%08x ", key);
    std::printf("\n");
    std::fflush(stdout);
    proceduralRecipes_.SetShellVariants(recipeId, std::move(keys));
}

Vixen::SVO::RecipeRegistry::RegisterResult VulkanGraphApplication::ReplaceProceduralRecipe(
    uint32_t recipeId, Vixen::SVO::RecipeRegistry::RecipeEntry entry) {
    if (graphCompiled) {
        const auto result = SubmitRecipeMutation(recipeId, std::move(entry), /*replace=*/true);
        if (result == Vixen::SVO::RecipeRegistry::RegisterResult::Ok) AdvanceRecipeEditSerial();
        return result;
    }

    (void)AnalyzeEntry(entry);
    const auto result = proceduralRecipes_.Replace(recipeId, entry);
    if (result == Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
        RegisterRecipeContent(recipeId, entry);
        AdvanceRecipeEditSerial();
    }
    return result;
}

void VulkanGraphApplication::RefreshProceduralRecipeOperands() {
    if (!renderGraph || !bodyOctreeSceneNode_.IsValid()) return;
    auto* bodyScene = static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
        renderGraph->GetInstance(bodyOctreeSceneNode_));
    if (!bodyScene) return;

    std::vector<YkRecipeGpuTapeInstruction> tape;
    for (const uint32_t recipeId : proceduralRecipes_.Ids()) {
        const auto* entry = proceduralRecipes_.Get(recipeId);
        if (!entry) continue;
        const auto lowered = Vixen::SVO::Recipe::RecipeGpuTape(entry->bytecode);
        if (lowered.status != YK_RECIPE_GPU_OK || lowered.instructions.size() != entry->bytecode.size()) {
            if (mainLogger) {
                mainLogger->Error("[VulkanGraphApplication::RefreshProceduralRecipeOperands] recipe " +
                    std::to_string(recipeId) + " refused: " +
                    (lowered.diagnostic.empty() ? "GPU tape lowering failed" : lowered.diagnostic));
            }
            return;
        }
        tape.insert(tape.end(), lowered.instructions.begin(), lowered.instructions.end());
    }
    bodyScene->SetRecipeTape(std::move(tape));
}

void VulkanGraphApplication::StartProceduralPrecompile() {
    // Compile off the frame thread: the builders run here (they read the live recipe table), their
    // Build() runs on workers and fills the shader cache; the swap happens in PollProceduralShaderSwap.
    VIXEN_FRAME_TRACE_SCOPE("update", "procedural-precompile-begin");
    // R864.4: the precompile workers also create every downstream compute pipeline through its
    // cacher key, so the swap's RecompileDirtyNodes finds them instead of compiling on the frame thread.
    const auto pipelineNodes = renderGraph->GetInstancesOfType<Vixen::RenderGraph::ComputePipelineNodeType>();
    for (const auto handle : proceduralShaderLibNodes_) {
        auto* lib = dynamic_cast<ShaderLibraryNode*>(renderGraph->GetInstance(handle));
        if (!lib) continue;
        std::vector<ShaderLibraryNode::PipelineWarmer> warmers;
        for (auto* node : pipelineNodes) {
            if (!node->DependsOn(lib)) continue;
            if (auto* pipelineNode = dynamic_cast<Vixen::RenderGraph::ComputePipelineNode*>(node)) {
                if (auto warm = pipelineNode->MakePipelineWarmer()) warmers.push_back(std::move(warm));
            }
        }
        lib->BeginPrecompile(std::move(warmers));
    }
    proceduralSwapPending_ = true;
}

void VulkanGraphApplication::RecompileProceduralShader() {
    if (!graphCompiled) {
        // Nothing to mark dirty yet -- the first Compile() will read proceduralRecipes_'s
        // current contents when the shader builder lambda runs (BuildRenderGraph.cpp).
        return;
    }
    if (!computeShaderLibNode_.IsValid()) {
        if (mainLogger) mainLogger->Warning("[VulkanGraphApplication::RecompileProceduralShader] computeShaderLibNode_ not found — cannot re-apply");
        recipeCompilePendingSerial_ = recipeEditSerial_;
        PublishRecipeCompileFailure();
        return;
    }
    // The editor's failure witness must exercise the same serial/failure contract without
    // launching the expensive worker precompile first.  A real compiler exception is reported
    // through this same publication path; the hook only selects that outcome deterministically.
    if (const char* forcedFailure = std::getenv("VIXEN_EDITOR_FORCE_RECIPE_COMPILE_FAILURE");
        forcedFailure && std::strcmp(forcedFailure, "1") == 0) {
        recipeCompilePendingSerial_ = recipeEditSerial_;
        recipeCompileFailureForced_ = true;
        if (mainLogger) {
            mainLogger->Error("[VulkanGraphApplication::RecompileProceduralShader] forced recipe preview compile failure");
        }
        return;
    }
    if (!pendingRecipeAnalyses_.empty()) {
        if (!lockstepProceduralRecompile_) {
            // Analyses still run on workers: the precompile starts when the last one is published.
            recompileAfterAnalysis_ = true;
            proceduralSwapPending_ = true;
            return;
        }
        PublishReadyRecipeAnalyses(/*wait=*/true);
    }
    StartProceduralPrecompile();
    if (lockstepProceduralRecompile_) {
        for (const auto handle : proceduralShaderLibNodes_) {
            if (auto* lib = dynamic_cast<ShaderLibraryNode*>(renderGraph->GetInstance(handle))) lib->WaitPrecompile();
        }
        proceduralSwapPending_ = false;
        MarkProceduralShaderLibsDirty();  // recompiles at the next update tick, like the synchronous path did
        return;
    }
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[VulkanGraphApplication::RecompileProceduralShader] compiling " +
            std::to_string(proceduralShaderLibNodes_.size()) + " recipe source families off the frame thread");
    }
}

void VulkanGraphApplication::PollProceduralShaderSwap() {
    if (!renderGraph) return;
    if (!pendingRecipeAnalyses_.empty()) {
        PublishReadyRecipeAnalyses(/*wait=*/false);
        if (!pendingRecipeAnalyses_.empty()) return;
    }
    if (recompileAfterAnalysis_) {
        recompileAfterAnalysis_ = false;
        StartProceduralPrecompile();
    }
    if (!proceduralSwapPending_) return;
    for (const auto handle : proceduralShaderLibNodes_) {
        auto* lib = dynamic_cast<ShaderLibraryNode*>(renderGraph->GetInstance(handle));
        if (lib && !lib->IsPrecompileReady()) return;
    }
    proceduralSwapPending_ = false;
    MarkProceduralShaderLibsDirty();
}

void VulkanGraphApplication::FlushProceduralShaderSwap() {
    if (!renderGraph) return;
    PublishReadyRecipeAnalyses(/*wait=*/true);
    if (recompileAfterAnalysis_) {
        recompileAfterAnalysis_ = false;
        StartProceduralPrecompile();
    }
    if (!proceduralSwapPending_) return;
    for (const auto handle : proceduralShaderLibNodes_) {
        if (auto* lib = dynamic_cast<ShaderLibraryNode*>(renderGraph->GetInstance(handle))) lib->WaitPrecompile();
    }
    proceduralSwapPending_ = false;
    MarkProceduralShaderLibsDirty();
    renderGraph->RecompileDirtyNodes();
}

void VulkanGraphApplication::MarkProceduralShaderLibsDirty() {
    VIXEN_FRAME_TRACE_SCOPE("update", "procedural-swap-mark");
    recipeCompilePendingSerial_ = recipeEditSerial_;
    // Replacing editor rule layers rebuilds the shader library and every dependent pipeline.
    // Drain prior frame submissions first: those pipelines may still be referenced by GPU work
    // even though the graph's ordinary recompile path does not wait device-wide.
    if (auto* deviceInst = static_cast<DeviceNode*>(renderGraph->GetInstanceByName("main_device"))) {
        if (auto* device = deviceInst->GetVulkanDevice()) {
            VIXEN_FRAME_TRACE_SCOPE("recipe-edit", "swap-device-wait-idle");
            const VkResult waitResult = vkDeviceWaitIdle(device->device);
            if (waitResult == VK_ERROR_DEVICE_LOST) {
                renderGraph->NotifyDeviceLost(
                    "VulkanGraphApplication::MarkProceduralShaderLibsDirty vkDeviceWaitIdle");
                return;
            }
        }
    }
    for (const auto handle : proceduralShaderLibNodes_)
        renderGraph->MarkNodeNeedsRecompile(handle);
    // The swapped-in march bakes the occupancy-grid offsets of the registry it was spliced from;
    // publish that splice's grid now, with the device idle, so the two never disagree. Without
    // this the grid stayed at the last scene compile's layout, and a live registration whose id
    // sorts before other recipes shifted their offsets onto another recipe's grid (opus-rl0: a
    // swapped-in hull marched against a shell's grid rendered as a white blob).
    if (bodyOctreeSceneNode_.IsValid()) {
        if (auto* bodyScene = static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
                renderGraph->GetInstance(bodyOctreeSceneNode_)))
            bodyScene->PublishOccupancyGrid();
    }
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[VulkanGraphApplication::MarkProceduralShaderLibsDirty] marked " +
            std::to_string(proceduralShaderLibNodes_.size()) + " recipe source families dirty");
    }
}
