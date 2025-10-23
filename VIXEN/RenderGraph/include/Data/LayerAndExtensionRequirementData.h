#pragma once

#include "Headers.h"

namespace Vixen::RenderGraph {

const struct LayerRequirement {
    const std::string layerName;
    const bool optional = false;
};

const struct ExtensionRequirement {
    const std::string extensionName;
    const bool optional = false;
};

struct FeatureDependencyBundle {
    std::vector<LayerRequirement> requiredLayers;
    std::vector<ExtensionRequirement> requiredExtensions;
};

struct Feature {
    const std::string featureName;
    const std::string description;
    FeatureDependencyBundle dependencies;
};

struct FeatureAvailability {
    std::string featureName;
    bool isAvailable;
    std::vector<std::string> missingLayers;
    std::vector<std::string> missingExtensions;
    std::vector<std::string> satisfiedLayers;
    std::vector<std::string> satisfiedExtensions;
};

struct NodeFeatureProfile {
    std::string nodeTypeName;
    std::vector<Feature> features;
    std::vector<FeatureAvailability> featureAvailabilities;
    bool canExecute = false;
};

// Feature Builder
class NodeFeatureProfileBuilder {
    public:
    NodeFeatureProfileBuilder() = default;
    ~NodeFeatureProfileBuilder() = default;

    NodeFeatureProfileBuilder& CreateNewProfile(const std::string& nodeTypeName) {
        profile = std::make_unique<NodeFeatureProfile>();
        profile->nodeTypeName = nodeTypeName;
        return *this;
    }

    NodeFeatureProfileBuilder& AddFeature(const std::string& featureName, const std::string& description, const FeatureDependencyBundle& dependencies = {}) {
        if (profile) {
            profile->features.push_back({ featureName, description, dependencies });
        }
        return *this;
    }

    NodeFeatureProfileBuilder& AddFeatureAvailability(const std::string& featureName, bool isAvailable) {
        if (profile) {
            profile->featureAvailabilities.push_back({ featureName, isAvailable, {}, {}, {}, {} });
        }
        return *this;
    }

    NodeFeatureProfileBuilder& SetCanExecute(bool canExecute) {
        if (profile) {
            profile->canExecute = canExecute;
        }
        return *this;
    }

    std::unique_ptr<NodeFeatureProfile> Build() {
        if(!profile) {
            return nullptr;
        }

        return std::move(profile);
    }

    private:
    std::unique_ptr<NodeFeatureProfile> profile;

};




} // namespace Vixen::RenderGraph