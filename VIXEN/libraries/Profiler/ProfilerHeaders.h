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
