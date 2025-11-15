#pragma once

// ============================================================================
// CENTRAL HEADER - Platform, Vulkan, Third-Party, and STL
// ============================================================================
// This is the single source of truth for common headers used across the project.
// Consolidates functionality from old pch.h and Headers.h.

// ============================================================================
// Platform Configuration
// ============================================================================
#ifdef _WIN32
#pragma comment(linker, "/subsystem:console")
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define APP_NAME_STR_LEN 80
#define _CRT_SECURE_NO_WARNINGS
#else // _WIN32
#define  VK_USE_PLATFORM_XCB_KHR
#include <unistd.h>
#endif // _WIN32

// ============================================================================
// Vulkan API
// ============================================================================
#define VK_Enable_Beta_Extensions
#include <vulkan/vulkan.h>
#ifdef AUTO_COMPILE_GLSL_TO_SPV
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#endif // AUTO_COMPILE_GLSL_TO_SPV

// ============================================================================
// Third-Party Libraries
// ============================================================================

// GLM - Mathematics library
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  // Vulkan uses [0,1] depth range
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// GLI - Texture library for compressed formats (DDS/KTX)
#include <gli/gli.hpp>

// magic_enum - Enum reflection (only available in RenderGraph context)
// Note: magic_enum is fetched by RenderGraph/CMakeLists.txt via FetchContent
// Files outside RenderGraph should not rely on this header
#ifdef RENDERGRAPH_HAS_MAGIC_ENUM
#include <magic_enum/magic_enum.hpp>
#endif

// ============================================================================
// Standard Library - Containers
// ============================================================================
#include <vector>
#include <array>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <queue>
#include <deque>

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
#include <any>
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
#include <future>

// ============================================================================
// Standard Library - Other
// ============================================================================
#include <chrono>
#include <system_error>
#include <filesystem>
#include <cassert>
#include <cstdint>
#include <cstring>

// ============================================================================
// Platform-Specific Headers
// ============================================================================
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// ============================================================================
// Project-Specific Error Handling
// ============================================================================
#include "error/VulkanError.h"