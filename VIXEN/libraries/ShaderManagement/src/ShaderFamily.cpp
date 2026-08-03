#include "ShaderFamily.h"

#include "ShaderPreprocessor.h"

#include <algorithm>

namespace ShaderManagement {

ShaderFamily::ShaderFamily(Config config, SourceProvider source,
                           BuilderConfigurator configure)
    : config_(std::move(config))
    , source_(std::move(source))
    , configure_(std::move(configure)) {}

std::shared_ptr<ShaderDataBundle> ShaderFamily::Get(
    std::vector<std::string> features) {
    // Canonical member key: sorted, deduplicated feature list.
    std::sort(features.begin(), features.end());
    features.erase(std::unique(features.begin(), features.end()),
                   features.end());
    std::string key;
    for (const auto& f : features) {
        if (!key.empty()) key += ';';
        key += f;
    }

    auto it = members_.find(key);
    if (it != members_.end()) {
        return it->second;
    }

    const std::string spliced =
        ShaderPreprocessor::InjectFeatureDefines(source_(), features);

    ShaderBundleBuilder builder;
    builder.SetProgramName(config_.name)
           .SetPipelineType(PipelineTypeConstraint::Compute)
           .EnableSdiGeneration(false);
    if (configure_) {
        configure_(builder);
    }
    builder.AddStage(config_.stage, spliced);

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
