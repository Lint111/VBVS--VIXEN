#pragma once

// ============================================================================
// ShaderManagement Library - Minimal Headers
// ============================================================================
// Headers needed by ShaderManagement library - no GLI or other texture libraries

// ============================================================================
// Platform Configuration
// ============================================================================
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#else // _WIN32
#define VK_USE_PLATFORM_XCB_KHR
#include <unistd.h>
#endif // _WIN32

// ============================================================================
// Vulkan API
// ============================================================================
#define VK_Enable_Beta_Extensions
#include <vulkan/vulkan.h>

// ============================================================================
// Standard Library - Containers
// ============================================================================
#include <vector>
#include <array>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>

// ============================================================================
// Standard Library - Utilities
// ============================================================================
#include <string>
#include <string_view>
#include <memory>
#include <functional>
#include <algorithm>
#include <optional>
#include <variant>
#include <type_traits>

// ============================================================================
// Standard Library - I/O
// ============================================================================
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

// ============================================================================
// Standard Library - Concurrency
// ============================================================================
#include <mutex>
#include <atomic>
#include <thread>

// ============================================================================
// Standard Library - Other
// ============================================================================
#include <chrono>
#include <system_error>
#include <filesystem>
#include <cassert>
#include <cstdint>
#include <cstring>
