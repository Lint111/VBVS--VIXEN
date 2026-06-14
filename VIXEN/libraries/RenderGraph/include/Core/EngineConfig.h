#pragma once

#include <filesystem>
#include <functional>

namespace Vixen::Log { class Logger; }  // exposed globally as ::Logger via a using-decl in Logger.h
namespace CashSystem { class MainCacher; }

namespace Vixen::RenderGraph {

class NodeTypeRegistry;

/**
 * @brief Configuration for constructing an EngineContext (AR#7).
 *
 * All pointers are NON-OWNING. The node-type registration is caller-supplied: the set is
 * host-specific (the application registers a superset of the benchmark's set, and some
 * registrations pull node headers that include RenderGraph.h), so EngineContext invokes this
 * callback on its fresh registry rather than hardcoding one host's node list.
 */
struct EngineConfig {
    /// Engine-level diagnostics logger (graph + nodes). May be null.
    Vixen::Log::Logger* logger = nullptr;

    /// Cache provider. Null => the process-wide CashSystem::MainCacher::Instance() is used.
    CashSystem::MainCacher* mainCacher = nullptr;

    /// Directory where the CalibrationStore persists task profiles.
    std::filesystem::path calibrationDir = "calibration";

    /// Stand up the autonomous (event-driven) CalibrationStore. BenchmarkRunner sets this false.
    bool enableCalibration = true;

    /// Populate the node-type set on the engine's fresh registry. Defined by the host, where the
    /// concrete node-type headers are in scope. If empty, no node types are registered.
    std::function<void(NodeTypeRegistry&)> registerNodeTypes;
};

} // namespace Vixen::RenderGraph
