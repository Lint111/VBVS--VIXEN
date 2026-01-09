// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.

#include "Core/TaskProfileRegistry.h"
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

} // namespace Vixen::RenderGraph
