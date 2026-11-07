// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "Core/TaskProfileRegistry.h"
#include <nlohmann/json.hpp>
#include "Core/TaskProfiles/ProfileFactories.h"

namespace Vixen::RenderGraph {

void TaskProfileRegistry::Init() {
    if (initialized_) {
        return;  // Idempotent
    }

    // Register built-in profile factories for deserialization
    RegisterBuiltInProfileFactories(*this);

    initialized_ = true;
}

void TaskProfileRegistry::SaveState(nlohmann::json& j) const {
    j["profiles"] = nlohmann::json::array();
    for (const auto& [id, profile] : profiles_) {
        nlohmann::json profileJson;
        profile->SaveState(profileJson);
        j["profiles"].push_back(profileJson);
    }
}

size_t TaskProfileRegistry::LoadState(const nlohmann::json& j) {
    size_t loaded = 0;
    if (!j.contains("profiles") || !j["profiles"].is_array()) {
        return 0;
    }

    for (const auto& profileJson : j["profiles"]) {
        if (!profileJson.contains("typeName")) {
            continue;
        }

        std::string typeName = profileJson["typeName"];
        auto factoryIt = factories_.find(typeName);
        if (factoryIt == factories_.end()) {
            // No factory registered for this type
            continue;
        }

        // Create instance via factory and load its calibration state
        auto profile = factoryIt->second();
        if (!profile) continue;
        profile->LoadState(profileJson);

        // AR#1 Phase 3 fix: if a profile with this id is already registered, load the calibration INTO
        // the existing object in place rather than replacing it. RegisterTask does
        // profiles_[id] = std::move(new), which frees the existing profile — and nodes cache a raw
        // ITaskProfile* to it (e.g. ComputeDispatchNode::gpuProfile_, acquired during their Compile).
        // Replacing it dangles those pointers: a use-after-free that crashed in gpuProfile_->Sample().
        // DeviceNode publishes DeviceMetadataEvent on every (re)compile -> CalibrationStore::Load() ->
        // here, so a device-loss rebuild re-ran this and made the UAF reliable; once a calibration file
        // exists it also bites on a normal first frame. In-place load preserves object identity.
        if (ITaskProfile* existing = GetProfile(profile->GetTaskId())) {
            existing->LoadState(profileJson);
        } else {
            RegisterTask(std::move(profile));
        }
        ++loaded;
    }

    return loaded;
}

} // namespace Vixen::RenderGraph
