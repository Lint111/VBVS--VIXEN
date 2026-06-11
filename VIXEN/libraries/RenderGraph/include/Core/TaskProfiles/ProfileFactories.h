// Copyright (C) 2025 Lior Yanai (eLiorg)
// Licensed under the GPL-3.0 License.
// See LICENSE file in the project root for full license information.
#pragma once

/**
 * @file ProfileFactories.h
 * @brief Built-in profile factory registration
 *
 * Sprint 6.5: Calibration persistence support
 *
 * Provides factory registration for built-in profile types.
 * Called by TaskProfileRegistry::Init() to enable deserialization.
 */

#include "SimpleTaskProfile.h"
#include "ResolutionTaskProfile.h"
#include <functional>
#include <memory>
#include <string>

namespace Vixen::RenderGraph {

// Forward declaration
class TaskProfileRegistry;

/**
 * @brief Register all built-in profile factories
 *
 * Registers factories for:
 * - SimpleTaskProfile
 * - ResolutionTaskProfile
 *
 * @param registry Registry to register factories with
 */
inline void RegisterBuiltInProfileFactories(TaskProfileRegistry& registry);

} // namespace Vixen::RenderGraph

// Include TaskProfileRegistry for implementation
// (circular dependency resolved via forward decl + inline impl)
#include "../TaskProfileRegistry.h"

namespace Vixen::RenderGraph {

inline void RegisterBuiltInProfileFactories(TaskProfileRegistry& registry) {
    // SimpleTaskProfile factory
    registry.RegisterFactory("SimpleTaskProfile", []() {
        return std::make_unique<SimpleTaskProfile>("default", "default");
    });

    // ResolutionTaskProfile factory (uses default constructor for deserialization)
    registry.RegisterFactory("ResolutionTaskProfile", []() {
        return std::make_unique<ResolutionTaskProfile>();
    });
}

} // namespace Vixen::RenderGraph
