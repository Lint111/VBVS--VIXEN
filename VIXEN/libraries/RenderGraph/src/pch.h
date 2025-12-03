#pragma once

// ============================================================================
// RenderGraph Precompiled Header
// ============================================================================
// This file consolidates all common headers for RenderGraph.
// Simply forwards to the central Headers.h.
//
// Note: magic_enum is available via RenderGraph's CMakeLists.txt (FetchContent).
// Include it explicitly where needed: #include <magic_enum/magic_enum.hpp>

// Include central header
#include "Headers.h"

// Legacy content below (kept for reference):
// ============================================================================
// Precompiled Header for RenderGraph (DEPRECATED)
// ============================================================================
// Common headers used throughout RenderGraph to speed up compilation
// This file is precompiled once and reused across all translation units

// Standard Library - Containers
#include <vector>
#include <array>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <queue>
#include <deque>

// Standard Library - Utilities
#include <string>
#include <string_view>
#include <memory>
#include <functional>
#include <algorithm>
#include <optional>
#include <variant>
#include <any>
#include <type_traits>

// Standard Library - I/O
#include <iostream>
#include <fstream>
#include <sstream>

// Standard Library - Concurrency
#include <mutex>
#include <atomic>
#include <thread>
#include <future>

// Standard Library - Other
#include <chrono>
#include <system_error>
#include <filesystem>
#include <cassert>
#include <cstdint>
#include <cstring>

// Third-party - magic_enum (used extensively)
#include <magic_enum/magic_enum.hpp>

// Vulkan headers (conditional)
#ifndef VULKAN_TRIMMED_BUILD_ACTIVE
    // Include minimal Vulkan headers if available
    // These are forward declarations only - actual types in specific headers
#endif
