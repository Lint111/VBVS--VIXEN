// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Core/TypedNodeInstance.h"
#include "Data/Nodes/PhotonCellParamsConfigNodeConfig.h"
#include "PhotonCells.h"

#include <cstdint>
#include <memory>

namespace Vixen::RenderGraph {

struct PhotonCellConfiguration {
    float cellSize0 = Vixen::SVO::PhotonCells::kCellSize0;
    float temporalAlpha = Vixen::SVO::PhotonCells::kTemporalAlpha;
    float radianceClamp = Vixen::SVO::PhotonCells::kRadianceClamp;
    uint32_t maxAge = Vixen::SVO::PhotonCells::kMaxAge;
    uint64_t diagnosticFrame = 0;
    bool clearRequested = false;
    bool probeLog = false;
};

class PhotonCellParamsConfigNodeType
    : public TypedNodeType<PhotonCellParamsConfigNodeConfig> {
public:
    explicit PhotonCellParamsConfigNodeType(
        const std::string& typeName = "PhotonCellParamsConfig")
        : TypedNodeType<PhotonCellParamsConfigNodeConfig>(typeName) {}
    ~PhotonCellParamsConfigNodeType() override = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName) const override;
};

/**
 * @brief Host-written frame ring for PhotonCellsCommon.glsl's 48-byte block.
 *
 * The application writes before graph Execute, using the same predicted frame
 * slot as the descriptor node emits.  Ring buffers survive graph recompiles
 * and are released only at final teardown, mirroring HitAccumParams.
 */
class PhotonCellParamsConfigNode
    : public TypedNode<PhotonCellParamsConfigNodeConfig> {
public:
    using Base = TypedNode<PhotonCellParamsConfigNodeConfig>;

    PhotonCellParamsConfigNode(const std::string& instanceName, NodeType* nodeType);
    ~PhotonCellParamsConfigNode() override = default;

    static bool FeatureEnabled();
    static PhotonCellConfiguration ReadConfiguration();

    void* MapCurrentForWrite(uint32_t frameIndex) const;
    void UnmapCurrentForWrite() const {}

    /** Publish the frame-owned generation and camera-independent owner knobs. */
    void PrepareFrame(uint32_t frameIndex, float primaryCoef, uint64_t frameNumber);

    const PhotonCellConfiguration& Configuration() const { return configuration_; }
    uint32_t Generation() const { return generation_; }
    uint32_t GenerationForDiagnostic(uint64_t sampleFrame) const {
        return sampleFrame == 0 ? generation_ : diagnosticGeneration_;
    }
    float PrimaryCoef() const { return primaryCoef_; }
    float PrimaryBias() const { return primaryBias_; }
    float CellSize0() const { return configuration_.cellSize0; }
    bool DiagnosticDue(uint64_t frameNumber) const;
    void MarkDiagnosticFired() { diagnosticFired_ = true; }
    bool ProbeLogEnabled() const { return configuration_.probeLog; }

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    static bool EnvironmentFlag(const char* name);
    static const uint32_t kRingSize;
    PerFrameResources perFrame_;
    PhotonCellConfiguration configuration_{};
    uint32_t generation_ = 0;
    uint32_t diagnosticGeneration_ = 0;
    float primaryCoef_ = 0.0f;
    float primaryBias_ = 0.0f;
    bool diagnosticFired_ = false;
};

} // namespace Vixen::RenderGraph
