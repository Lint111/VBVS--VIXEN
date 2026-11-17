// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "Nodes/ComputeStageNode.h"
#include "Connection/SdiHazardCensus.h"

#include <cstdint>
#include <memory>

namespace Vixen::RenderGraph {

class ShaderLibraryNode;

/** Engine-owned descriptor manifest for the standalone photon shaders. */
struct PhotonCellMemberInfo {
    const char* name;
    bool isPushMember;
    uint32_t set;
    uint32_t binding;
    uint32_t offset;
    SdiAccess access;
    uint32_t featureCount;
    const char* const* features;
};

class PhotonCellDepositNode;

class PhotonCellDepositNodeType : public TypedNodeType<ComputeStageNodeConfig, PhotonCellDepositNode> {
public:
    explicit PhotonCellDepositNodeType(const std::string& typeName = "PhotonCellDeposit")
        : TypedNodeType<ComputeStageNodeConfig, PhotonCellDepositNode>(typeName) {}
    ~PhotonCellDepositNodeType() override = default;
};

class PhotonCellDepositNode : public ComputeStageNode {
public:
    struct Metadata { static constexpr const char* PROGRAM_NAME = "PhotonDeposit"; };
    inline static constexpr PhotonCellMemberInfo MEMBERS[] = {
        {"HitRecordBuffer", false, 0, 2, 0, SdiAccess::ReadOnly, 0, nullptr},
        {"PhotonCellTable", false, 0, 0, 0, SdiAccess::ReadWrite, 0, nullptr},
        {"PhotonCellParamsSSBO", false, 0, 1, 0, SdiAccess::ReadOnly, 0, nullptr},
        {"LightingConfigSSBO", false, 0, 3, 0, SdiAccess::ReadOnly, 0, nullptr},
        {"ShadowConfigSSBO", false, 0, 4, 0, SdiAccess::ReadOnly, 0, nullptr},
    };

    PhotonCellDepositNode(const std::string& instanceName, NodeType* nodeType);
    ~PhotonCellDepositNode() override = default;

    void ConfigureForRecordCount(uint32_t recordCount);
    void RegisterShader(ShaderLibraryNode& shaderLibrary) const;
};

class PhotonCellFoldNode;

class PhotonCellFoldNodeType : public TypedNodeType<ComputeStageNodeConfig, PhotonCellFoldNode> {
public:
    explicit PhotonCellFoldNodeType(const std::string& typeName = "PhotonCellFold")
        : TypedNodeType<ComputeStageNodeConfig, PhotonCellFoldNode>(typeName) {}
    ~PhotonCellFoldNodeType() override = default;
};

class PhotonCellFoldNode : public ComputeStageNode {
public:
    struct Metadata { static constexpr const char* PROGRAM_NAME = "PhotonCellFold"; };
    inline static constexpr PhotonCellMemberInfo MEMBERS[] = {
        {"PhotonCellTable", false, 0, 0, 0, SdiAccess::ReadWrite, 0, nullptr},
        {"PhotonCellParamsSSBO", false, 0, 1, 0, SdiAccess::ReadOnly, 0, nullptr},
    };

    PhotonCellFoldNode(const std::string& instanceName, NodeType* nodeType);
    ~PhotonCellFoldNode() override = default;

    void RegisterShader(ShaderLibraryNode& shaderLibrary) const;
};

class PhotonCellClearNode;

class PhotonCellClearNodeType : public TypedNodeType<ComputeStageNodeConfig, PhotonCellClearNode> {
public:
    explicit PhotonCellClearNodeType(const std::string& typeName = "PhotonCellClear")
        : TypedNodeType<ComputeStageNodeConfig, PhotonCellClearNode>(typeName) {}
    ~PhotonCellClearNodeType() override = default;
};

class PhotonCellClearNode : public ComputeStageNode {
public:
    struct Metadata { static constexpr const char* PROGRAM_NAME = "PhotonCellClear"; };
    inline static constexpr PhotonCellMemberInfo MEMBERS[] = {
        {"PhotonCellTable", false, 0, 0, 0, SdiAccess::WriteOnly, 0, nullptr},
    };

    PhotonCellClearNode(const std::string& instanceName, NodeType* nodeType);
    ~PhotonCellClearNode() override = default;

    void RegisterShader(ShaderLibraryNode& shaderLibrary) const;

protected:
    void TypedExecuteImpl(TypedExecuteContext& ctx) override;

private:
    bool clearPending_ = true;
};

} // namespace Vixen::RenderGraph
