#pragma once

// Semantic Shader Wiring S2 — synthesis slice B: ShaderFamily.
//
// ONE program identity whose feature variants are cached as family members.
// The static declarations already exist (shaders/sdi-variants.json enumerates
// each family's variant sets; the merged SDI is its interface face) — this is
// the RUNTIME object: Get(featureSet) applies the canonical after-#version
// define splice (ShaderPreprocessor::InjectFeatureDefines — the same
// mechanism the graph builders and sdi_tool use) and builds, or returns the
// cached, ShaderDataBundle for exactly that set. The content-hash shader
// cache underneath already dedups identical compiles; the family adds
// grouping, member selection, and prewarm.

#include "ShaderBundleBuilder.h"
#include "ShaderStage.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ShaderManagement {

class ShaderFamily {
public:
    struct Config {
        std::string name;
        ShaderStage stage = ShaderStage::Compute;
    };

    /// Returns the family's base source (post any content splice, PRE feature
    /// defines — features are the family's own axis).
    using SourceProvider = std::function<std::string()>;

    /// Optional hook applied to every member's builder (target versions,
    /// include paths, cache manager, SDI options…) so families keep parity
    /// with the graph's existing builder lambdas.
    using BuilderConfigurator = std::function<void(ShaderBundleBuilder&)>;

    ShaderFamily(Config config, SourceProvider source,
                 BuilderConfigurator configure = {});

    /**
     * @brief The family member for a feature set (canonicalized: sorted,
     *        deduplicated). Builds on first request, cached thereafter.
     * @return The member's bundle, or nullptr if its compile failed.
     */
    std::shared_ptr<ShaderDataBundle> Get(std::vector<std::string> features);

    const std::string& GetName() const { return config_.name; }

private:
    Config config_;
    SourceProvider source_;
    BuilderConfigurator configure_;
    std::map<std::string, std::shared_ptr<ShaderDataBundle>> members_;
};

} // namespace ShaderManagement
