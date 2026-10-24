// Generated GPU resource slots. Define YEROKET_GPU_NODE_<system> inside its node config before including.
#if defined(YEROKET_GPU_NODE_ExposureTonemapSystem)
INPUT_SLOT_SYNC(sceneRadianceHistoryOutput_current, ::Vixen::Vulkan::Resources::IRenderTarget*, 0, SlotNullability::Required, SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel, ::Vixen::RenderGraph::AccessKind::ComputeStorageRead);
INPUT_SLOT_SYNC(outputImage_current, ::Vixen::Vulkan::Resources::IRenderTarget*, 1, SlotNullability::Required, SlotRole::Execute, SlotMutability::ReadWrite, SlotScope::NodeLevel, ::Vixen::RenderGraph::AccessKind::ComputeStorageWrite);
INPUT_SLOT_SYNC(meter, VkBuffer, 2, SlotNullability::Required, SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel, ::Vixen::RenderGraph::AccessKind::ComputeStorageRead);
INPUT_SLOT_SYNC(sceneLook, VkBuffer, 3, SlotNullability::Required, SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel, ::Vixen::RenderGraph::AccessKind::ComputeStorageRead);
static constexpr uint32_t GeneratedGpuAccessSlots = 4u;
#endif
