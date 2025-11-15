#pragma once

// ============================================================================
// DEPRECATED - Use Headers.h instead
// ============================================================================
// This file has been consolidated into include/Headers.h (the central header).
// All functionality from pch.h is now in Headers.h.
// This file is kept temporarily for backward compatibility and will be removed.

#warning "pch.h is deprecated - use Headers.h instead (central header)"

// Forward to central header
#include "Headers.h"

// Legacy content below (now redundant):
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
