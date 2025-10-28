#pragma once

#include "CacherBase.h"
#include "TypedCacher.h"

#include <cstdint>
#include <string>
#include <filesystem>

namespace CashSystem {

// Forward-declare an opaque pipeline wrapper here. Replace with real wrapper type when integrating.
struct PipelineWrapper {
    int placeholder = 0;
};

struct PipelineCreateParams {
    // Minimal fields for a create params sketch. Extend as needed.
    std::string vertexShaderChecksum;
    std::string fragmentShaderChecksum;
    std::string layoutKey;
    std::string renderPassKey;
};

class PipelineCacher : public TypedCacher<PipelineWrapper, PipelineCreateParams> {
public:
    using Base = TypedCacher<PipelineWrapper, PipelineCreateParams>;
    using PtrT = typename Base::PtrT;

    PipelineCacher() = default;
    ~PipelineCacher() override = default;

protected:
    // implement TypedCacher hooks
    PtrT Create(const PipelineCreateParams& ci) override;
    std::uint64_t ComputeKey(const PipelineCreateParams& ci) const override;

    // Serialization specialized for pipelines
    bool SerializeToFile(const std::filesystem::path& path) const override;
    bool DeserializeFromFile(const std::filesystem::path& path, void* device) override;
    std::string_view name() const noexcept override { return "PipelineCacher"; }
};

} // namespace CashSystem
