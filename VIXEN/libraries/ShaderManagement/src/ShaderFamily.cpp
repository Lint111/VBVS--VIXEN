#include "ShaderFamily.h"

#include "ShaderPreprocessor.h"

#include <algorithm>

namespace ShaderManagement {

namespace {
void Canonicalize(std::vector<std::string>& features) {
    std::sort(features.begin(), features.end());
    features.erase(std::unique(features.begin(), features.end()),
                   features.end());
}
} // namespace

ShaderFamily::ShaderFamily(Config config, SourceProvider source,
                           BuilderConfigurator configure)
    : config_(std::move(config))
    , source_(std::move(source))
    , configure_(std::move(configure)) {}

ShaderBundleBuilder ShaderFamily::MakeBuilder(
    std::vector<std::string> features) const {
    Canonicalize(features);
    const std::string spliced =
        ShaderPreprocessor::InjectFeatureDefines(source_(), features);

    ShaderBundleBuilder builder;
    builder.SetProgramName(config_.name)
           .SetPipelineType(PipelineTypeConstraint::Compute);
    if (configure_) {
        configure_(builder);
    }
    builder.AddStage(config_.stage, spliced);
    return builder;
}

std::shared_ptr<ShaderDataBundle> ShaderFamily::Get(
    std::vector<std::string> features) {
    Canonicalize(features);
    std::string key;
    for (const auto& f : features) {
        if (!key.empty()) key += ';';
        key += f;
    }

    auto it = members_.find(key);
    if (it != members_.end()) {
        return it->second;
    }

    ShaderBundleBuilder builder = MakeBuilder(features);
    builder.EnableSdiGeneration(false);

    auto result = builder.Build();
    std::shared_ptr<ShaderDataBundle> bundle;
    if (result.success && result.bundle) {
        bundle = std::shared_ptr<ShaderDataBundle>(std::move(result.bundle));
    }
    // Failed compiles cache nullptr deliberately: the failure is per-member
    // and deterministic for a given source — re-Get returns the same verdict
    // without recompiling.
    members_.emplace(key, bundle);
    return bundle;
}

} // namespace ShaderManagement
