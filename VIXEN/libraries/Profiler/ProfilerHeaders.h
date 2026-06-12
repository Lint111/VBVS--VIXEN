#pragma once

/**
 * @file ProfilerHeaders.h
 * @brief Precompiled header for Profiler library
 *
 * Contains commonly used STL headers and stable project headers
 * to speed up compilation.
 */

// ============================================================================
// Platform Workarounds
// ============================================================================
// Prevent Windows.h from defining min/max macros (conflicts with STL/GLM)
#ifndef NOMINMAX
#define NOMINMAX
#endif

// Trim the Win32 API surface pulled in by any transitive windows.h include. WIN32_LEAN_AND_MEAN
// matches the other libraries' PCHs (RenderGraphHeaders.h, ShaderManagementHeaders.h), but the
// critical one here is NOGDI: it excludes wingdi.h, which #defines DeviceCapabilities (->
// DeviceCapabilitiesW, a GDI function). Without NOGDI that macro clobbers our
// Profiler::DeviceCapabilities struct (the struct name resolves to a function -> "type assumed
// int" across the whole Profiler).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif

// ============================================================================
// Standard Library - Containers
// ============================================================================
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <array>
#include <queue>

// ============================================================================
// Standard Library - Strings & I/O
// ============================================================================
#include <string>
#include <string_view>
#include <sstream>
#include <fstream>
#include <iomanip>

// ============================================================================
// Standard Library - Memory & Utilities
// ============================================================================
#include <memory>
#include <functional>
#include <optional>
#include <variant>
#include <any>

// ============================================================================
// Standard Library - Algorithms & Math
// ============================================================================
#include <algorithm>
#include <cmath>
#include <cstdint>

// ============================================================================
// Standard Library - Time & Random
// ============================================================================
#include <chrono>
#include <random>

// ============================================================================
// Standard Library - Filesystem
// ============================================================================
#include <filesystem>

// ============================================================================
// Third Party - JSON
// ============================================================================
#include <nlohmann/json.hpp>

// NOTE: GLM is excluded from PCH due to template parameter conflicts
// on some platforms. Include GLM directly in source files that need it.

// ============================================================================
// Project - Logging (stable, widely used)
// ============================================================================
#include <Logger.h>
